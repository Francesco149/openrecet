/*
 * scene_worldmap.c — see scene_worldmap.h.
 *
 * Engine sources:
 *   FUN_004735ad @ 0x4735ad (98 bytes)  — the 4 fixed BMP/TGA loads.
 *   FUN_0049de20 @ 0x49de20 (374 bytes) — the mode-8 scene-init state
 *     machine (destination model + tutorial gating). Ported here as
 *     scene_worldmap_init_state. Strings extracted via
 *       tools/analyze/pe.py str 0x005c87f4 0x005c880c 0x005c8824 0x005c883c
 *   .data tables extracted from vendor/unpacked @ 0x5fd590 (per-dest
 *     layout) + 0x5fd620 (3×5 cursor grid).
 */

#include "scene_worldmap.h"

#include <stdint.h>

#include "worker_load.h"
#include "scene.h"              /* SCENE_STATE_WORLDMAP (=8) — primary worker case index; g_scene_state */
#include "save_work.h"          /* live working save arena (tutorial flags + day/tod) */
#include "save_bank.h"          /* SAVE_BANK_FIELD_CARD_DAY / _CLOCK_TARGET (working dwords) */
#include "title_save_dialog.h"  /* shared cursor: set_visible (FUN_0043561a) + snap/slide (FUN_00435693/710) */
#include "scene1_top_hud.h"     /* scene1_top_hud_tooltip_reset (FUN_004060ff) — host-side decl */
#include "sim.h"                /* g_sim_buttons[0].pressed/held — DAT_073dddd4/d6 */
#include "audio.h"              /* audio_play_se_by_id — move/confirm/denied SE (fixed id, no RNG) */
#include "fade.h"               /* fade_phase1_start/_is_done/_phase_out_start — FUN_004526f5/4528b3/45281c */
#include "call_trace.h"         /* CALL_TRACE_* — flow-trace fields for the both-target verify */

/* ─── pre-baked asset table ──────────────────────────────────────────── */

const scene_worldmap_asset_t scene_worldmap_assets[SCENE_WORLDMAP_COUNT] = {
    [SCENE_WORLDMAP_TEX_NOMAL]    = { "bmp/worldmap_nomal.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_YUGATA]   = { "bmp/worldmap_yugata.bmp", 0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_NIGHT]    = { "bmp/worldmap_night.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_MAPPOINT] = { "bmp/mappoint.tga",        0x100, 0x400 },
};

const char *scene_worldmap_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) return 0;
    return scene_worldmap_assets[slot].path;
}

int scene_worldmap_dims(int slot, uint32_t *w, uint32_t *h)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) {
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    if (w) *w = scene_worldmap_assets[slot].expected_w;
    if (h) *h = scene_worldmap_assets[slot].expected_h;
    return 1;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_worldmap_load_with(scene_worldmap_load_fn load_fn,
                             void *userdata)
{
    /* Engine FUN_004735ad: 4 unconditional sprite_load calls in fixed
     * order. The first arg (kind = 10) is the engine's per-scene
     * texture-group flag, which openrecet's sprite_load drops. */
    int loads = 0;
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        const scene_worldmap_asset_t *a = &scene_worldmap_assets[i];
        if (load_fn) {
            load_fn(a->path, a->expected_w, a->expected_h, i, userdata);
        }
        loads++;
    }
    return loads;
}

/* ─── world-map destination model + scene-init (FUN_0049de20) ─────────────
 *
 * The mode-8 (WORLD MAP) scene globals + the .data layout tables. Pure-C —
 * the init runs on the worker thread for mode 8 (before the texture load).
 * See docs/findings/town-map-RE.md §3-4. */

/* DAT_005fd590 (per-dest {x,y,sprite_row}, stride 0xc, 7 entries). */
const scene_worldmap_dest_t
    scene_worldmap_dest_layout[SCENE_WORLDMAP_DEST_COUNT] = {
    /*   x       y     row */
    { 230.0f, 400.0f, 0 },  /* dest 0 — Shop / home (your shop) */
    {  43.0f, 294.0f, 3 },  /* dest 1 */
    { 230.0f, 276.0f, 1 },  /* dest 2 */
    { 440.0f, 276.0f, 5 },  /* dest 3 — Market (tutorial-forced target) */
    {  30.0f, 196.0f, 4 },  /* dest 4 */
    { 160.0f, 136.0f, 2 },  /* dest 5 */
    { 448.0f,  88.0f, 6 },  /* dest 6 */
};

