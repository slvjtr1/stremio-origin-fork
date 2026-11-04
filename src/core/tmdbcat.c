#include "core/tmdbcat.h"
#include "core/tmdb.h"
#include "core/store.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- TMDB id -> IMDb id, kept: "m<id>" / "t<id>" -> "tt..." ("" = TMDB knows none) ----------------------------- */
#define MAP_KEY "tmdb:imdb-map"
static cJSON *map; static int mapDirty;
static void map_load(void) {
  if (map) return; size_t n = 0; unsigned char *t = cache_read(MAP_KEY, 0, &n);
  map = t ? cJSON_Parse((const char *)t) : NULL; free(t); if (!cJSON_IsObject(map)) { cJSON_Delete(map); map = cJSON_CreateObject(); }
}
static void map_save(void) {
  if (!mapDirty || !map) return; mapDirty = 0;
  if (cJSON_GetArraySize(map) > 6000) { cJSON *f = map->child; for (int i = 0; f && i < 1500; i++) { cJSON *nx = f->next; cJSON_Delete(cJSON_DetachItemViaPointer(map, f)); f = nx; } } /* the oldest go */
  char *s = cJSON_PrintUnformatted(map); if (s) { cache_write(MAP_KEY, (unsigned char *)s, strlen(s)); free(s); }
}
static const char *map_get(const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(map, k); return cJSON_IsString(v) ? v->valuestring : NULL; }

/* ---- one list being answered ---------------------------------------------------------------------------------- */
typedef struct { NetCb cb; void *ud; cJSON *res; int pending, done; char kind; } Job; /* kind: 'm', 't' or 0 (each result says) */
typedef struct { Job *j; char key[24]; } Ext;
static char kind_of(const Job *j, const cJSON *r) {
  if (j->kind) return j->kind;
  const char *mt = js_str(r, "media_type", ""); return !strcmp(mt, "movie") ? 'm' : !strcmp(mt, "tv") ? 't' : 0;
}
static void finish(Job *j) {
  if (j->done) return; j->done = 1;
  cJSON *out = cJSON_CreateObject(), *metas = cJSON_AddArrayToObject(out, "metas"), *r;
  cJSON_ArrayForEach(r, j->res) {
    char k = kind_of(j, r); if (!k || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "adult"))) continue;
    char key[24]; snprintf(key, sizeof key, "%c%d", k, (int)js_num(r, "id", 0)); const char *tt = map_get(key); if (!tt || strncmp(tt, "tt", 2)) continue;
    const char *nm = js_str(r, k == 'm' ? "title" : "name", ""), *pp = js_str(r, "poster_path", ""), *bp = js_str(r, "backdrop_path", ""), *dt = js_str(r, k == 'm' ? "release_date" : "first_air_date", "");
    if (!*nm) continue;
    cJSON *m = cJSON_CreateObject(); char u[300];
    cJSON_AddStringToObject(m, "id", tt); cJSON_AddStringToObject(m, "type", k == 'm' ? "movie" : "series"); cJSON_AddStringToObject(m, "name", nm);
    if (*pp) { snprintf(u, sizeof u, "https://image.tmdb.org/t/p/w342%s", pp); cJSON_AddStringToObject(m, "poster", u); }
    if (*bp) { snprintf(u, sizeof u, "https://image.tmdb.org/t/p/w1280%s", bp); cJSON_AddStringToObject(m, "background", u); }
    cJSON_AddStringToObject(m, "description", js_str(r, "overview", ""));
    if (strlen(dt) >= 4) { char y[8]; snprintf(y, sizeof y, "%.4s", dt); cJSON_AddStringToObject(m, "releaseInfo", y); }
    cJSON_AddItemToArray(metas, m); }
  char *txt = cJSON_PrintUnformatted(out); cJSON_Delete(out); cJSON_Delete(j->res); map_save();
  j->cb(200, (const unsigned char *)txt, strlen(txt), j->ud); free(txt); free(j);
}
static void on_ext(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; Ext *e = ud; Job *j = e->j; cJSON *x = st == 200 ? net_json(b) : NULL;
  if (x) { const char *tt = js_str(x, "imdb_id", ""); cJSON_DeleteItemFromObjectCaseSensitive(map, e->key); cJSON_AddStringToObject(map, e->key, tt && !strncmp(tt, "tt", 2) ? tt : ""); mapDirty = 1; } /* (no answer: asked again next time) */
  net_json_free(x); free(e);
  if (--j->pending <= 0) finish(j);
}
static void on_list(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; Job *j = ud; cJSON *x = st == 200 ? net_json(b) : NULL; const cJSON *rs = js_arr(x, "results");
  if (!rs) { net_json_free(x); j->cb(st == 200 ? 404 : (st ? st : -1), NULL, 0, j->ud); free(j); return; }
  map_load(); j->res = cJSON_Duplicate(rs, 1); net_json_free(x); j->pending = 1; /* (the list's own count, let go below) */
  const cJSON *r; cJSON_ArrayForEach(r, j->res) {
    char k = kind_of(j, r); int id = (int)js_num(r, "id", 0); if (!k || !id) continue;
    char key[24]; snprintf(key, sizeof key, "%c%d", k, id); if (map_get(key)) continue;
    Ext *e = calloc(1, sizeof *e); e->j = j; snprintf(e->key, sizeof e->key, "%s", key); char p[64]; snprintf(p, sizeof p, "/%s/%d/external_ids", k == 'm' ? "movie" : "tv", id);
    j->pending++; if (!tmdb_request(p, on_ext, e)) { j->pending--; free(e); } }
  if (--j->pending <= 0) finish(j);
}

