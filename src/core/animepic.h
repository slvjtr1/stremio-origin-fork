/* Anime picture: while an anime plays, the TV's own picture processor is turned toward drawn pictures (its Super
   Resolution up, gradation smoothing and compression-noise cleaning on), then put back as it was when the player
   closes. The TV draws the video itself, so its processor is the one that can work on it, on the way to the panel's 4K.
   Needs the Homebrew Channel with root (the same way the app reopens after an update); without it nothing changes. */
#ifndef SB_ANIMEPIC_H
#define SB_ANIMEPIC_H
int  animepic_enabled(void);     /* Settings > Playback > Anime picture */
void animepic_start(void);       /* an anime starts: tuned on a worker thread */
void animepic_stop(void);        /* the player closes: put back as it was */
int  animepic_poll(void);        /* 1 once, when the picture was tuned (the notice) */
void animepic_tick(void);        /* per frame (main thread): the store follows the workers */
void animepic_quit(void);        /* the app closing: put back at once */
void animepic_recover(void);     /* app start: a picture left tuned (the app closed mid-anime) is put back */
#endif