/* DAT_005fd620 — 3 col × 5 row cursor grid (col + row*3 → dest-id, -1 = empty). */
const int scene_worldmap_grid[SCENE_WORLDMAP_GRID_COLS *
                              SCENE_WORLDMAP_GRID_ROWS] = {
    -1, -1, -1,
     4,  5,  6,
     1,  2,  3,
     1,  0,  3,
     0, -1,  0,
};

/* Engine world-map scene globals (DAT_096435xx / DAT_005fd588). */
static int   s_dest_count    = SCENE_WORLDMAP_DEST_COUNT; /* DAT_005fd588 */
static int   s_sel_dest      = 0;     /* DAT_09643684 — selected dest (cursor) */
static float s_entry_timer   = 0.0f;  /* _DAT_09643628 — intro timer */
static int   s_exit_counter  = 0;     /* DAT_0964367c  — exit-in-progress */
static int   s_misc_680      = 0;     /* _DAT_09643680 */
static int   s_dest_pos[SCENE_WORLDMAP_DEST_COUNT]    = { 0 }; /* DAT_096435d8 */
static int   s_dest_state[SCENE_WORLDMAP_DEST_COUNT]  = { 0 }; /* DAT_09643588 */
static int   s_dest_closed[SCENE_WORLDMAP_DEST_COUNT] = { 0 }; /* DAT_0964362c */

void  scene_worldmap_set_sel_dest(int dest) { s_sel_dest = dest; }  /* FUN_0049de0e */
int   scene_worldmap_sel_dest(void)         { return s_sel_dest; }
int   scene_worldmap_dest_count(void)       { return s_dest_count; }
float scene_worldmap_entry_timer(void)      { return s_entry_timer; }

int scene_worldmap_dest_state(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_state[i] : 0;
}
int scene_worldmap_dest_closed(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_closed[i] : 0;
}
int scene_worldmap_dest_pos(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_pos[i] : 0;
}

/* Working-arena field locations (base DAT_044e3798, per-slot; see
 * docs/findings/save-working-arena.md). Byte offsets for the BYTE tutorial
 * flags; dword indices for the INT day/tod state. */
#define WM_OFF_TUTORIAL_A  0x2bc61      /* DAT_0450f3f9 — set by the door's first exit */
#define WM_OFF_TUTORIAL_B  0x2bc70      /* DAT_0450f408 */
#define WM_OFF_EVENT_FLAG  0x2bc7c      /* DAT_0450f414 */
#define WM_DW_DAY          SAVE_BANK_FIELD_CARD_DAY    /* 0xb0fb — DAT_0450fb84 (day#) */
#define WM_DW_TOD          SAVE_BANK_FIELD_CLOCK_TARGET/* 0xb0fc — DAT_0450fb88 (0 day/1 eve/2 night) */
#define WM_DW_05A0         0xb3a2u                     /* DAT_045105a0 */

/* PORT-DEBT(event-probe, FUN_0045de68): the per-destination "has an event
 * today" probe reads the event-table arena (DAT_06a49c44, 5000-int strides)
 * + the day/tod gates — the event system is unported. Returns 0 (no event).
 * Only affects ENABLED destinations (state != 0); in the tutorial gate (the
 * town-map-load recording) only dest 3 is enabled and already at state 2, so
 * this is a no-op there. Retire when the event tables port. */
static int scene_worldmap_dest_has_event(int dest)
{
    (void)dest;
    return 0;
}

/* DAT_045105a0[slot] != 0 — the dest-0 ("your shop") tooltip variant selector
 * read by FUN_00406584's mode-8 block (all.c:4782).  Non-zero ⇒ band 1
 * ("Returning to the shop will take 1 period of time"); zero ⇒ band 3 ("If you
 * return now no time will pass").  This live working-arena flag marks an active
 * shop session (set by the shop open/sale paths, cleared via the display-stand
 * interaction); it is 0 on a tutorial Continue, matching the user's flagged
 * "no time will pass" frame. */
int scene_worldmap_return_pending(void)
{
    const int slot = save_work_active_slot();           /* DAT_0438b1e0 */
    const uint32_t *dw = save_work_dwords_at(slot);
    return (dw && dw[WM_DW_05A0] != 0) ? 1 : 0;
}

