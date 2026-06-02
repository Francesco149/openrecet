#ifndef SCENE1_MERCHANT_HUD_H
#define SCENE1_MERCHANT_HUD_H

#include <stdint.h>

/*
 * scene1_merchant_hud.{c,h} — the persistent HOUSE/town bottom-LEFT HUD:
 * the circular "Merchant Level" badge (level number in a disc), the gold
 * "Merchant Level" label, and the horizontal experience bar.
 *
 * Port of the main body of FUN_00409925 (0x409925, 3434 B) — specifically
 * the block at decomp L124-L179 (asm 0x409cf0-0x409f6x).  That function is
 * the HOUSE-town HUD; its tail (LAB_0040a5fd) is the "Button 4: Change
 * Camera" hint already ported as scene1_top_hud_camera_hint().  The leading
 * block (the shop-table item tooltip) and trailing block (shop/stocking UI)
 * are event/shop-state gated and dormant in free-roam.
 *
 * The badge body, disc, bar groove and "Merchant Level" text are baked into
 * bmp/item_win.tga (g_sysassets.item_win_tga, DAT_073d8748).  Two 192x40
 * frame layers (a back layer, src (640,544)-(832,584), and a front layer,
 * src (640,640)-(832,680)) sandwich the dynamic experience-bar fill
 * (src row y 592-632).  The level number is drawn last from a dedicated
 * large-digit glyph row (src y 848-888, 32 px/digit) — this is the port of
 * the sub-helper FUN_00481ec3.
 *
 * Render-state quirk (faithful): the three frame layers are drawn with
 * COLOROP = ADDSIGNED (result = texture + diffuse - 0.5), the diffuse being
 * a grey pulse that idles at 0x7f (≈0.5, so the frame shows at its native
 * brightness) and brightens during an experience gain — a "breathing" glow.
 * The level digits revert to COLOROP = MODULATE.
 *
 * Visibility == the top HUD's: both are emitted by the aggregator
 * FUN_0040a765 on the INGAME + HOUSE (stage 0) + status-screen-closed path,
 * and both are suppressed by the same screen-covering-cutscene gate (iv1_1)
 * that wraps scene1_hud_render in main.c.  The bottom-left HUD has NO
 * internal dialogue gate, so — like the top HUD — it stays visible during
 * the iv1_2 dialogue drawn over the live HOUSE map (only the camera hint
 * self-gates on dialogue).
 *
 * Displayed values come from the running game state, only partially modelled
 * in the port yet, so they live behind setters defaulting to the new-game
 * HOUSE values (Merchant Level 1, empty experience bar) — exactly what the
 * cap_05 house-walk frames show.
 */

/* Game-state inputs (defaults match a fresh new-game HOUSE frame). */
void scene1_merchant_hud_set_level(int level);  /* DAT_0450fb98[slot]; rendered as level+1 */
void scene1_merchant_hud_set_xp(int current, int level_start, int level_end);
/*   current     = _DAT_0438b91c      (experience, animated float in engine)
 *   level_start = DAT_0450fb90[slot]  (experience at the current level's floor)
 *   level_end   = DAT_0450fb94[slot]  (experience needed for the next level)
 * Bar fill = (current - level_start) / (level_end - level_start). */

int scene1_merchant_hud_level(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* Render the bottom-left "Merchant Level" badge + experience bar
 * (FUN_00409925 body, L124-L179). */
void scene1_merchant_hud_render(struct IDirect3DDevice8 *dev);
#endif

#endif /* SCENE1_MERCHANT_HUD_H */
