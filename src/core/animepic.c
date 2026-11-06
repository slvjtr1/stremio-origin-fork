#include "core/animepic.h"
#include "core/store.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXEC "luna://org.webosbrew.hbchannel.service/exec"
#define SAVED "animePicSaved"
/* the picture settings asked for, and their values for a drawn picture: only those this TV has are changed */
static const struct { const char *k, *v; } WANT[] = {
  { "superResolution", "high" },        /* the TV's own upscaler, at its strongest: sharper lines on the way to 4K */
  { "smoothGradation", "medium" },      /* the flat colour fills without bands */
  { "mpegNoiseReduction", "medium" },   /* the blocks and ringing of a compressed stream around the lines */
  { "noiseReduction", "off" },          /* no smearing of fine line work */
};
#define NW ((int)(sizeof WANT / sizeof *WANT))

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int busy, on, gen, notice, clearSaved;
static cJSON *toSave; /* handed to the main thread: the store is kept there */
static cJSON *saved; /* the values before (owned here; a copy is kept in the store until put back) */

int animepic_enabled(void) { return !strcmp(store_get("animeEnhance", "true"), "true"); }

/* a command through the Channel: its output, or NULL (no Channel, no root, refused) */
static cJSON *sh(const char *cmd) {
  cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "command", cmd); char *ps = cJSON_PrintUnformatted(p); cJSON_Delete(p);
  char *r = plat_luna_call(EXEC, ps, 4000); free(ps); if (!r) return NULL;
  cJSON *j = cJSON_Parse(r); free(r); if (!j) return NULL;
  cJSON *rv = cJSON_GetObjectItemCaseSensitive(j, "returnValue"), *out = cJSON_GetObjectItemCaseSensitive(j, "stdoutString");
  cJSON *o = cJSON_IsTrue(rv) && cJSON_IsString(out) ? cJSON_Parse(out->valuestring) : NULL; cJSON_Delete(j); return o;
}
static int clean(const char *s) { for (; *s; s++) if (*s == '\'' || *s == '\\' || *s == '`' || *s == '$') return 0; return 1; } /* nothing that could leave the quotes */
static int set_picture(const cJSON *settings) {
  char *js = cJSON_PrintUnformatted(settings); if (!js) return 0; int ok = 0;
  if (clean(js)) { char cmd[1200]; snprintf(cmd, sizeof cmd, "luna-send -n 1 -f luna://com.webos.settingsservice/setSystemSettings '{\"category\":\"picture\",\"settings\":%s}'", js);
    cJSON *o = sh(cmd); ok = o && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "returnValue")); cJSON_Delete(o); }
  free(js); return ok;
}
static void *stop_job(void *ud);
static void *start_job(void *ud) {
  int g = (int)(long)ud;
  char cmd[600]; int n = snprintf(cmd, sizeof cmd, "luna-send -n 1 -f luna://com.webos.settingsservice/getSystemSettings '{\"category\":\"picture\",\"keys\":[");
  for (int i = 0; i < NW; i++) n += snprintf(cmd + n, sizeof cmd - n, "%s\"%s\"", i ? "," : "", WANT[i].k); snprintf(cmd + n, sizeof cmd - n, "]}'");
  cJSON *o = sh(cmd), *have = o ? cJSON_GetObjectItemCaseSensitive(o, "settings") : NULL;
  cJSON *old = cJSON_CreateObject(), *nw = cJSON_CreateObject(); int k = 0;
  for (int i = 0; i < NW && cJSON_IsObject(have); i++) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(have, WANT[i].k);
    if (cJSON_IsString(v) && clean(v->valuestring)) { cJSON_AddStringToObject(old, WANT[i].k, v->valuestring); if (strcmp(v->valuestring, WANT[i].v)) { cJSON_AddStringToObject(nw, WANT[i].k, WANT[i].v); k++; } } }
  int ok = 0;
  pthread_mutex_lock(&mu); int still = g == gen; pthread_mutex_unlock(&mu);
  if (still && cJSON_GetArraySize(old)) ok = !k || set_picture(nw);
  fprintf(stderr, "[animepic] %s (%d of %d settings on this TV, %d changed)\n", !o ? "no Homebrew Channel root: the picture stays as it is" : ok ? "tuned for a drawn picture" : "the TV did not take it", cJSON_GetArraySize(old), NW, k);
  cJSON_Delete(o); cJSON_Delete(nw);
  pthread_mutex_lock(&mu);
  if (ok) { cJSON_Delete(saved); saved = old; cJSON_Delete(toSave); toSave = cJSON_Duplicate(old, 1); old = NULL; on = 1; if (g == gen) notice = 1; } /* (kept in the store: put back even if the app closes mid-anime) */
  int stopNow = on && g != gen; busy = 0; pthread_mutex_unlock(&mu);
  cJSON_Delete(old);
  if (stopNow) animepic_stop(); /* the player closed while it worked */
  return NULL;
}
static void *stop_job(void *ud) {
  cJSON *s = ud; int ok = set_picture(s); fprintf(stderr, "[animepic] picture %s\n", ok ? "put back" : "could not be put back (tried again next start)");
  if (ok) { pthread_mutex_lock(&mu); clearSaved = 1; pthread_mutex_unlock(&mu); } cJSON_Delete(s); return NULL;
}
/* one worker, the jobs in the order asked: a picture put back is back before the next episode reads it */
static pthread_cond_t qcv = PTHREAD_COND_INITIALIZER; static void *(*qfn[16])(void *); static void *qarg[16]; static int qh, qn, worker;
static void *run(void *ud) { (void)ud;
  for (;;) { pthread_mutex_lock(&mu); while (!qn) pthread_cond_wait(&qcv, &mu); void *(*fn)(void *) = qfn[qh]; void *a = qarg[qh]; qh = (qh + 1) % 16; qn--; pthread_mutex_unlock(&mu); fn(a); } return NULL; }
