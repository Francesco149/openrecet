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
 *   the rest      the adventurer gauges (chara 30's ammo, chara 42's charge),
 *                 the floor events, the minimap (map.bmp), the floor plate,
 *                 the kill combo, the word / number popups (DAT_044e28f0),
 *                 the struck enemy's bar (ene_hp2 + its face), and the
 *                 "LEVEL UP!" pop FUN_00407ab4.
 *
 * The panel slides down with the HUD slide (DAT_0438b1dc · 128).  Float math
 * follows the disassembly (x87.h): ST0 chains long double, fstp m32 spill32.
 */
#ifndef OPENRECET_DUNGEON_HUD_H
#define OPENRECET_DUNGEON_HUD_H

#include <stdint.h>

#ifdef _WIN32
struct IDirect3DDevice8;

/* FUN_00407cac */
void dungeon_hud_render(struct IDirect3DDevice8 *dev);

/* FUN_004072f5(x, y, chara, full): the party panel at (x, y + 376); `full`
 * shows the gauges at their maxima. */
void dungeon_hud_party_panel(struct IDirect3DDevice8 *dev, float x, float y,
                             int chara, int full);

/* FUN_00407ab4(x, y): the "LEVEL UP!" pop while DAT_0438b920 runs — the floor
 * HUD's at (16, 400), the house's FUN_00409925 above its level badge. */
void dungeon_hud_levelup_pop(struct IDirect3DDevice8 *dev, float x, float y);
/* FUN_0040c962: the "got an item" notice (after FUN_0045404b, no dialogue) */
void dungeon_hud_got_item(struct IDirect3DDevice8 *dev);

/* The floor's HUD textures: FUN_00474a9a's dungeon arm loads bmp/map.bmp
 * (DAT_073d8768), FUN_00473c15 the faces of the roster's kinds
 * (off_005c7e2c → DAT_073ac878, 64×64).  Kept once loaded. */
void dungeon_hud_load_map(struct IDirect3DDevice8 *dev);
void dungeon_hud_load_faces(struct IDirect3DDevice8 *dev, const int32_t present[100]);

/* DAT_073b1ab8: FUN_00473c15's tail — the floor's title card (the dungeon's
 * title or the boss's name), reloaded per floor */
void dungeon_hud_load_title(struct IDirect3DDevice8 *dev);

/* FUN_00406d50's tail: the floor-title banner (and kind 1's white flash) */
void dungeon_hud_title_banner(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_DUNGEON_HUD_H */
