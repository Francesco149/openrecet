/*
 * scene1_counter_menu.h — the HOUSE counter menu ("Open Store" / … / "Cancel").
 *
 * Z at the sell counter while the shop is closed for the day (DAT_0450f3ff == 0)
 * does NOT enter customer service directly: FUN_0048670f sets cc08 = 0, builds
 * the entry list (FUN_004850fe) and the player picks an entry.  Z on an entry
 * sets cc08 = 10 — a 15-frame close (DAT_0438b754) — after which the entry is
 * dispatched: "Open Store" → cc08 = 4 + FUN_0045edaa (the general shop session),
 * "Cancel" → cc08 = 1.  The top-left help box "Opening the shop will take 1
 * period of time" (DAT_00529708 band 2) shows while the cursor is on entry 0.
 *
 * The cc08 dispatch (it writes the player controller's cc08) stays in
 * scene1_player_ctrl.c, like the cc04 display menu; this module owns the menu
 * state, the list build, the slide ramp and the render.
 *
 * Engine globals mirrored:
 *   DAT_074b2d98[] / DAT_005cd0bc   list A (counter menu) entries / count
 *   DAT_074b28b0[] / DAT_005cd0c0   list B (the cc08 15/16 Tear menu, built by the
 *                                   same FUN_004850fe)
 *   DAT_0438b8c0 / b8c4 / b8c8      the cc08 17 / 18 window slide (0..10), the
 *                                   lecture cursor (0..28) and its scroll (0..24)
 *   DAT_0438cc0c / DAT_0438cc10     cursor / secondary cursor
 *   DAT_0438b74c / DAT_0438b750     slide-in counters (0..12)
 *   DAT_0438b754                    close timer (cc08 10/16: 1..15)
 */
#ifndef OPENRECET_SCENE1_COUNTER_MENU_H
#define OPENRECET_SCENE1_COUNTER_MENU_H

#include <stdint.h>

#define COUNTER_MENU_MAX           8
#define COUNTER_MENU_OPEN_STORE    0   /* off_0052974c[0] "Open Store" */
#define COUNTER_MENU_CANCEL        6   /* off_0052974c[6] "Cancel"     */

/* FUN_004850fe — build list A (+ list B) from the working bank (byte view of
 * &DAT_044e3798 + slot*188360).  NULL bank → the bank-less defaults (level 0). */
void counter_menu_build(const uint8_t *bank);

/* The counter-Z open (FUN_0048670f asm 0x488bdf-0x488bf0): cursor, secondary
 * cursor and close timer → 0.  The caller sets cc08 = 0 and builds the list. */
void counter_menu_open(void);

/* The per-frame slide ramp (FUN_0048670f all.c:96212-96225), run before the cc08
 * dispatch with that frame's cc08: b74c +1 (max 12) while cc08 ∈ {0,10,15,16,30},
 * else −1 (min 0); b750 likewise for cc08 == 30. */
void counter_menu_slide_tick(int cc08);

/* cc08 == 0 input (FUN_0048670f case 0, asm 0x4878b3-0x487924), after the slide
 * gate.  pressed = DAT_073dddd4 (edge), held = DAT_073dddd6 (repeat).  Returns
 * COUNTER_MENU_IN_* and plays the nav / confirm / cancel SE. */
#define COUNTER_MENU_IN_NONE     0
#define COUNTER_MENU_IN_CANCEL   1   /* 0x20 → cc08 = 1, SE 0x13d */
#define COUNTER_MENU_IN_CONFIRM  2   /* 0x10 → cc08 = 10, SE 0x143 */
int counter_menu_input(uint16_t pressed, uint16_t held);

/* cc08 == 10 (case 10, asm 0x4870b9): ++b754; at 15 returns the selected entry
 * id (list A [cursor]), else -1. */
int counter_menu_close_tick(void);

int counter_menu_count(void);          /* DAT_005cd0bc */
int counter_menu_entry(int i);         /* DAT_074b2d98[i] */
int counter_menu_cursor(void);         /* DAT_0438cc0c */
int counter_menu_slide(void);          /* DAT_0438b74c */
int counter_menu_timer(void);          /* DAT_0438b754 */
const char *counter_menu_label(int entry);   /* off_0052974c[entry] */

/* ── Tear's menu (cc08 15 / 16 / 17 / 18 / 30, FUN_0048670f) ─────────────── */
#define TEAR_MENU_MAX_LECTURE  28

/* cc08 15 after the slide gate (b74c >= 10): B → TEAR_MENU_IN_CANCEL (SE
 * 0x13d), A → b754 = 0 + SE 0x143 → TEAR_MENU_IN_CONFIRM, else the cursor
 * over list B (SE 0x146). */
#define TEAR_MENU_IN_NONE     0
#define TEAR_MENU_IN_CANCEL   1
#define TEAR_MENU_IN_CONFIRM  2
int  tear_menu_input(uint16_t pressed, uint16_t held);
/* cc08 15's head: the window slide b8c0 closes 1 a tick */
void tear_menu_window_close_tick(void);
/* cc08 16: ++b754; at 15 the selected list-B entry, else -1 */
int  tear_menu_close_tick(void);
/* cc08 16 entry 5: the lectures (cc08 18) — the cursor on the last lecture the
 * merchant level opens (DAT_005291c0's levels <= `level`, max 28), the scroll
 * one above it (max 24); SE 0x174; the hand cursor at (152, 32·(b8c4−b8c8)+164) */
void tear_lecture_open(int32_t level);
/* cc08 17 / 18: the window slides in (b8c0 → 10); then A / B returns 1 (the
 * caller goes back to 15); cc08 18 moves the cursor (up / down 1, the two
 * page keys 5, SE 0x146 + the hand cursor slide) */
int  tear_note_tick(uint16_t pressed);
int  tear_lecture_tick(uint16_t pressed, uint16_t held);
/* cc08 30, the "open with" choice (cc10 over 1 + the work bytes +0x2bc90 /
 * +0x2bc91): returns TEAR_MENU_IN_CANCEL on B (once b750 is in), 1 + the
 * choice (b754 reaching 15 after A) as TEAR_MENU_IN_CONFIRM with *choice set */
int  tear_open_menu_tick(const uint8_t *bank, uint16_t pressed, uint16_t held, int *choice);
int  tear_menu_cursor2(void);          /* DAT_0438cc10 */
void tear_menu_set_cursor2(int v);
void tear_menu_set_timer(int v);       /* DAT_0438b754 */
int  tear_menu_window(void);           /* DAT_0438b8c0 */
int  tear_lecture_cursor(void);        /* DAT_0438b8c4 */
int  tear_lecture_top(void);           /* DAT_0438b8c8 */
int  counter_menu_slide2(void);        /* DAT_0438b750 */

void counter_menu_stage_reset(void);   /* FUN_00436f97: slides + timer + b8c0..b8c8 = 0 */
void counter_menu_reset(void);         /* BSS state (tests / new process) */

/* FUN_00409925's cc08 0/10 block: the entry buttons + labels (item_win.tga).
 * Inert for any other cc08. */
struct IDirect3DDevice8;
void counter_menu_render(struct IDirect3DDevice8 *dev, int cc08);

#endif /* OPENRECET_SCENE1_COUNTER_MENU_H */
