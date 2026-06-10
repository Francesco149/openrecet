/*
 * scene_guild.c — see scene_guild.h.
 *
 * Engine sources:
 *   FUN_004922c0 @ 0x4922c0  — per-location event tick.  Ported MINIMALLY here:
 *     the entry-tick counter (DAT_09642c38) + the first-visit cutscene branch
 *     (all.c:94764-94775).  The fade gate (FUN_00434d6a), the daily-event probe
 *     (FUN_0045de68), the group-6 follow-on cutscenes, and the guildmaster
 *     idle-anim counter tail (DAT_09642c40 et al., all.c:94811+) are PORT-DEBT.
 *   FUN_00494a73 @ 0x494a73  — the 2D bg blit (guild variant 0).
 *   FUN_00473769 @ 0x473769  — the texture-group-7 load (guild variant 0).
 *
 * The first-visit cutscene is iv1_3.ivt, armed through the shared dialogue
 * runtime (scene1_intro_dialogue_start_single(1,3) = port of FUN_0044ba2c(1,3,1)
 * → FUN_00452d07).  See docs/findings/merchant-guild-RE.md.
 */

#include "scene_guild.h"

#include <stdint.h>
#include <stddef.h>   /* NULL */

#include "save_work.h"               /* save_work_dwords_at / _active_slot */
#include "scene1_intro_dialogue.h"   /* scene1_intro_dialogue_start_single */

/* ─── working-arena field offsets (base DAT_044e3798, per-slot) ──────────────
 * The port pins the engine's per-location stage index (DAT_0438b1e0) to the
 * active save slot (save_work_active_slot) — see docs/findings/merchant-guild-RE.md
 * and the worldmap/tutorial siblings that share this base+scheme. */
#define GUILD_FIRSTVISIT_OFF  0x2bc5c   /* DAT_0450f3f4 — guild first-visit seen flag */

/* Market variant flag (engine DAT_0963c5f0). */
static int s_variant = 0;

/* Per-entry event-tick counter (engine DAT_09642c38). */
static int s_entry_tick = 0;

void scene_guild_set_variant(int v) { s_variant = v; }
int  scene_guild_variant(void)      { return s_variant; }

void scene_guild_enter_reset(void)  { s_entry_tick = 0; }

/* ─── pure-C event tick (FUN_00490e24 → FUN_004922c0, first-visit subset) ──── */

void scene_guild_sim(void)
{
    /* FUN_00490e24: the gate FUN_0044c7b8() is a `return 0` stub, so the event
     * tick FUN_004922c0 always runs. */

    /* FUN_004922c0 top (all.c:94752): the entry-tick counter increments every
     * frame.  Retail also early-outs here via the press-to-continue fade gate
     * FUN_00434d6a (DAT_0438b148) — settled (0) on a fresh guild entry, so it
     * returns 0/proceed; its ramp path (the result-screen flow) is PORT-DEBT. */
    s_entry_tick++;

    /* First-visit branch (all.c:94764-94775), gated on the 2nd entry tick. */
    if (s_entry_tick != 2)
        return;

    /* Guard (&DAT_045114fc)[loc] != 2 — location-type is not "dungeon".  Mode 6
     * is the market/guild, NEVER a dungeon, so this guard is structurally
     * always-true here.  We do NOT read the byte: under the port's loc→slot
     * pinning DAT_045114fc's 0xb7f2 stride does not match the working bank's
     * 0x2dfc8 stride for non-zero slots, so a literal read is unreliable —
     * whereas the mode-6 invariant is exact.  PORT-DEBT(loc-routing). */

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    uint8_t *bb = (uint8_t *)bank;

    /* (&DAT_0450f3f4)[loc] == 0 — guild first-visit unseen.  Mark seen (fires
     * once, persists in the save bank) + arm the iv1_3 cutscene. */
    if (bb[GUILD_FIRSTVISIT_OFF] == 0) {
        bb[GUILD_FIRSTVISIT_OFF] = 1;
        scene1_intro_dialogue_start_single(1, 3);   /* FUN_0044ba2c(1,3,1) → iv1_3.ivt */
    }
}

/* ─── Win32 worker_load wiring + render ─────────────────────────────────────── */

#ifdef _WIN32

#include "render_quad.h"   /* render_quad_state_setup/bind/add/add_mirrored/flush */
#include "sprite.h"        /* sprite_t, sprite_load */
#include "worker_load.h"   /* worker_load_set_cb */

sprite_t g_scene_guild[SCENE_GUILD_TEX_COUNT];

