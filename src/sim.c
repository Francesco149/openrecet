/*
 * sim.c — per-frame simulation halves.
 *
 * Engine source for sim_a: FUN_004536cb @ 0x4536cb (1745 bytes).
 *
 * What's actually ported in this commit:
 *   - The button-state ring at the top of FUN_004536cb (lines 42-70):
 *     current/prev/pressed/held-with-repeat masks for two players,
 *     plus the 16-short per-bit auto-repeat counters. Pure-C helper
 *     `sim_button_ring_update` exposes the math for tests.
 *   - The state-dispatch tail at LAB_00453cfb: `DAT_0438b8cc++`.
 *     The time-dilation float math and the `FUN_004526ab` post-frame
 *     helper are stubbed (no consumers yet in our skeleton).
 *   - Scene dispatch for state == 0 → `scene_title_sim_default`.
 *     Other scene states (1..16) and the mode-escape sub-blocks
 *     (DAT_06a49954/DAT_06a499c4/DAT_06a49964/DAT_06a49998) are
 *     omitted — they unlock as scenes get ported.
 *
 * Not yet ported:
 *   - FUN_0049966a (sim_b) entirely — the scheduler tolerates NULL.
 *   - Side-effect callouts inside the ring loop: DAT_0438b1e0 visit
 *     counter is still a stub. FUN_0047c29d is wired (font_age_tick).
 *
 * See docs/findings/winmain-and-bootstrap.md §"Sim halves" (TBD) and
 * docs/decompiled/by-address/4536cb.c.
 */

#include "sim.h"

#include "call_trace.h"

#include <string.h>

#include "debug_param_tick.h" /* engine FUN_00405552 — debug-param tick gate */
#include "fade.h"         /* fade_tick — engine's per-frame fade counter advance */
#include "font.h"         /* font_age_tick — engine's per-frame LRU bump */
#include "input.h"        /* g_input_state for cur-buttons read */
#include "nowloading.h"   /* nowloading_set_active(0) — drop the overlay gate */
#include "scene.h"        /* g_scene_state dispatch */
#include "scene1_intro_dialogue.h" /* opening-prologue dialogue (TEXT_ANIM + inter-script load) */
#include "skip_event.h"            /* ESC "skip this event?" prompt */
#include "scene1_particles_tick.h"  /* engine FUN_0040fb3a — LAB_00453bed body */
#include "scene_worldmap.h" /* scene_worldmap_sim — mode-8 per-state callee (FUN_0049e163) */
#include "scene_guild.h"    /* scene_guild_sim — mode-6 per-state callee (FUN_00490e24) */
#include "scene1_top_hud.h" /* scene1_top_hud_worldmap_tooltip_tick — FUN_00406584 mode-8 selector */
#include "scene1_sim.h"   /* scene1_ingame_tick — engine FUN_004427d3 wrapper */
#include "scene_title.h"  /* scene_title_sim_default + g_scene_title_* */
#include "title_save_dialog.h" /* title_save_dialog_anim_tick — the shared
                                * cursor's FUN_004356cd (bob b154 + shake slide),
                                * which retail runs every state-1 frame via
                                * FUN_00406584 (see the INGAME case below). */
#include "stage_load_pulse.h" /* engine FUN_004693e3 — stage-load animation pulse */
#include "worker_load.h"  /* worker_load_busy — primary asset-load worker gate */

struct sim_player_buttons g_sim_buttons[SIM_NUM_PLAYERS];
uint32_t                  g_sim_frame_count;

/* ─── scene-effect counters (FUN_004532df state) ────────────────────────
 *
 * All BSS-zero on init. See sim.h header banner for what each counter
 * is read by; they sit dormant today (no setter is wired yet). */
static int32_t g_sim_counter_990;
static int32_t g_sim_counter_994;
static int32_t g_sim_counter_998;
static int32_t g_sim_counter_99c;
static int32_t g_sim_mode_9a0;
static int32_t g_sim_threshold94;   /* DAT_005c5938 — latched by FUN_004532bc */

/* ─── pure-C button-state ring (FUN_004536cb lines 42-70) ────────────── */

