#pragma once
/* Downloads: films and episodes kept on the USB drive (see downloads.c). Everything here is the main thread's */
#include "core/stremio.h"
int  dl_drive(void);                 /* a drive is there: only then is anything of this shown */
int  dl_count(void);                 /* the rows: the one in flight first, then the finished, newest first */
const char *dl_name(int i);
void dl_line(int i, char *out, size_t cap); /* "42%  ·  1.2 GB / 2.9 GB  ·  OK stops it" or "2.9 GB  ·  On the drive. OK plays it" */
unsigned dl_serial(void);            /* changes when the rows or their lines do */
void dl_start(const Stream *s, const char *type, const char *id, const char *vid, const char *title);
int  dl_act(int i, Stream *play, char *type, char *id, char *vid); /* OK on row i: 1 = play this (type 16, id 80, vid 96 bytes); 0 = it was the one in flight, now stopped */
void dl_clear(void);
void dl_pump(void);                  /* every frame */
void dl_shutdown(void);