void scene_worldmap_init_state(void)   /* FUN_0049de20 @ 0x49de20 */
{
    const int slot = save_work_active_slot();           /* DAT_0438b1e0 */
    const uint32_t *dw = save_work_dwords_at(slot);
    const uint8_t  *bb = (const uint8_t *)dw;            /* same per-slot block, byte view */
    int i;

    s_entry_timer  = 0.0f;   /* _DAT_09643628 = 0 */
    s_exit_counter = 0;      /* DAT_0964367c  = 0 */
    s_misc_680     = 0;      /* _DAT_09643680 = 0 */

    /* Restart the travel-time tooltip's slide-in (the FUN_004060ff reset the
     * engine applies on the way out of the world map; doing it on entry gives a
     * fresh slide-in on every visit — see scene1_top_hud_tooltip_reset). */
    scene1_top_hud_tooltip_reset();

    /* FUN_0043561a + FUN_00435693: raise the SHARED cursor + snap it to the
     * selected dest's marker (x-16, y+28). This raise is the engine's
     * "PAUSE_OPEN" anchor at the world-map load — the shared b150 cursor for
     * the destination pointer (red herring, town-map-RE.md §1). */
    title_save_dialog_cursor_set_visible(1);
    title_save_dialog_cursor_snap(
        scene_worldmap_dest_layout[s_sel_dest].x - 16.0f,
        scene_worldmap_dest_layout[s_sel_dest].y + 28.0f);

    s_dest_count = SCENE_WORLDMAP_DEST_COUNT;   /* DAT_005fd588 = 7 */
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_closed[i] = 0;
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_state[i]  = 0;
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_pos[i]    = i; /* DAT_096435d8[i]=i */

    if (bb == NULL) return;   /* defensive: no working arena bound */

    /* Tutorial gating (all.c:102868). The door-exit sets flag A on the first
     * exit → the `else` branch → dest 3 (Market) highlighted, the rest
     * disabled. Flag-A-clear + flag-B-clear = all unlocked (NORMAL); flag-B
     * set = only dest 0 highlighted. */
    if (bb[WM_OFF_TUTORIAL_A] == 0) {
        if (bb[WM_OFF_TUTORIAL_B] == 0) {
            for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_state[i] = 1;
        } else {
            s_dest_state[0] = 2;
        }
    } else {
        s_dest_state[3] = 2;   /* _DAT_09643594 = 2 — Market highlighted */
    }

    {
        const int day = (int)dw[WM_DW_DAY];   /* DAT_0450fb84 */
        const int tod = (int)dw[WM_DW_TOD];   /* DAT_0450fb88 */

        /* Time-of-day / day closures (all.c:102883). */
        if (day % 7 == 3) {
            s_dest_closed[6] = 1;   /* _DAT_09643644 = 1 */
            s_dest_state[6]  = 0;   /* _DAT_096435a0 = 0 */
        }
        if (tod < 3 && (tod < 2 || (int)dw[WM_DW_05A0] == 0)) {
            if (bb[WM_OFF_EVENT_FLAG] != 0) s_dest_state[6] = 2; /* _DAT_096435a0 = 2 */
        } else {
            s_dest_state[6] = 0;
        }
        if (tod == 3) s_dest_state[1] = 0;   /* DAT_0964358c  = 0 */
        if (tod <  2) s_dest_state[4] = 0;   /* _DAT_09643598 = 0 */
        if (tod == 3) s_dest_state[5] = 0;   /* _DAT_0964359c = 0 */
    }

    /* Event upgrade (all.c:102905): dest 1..5 — an enabled dest with an event
     * today becomes highlighted. PORT-DEBT event probe → no-op for now. */
    for (i = 1; i < 6; i++) {
        if (scene_worldmap_dest_has_event(i) && s_dest_state[i] != 0)
            s_dest_state[i] = 2;
    }

    /* PORT-DEBT(stage-scratch, FUN_00435c98): the engine tail re-inits the
     * INGAME stage/camera scratch + the day-display float (_DAT_0438b91c).
     * Deferred — it runs AFTER the destination state array is set, so it has
     * no effect on the model; revisit if the world-map render (T3) needs the
     * camera / day-display globals. */
}

