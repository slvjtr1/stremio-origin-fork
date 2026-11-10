/* Downloads (downloads.h): a film or an episode kept on the USB drive, to watch without the network. There only while a
   drive is: no drive, no row anywhere. Before a byte is written the stream is asked for its size and the drive for its
   room; too little room, an unknown size, or a file past what the drive's format can hold, and nothing is downloaded.
   One download at a time, on its own thread, into "<drive>/StremioOrigin/<name>.part", renamed when it is whole. */
#include "core/downloads.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/net.h"
#include "core/usb.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define DL_MAX 24
#define ROOM_SPARE (200.0 * 1048576.0) /* left free on the drive after the file */
typedef struct { char name[160], type[16], id[80], vid[96], file[400]; double size; } Entry;
static Entry list[DL_MAX]; static int n; static unsigned serial = 1; static char dir[640], seenId[96];
/* the one in flight */
static struct { int on, asking; volatile int cancel, done, ok; Entry e; char url[SB_URL_MAX], part[1100], full[1100]; volatile double got; unsigned req; } A;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static void list_path(char *o, size_t cap) { snprintf(o, cap, "%s/downloads.json", dir); }
static void save(void) {
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < n; i++) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "name", list[i].name); cJSON_AddStringToObject(o, "type", list[i].type); cJSON_AddStringToObject(o, "id", list[i].id);
    cJSON_AddStringToObject(o, "vid", list[i].vid); cJSON_AddStringToObject(o, "file", list[i].file); cJSON_AddNumberToObject(o, "size", list[i].size); cJSON_AddItemToArray(a, o); }
  char *t = cJSON_PrintUnformatted(a), p[700], tmp[720]; cJSON_Delete(a); if (!t) return; list_path(p, sizeof p); snprintf(tmp, sizeof tmp, "%s.tmp", p);
  FILE *f = fopen(tmp, "wb"); if (f) { size_t l = strlen(t), w = fwrite(t, 1, l, f); fclose(f); if (w == l) rename(tmp, p); else remove(tmp); }
  free(t);
}
static const char *str(const cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsString(v) ? v->valuestring : ""; }
/* only a plain file name of this folder is ever opened or removed: never a path a list on the drive names */
static int plain_name(const char *f) { return f[0] && !strchr(f, '/') && !strchr(f, '\\') && strcmp(f, ".") && strcmp(f, "..") && !strstr(f, ".."); }
static void load(void) {
  n = 0; char p[700]; list_path(p, sizeof p); FILE *f = fopen(p, "rb"); if (!f) return;
  char *t = malloc(65537); size_t g = fread(t, 1, 65536, f); t[g] = 0; fclose(f); cJSON *a = cJSON_Parse(t); free(t); const cJSON *o;
  if (cJSON_IsArray(a)) cJSON_ArrayForEach(o, a) { if (n >= DL_MAX) break; Entry *e = &list[n]; memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", str(o, "name")); snprintf(e->type, sizeof e->type, "%s", str(o, "type")); snprintf(e->id, sizeof e->id, "%s", str(o, "id"));
    snprintf(e->vid, sizeof e->vid, "%s", str(o, "vid")); snprintf(e->file, sizeof e->file, "%s", str(o, "file")); const cJSON *sz = cJSON_GetObjectItemCaseSensitive(o, "size"); e->size = cJSON_IsNumber(sz) ? sz->valuedouble : 0;
    char fp[1100]; snprintf(fp, sizeof fp, "%s/%s", dir, e->file); if (plain_name(e->file) && e->name[0] && !access(fp, R_OK)) n++; } /* (a file taken off the drive: its line goes) */
  cJSON_Delete(a);
}
/* the drive there now: its folder and its list (again whenever another drive comes) */
static int drive(UsbDrive *d) {
  UsbDrive t; if (!d) d = &t; if (!usb_drive(d)) { if (seenId[0]) { seenId[0] = 0; dir[0] = 0; n = 0; serial++; } return 0; }
  if (strcmp(seenId, d->id)) { snprintf(seenId, sizeof seenId, "%s", d->id); snprintf(dir, sizeof dir, "%s/StremioOrigin", d->mnt); load(); serial++; }
  return 1;
}
int dl_drive(void) { return drive(NULL); }
unsigned dl_serial(void) { return serial + (unsigned)(A.on ? (int)(A.got / (8 << 20)) : 0); } /* (moves as the download does: its line follows) */
int dl_count(void) { return drive(NULL) ? n + (A.on ? 1 : 0) : 0; }
static void gb(double b, char *o, size_t cap) { if (b >= 1073741824.0) snprintf(o, cap, "%.1f GB", b / 1073741824.0); else snprintf(o, cap, "%.0f MB", b / 1048576.0); }
/* row i: the running one first, then the finished, newest first */
static Entry *at(int i, int *running) { *running = 0; if (A.on) { if (!i) { *running = 1; return &A.e; } i--; } return i >= 0 && i < n ? &list[n - 1 - i] : NULL; }
const char *dl_name(int i) { int r; Entry *e = at(i, &r); return e ? e->name : ""; }
void dl_line(int i, char *out, size_t cap) {
  int r; Entry *e = at(i, &r); out[0] = 0; if (!e) return; char a[24], b[24];
  if (r) { if (A.asking) { snprintf(out, cap, "%s", T("Checking its size and the drive\xe2\x80\x99s room\xe2\x80\xa6")); return; }
    gb(A.got, a, sizeof a); gb(e->size, b, sizeof b); char pc[8]; snprintf(pc, sizeof pc, "%d", e->size > 0 ? (int)(A.got * 100 / e->size) : 0);
    snprintf(out, cap, "%s%%  \xc2\xb7  %s / %s  \xc2\xb7  %s", pc, a, b, T("OK stops it")); }
  else { gb(e->size, a, sizeof a); snprintf(out, cap, "%s  \xc2\xb7  %s", a, T("On the drive. OK plays it")); }
}
static int sink(const unsigned char *p, size_t len, void *ud) { FILE *f = ud; if (A.cancel) return 0; if (len && fwrite(p, 1, len, f) != len) return 0; A.got += (double)len; return 1; }
static void *work(void *arg) {
  (void)arg; mkdir(dir, 0755); FILE *f = fopen(A.part, "wb"); int ok = 0;
  if (f) { net_stream_sync(A.url, 12 * 3600 * 1000, sink, f); ok = !fflush(f) && !A.cancel; fclose(f); if (ok && A.got + 0.5 < A.e.size) ok = 0; } /* whole, or not kept */
  if (ok) ok = rename(A.part, A.full) == 0; if (!ok) remove(A.part);
  fprintf(stderr, "[dl] %s: %s (%.0f of %.0f MB)\n", A.e.file, ok ? "done" : A.cancel ? "stopped" : "failed", A.got / 1048576.0, A.e.size / 1048576.0);
  pthread_mutex_lock(&mu); A.ok = ok; A.done = 1; pthread_mutex_unlock(&mu); return NULL;
}
static void refuse(const char *why) { A.on = 0; A.asking = 0; serial++; toast(why, 4200, NULL); }
static void on_size(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if (!A.on || !A.asking || (unsigned)(size_t)ud != A.req) return; A.asking = 0;
  double size = st >= 200 && st < 400 && body ? atof((const char *)body) : 0; UsbDrive d; struct statvfs v;
  if (size <= 0) { refuse(T("This stream doesn\xe2\x80\x99t say how large it is: not downloaded")); return; }
  if (!drive(&d) || statvfs(d.mnt, &v)) { refuse(T("The drive is gone: not downloaded")); return; }
  double room = (double)v.f_bavail * (double)v.f_frsize; char a[24], b[24]; gb(size, a, sizeof a); gb(room, b, sizeof b);
  if ((!strcasecmp(d.fs, "vfat") || !strcasecmp(d.fs, "msdos") || !strcasecmp(d.fs, "fat32")) && size >= 4294967295.0) { refuse(T1("{n}: too large for this drive\xe2\x80\x99s format (FAT32 holds 4 GB a file). Nothing was downloaded", "n", a)); return; }
  if (size + ROOM_SPARE > room) { char m[300]; snprintf(m, sizeof m, "%s  \xc2\xb7  %s", T1("Not enough room on the drive: this needs {n}", "n", a), T1("{n} free", "n", b)); refuse(m); return; } /* not one byte written */
  A.e.size = size; A.got = 0; A.cancel = 0; A.done = 0; A.ok = 0; mkdir(dir, 0755);
  pthread_t t; if (pthread_create(&t, NULL, work, NULL)) { refuse(T("Could not start the download")); return; } pthread_detach(t);
  serial++; toast(T1("Downloading to the drive: {n}", "n", a), 3200, NULL);
}
void dl_start(const Stream *s, const char *type, const char *id, const char *vid, const char *title) {
  if (!s || !drive(NULL)) { toast(T("Plug in a USB drive to download"), 3000, NULL); return; }
  if (A.on) { toast(T("One download at a time: this one starts when you ask again after it ends"), 3600, NULL); return; }
  if (s->p2p || strncmp(s->url, "http", 4)) { toast(T("Only direct streams can be downloaded, not torrents"), 3600, NULL); return; }
  if (n >= DL_MAX) { toast(T("The downloads list is full: remove some first"), 3200, NULL); return; }
  memset(&A, 0, sizeof A); Entry *e = &A.e; snprintf(e->name, sizeof e->name, "%s", title && *title ? title : id); snprintf(e->type, sizeof e->type, "%s", type); snprintf(e->id, sizeof e->id, "%s", id); snprintf(e->vid, sizeof e->vid, "%s", vid ? vid : "");
  /* the file's name from the title: letters, digits and a few marks; its ending from the stream's own (a video's), else .mkv */
  char base[96]; size_t o = 0; for (const char *p = e->name; *p && o + 1 < sizeof base; p++) { unsigned char c = (unsigned char)*p; base[o++] = (c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '(' || c == ')') ? (char)c : '_'; } base[o] = 0;
  const char *ext = ".mkv"; { static const char *const V[] = { ".mkv", ".mp4", ".avi", ".ts", ".m4v", ".webm", ".mov" }; char u[SB_URL_MAX]; snprintf(u, sizeof u, "%s", s->filename[0] ? s->filename : s->url); char *q = strchr(u, '?'); if (q) *q = 0; char *d = strrchr(u, '.');
    if (d) for (unsigned i = 0; i < sizeof V / sizeof *V; i++) if (!strcasecmp(d, V[i])) ext = V[i]; }
  snprintf(e->file, sizeof e->file, "%s_%s%s", base, e->vid[0] ? strrchr(e->vid, ':') ? strrchr(e->vid, ':') + 1 : "0" : "film", ext); for (char *p = e->file; *p; p++) if (*p == ':' || *p == '/') *p = '_';
  snprintf(A.url, sizeof A.url, "%s", s->url); snprintf(A.full, sizeof A.full, "%s/%s", dir, e->file); snprintf(A.part, sizeof A.part, "%s.part", A.full);
  static unsigned rq; A.on = 1; A.asking = 1; A.req = ++rq; serial++;
  net_size(A.url, on_size, (void *)(size_t)A.req); /* its size first: nothing is written before the drive's room is known to be enough */
}
int dl_act(int i, Stream *play, char *type, char *id, char *vid) {
  int r; Entry *e = at(i, &r); if (!e) return 0;
  if (r) { if (A.asking) refuse(T("Download stopped")); else { A.cancel = 1; toast(T("Download stopped"), 2400, NULL); } return 0; }
  memset(play, 0, sizeof *play); snprintf(play->url, sizeof play->url, "file://%s/%s", dir, e->file); snprintf(play->name, sizeof play->name, "%s", T("Downloaded")); snprintf(play->title, sizeof play->title, "%s", e->name); snprintf(play->filename, sizeof play->filename, "%s", e->file); play->size = e->size;
  snprintf(type, 16, "%s", e->type); snprintf(id, 80, "%s", e->id); snprintf(vid, 96, "%s", e->vid[0] ? e->vid : e->id); return 1;
}
void dl_clear(void) { /* every finished download off the drive */
  if (!drive(NULL)) return; for (int i = 0; i < n; i++) if (plain_name(list[i].file)) { char fp[1100]; snprintf(fp, sizeof fp, "%s/%s", dir, list[i].file); remove(fp); }
  n = 0; save(); serial++; toast(T("Downloads removed from the drive"), 2600, NULL);
}
void dl_pump(void) {
  if (!A.on || A.asking) return; pthread_mutex_lock(&mu); int done = A.done, ok = A.ok; pthread_mutex_unlock(&mu); if (!done) return;
  A.on = 0; serial++;
  if (ok && n < DL_MAX) { list[n++] = A.e; save(); toast(T1("Downloaded: {n}", "n", A.e.name), 3600, NULL); }
  else if (!A.cancel) toast(T("The download failed: nothing was kept"), 3600, NULL);
}
void dl_shutdown(void) { if (A.on && !A.asking) { A.cancel = 1; for (int i = 0; i < 150 && !A.done; i++) usleep(20000); } }
