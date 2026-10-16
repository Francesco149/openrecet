/*
 * movie.h — the title's attract movie (recet_op.wmv) through DirectShow.
 *
 * Retail: FUN_0040cc87 (open + run), FUN_0040ce0d (release), FUN_0040cea6
 * (the per-tick poll at FUN_004536cb's head).  The title's main-menu counter
 * DAT_09643518 reaching 7140 opens the movie; while it plays the whole sim
 * tick returns after the button ring, and A (pressed 0x10) or the end of the
 * movie (EC_COMPLETE) releases it and zeroes the counter.  The movie's end is
 * wall-clock (DirectShow plays in real time) in retail too.
 */
#ifndef OPENRECET_MOVIE_H
#define OPENRECET_MOVIE_H

#include <stdint.h>

/* FUN_0040cc87: release any graph, DAT_006483bc = 1, build the filter graph
 * for `file` (relative to the game directory), owner = the game window, run.
 * 1 = playing. */
int  movie_open(const char *file);

/* FUN_0040ce0d: stop + release every interface. */
void movie_release(void);

/* FUN_0040cea6: 0 when no graph exists; else the BGM volume, the completion
 * poll (EC_COMPLETE → DAT_09643518 = 0, DAT_005d1960 = -1, release) and 1. */
int  movie_poll(void);

/* the game window the video renders into (DAT_073dfc7c / hWndParent) */
void movie_set_owner(void *hwnd);

/* --silent-audio: the movie's audio at -10000 cB (harness only) */
void movie_set_silent(int on);

#endif