/* Cursor navigation — port of FUN_0049dfc1(param) @ 0x49dfc1 (409 B,
 * all.c:102922). Walks the 3-col × 5-row destination grid
 * (scene_worldmap_grid) from the currently-selected destination in the
 * held direction, finds the next PRESENT destination, selects it, eases the
 * shared cursor to its marker, and plays the move SE. `param` scales the
 * cursor target (0.7 for the zoomed-out view); the mode-8 sim calls it with 0.
 *
 * Direction comes from the held+auto-repeat mask DAT_073dddd6
 * (g_sim_buttons[0].held) — the SAME global the engine reads here, NOT the
 * one-shot `pressed`: bit 0x2 → du=-1, 0x1 → du=+1, 0x4 → dv=-1, 0x8 → dv=+1
 * (priority 0x2>0x1>0x4>0x8). `du` steps the column (mod 3), `dv` the row
 * (mod 5) — left/right moves within a row, up/down between rows. (Note: the
 * RE-doc prose "bit2→up" was the inverse; the engine button layout is
 * 0x01=Right/0x02=Left/0x04=Up/0x08=Down, which makes this intuitive.)
 *
 * Faithful to the decompile's goto structure: the outer scan locates the
 * selected dest's cell, then an inner 5-step walk searches outward; a second
 * pass (from the next row) retries once when the first finds nothing and the
 * move had a horizontal (du) component. */
static void scene_worldmap_cursor_nav(int param)
{
    const float scale = (param != 0) ? 0.7f : 1.0f;
    int du = 0, dv = 0;

    const unsigned held = g_sim_buttons[0].held;   /* DAT_073dddd6 */
    if      (held & 0x2) du = -1;
    else if (held & 0x1) du =  1;
    else if (held & 0x4) dv = -1;
    else if (held & 0x8) dv =  1;
    else return;

    for (int row = 0; row < SCENE_WORLDMAP_GRID_ROWS; row++) {
        for (int col = 0; col < SCENE_WORLDMAP_GRID_COLS; col++) {
            if (s_sel_dest != scene_worldmap_grid[col + row * 3])
                continue;

            /* found the selected dest's cell at (col,row) — search outward.
             * `wr` (engine iVar5) is cumulative across steps AND the retry
             * pass; `wc` (iVar4) resets each pass. */
            int wr      = row;
            int retried = 0;
            for (;;) {
                int wc = col;
                for (int step = 0; step < 5; step++) {
                    wc = (du + 3 + wc) % 3;
                    wr = (dv + 5 + wr) % 5;
                    const int cand = scene_worldmap_grid[wc + wr * 3];
                    if (cand == -1)
                        continue;
                    /* candidate cell holds a dest id — is that dest present? */
                    for (int k = 0; k < s_dest_count; k++) {
                        if (s_dest_pos[k] != cand)
                            continue;
                        if ((col != wc || row != wr) &&
                            cand != scene_worldmap_grid[col + row * 3]) {
                            s_sel_dest = cand;                 /* DAT_09643684 */
                            const scene_worldmap_dest_t *L =
                                &scene_worldmap_dest_layout[s_sel_dest];
                            title_save_dialog_cursor_slide(    /* FUN_00435710 */
                                (L->x - 16.0f) * scale,
                                (L->y + 28.0f) * scale);
                            audio_play_se_by_id(0x146);        /* move SE (no RNG) */
                            return;
                        }
                        break;   /* present but same/current cell — keep stepping */
                    }
                }
                /* 5-step pass exhausted with no move (engine LAB_0049e113). */
                if (retried || du == 0)
                    return;
                wr      = (row + 6) % 5;   /* retry from the next row down */
                retried = 1;
            }
        }
    }
}