void sim_button_ring_update(uint16_t cur,
                            uint16_t *prev,
                            int16_t   repeat[16],
                            uint16_t *out_pressed,
                            uint16_t *out_held)
{
    const uint16_t p = *prev;

    /* `pressed` and the initial `held` are derived first, then the
     * per-bit repeat loop masks specific bits out of `held`. */
    uint16_t pressed = (uint16_t)(~p & cur);
    uint16_t held    = cur;

    for (int i = 0; i < 16; i++) {
        const int bit = 1 << i;
        const int changed = ((cur ^ p) & bit) != 0;

        if (changed) {
            /* Rising or falling edge: reset the repeat counter so the
             * next 12 frames of unchanged state are a "settle" window. */
            repeat[i] = 0xc;
        } else {
            /* Bit's value matches the previous frame. The engine first
             * clamps the counter into [1, 0xc] (after possibly being
             * disturbed by an outside writer), then on the "else"
             * branch decrements; while counter > 0 the bit is gated
             * out of `held`. */
            if (repeat[i] > 0xc) {
                repeat[i] = 0xc;
            }
            if (repeat[i] < 1) {
                /* Counter reached 0: reload to 4 and let the bit through
                 * (the auto-repeat pulse). Mutually exclusive with the
                 * decrement-and-gate branch below — engine FUN_004536cb
                 * `if (counter < 1) counter = 4; else { counter--; clear; }`. */
                repeat[i] = 4;
            } else {
                /* Counter still settling: decrement and gate the bit out of
                 * `held` UNCONDITIONALLY. The engine clears the repeat bit on
                 * every frame it takes this branch (50394-50396), including
                 * the 1→0 frame. (The old port gated only while the post-
                 * decrement value was >0, which let the bit through one frame
                 * early AND again on the next reload frame — a double-pulse
                 * that made the cursor repeat ~2× too fast vs retail. §89.) */
                repeat[i] = (int16_t)(repeat[i] - 1);
                held = (uint16_t)(held & ~bit);
            }
        }
    }

    *prev        = cur;
    *out_pressed = pressed;
    *out_held    = held;
}

/* ─── FUN_004532df scene-effect counter pump ────────────────────────── */

void sim_loading_pump_pure(int32_t *c990,
                           int32_t *c994,
                           int32_t *c998,
                           int32_t  mode,
                           int32_t  threshold94)
{
    /* Engine L50124-50126: 990 cycles 1..0x1f then wraps to 0. */
    if (*c990 > 0) {
        *c990 = *c990 + 1;
        if (*c990 == 0x20) *c990 = 0;
    }

    /* Engine L50127-50129: 994 cycles 1..(threshold-1) then wraps. The
     * threshold is the latched DAT_005c5938 (engine FUN_004532bc); if
     * it's 0 (cold-start BSS-zero) the comparison `0 <= 1` is true
     * immediately so the counter wraps the very next tick — matches
     * the engine's behavior when an effect is started before the
     * threshold is set, which is dormant in vendor data. */
    if (*c994 > 0) {
        *c994 = *c994 + 1;
        if (threshold94 <= *c994) *c994 = 0;
    }

    /* Engine L50130-50140: 998 has two modes.
     *
     *   mode==0: cyclic 1..0x13 → wraps to 0 at 0x14.
     *   mode!=0: monotone with ceiling at 0xc (engine `if (0xc < v) v = 0xc;`).
     */
    if (mode == 0) {
        if (*c998 > 0) {
            *c998 = *c998 + 1;
            if (*c998 == 0x14) *c998 = 0;
        }
    } else if (*c998 > 0) {
        *c998 = *c998 + 1;
        if (*c998 > 0xc) *c998 = 0xc;
    }
}

void sim_loading_pump(void)
{
    /* E.2 probe — FUN_004532df @ 0x4532df. */
    CALL_TRACE_ENTER(0x4532dfu);

    sim_loading_pump_pure(&g_sim_counter_990,
                          &g_sim_counter_994,
                          &g_sim_counter_998,
                          g_sim_mode_9a0,
                          g_sim_threshold94);
    /* DAT_06a4999c is pumped from FUN_004547ab (render side), not from
     * here — engine inlines that one separately at L51057-51064. We
     * leave it untouched in this helper so a future render-side port
     * can drive it without double-pumping. */
}

