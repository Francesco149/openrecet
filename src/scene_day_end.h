/*
 * scene_day_end.h — the day-end summary ("Daily Profits"), pause action 1.
 *
 * Opened by the day close (FUN_004536b9 = FUN_00453384(1), scene1_day_close.c)
 * through the pause ramp: scene 9 + worker case 9 with DAT_06a4997c == 1 →
 * FUN_0048da71 (init) + FUN_00473618 (assets); then FUN_004536cb runs
 * FUN_0048dbfb every frame while the ramp is past 3, and the render dispatch
 * draws FUN_0048ef00.
 *
 * The screen: three panels (Sold / Bought / Found: today's item lists, work
 * dwords 44999 + 100·k, counts 45299 + k), the "Daily Profits / Day N" banner,
 * "Profits" counting up to gold − day-start gold, "Score Difference" counting
 * up to today's score, Tear's rating stamp (hanko.tga) + voice; Z after the
 * count → the next day (or, on a payment day, the debt payment and its
 * outcome).
 *
 * RE: docs/findings/day-close-RE.md §5.
 */
#ifndef OPENRECET_SCENE_DAY_END_H
#define OPENRECET_SCENE_DAY_END_H

#include <stdint.h>

/* FUN_0048da71 — reset the screen and compute its targets from the work
 * record (writes the score total and the period-end cache).  Also the
 * next-day re-init. */
void day_end_init(void);

/* FUN_0048dbfb — one summary frame.  `pressed`/`held` = DAT_073dddd4/d6. */
void day_end_tick(uint16_t pressed, uint16_t held);

/* Accessors (state_va / tests). */
int day_end_frame(void);           /* DAT_074b2f58 */
int day_end_profit_shown(void);    /* DAT_074b2f1c */
int day_end_profit(void);          /* DAT_074b2f20 */
int day_end_score_shown(void);     /* DAT_074b2f24 */
int day_end_after(void);           /* DAT_074b2f2c */
int day_end_rating(void);          /* DAT_074b2f34 */
int day_end_exit_phase(void);      /* DAT_074b2f08 */
int day_end_paying(void);          /* DAT_074b2f0c */

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_00473618 — choushuu.tga, result_bord01.tga, hanko.tga. */
void day_end_load_assets(struct IDirect3DDevice8 *dev);
/* FUN_0048ef00. */
void day_end_render(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_SCENE_DAY_END_H */