/* Destination transition tail — port of FUN_0049e163's LAB_0049e304 +
 * dest→mode table (all.c:103079). Fires once the exit dissolve completes:
 * picks the selected destination's scene mode, then kicks the fade-in + asset
 * load — the same machinery the door-exit uses (scene1_player_ctrl.c stage-2).
 *
 * PORT-DEBT(worldmap-dest-scenes): the destination scenes themselves
 * (modes 1/6/0xb/0xd/0xe/0xf) are separate, mostly-unported arcs — selecting a
 * destination fades out, spawns the load, and switches g_scene_state to the
 * target mode, which renders blank until that scene ports. main.c's render
 * `default` + the worker's unconditional thread cleanup (and bounds-checked
 * dispatch) keep this safe (no hang/crash). The per-destination scene-init
 * helpers (FUN_0045e019/196/3cd, FUN_00490e16, FUN_004060ff, FUN_0044bce7) +
 * the worldmap teardown (FUN_0047360f slot-10 unload, FUN_00436f97 furniture)
 * are deferred no-ops. The tutorial recording never presses Z at the town map,
 * so this is NOT exercised by T4's verification. Retire with each dest arc. */
static void scene_worldmap_exit_to_dest(void)
{
    int mode;
    switch (s_sel_dest) {                /* DAT_09643684 → DAT_0438b1c0 */
    case 0:  mode = 1;    break;         /* your shop / home (INGAME) — FUN_004060ff */
    case 6:  mode = 0xb;  break;
    case 5:  mode = 0xd;  break;         /* FUN_0045e3cd */
    case 4:  mode = 0xf;  break;         /* FUN_0045e196 */
    case 2:  mode = 0xe;  break;         /* FUN_0045e019 */
    case 3:  mode = 6;    break;         /* Market — FUN_00490e16(0) */
    default: mode = 6;    break;         /* dest 1 — FUN_00490e16(1) */
    }

    g_scene_state = mode;                /* DAT_0438b1c0 = <dest mode> */
    fade_phase_out_start(0, 0x11);       /* FUN_0045281c(0,0x11) — fade-IN */
    worker_load_spawn();                 /* FUN_00452cde — load worker */
}

/* Per-frame update — mode-8 sim. Port of FUN_0049e163 @ 0x49e163 (575 B),
 * the engine world-map per-state callee (update-dispatch FUN_004536cb case 8,
 * all.c:50605 → 103024). Runs in sim.c case 8 AFTER the shared cursor-anim
 * tick (FUN_00406584 → title_save_dialog_anim_tick, which eases the cursor
 * toward the nav slide target) and the particle tick (FUN_0040fb3a). See
 * docs/findings/town-map-RE.md §3.
 *
 *   Block A — pending-delivery early-out (PORT-DEBT, see below).
 *   Block B — entry-timer cursor snap (timer < 3): pin the destination
 *             pointer to the selected marker while the map eases in.
 *   Block C — input: once timer > 10, Z-up → cursor nav (the 3×5 grid walk);
 *             Z on a disabled dest → denied SE; Z on an enabled dest → arm the
 *             exit (dissolve fade + confirm SE).
 *   Exit state machine — while exiting, wait for the dissolve, then transition
 *             to the selected destination's scene mode.
 *   Tail — advance the entry timer (+1/frame). */
