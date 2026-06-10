/*
 * scene_guild.h — engine mode 6 ("Merchant's Guild" / Market) scene.
 *
 * The world-map "Merchant's Guild" (dest 3) is internally the Market scene,
 * engine mode 6 (g_scene_state == 6).  dest 3 enters via FUN_00490e16(0)
 * (variant flag DAT_0963c5f0 = 0, the guild), dest 1 via FUN_00490e16(1)
 * (the other mode-6 destination).  See docs/findings/merchant-guild-RE.md and
 * docs/findings/town-map-RE.md:170 (the dest→mode table).
 *
 * Engine sources ported here:
 *   FUN_00490e24 @ 0x490e24 (17 B)  — mode-6 update wrapper (gate then event tick).
 *   FUN_004922c0 @ 0x4922c0          — per-location event tick.  Ported MINIMALLY:
 *                                      the entry-tick counter + the first-visit
 *                                      cutscene branch (iv1_3.ivt).  The daily-event
 *                                      probe (FUN_0045de68, event system unported)
 *                                      and the group-6 follow-on cutscenes
 *                                      (leaving-guild / return) are PORT-DEBT.
 *   FUN_00490e35 @ 0x490e35 (15 B)  — mode-6 render wrapper.
 *   FUN_00494a73 @ 0x494a73 (561 B) — the 2D bg blit (guild variant: full-screen
 *                                      bg_guild.bmp + the mirrored guildmaster).
 *   FUN_00473769 @ 0x473769          — the texture-group-7 load (guild variant).
 *
 * The cutscene reuses the shared dialogue runtime via
 * scene1_intro_dialogue_start_single(1,3) — NO new dialogue code.
 */
#ifndef OPENRECET_SCENE_GUILD_H
#define OPENRECET_SCENE_GUILD_H

#include <stdint.h>

struct IDirect3DDevice8;

/* Texture slots (engine DAT_073da000/010/020 — the shared mode-6 bg bank). */
enum {
    SCENE_GUILD_TEX_BG = 0,    /* DAT_073da000 — bmp/ivent/bg_guild.bmp     (1024x512) */
    SCENE_GUILD_TEX_KEEPER,    /* DAT_073da010 — bmp/ivent/13syounin_01.tga ( 512x512) */
    SCENE_GUILD_TEX_BORD,      /* DAT_073da020 — bmp/result/bord01.tga      ( 512x256) */
    SCENE_GUILD_TEX_COUNT
};

/* Set / read the market variant flag (engine DAT_0963c5f0, set by FUN_00490e16):
 *   0 = Merchant's Guild (world-map dest 3, the tutorial-forced target)
 *   1 = the other mode-6 destination (dest 1)
 * The world-map exit-to-dest path sets this before switching g_scene_state=6. */
void scene_guild_set_variant(int v);
int  scene_guild_variant(void);

/* Reset the per-entry event-tick counter (engine DAT_09642c38, zeroed by the
 * scene-init FUN_0049174e).  Called by the worker-load cb on scene entry; the
 * first-visit cutscene fires on the 2nd tick after this. */
void scene_guild_enter_reset(void);

/* Per-frame update — port of FUN_00490e24 → FUN_004922c0 (mode-6 event tick).
 * Pure logic (no device): advances the entry-tick counter and arms the
 * first-visit cutscene through the shared dialogue runtime.  Driven from
 * sim.c's scene dispatch (case 6). */
void scene_guild_sim(void);

#ifdef _WIN32

/* Register the worker_load case-6 loader + cache the device.  Call once at
 * startup (alongside scene_worldmap_init et al.). */
void scene_guild_init(struct IDirect3DDevice8 *dev);

/* Per-frame render — port of FUN_00490e35 → FUN_00494a73 (the guild bg).
 * The cutscene dialogue draws ON TOP via scene1_dialogue_draw (main.c). */
void scene_guild_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_GUILD_H */
