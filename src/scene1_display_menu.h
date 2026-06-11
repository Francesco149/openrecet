/*
 * scene1_display_menu.h — the cc04==1 in-house display-stand "remove item"
 * menu (A2): the shared inventory-picker UPDATE + the confirm→removal that
 * writes the display grid + returns the item to the inventory.
 *
 * This is the per-frame half of the interaction the A0/A0b/A1 chips set up
 * (scene1_shop_display.c = the layout grid + cell highlight + the open gate).
 * When the player faces a stand and presses Z, scene1_player_ctrl's open gate
 * sets cc04=1 and calls display_menu_open(); from then on, while cc04 != 0, the
 * cc04 menu arm calls display_menu_update() every frame and acts on its 1/2/3
 * return — the removal, the cancel, and the brief pick-up pose.
 *
 * Engine functions ported here (all.c / vendor/unpacked asm):
 *   - FUN_00468338 (0x468338) — the inventory-window OPEN/build.  A2 ports the
 *     LIST-INIT only (the index-0 "select none" entry + single tab the removal
 *     path uses).  PORT-DEBT(A3, FUN_00468338-population): the full inventory
 *     scan + item-DB category filter + FUN_0045526a sort that fills the rest of
 *     the list is deferred to the render chip — it only changes what the menu
 *     DISPLAYS / which item the cursor lands on while NAVIGATING, neither of
 *     which the "select none" removal touches (cursor sits on entry 0 = -1).
 *   - FUN_00469414 (0x469414) — the per-frame menu UPDATE.  Returns 0 (idle /
 *     sliding), 1 (confirm), 2 (cancel), 3 (pick-up arm).  A2 ports the slide
 *     gate + the 6-frame confirm countdown (DAT_0734b994) + Z-confirm + cancel;
 *     PORT-DEBT(A3): the in-list cursor/quantity navigation (DAT_073dddd6 nav
 *     masks) + the b9a8==6 auto-sort + the FUN_00468246 highlight recount, none
 *     reachable without nav input during the removal drive.
 *   - FUN_00469a9f (0x469a9f) — the selected list item (-1 = "select none").
 *   - FUN_00468d22 (0x468d22) — return an item to the first empty inventory slot
 *     (+ bump the live count).  Used to give the removed sword back to the bag.
 *   - FUN_00469241 (0x469241) — remove an item from the inventory table (no-op
 *     on -1, so inert for the removal; ported for the future place path).
 *
 * Full RE: docs/findings/shop-display-menu-RE.md.
 */

#ifndef OPENRECET_SCENE1_DISPLAY_MENU_H
#define OPENRECET_SCENE1_DISPLAY_MENU_H

#include <stdint.h>

/* FUN_00468338(mode, first_open): open the display-stand inventory window.
 * `mode` is the engine param_1 (0 for the display stand); `first_open` is the
 * DAT_0438cbe8 open-once latch (param_2) — non-zero zeroes the per-tab cursor /
 * scroll.  Inits the slide (FUN_004693e3 ramp via stage_load_pulse) + the
 * index-0 "select none" list entry. */
void display_menu_open(int mode, int first_open);

/* FUN_004681ec: set the window-type flag (DAT_0734b990) — selects the baked
 * prompt bubble the render slides in with the panel.  The cc04 arm sets it
 * right after display_menu_open(): 1 = "What will you place?" (faced cell
 * empty), 2 = "Exchange with what?" (faced cell occupied); a highlighted
 * Vender-category item overrides either to "Place Vending Machine".
 * display_menu_open() resets it to 0 (no bubble). */
void display_menu_set_window_flag(int flag);

/* FUN_004682d8: set the buy/sell price multiplier (_DAT_005c6ee8) that scales
 * the description-panel "Base Price".  1.0 house, 0.7 guild buy, 0.3 guild sell. */
void display_menu_set_price_mult(float m);

/* FUN_00469414(param): one frame of the menu update.  Returns:
 *   0 — idle / still sliding in / mid-countdown
 *   1 — CONFIRM (the selected entry was committed: place or, for -1, remove)
 *   2 — CANCEL (the player backed out)
 *   3 — pick-up arm (Z edge frame: arms the confirm countdown; the engine
 *       briefly poses the player carrying the item)
 * `param` is the engine's param_1 (1 from the cc04==1 dispatch). */
int display_menu_update(int param);

/* FUN_00469a9f(): the currently-selected list item id, -1 == "select none". */
int display_menu_selected(void);

/* DAT_005c6ee4: how many of the highlighted item the player holds in inventory
 * ("Number possessed" in the description panel).  Recomputed inside
 * display_menu_open / _update; -1 before the first recount. */
int display_menu_possessed(void);

/* FUN_00468d22(bank, item): return `item` to the first empty inventory slot of
 * `bank` (the working-bank dword array) and bump the live item count.  No-op on
 * item == -1.  Used to give a removed display item back to the player's bag. */
void display_menu_inventory_return(uint32_t *bank, int item);

/* FUN_00469241(bank, item): remove the first occurrence of `item` from `bank`'s
 * inventory table (shift the tail down, blank the last slot, drop the count).
 * Returns 1 if an item was removed, 0 otherwise (incl. item == -1).  Inert for
 * the "select none" removal; ported for the future place-an-item path. */
int display_menu_inventory_remove(uint32_t *bank, int item);

/* Reset the picker state to BSS-zero (HOUSE re-entry / test setup). */
void display_menu_reset(void);

/* Slide counter (DAT_0734b98c, 0..5) for the render gate / tests. */
int display_menu_slide(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_0046b00a(0,0): render the display-stand remove-item menu (the item_win
 * parchment panel + category frame + scroll arrows + item rows + cursor).
 * No-op while the slide counter is 0 (closed).  Wired into the HOUSE render
 * tail after scene1_render_overlay. */
void display_menu_render(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_SCENE1_DISPLAY_MENU_H */