void scene_worldmap_sim(void)
{
    CALL_TRACE_BEGIN(0x49e163u);
    CALL_TRACE_I32("sel",   s_sel_dest);                        /* DAT_09643684 */
    CALL_TRACE_F32("timer", s_entry_timer);                     /* _DAT_09643628 */
    CALL_TRACE_I32("exitc", s_exit_counter);                    /* DAT_0964367c */
    CALL_TRACE_I32("state", scene_worldmap_dest_state(s_sel_dest));
    CALL_TRACE_F32("curx",  title_save_dialog_get_shake_pos_x());
    CALL_TRACE_F32("cury",  title_save_dialog_get_shake_pos_y());
    CALL_TRACE_U32("held",  g_sim_buttons[0].held);             /* DAT_073dddd6 */
    CALL_TRACE_END();

    /* ── Block A — pending-delivery early-out (all.c:103035) ──────────────
     * PORT-DEBT(worldmap-delivery-return): when the player returns to the
     * shop with an undelivered order pending (DAT_0450f49a[slot] != 0), the
     * engine kicks a delivery-return scene load (FUN_0044ba2c → scene
     * 0xc/0x14) + inventory return (FUN_00468d22) and early-returns (no
     * timer++). The delivery/event subsystem is unported; the flag is
     * BSS-zero on a tutorial Continue (the town-map-load recording), so this
     * path is never taken there. Deferred — retire with the delivery arc. */

    /* ── Block B — entry-timer cursor snap (all.c:103050) ─────────────────
     * For the first 3 sim frames after the map loads, keep the shared cursor
     * snapped to the selected destination's marker (no easing yet). */
    if (s_entry_timer < 3.0f) {
        const scene_worldmap_dest_t *L = &scene_worldmap_dest_layout[s_sel_dest];
        title_save_dialog_cursor_set_visible(1);                   /* FUN_0043561a */
        title_save_dialog_cursor_snap(L->x - 16.0f, L->y + 28.0f); /* FUN_00435693 */
    }

    /* DAT_0438bed4 = 0 (all.c:103055): the engine clears the NEW-GAME flag
     * every world-map frame so a destination loads as a CONTINUE. The port
     * models that flag as g_scene_title_anim.continue_mode; the world map is
     * only reached via a Continue (continue_mode already 1 ≡ DAT_0438bed4==0),
     * so this write is a no-op here. */

    /* ── Block C — input (all.c:103056) ──────────────────────────────────── */
    if (s_exit_counter == 0) {
        if (s_entry_timer > 10.0f) {                    /* intro ease finished */
            if ((g_sim_buttons[0].pressed & 0x10) == 0) {  /* DAT_073dddd4 — Z up */
                scene_worldmap_cursor_nav(0);              /* FUN_0049dfc1(0) */
            } else if (scene_worldmap_dest_state(s_sel_dest) == 0) {  /* disabled dest */
                audio_play_se_by_id(0x16a);                /* denied SE (no RNG) */
            } else {                                       /* enabled → arm exit */
                s_exit_counter = 1;                        /* DAT_0964367c = 1 */
                fade_phase1_start(0, 0x11);                /* FUN_004526f5 dissolve */
                audio_play_se_by_id(0x143);                /* confirm SE (no RNG) */
            }
        }
    } else {
        /* exit in progress: wait for the dissolve, then transition. The engine
         * increments DAT_0964367c each frame here; its redundant `== 1` fade
         * re-arm is dead (counter is >= 2 after the increment) and omitted. */
        s_exit_counter++;
        if (fade_is_done()) {                              /* FUN_004528b3 */
            scene_worldmap_exit_to_dest();
            s_exit_counter = 0;                            /* DAT_0964367c = 0 */
        }
    }

    s_entry_timer += 1.0f;                                  /* _DAT_09643628++ */
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <math.h>

#include "render_quad.h"      /* FUN_00404efc/405354/49b425 — quad add/flush/state +
                              * brings in <d3d8.h> with COBJMACROS+CINTERFACE */
#include "font_draw.h"        /* FUN_0047d14c — centred text ("Closed") */
#include "scene1_top_hud.h"   /* scene1_top_hud_clock_phase — DAT_0438b7d4 */
#include "sprite.h"

sprite_t g_scene_worldmap[SCENE_WORLDMAP_COUNT];

static IDirect3DDevice8 *g_scene_worldmap_dev = 0;

/* Win32 load_fn: drives sprite_load against the per-slot sprite_t. */
static int win32_load_fn(const char *path,
                         uint32_t w, uint32_t h,
                         int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(10, dst, name, w, h). The kind=10 is the
     * scene-1 texture-group flag; sprite_load drops it (same as the
     * rest of the openrecet call sites). */
    return sprite_load(dev, path, w, h, &g_scene_worldmap[slot]);
}

static void scene_worldmap_body(void)
{
    /* Engine LAB_00452c96's inner-body calls (objdump @ 0x452c96..):
     *     call 0x49de20    ; FUN_0049de20  — world-map state machine
     *     call 0x4735ad    ; FUN_004735ad  — world-map BMP loader  (THIS)
     *
     * This is the SECONDARY C96 body, which has no port-side spawner caller
     * (dormant). The live world-map path is the PRIMARY worker case-8 body
     * below; keep this BMP-only (the dormant half-port). */
    scene_worldmap_load_with(win32_load_fn, g_scene_worldmap_dev);
}

/* PRIMARY worker case-8 body — engine primary jump-table @ 0x452984:
 *     call 0x49de20    ; FUN_0049de20  — world-map scene init
 *     call 0x4735ad    ; FUN_004735ad  — world-map BMP loader
 * The door-exit stage-2 (scene1_player_ctrl.c) sets g_scene_state = 8 +
 * spawns the primary worker; worker_load_thread_proc dispatches it here. */
static void scene_worldmap_primary_cb(void)
{
    scene_worldmap_init_state();
    scene_worldmap_load_with(win32_load_fn, g_scene_worldmap_dev);
}

void scene_worldmap_init(struct IDirect3DDevice8 *dev)
{
    g_scene_worldmap_dev = (IDirect3DDevice8 *)dev;
    /* Secondary C96 body (BMP-only, dormant — no spawner caller yet). */
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C96, scene_worldmap_body);
    /* PRIMARY case-8 body (init + load) — the live door→world-map path. */
    worker_load_set_cb(SCENE_STATE_WORLDMAP, scene_worldmap_primary_cb);
}

