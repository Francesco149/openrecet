/*
 * scene1_effects — FUN_004176ff (30 KB: the floor effects pass, under its own
 * z_far 500 projection), ported section by section; its overlay layer-0
 * dispatch sits in scene1_render.c.
 *
 * PORT-DEBT(stub, FUN_004176ff): unported sections — the table-A loop on
 * effect_shot.bmp (0x41932d), the effect_mahi pass (0x419c51, where retail
 * switches to z_far 500), the table-B loops 0x41a263 (on effect.bmp, before
 * the table-A loop) and 0x41e9aa, FUN_00414ee2 at 0x41ec03 and table A's
 * 69 / 64 loop (0x41ec50).
 *
 *   the exit circle   the dungeon objects of type 8 (state +0x268 != 3):
 *                     mahoujin.bmp on DAT_0064a2c8's floor quad, world
 *                     RotY(DAT_0438b8cc · 0.02) · S(+0x80 · 0.024) · T(pos),
 *                     SRCALPHA / INVSRCCOLOR, no Z write
 */
#ifndef OPENRECET_SCENE1_EFFECTS_H
#define OPENRECET_SCENE1_EFFECTS_H

#ifdef _WIN32
struct IDirect3DDevice8;

/* FUN_00474a9a's dungeon arm: bmp/mahoujin.bmp (DAT_073b18c8) */
void scene1_effects_load_dungeon(struct IDirect3DDevice8 *dev);

/* FUN_004176ff's first loop (all_ida 15160..15190) */
void scene1_effects_exit_circle(struct IDirect3DDevice8 *dev);

/* FUN_004176ff 0x417a6c..0x41e98c: table B's weapon-swing trails and projectiles
 * on effect_shot.bmp, the snow, the table-A loop, the glows / orbs and the puffs */
void scene1_effects_table_b(struct IDirect3DDevice8 *dev);
#endif

/* FUN_0040f892 (top of FUN_004176ff): the type-148 trail records' history,
 * spline strip and age (they die at 18) — render-coupled, as in retail */
void scene1_effects_tick_148(void);

#endif /* OPENRECET_SCENE1_EFFECTS_H */
