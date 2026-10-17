/*
 * dungeon_hud — the floor HUD, FUN_00407cac (FUN_0040a765 draws it in place of
 * the house's FUN_00409925 when the stage maptype is >= 1).
 *
 *   FUN_004072f5  the party panel (bottom left): the hpmp_base backdrop and
 *                 overlay pulsing red as HP falls (white as it rises), the
 *                 19-segment EXP fan on hpmp_exp, the HP / SP bars and their
 *                 "HP" / "SP" labels, the cur/max digits (FUN_00406c64), the
 *                 adventurer's portrait, the skill icons (the selected one lit)
 *                 and the level (FUN_00406a60).
 *
 * The panel slides down with the HUD slide (DAT_0438b1dc · 128).  Float math
 * follows the disassembly (x87.h): ST0 chains long double, fstp m32 spill32.
 */
#ifndef OPENRECET_DUNGEON_HUD_H
#define OPENRECET_DUNGEON_HUD_H

#ifdef _WIN32
struct IDirect3DDevice8;

/* FUN_00407cac */
void dungeon_hud_render(struct IDirect3DDevice8 *dev);

/* FUN_004072f5(x, y, chara, full): the party panel at (x, y + 376); `full`
 * shows the gauges at their maxima (the results screen's call). */
void dungeon_hud_party_panel(struct IDirect3DDevice8 *dev, float x, float y,
                             int chara, int full);
#endif

#endif /* OPENRECET_DUNGEON_HUD_H */