/* Per-frame render — mode-8 render dispatch (main.c render switch case 8).
 *
 * Port of FUN_0049e3a3(scale) @ 0x49e3a3 (the body of the mode-8 render
 * wrapper FUN_0049e686 → FUN_0049e3a3(1.0)). Three layers (all.c:103136):
 *   1. the worldmap photo with a time-of-day crossfade (2 passes),
 *   2. the mappoint.tga destination markers (per-state alpha/size + the
 *      selected dest drawn bigger), under COLOROP=ADDSIGNED,
 *   3. centred red "Closed" labels for closed destinations.
 *
 * `scale` is always 1.0 (the wrapper hard-codes it), so the bg dst is
 * 640x480 in 640-relative coords — render_quad_add then scales by
 * screen_w/640 like the engine's FUN_00404efc. The engine wrapper also
 * Clears to cyan first; the port relies on main.c's per-frame Clear (the
 * opaque bg covers the whole framebuffer, so the clear colour is moot).
 *
 * NOTE: the engine dispatch is `FUN_0049e686(); FUN_0040a765();` — the
 * world map also draws the full INGAME HUD aggregator (top clock/Day/money
 * + the tutorial text box). That is a separate, larger shared function
 * (FUN_0040a765); wiring it for mode 8 is a follow-up chip. T3 is the
 * worldmap scene body only. The trailing COLOROP reset below restores
 * MODULATE precisely so that HUD (when wired) draws under the right op. */