int32_t sim_get_counter_990(void)   { return g_sim_counter_990; }
int32_t sim_get_counter_994(void)   { return g_sim_counter_994; }
int32_t sim_get_counter_998(void)   { return g_sim_counter_998; }
int32_t sim_get_counter_99c(void)   { return g_sim_counter_99c; }
int32_t sim_get_mode_9a0(void)      { return g_sim_mode_9a0;    }
int32_t sim_get_threshold94(void)   { return g_sim_threshold94; }

void sim_set_counter_990(int32_t v) { g_sim_counter_990 = v; }
void sim_set_counter_994(int32_t v, int32_t threshold94)
{
    g_sim_counter_994 = v;
    g_sim_threshold94 = threshold94;
}
void sim_set_counter_998(int32_t v) { g_sim_counter_998 = v; }
void sim_set_counter_99c(int32_t v) { g_sim_counter_99c = v; }
void sim_set_mode_9a0(int32_t v)    { g_sim_mode_9a0    = v; }

/* ─── lifecycle + frame entry ────────────────────────────────────────── */

void sim_phasepin(void) { g_sim_frame_count = 0; }

void sim_init(void)
{
    memset(g_sim_buttons, 0, sizeof g_sim_buttons);
    g_sim_frame_count   = 0;
    g_sim_counter_990   = 0;
    g_sim_counter_994   = 0;
    g_sim_counter_998   = 0;
    g_sim_counter_99c   = 0;
    g_sim_mode_9a0      = 0;
    g_sim_threshold94   = 0;
}

