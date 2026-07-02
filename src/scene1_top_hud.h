#ifndef SCENE1_TOP_HUD_H
#define SCENE1_TOP_HUD_H

#include <stdint.h>

/*
 * scene1_top_hud.{c,h} — the persistent HOUSE/town top-left HUD:
 * the gold clock dial + rotating hand, the "Day N" badge, and the
 * player's money ("1,000pix") on the gold vine banner.
 *
 * Port of FUN_00406d50 (0x406d50, 1445 B), called unconditionally from
 * the HUD aggregator FUN_0040a765 (decomp L6980) and so drawn every
 * INGAME HUD frame.  Uses bmp/item_win.tga (g_sysassets.item_win_tga,
 * DAT_073d8748) for the frame, hand, digit glyphs, comma, and pix icon.
 * Sub-helpers: the rotated clock hand is render_quad_draw_rotated_rect
 * (FUN_00406241); the number rows are scene1_top_hud_draw_number
 * (FUN_00406a60).  See docs/findings/house-top-hud.md.
 *
 * The displayed values come from the running game state, which is only
 * partially modelled in the port yet, so they live behind setters that
 * default to the new-game HOUSE values (day 1, 1000 pix, clock phase 0)
 * — which is exactly what the house-walk-tables cap_05 shows.
 */

/* Game-state inputs (defaults match a fresh new-game HOUSE frame). */
void scene1_top_hud_set_day(int day);          /* DAT_0450fb84[slot]; rendered as day+1 */
void scene1_top_hud_set_money(int money);      /* DAT_0438b918 */
void scene1_top_hud_set_clock_phase(float p);  /* DAT_0438b7d4 (time-of-day, 0..3.5) */

int   scene1_top_hud_day(void);
int   scene1_top_hud_money(void);
float scene1_top_hud_clock_phase(void);

/* FUN_00406584 (all.c:4849): ease the displayed money toward `bank_gold` by one
 * rolling step (rand-based, consumes one rng_next15 per rolling frame; a no-op
 * at rest).  Call each frame a scene shows the HUD so a purchase/sale rolls the
 * digits instead of snapping. */
void  scene1_top_hud_money_tick(int bank_gold);

/* Screen-shake jitter (FUN_0040656e + FUN_00406584 asm 0x406762-0x4067c8).
 * `_pulse` arms the 4-frame timer (DAT_00648280=4) — fired per landing sale
 * coin by the Table-B type-4 terminal kill; `_tick` draws the per-frame
 * ±1..2 px offsets (FOUR LCG draws per shake frame — rng-load-bearing;
 * RE §21.31.2).  Call `_tick` right before money_tick (engine block order). */
void    scene1_top_hud_shake_pulse(void);
void    scene1_top_hud_shake_tick(void);
int32_t scene1_top_hud_shake_x(void);
int32_t scene1_top_hud_shake_y(void);
int32_t scene1_top_hud_shake_timer(void);

/* Merchant-XP bar animator (FUN_00406584 all.c:4799-4848).  Eases the live
 * bar value (_DAT_0438b91c) toward the bank merchant EXP, runs the glow-flash
 * counter (DAT_0064827c) and the level-up (bank level/start/end + the
 * DAT_0438b920 banner timer + SE 00re_sys03a).  Call `_xp_tick` right before
 * shake_tick each HUD frame (engine block order; rng-neutral).  `_xp_snap`
 * mirrors the engine's load-time b91c = bank-exp snap. */
void    scene1_top_hud_xp_tick(uint32_t *bankw);
void    scene1_top_hud_xp_snap(int bank_exp);
float   scene1_top_hud_xp_anim(void);
int32_t scene1_top_hud_xp_flash(void);
int32_t scene1_top_hud_levelup_timer(void);

/* World-map travel-time tooltip (FUN_00406d50 Draw-2 + the FUN_00406584 mode-8
 * band selector).  `_tick` is called per world-map sim frame with the selected
 * destination (DAT_09643684) and the dest-0 variant flag (DAT_045105a0!=0);
 * `_reset` (FUN_004060ff) restarts the slide-in at world-map init. */
void scene1_top_hud_worldmap_tooltip_tick(int sel_dest, int return_pending);
void scene1_top_hud_tooltip_reset(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* Render the persistent top HUD (FUN_00406d50). */
void scene1_top_hud_render(struct IDirect3DDevice8 *dev);

/* The bottom-right "Button 4: Change Camera" control hint (FUN_00409925's
 * LAB_0040a5fd tail).  Self-gates on no-dialogue-active; drawn in free-roam. */
void scene1_top_hud_camera_hint(struct IDirect3DDevice8 *dev);

/* FUN_00406a60 — draw an integer as digit-glyph sprites from item_win.tga
 * (x = right anchor).  `icon` prepends the pix-coin sprite; `comma` inserts
 * thousands separators.  Shared with the save-slot picker (FUN_0049b556).
 * Binds item_win.tga and flushes internally. */
void scene1_top_hud_draw_number(struct IDirect3DDevice8 *dev,
                                float x, float y,
                                int value, int icon,
                                uint32_t color, int comma);
#endif

#endif /* SCENE1_TOP_HUD_H */
