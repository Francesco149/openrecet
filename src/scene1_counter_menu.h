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
 *                                   same FUN_004850fe; its arms are unported)
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

void counter_menu_reset(void);         /* BSS state (tests / new process) */

/* FUN_00409925's cc08 0/10 block: the entry buttons + labels (item_win.tga).
 * Inert for any other cc08. */
struct IDirect3DDevice8;
void counter_menu_render(struct IDirect3DDevice8 *dev, int cc08);

#endif /* OPENRECET_SCENE1_COUNTER_MENU_H */
