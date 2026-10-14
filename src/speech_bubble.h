/*
 * speech_bubble.h — the in-scene speech bubble widget (engine FUN_00406159
 * family).  One bubble at a time: up to 4 text lines split on "<BR>", a
 * pop-in scale (DAT_0064825c / 10), a 10-frame fade-out, an optional
 * mirrored tail (FUN_0040697d) and a text scale (DAT_00529700).
 *
 * Writers: the actor-say driver FUN_00485861 (scene1_actor_say.c — the
 * day-close lines, dungeon pickups); state reset by FUN_004060ff (scene
 * entry) and FUN_0040622f (the day-end summary init FUN_0048da71).
 *
 * Engine globals:
 *   DAT_00647e20  close counter (1 = closing; ++ to 10 → hidden)
 *   DAT_00647e24  4 × 256-byte lines
 *   DAT_00648224/228  screen x / y (640×480)
 *   DAT_0064822c..238 per-line x offsets (only ever 0)
 *   DAT_00648244  line count
 *   DAT_00648250  "a bubble was opened" (write-only here)
 *   DAT_00648254  timed variant (auto-close at age 180; small box)
 *   DAT_00648258  age (1 at open, ++ per FUN_00406584 tick; 0 = hidden)
 *   DAT_0064825c  pop counter (scale = min(pop/10, 1))
 *   DAT_00648260/264  tail mirror flag / its pair
 *   DAT_00648268  (zeroed at open; no reader)
 *   DAT_00529700  text scale (.data 0.5)
 */
#ifndef OPENRECET_SPEECH_BUBBLE_H
#define OPENRECET_SPEECH_BUBBLE_H

#define SPEECH_BUBBLE_LINES    4
#define SPEECH_BUBBLE_LINE_LEN 256

/* FUN_00406159 — open a bubble at (x, y) with `text` ("<BR>" = new line).
 * SE 0x174 on the first open since the last reset, else 0x172. */
void speech_bubble_open(float x, float y, const char *text);

/* FUN_00406212 — start the 10-frame close; SE 0x174 when `se` != 0. */
void speech_bubble_close(int se);

/* FUN_0040622f — hide at once (close counter, age, pop = 0). */
void speech_bubble_hide(void);

/* FUN_004060ff's bubble part: hide + zero the line offsets / timed flag. */
void speech_bubble_reset(void);

void speech_bubble_set_mirror(int a, int b);     /* FUN_0040697d */
void speech_bubble_set_text_scale(float s);      /* FUN_00406990 */
void speech_bubble_set_pos(float x, float y);    /* FUN_004069a6 */

/* FUN_00406584's bubble block (all.c:4615-4630): age/pop ++, the timed
 * variant's auto-close at 180, the close countdown. */
void speech_bubble_tick(void);

/* Accessors (tests, state_va, render). */
int         speech_bubble_age(void);            /* DAT_00648258 */
int         speech_bubble_pop(void);            /* DAT_0064825c */
int         speech_bubble_close_ctr(void);      /* DAT_00647e20 */
int         speech_bubble_line_count(void);     /* DAT_00648244 */
const char *speech_bubble_line(int i);          /* DAT_00647e24 + i·256 */
int         speech_bubble_mirror(void);         /* DAT_00648260 */
float       speech_bubble_x(void);              /* DAT_00648224 */
float       speech_bubble_y(void);              /* DAT_00648228 */
float       speech_bubble_text_scale(void);     /* DAT_00529700 */

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_0040c4eb — draw the bubble + its text (HUD aggregator, before the
 * merchant HUD).  `suppress` = the engine's scene gates (dialogue up,
 * load screen, …) folded by the caller. */
void speech_bubble_render(struct IDirect3DDevice8 *dev, int suppress);
#endif

#endif /* OPENRECET_SPEECH_BUBBLE_H */