void sim_step_a(void)
{
    /* E.2 probe — FUN_004536cb @ 0x4536cb. Marked STUB because we
     * port the worker-busy gate, button ring, scene dispatch, and
     * fade-tick tail but skip the engine's middle: DAT_06a499cc
     * one-shot init, video poll (FUN_0040cea6), DAT_06a499c8 scene-
     * transition counter, DAT_06a49998==3 transition arm, DAT_06a499c4
     * scene-reseed check, plus several smaller writes around
     * DAT_06a4993c. The unported regions are dormant in the captured
     * pre-3D trace, so the count parity holds, but the body is far
     * from complete. */
    CALL_TRACE_ENTER_STUB(0x4536cbu);

    /* FUN_0047c29d (font_age_tick) — engine L50362, runs BEFORE the
     * worker-busy check. Glyph cache aging keeps ticking even during
     * the loading screen (the engine still draws font on top of the
     * loading overlay for any UI element that was alive pre-fade). */
    font_age_tick();

    /* Engine FUN_004536cb L50363-50367: while the primary asset-load
     * worker is busy, pump the scene-effect counters and short-circuit
     * the rest of sim. Once the worker drops, clear the primary
     * nowloading gate — the overlay disappears on the very next
     * render. See sim.h's sim_step_a banner for full notes. */
    if (worker_load_busy()) {
        sim_loading_pump();
        return;
    }
    nowloading_set_active(0);

    /* Run the button-state ring for every player. Player N's `cur`
     * comes from g_input_state[N].buttons (written by input_poll).
     *
     * Engine quirk: the ring loop in FUN_004536cb walks both blocks
     * unconditionally, even though FUN_0047b73c only writes player 0
     * (see input.{c,h} note on "DAT_073dddd0 / DAT_073dddfa"). We
     * mirror that — block 1 stays cur=0 and the math degenerates to
     * `pressed=0, held=0, repeat clears` automatically. */
    for (int i = 0; i < SIM_NUM_PLAYERS; i++) {
        const uint16_t cur = g_input_state[i].buttons;
        sim_button_ring_update(cur,
                               &g_sim_buttons[i].prev,
                               g_sim_buttons[i].repeat,
                               &g_sim_buttons[i].pressed,
                               &g_sim_buttons[i].held);
        g_sim_buttons[i].cur = cur;
    }

    /* Opening-prologue dialogue. Ticks on INGAME, non-loading frames (we only
     * reach here with the worker idle), driving the iv1_1→iv1_2 reveal +
     * TEXT_ANIM_START/END anchors off player 1's held buttons. Dormant unless
     * armed at new-game; a no-op once the 46 lines complete.
     * See src/scene1_intro_dialogue.h. */
    if (g_scene_state == SCENE_STATE_INGAME) {
        /* The skip prompt is armed through the real ESC dispatch
         * (esc_pressed() → skip_event_arm()). In normal play that fires from
         * WndProc's WM_KEYDOWN; under the TAS harness the segtrace {esc:N} op
         * replays the same esc_pressed() call (main.c segtrace_esc_cb) — so
         * tests drive the prompt by adding an {esc} to the trace, not by any
         * env-var/frame hack. See tests/scenarios/intro-skip-prompt. */
        if (skip_event_open()) {
            /* The skip-event prompt is modal (retail FUN_004536cb routes the
             * frame to LAB_00453cfb, skipping the scene tick, while the prompt
             * is up): freeze the dialogue and run the prompt. On "Yes" tear the
             * event down to free-roam; "No"/cancel resumes the dialogue next
             * frame (the runtime is left untouched). See src/skip_event.h.
             * PORT-DEBT(simplified, FUN_004536cb): retail freezes the *entire*
             * in-game sim here (LAB_00453cfb); the port freezes the dialogue —
             * the only prologue consumer — and lets the (stubbed) siblings run. */
            if (skip_event_tick(g_input_state[0].buttons) == SKIP_EVENT_CONFIRMED)
                scene1_intro_dialogue_skip_to_end();
        } else {
            scene1_intro_dialogue_tick(g_input_state[0].buttons);
        }
    } else if (g_scene_state == 6) {
        /* Merchant's Guild (mode 6) first-visit cutscene (iv1_3), armed by
         * scene_guild_sim's first-visit branch.  Tick the shared dialogue
         * runtime so it loads + advances + fast-forwards (X-hold), exactly as
         * the INGAME tutorial dialogues do.  No ESC skip-prompt modal here —
         * that flow is prologue-only (skip_event is armed by the title/HOUSE
         * path).  No-op until armed; a no-op once the script completes. */
        scene1_intro_dialogue_tick(g_input_state[0].buttons);
    }

    /* Engine FUN_004536cb L50470-50471: two unconditional per-frame
     * helpers, run between the button mode-cycle block (above) and
     * the scene-state dispatch (below). In the engine they sit
     * AFTER the video poll + DAT_06a499c8 scene-transition counter +
     * DAT_06a49998==3 arm — all unported, all dormant in the captured
     * trace — so the relative order vs the rest of our sim_step_a
     * doesn't matter for trace parity. */
    debug_param_tick();   /* FUN_00405552 — body deferred (gate=0 path) */
    stage_load_pulse_tick(); /* FUN_004693e3 — counter ramp 0..5 */

    /* Scene dispatch by `g_scene_state` (engine global DAT_0438b1c0).
     *
     * Wired today (see docs/findings/sim-step-a-dispatch.md):
     *   - state 0  (title)  → scene_title_sim_default
     *   - state 1  (INGAME) → scene1_ingame_tick   (Cs1)
     *   - state 8  (WORLD MAP) → scene1_particles_tick + scene_worldmap_sim
     *     (the per-state callee FUN_0049e163; body is the T4 chip).
     *   - states 2, 3, 6, 7, 0xb, 0xd-0x10 → engine LAB_00453bed,
     *     ported as a bare scene1_particles_tick call.  The engine
     *     also runs FUN_00406584 (Cs3) and a per-state callee here;
     *     both stay stubbed for these.
     *
     * States 4, 5, 0xa, 0xc, plus any state >= 0x11, explicitly do
     * NOT hit the particle tick in the engine (block 21 of the
     * survey).  State 5 invokes FUN_0046c039 (worldmap, unported);
     * state 0xa invokes FUN_0047e711 (unported); state 9 has its own
     * `DAT_06a4997c`-selector path in block 17 (Cs4).
     */
    switch (g_scene_state) {
    case SCENE_STATE_TITLE:
        scene_title_sim_default();
        break;
    case SCENE_STATE_INGAME:
        /* Cs1 — minimal port of FUN_004427d3.  The engine's INGAME
         * path also runs 5 unported siblings (player + NPC + world
         * tick + UI + camera).  See src/scene1_sim.c header. */
        scene1_ingame_tick();
        /* FUN_00406584 (Cs3) runs right after FUN_004427d3 on every
         * state-1 frame (engine FUN_004536cb L50556). Its only part we
         * need here is the FUN_004356cd call it makes — the shared menu
         * cursor's per-frame bob (DAT_0438b154) + shake-slide step. Retail
         * advances b154 across the WHOLE prologue this way, so when the
         * skip-event choice box opens its hand cursor's bob phase is already
         * accumulated; without this the port's bob would be frozen at the
         * stale title-scene value (a visible cursor-position divergence on
         * the skip prompt). The rest of FUN_00406584 stays unported. */
        title_save_dialog_anim_tick();
        break;

    /* Cs2 — LAB_00453bed mass dispatch.  The mode-8 per-state callee
     * FUN_0049e163 is wired (scene_worldmap_sim, above).  The rest
     * (FUN_0049d8a4 / FUN_0041ee24 / FUN_00490e24 / FUN_0049db8a /
     * FUN_0045c051 / FUN_0045e053 / FUN_0045e1a5 / FUN_0045e2dd /
     * FUN_0045e3dc) stay stubbed — scene-specific update routines
     * (cutscene / dialog / dungeon / ending) with no consumer wired today. */
    case SCENE_STATE_WORLDMAP:  /* 8 — WORLD / TOWN map */
        /* Engine LAB_00453bed for mode 8 (all.c:50586): FUN_00406584 (the
         * shared cursor anim/slide step) → FUN_0040fb3a (particles) → the
         * per-state callee FUN_0049e163 (world-map sim), in that order.
         * title_save_dialog_anim_tick is the FUN_00406584 subset that eases
         * the shared cursor toward the destination-pointer slide target the
         * sim arms via FUN_00435710 — it MUST run before the sim each frame
         * so the cursor position tracks retail (T4). The bob (b154) it also
         * advances is consumed by the destination-pointer render. */
        title_save_dialog_anim_tick();
        /* FUN_00406584 mode-8 selector (all.c:4776): set the travel-time tooltip
         * band from the destination under the cursor + ramp its slide-in.  Runs
         * BEFORE the nav (scene_worldmap_sim) so the box lags a cursor move by
         * one frame, matching the engine's pre-sim FUN_00406584. */
        scene1_top_hud_worldmap_tooltip_tick(scene_worldmap_sel_dest(),
                                             scene_worldmap_return_pending());
        scene1_particles_tick();
        scene_worldmap_sim();
        break;

    case 6:    /* Merchant's Guild / Market (engine FUN_00490e24 → FUN_004922c0) */
        /* LAB_00453bed for mode 6: FUN_00406584 (shared cursor anim) →
         * FUN_0040fb3a (particles) → the per-state callee FUN_00490e24, in that
         * order.  The cursor-anim sibling stays stubbed (no guild consumer wired
         * yet); scene_guild_sim is the per-state event tick (entry-tick counter
         * + first-visit iv1_3 cutscene trigger). */
        scene1_particles_tick();
        scene_guild_sim();
        break;

    case 2:    /* cutscene  (engine FUN_0049d8a4) */
    case 3:    /* dialog    (engine FUN_0041ee24) */
    case 7:    /* dungeon idle  (engine FUN_0049db8a) */
    case 0xb:  /* (engine FUN_0045c051, 3021 B — biggest scene callee) */
    case 0xd:  /* ending arm A (engine FUN_0045e3dc) */
    case 0xe:  /* ending arm B (engine FUN_0045e053) */
    case 0xf:  /* ending arm C (engine FUN_0045e1a5) */
    case 0x10: /* ending arm D (engine FUN_0045e2dd) */
        scene1_particles_tick();
        break;

    default:
        /* States 4, 5, 9, 0xa, 0xc, and any >= 0x11.  Dormant in our
         * port — neither the engine nor we run scene1_particles_tick
         * here. */
        break;
    }

    /* Engine FUN_004536cb LAB_00453cfb tail (line 318): per-tick fade
     * counter advance. Runs unconditionally — phase==0 is a no-op. */
    fade_tick();

    g_sim_frame_count++;
}