static IDirect3DDevice8 *g_scene_guild_dev = NULL;

/* Guild variant (DAT_0963c5f0 == 0) texture set — FUN_00473769 group-7 loads
 * (all.c:72012-72014).  Engine kind=7 dropped by sprite_load, as elsewhere. */
static const struct { const char *path; uint32_t w, h; }
g_scene_guild_assets[SCENE_GUILD_TEX_COUNT] = {
    [SCENE_GUILD_TEX_BG]     = { "bmp/ivent/bg_guild.bmp",     0x400, 0x200 },
    [SCENE_GUILD_TEX_KEEPER] = { "bmp/ivent/13syounin_01.tga", 0x200, 0x200 },
    [SCENE_GUILD_TEX_BORD]   = { "bmp/result/bord01.tga",      0x200, 0x100 },
};

/* worker_load case-6 body — port of the scene-init FUN_0049174e's relevant
 * effects (reset the entry-tick counter) + the FUN_00473769 texture load.
 * Runs on the load worker; serialized before the first mode-6 sim tick by the
 * worker_load_busy() gate in sim_step_a. */
static void scene_guild_load_cb(void)
{
    scene_guild_enter_reset();   /* DAT_09642c38 = 0 (FUN_0049174e) */

    /* PORT-DEBT(variant-1 ichiba): only the guild variant (DAT_0963c5f0 == 0,
     * world-map dest 3) loads its texture set; the variant-1 (ichiba) set
     * (01recette_04/02tear_01 + ichiba/ichiba2 bg) is not yet wired — dest 1 is
     * not exercised by the merchant's-guild arc. */
    if (g_scene_guild_dev == NULL || s_variant != 0)
        return;
    for (int i = 0; i < SCENE_GUILD_TEX_COUNT; i++) {
        sprite_load(g_scene_guild_dev, g_scene_guild_assets[i].path,
                    g_scene_guild_assets[i].w, g_scene_guild_assets[i].h,
                    &g_scene_guild[i]);
    }
}

void scene_guild_init(struct IDirect3DDevice8 *dev)
{
    g_scene_guild_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_cb(6, scene_guild_load_cb);
}

void scene_guild_render(struct IDirect3DDevice8 *dev)
{
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;

    /* FUN_00490e35 → FUN_0049b425 (2D state preset; FUN_00494a73 re-calls it at
     * its top — an idempotent no-op the engine duplicates, collapsed here). */
    render_quad_state_setup(d);

    /* FUN_00494a73 normal path (DAT_09642c3c == 0).  The mid-transition path
     * (single bg blit @ alpha 0xff000000) is PORT-DEBT — not exercised by the
     * first-visit cutscene. */

    /* slot0: full-screen bg — dst (0,0,640,480), src top-left 640x480 of the
     * 1024x512 bmp (all.c:96191-96201). */
    if (g_scene_guild[SCENE_GUILD_TEX_BG].tex) {
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_bind(d, &g_scene_guild[SCENE_GUILD_TEX_BG]);
        render_quad_add(dst, src,
                        g_scene_guild[SCENE_GUILD_TEX_BG].width,
                        g_scene_guild[SCENE_GUILD_TEX_BG].height,
                        0xffffffffu);
        render_quad_flush(d);
    }

    /* slot1: the guildmaster (13syounin_01), drawn H-MIRRORED (FUN_00404e61)
     * into dst (-64,32,448,448) from the full 512x512 sprite (all.c:96217-96229,
     * guild variant — DAT_0963c5f0 == 0). */
    if (s_variant == 0 && g_scene_guild[SCENE_GUILD_TEX_KEEPER].tex) {
        const float dst[4] = { -64.0f, 32.0f, 448.0f, 448.0f };
        const float src[4] = { 0.0f, 0.0f, 512.0f, 512.0f };
        render_quad_bind(d, &g_scene_guild[SCENE_GUILD_TEX_KEEPER]);
        render_quad_add_mirrored(dst, src,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].width,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].height,
                        0xffffffffu);
        render_quad_flush(d);
    }

    /* PORT-DEBT(guild-ui): the FUN_00494a73 tail (FUN_0049404b fx,
     * FUN_0046b00a guild menu frame, FUN_0043537e, FUN_00491de0,
     * FUN_00435747 cursor, FUN_00435117) + the FUN_00490e35 trailing
     * FUN_00406d50 top-HUD (clock/Day/money) are not yet ported.  Verify
     * against the merchants-guild trace whether they're visible behind the
     * first-visit cutscene before adding them. */
}

#endif /* _WIN32 */