void scene_worldmap_render(struct IDirect3DDevice8 *dev)
{
    /* FUN_0049b425 — 2D render-state preset (alpha blend, COLOROP=MODULATE,
     * linear filter). The bg crossfade passes draw under MODULATE. */
    render_quad_state_setup(dev);

    /* ── 1. background: time-of-day crossfade (all.c:103162) ──────────────
     * tod = working dword 0xb0fc, used RAW (1=day / 2=eve / 3=night → the
     * texture indices land on worldmap_nomal/yugata/night via tod-1/tod-2).
     *   pass 0 (base):  worldmap[max(tod-2,0)] @ alpha 0xff
     *   pass 1 (blend): worldmap[max(tod-1,0)] @ alpha 0xff-ftol((tod-clock)*255)
     * clock = DAT_0438b7d4 (the animated time-of-day float; on a CONTINUE it
     * is snapped to tod, so the blend term is ~0 and pass 1 covers pass 0). */
    const int       slot  = save_work_active_slot();         /* DAT_0438b1e0 */
    const uint32_t *dw    = save_work_dwords_at(slot);
    const int       tod   = dw ? (int)dw[WM_DW_TOD] : 0;     /* DAT_0450fb88 */
    const float     clock = scene1_top_hud_clock_phase();    /* DAT_0438b7d4 */

    const float bg_dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };  /* scale(=1)*640/480 */
    const float bg_src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };  /* top-left of the 1024x512 bmp */

    for (int pass = 0; pass < 2; pass++) {
        int alpha = 0xff;
        int tex_idx;
        if (pass == 1) {
            /* 0x519630 = 255.0; FUN_00503954 = __ftol (truncate toward 0). */
            alpha   = 0xff - (int)(((float)tod - clock) * 255.0f);
            tex_idx = tod - 1;
        } else {
            tex_idx = tod - 2;
        }
        if (tex_idx < 0) tex_idx = 0;

        render_quad_bind(dev, &g_scene_worldmap[tex_idx]);
        render_quad_add(bg_dst, bg_src,
                        g_scene_worldmap[tex_idx].width,
                        g_scene_worldmap[tex_idx].height,
                        ((uint32_t)alpha << 24) | 0x00ffffffu);
        render_quad_flush(dev);
    }

    /* ── 2. destination markers (mappoint.tga, all.c:103193) ──────────────
     * Engine sets COLOROP=ADDSIGNED (8) before the markers and holds it
     * through the "Closed" labels, resetting to MODULATE (4) at the end. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    const sprite_t *mp = &g_scene_worldmap[SCENE_WORLDMAP_TEX_MAPPOINT];
    render_quad_bind(dev, mp);

    const float timer = scene_worldmap_entry_timer();   /* _DAT_09643628 (T4 sim: frozen→0) */
    const int   sel   = scene_worldmap_sel_dest();      /* DAT_09643684 */
    const int   n     = scene_worldmap_dest_count();    /* DAT_005fd588 */

    for (int i = 0; i < n; i++) {
        const int pos   = scene_worldmap_dest_pos(i);     /* DAT_096435d8[i] (map-pos) */
        const int state = scene_worldmap_dest_state(i);   /* DAT_09643588[i] */

        float w = 144.0f, h = 44.8f;   /* default marker size */
        int   size_alpha = 200;        /* iVar4 — ARGB alpha channel */
        int   grey       = 0x7f;       /* uVar1 — RGB (ADDSIGNED brightness) */
        if (state == 0) grey = 0x40;                                /* disabled: dim */
        if (state == 2)                                             /* highlighted: sin-pulse */
            grey = (int)(sinf(timer * 0.15f) * 16.0f + 143.0f);     /* FUN_00503a44 sinf + __ftol */
        if (pos == sel) { size_alpha = 0xff; w = 180.0f; h = 56.0f; } /* selected: bigger + opaque */

        const scene_worldmap_dest_t *L = &scene_worldmap_dest_layout[pos];
        const float row_top = (float)(L->sprite_row * 0x38);        /* 56 px tall rows */
        const float src[4] = { 0.0f, row_top, 180.0f, (float)((L->sprite_row + 1) * 0x38) };
        const float dst[4] = {
            (L->x + 90.0f) - w * 0.5f,
            (L->y + 28.0f) - h * 0.5f,
            w, h,
        };
        /* ARGB(size_alpha, grey, grey, grey) — engine's nested shift-or. */
        const uint32_t col =
            ((((uint32_t)size_alpha << 8 | (uint32_t)grey) << 8
              | (uint32_t)grey) << 8) | (uint32_t)grey;
        render_quad_add(dst, src, mp->width, mp->height, col);
    }
    render_quad_flush(dev);

    /* ── 3. "Closed" labels (all.c:103233) — centred red, scale 1.2 ──────── */
    for (int i = 0; i < n; i++) {
        if (scene_worldmap_dest_closed(i)) {
            const scene_worldmap_dest_t *L =
                &scene_worldmap_dest_layout[scene_worldmap_dest_pos(i)];
            font_draw_text_centered(dev, L->x + 94.0f, L->y + 16.0f,
                                    "Closed", 0xffff3737u, 1.2f);   /* s_Closed_005fd65c */
        }
    }
    render_quad_flush(dev);

    /* COLOROP reset → MODULATE (engine FUN_0049e3a3 tail, all.c:103244) so
     * the trailing HUD aggregator (and the next frame) draws under MODULATE. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

#endif /* _WIN32 */

/* ─── reset (pure-C model + Win32 sprites) ───────────────────────────── */

void scene_worldmap_reset(void)
{
    /* Pure-C destination model. */
    s_dest_count   = SCENE_WORLDMAP_DEST_COUNT;
    s_sel_dest     = 0;
    s_entry_timer  = 0.0f;
    s_exit_counter = 0;
    s_misc_680     = 0;
    for (int i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) {
        s_dest_pos[i]    = 0;
        s_dest_state[i]  = 0;
        s_dest_closed[i] = 0;
    }
#ifdef _WIN32
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        /* Same lazy-reset shape as scene_floor/jutan — leaves the
         * IDirect3D resource leaked, which is fine because tests on
         * Win32 don't run sprite_load, and the engine's lost-device
         * handling (not yet ported) will own the real teardown. */
        g_scene_worldmap[i].tex    = 0;
        g_scene_worldmap[i].width  = 0;
        g_scene_worldmap[i].height = 0;
    }
#endif
}