static void spawn(void *(*fn)(void *), void *arg) {
  pthread_mutex_lock(&mu);
  if (!worker) { pthread_t t; if (!pthread_create(&t, NULL, run, NULL)) { pthread_detach(t); worker = 1; } }
  if (worker && qn < 16) { int i = (qh + qn) % 16; qfn[i] = fn; qarg[i] = arg; qn++; pthread_cond_signal(&qcv); pthread_mutex_unlock(&mu); return; }
  pthread_mutex_unlock(&mu); if (fn == stop_job) cJSON_Delete(arg); else { pthread_mutex_lock(&mu); busy = 0; pthread_mutex_unlock(&mu); } /* (no thread: nothing done) */
}

void animepic_start(void) {
  if (!animepic_enabled()) return;
  pthread_mutex_lock(&mu); int go = !busy && !on; if (go) { busy = 1; gen++; } int g = gen; pthread_mutex_unlock(&mu);
  if (go) spawn(start_job, (void *)(long)g);
}
void animepic_stop(void) {
  pthread_mutex_lock(&mu); gen++; notice = 0; cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } pthread_mutex_unlock(&mu);
  if (s) spawn(stop_job, s);
}
void animepic_tick(void) {
  static int first = 1; if (first) { first = 0; animepic_recover(); } /* once, a moment after start */
  pthread_mutex_lock(&mu); cJSON *s = toSave; toSave = NULL; int c = clearSaved; clearSaved = 0; pthread_mutex_unlock(&mu);
  if (s) { store_set_node(SAVED, s); store_flush(); }
  if (c && !s) { store_remove(SAVED); store_flush(); }
}
int animepic_poll(void) { pthread_mutex_lock(&mu); int n = notice; notice = 0; pthread_mutex_unlock(&mu); return n; }
void animepic_recover(void) {
  cJSON *s = store_node(SAVED); if (!cJSON_IsObject(s) || !cJSON_GetArraySize(s)) return;
  fprintf(stderr, "[animepic] the picture was left tuned: putting it back\n"); spawn(stop_job, cJSON_Duplicate(s, 1));
}
/* the app closing with an anime still tuned: put back now, before it goes */
void animepic_quit(void) {
  pthread_mutex_lock(&mu); cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } gen++; pthread_mutex_unlock(&mu);
  if (s && set_picture(s)) store_remove(SAVED); cJSON_Delete(s);
}