int tmdbcat_is(const char *url) { return url && !strncmp(url, "tmdb:", 5); }
void tmdbcat_make(const char *path, char *out, size_t cap) { snprintf(out, cap, "tmdb:%s:%s", tmdb_lang(), path); }
void tmdbcat_url(const char *url, int page, NetCb cb, void *ud) {
  /* tmdb:<lang>:<path>[?query][.json] */
  const char *lang = url + 5, *c = strchr(lang, ':'); if (!c) { cb(-1, NULL, 0, ud); return; }
  char lg[8]; snprintf(lg, sizeof lg, "%.*s", (int)(c - lang < 7 ? c - lang : 7), lang);
  char path[700]; snprintf(path, sizeof path, "/%s", c + 1); size_t pl = strlen(path); if (pl > 5 && !strcmp(path + pl - 5, ".json")) path[pl - 5] = 0;
  char full[900]; snprintf(full, sizeof full, "%s%clanguage=%s&include_adult=false&page=%d", path, strchr(path, '?') ? '&' : '?', lg, page < 1 ? 1 : page);
  Job *j = calloc(1, sizeof *j); j->cb = cb; j->ud = ud;
  j->kind = strstr(path, "/multi") || strstr(path, "/all/") ? 0 : (!strncmp(path, "/tv", 3) || strstr(path, "/tv/") || strstr(path, "/tv?")) ? 't' : 'm';
  if (!tmdb_request(full, on_list, j)) { free(j); cb(-1, NULL, 0, ud); } /* no key */
}
int tmdbcat_genre(const char *type, const char *name) {
  static const struct { const char *n; int m, t; } G[] = {
    { "Action", 28, 10759 }, { "Adventure", 12, 10759 }, { "Animation", 16, 16 }, { "Comedy", 35, 35 }, { "Crime", 80, 80 }, { "Documentary", 99, 99 },
    { "Drama", 18, 18 }, { "Family", 10751, 10751 }, { "Fantasy", 14, 10765 }, { "History", 36, 10768 }, { "Horror", 27, 9648 }, { "Music", 10402, 0 },
    { "Mystery", 9648, 9648 }, { "Romance", 10749, 18 }, { "Sci-Fi", 878, 10765 }, { "Thriller", 53, 80 }, { "War", 10752, 10768 }, { "Western", 37, 37 },
    { "Reality-TV", 0, 10764 }, { "Talk-Show", 0, 10767 }, { "News", 0, 10763 } };
  int tv = type && !strcmp(type, "series");
  for (size_t i = 0; i < sizeof G / sizeof *G; i++) if (name && !strcasecmp(G[i].n, name)) return tv ? G[i].t : G[i].m;
  return 0;
}
