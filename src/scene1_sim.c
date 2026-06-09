/*
 * scene1_sim.c — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004536cb @ 0x4536cb state-1 sub-dispatch
 * (L50555-50568) — picks one of three arms based on flag globals,
 * then runs that arm's per-tick body.
 *
 * Cs1 ported FUN_004427d3 (30 B transition/paused wrapper) minimally:
 *
 *   FUN_0048407f  (  795 B) — unported
 *   FUN_00430c00  (  109 B) — unported
 *   FUN_0043ae20  (25750 B) — unported (player + NPC + world tick)
 *   FUN_0043a5d9  ( 1429 B) — unported
 *   FUN_0040fb3a            — ported as scene1_particles_tick()
 *   FUN_004426a7  (  300 B) — unported
 *
 * C8j.3 ports FUN_00442cef (2490 B default-running arm) as a thin
 * wrapper.  The engine body is dominated by gameplay-logic gates
 * (pause counters, cinematic counters, equip-bag counter, player-state
 * transitions) that gate the actual ticks.  With BSS-zero gates (HOUSE
 * default) the per-tick effect collapses to:
 *
 *   FUN_0043ae20  table B tick  → scene1_records_b_tick() (skeleton —
 *                                  outer loop + preamble only;
 *                                  per-type dispatch deferred to
 *                                  sub-chip ladder C8j-tick.2+)
 *   FUN_0044284b  table C tick  → scene1_records_c_tick()
 *   FUN_0040fb3a  table A tick  → scene1_particles_tick()
 *
 * The 5/3 stubbed siblings are safe to drop because their outputs feed
 * `FUN_0043ae20`'s game-logic tick (player movement, NPC AI, item
 * pickups), which we don't run.  Nothing in our port reads what they
 * would have written — scene-1 render reads only from the per-record
 * tables, which the integrators + spawn API populate independently.
 *
 * See docs/findings/sim-step-a-dispatch.md §"Cs1" for the original
 * Cs1 survey, and docs/findings/scene1-record-populators.md §"C8j.3"
 * for the C8j ladder.
 */

#include "scene1_sim.h"

#include "scene1_particles_tick.h"
#include "scene1_companion_ctrl.h"
#include "scene1_conversation_pose.h"
#include "scene1_player_ctrl.h"
#include "scene1_records_b_tick.h"
#include "scene1_records_c_tick.h"
#include "scene1_tutorial_dispatch.h"
#include "rng.h"
#include "call_trace.h"

/* Engine flag stand-ins.  All BSS-zero default → HOUSE selects the
 * default-running arm.  See header for engine-globals mapping. */
int g_scene1_ingame_transition_flag = 0;  /* DAT_0438b1d0 */
int g_scene1_ingame_skip_flag       = 0;  /* DAT_0438b1d8 */
int g_scene1_ingame_paused_flag     = 0;  /* DAT_0438b1c8 */

void scene1_ingame_transition_arm_tick(void)
{
    /* E.2 probe — FUN_004427d3 @ 0x4427d3.  STUB: body ports only 1 of
     * the engine's 6 calls (scene1_particles_tick); the other 5 are
     * dormant.  Marked stub so call_trace_diff surfaces this row as ≈
     * rather than ` ` (= full parity), preventing the false-positive
     * "this path is complete" reading. */
    CALL_TRACE_ENTER_STUB(0x4427d3u);

    /* FUN_004427d3 L10 — the one ported call.  The other 5 are
     * dormant; documented in this file's header. */
    scene1_particles_tick();
}

void scene1_ingame_default_arm_tick(void)
{
    /* E.2 probe — FUN_00442cef @ 0x442cef. */
    CALL_TRACE_ENTER(0x442cefu);

    /* FUN_0048407f conversation branch (the event-actor tick): hold the iv1_2
     * face-to-face pose on the freeroam chibis (Recette look-up/blink anim 6,
     * Tear talk anim 4) and advance their sprite anim.  Runs FIRST, before the
     * freeroam controllers, mirroring the branch's position ahead of the
     * per-actor anim step + the spring-follow.  Inert outside the conversation;
     * the companion controller reads scene1_conversation_pose_active() to yield
     * its anim/facing while the pose is held. */
    scene1_conversation_pose_tick();

    /* FUN_00442cef L40595-40598 — the player controller runs FIRST, before
     * the records-B tick, gated on DAT_0438be94 < 0x78 and a dispatcher that
     * selects FUN_0048670f (the live HOUSE branch) over the FUN_0048b3f6
     * variant.  Both gates open in HOUSE; W1 wires the entry (stub body). */
    scene1_player_ctrl_tick();

    /* Companion controller (FUN_0048a833).  The engine nests it INSIDE the player
     * driver's FUN_0048b850, right before the foot-dust emit, so the fairy's
     * wing-sparkle RNG precedes the dust's.  On a free-roam walk frame
     * player_ctrl_b850_move() already ran it there (and advanced db054 after the
     * dust); this fallback ticks+advances it on every OTHER frame — the dialogue /
     * display-menu / pre-move paths the engine reaches through FUN_0048b850's
     * other callers but the port's stubbed arms do not.  Gated on the latch so the
     * companion ticks exactly once per frame (engine-quirks §114). */
    if (!player_ctrl_companion_ticked()) {
        scene1_companion_ctrl_tick();
        scene1_companion_ctrl_advance_phase();
    }

    /* FUN_00442cef L40603 — gated on DAT_0438b4b4 == 0 (BSS-zero in
     * HOUSE → gate opens).  C8j-tick.1 ports the SKELETON ONLY (outer
     * loop + preamble pos+=vel + age++ + dead-slot skip).  Per-type
     * dispatch is a no-op stub until sub-chip ladder fills in bodies.
     * See docs/findings/scene1-records-b-tick.md. */
    scene1_records_b_tick();

    /* FUN_00442cef L40611 — unconditional inside the default-running
     * nested block. */
    scene1_records_c_tick();

    /* FUN_00442cef L40849 — the FUN_004427f1 → FUN_0044bd0d story-event
     * scheduler, just before the particles tail.  Focused port: only the
     * shop-display tutorial branches (iv1_5/iv1_6).  Runs AFTER the player
     * controller so a placement's condition flags (D1) are seen the same frame;
     * RNG-neutral (FUN_0044bd0d consumes no shared LCG), so its position relative
     * to the particles/overlay tail does not affect parity. */
    scene1_tutorial_dispatch_tick();

    /* FUN_00442cef L40851 (LAB_004435f7) — unconditional tail; reaches
     * every code path of the function including the early-return pause
     * branches. */
    scene1_particles_tick();

    /* FUN_00442cef tail, immediately after FUN_0040fb3a (scene1_particles_
     * tick) + FUN_004426a7 (unported, consumes no RNG): the engine's
     * developer coordinate overlay.  It calls the raw LCG once
     * (thunk_FUN_005041f6 @ 0x471084 → 442cef.c L421, ret 0x443606) and
     * sprintf()s the result as "%d", then the player X/Y/Z (DAT_056da1d8/dc/e0)
     * as "X:%f"/"Y:%f"/"Z:%f", into the debug text grid at DAT_06a47aac rows
     * 4-6 (FUN_00451874).  That grid is NOT drawn in the retail Steam build, so
     * we faithfully consume the RNG step but render nothing.
     *
     * The step is **UNCONDITIONAL** — `442cef.c` L418-421 `LAB_004435f7` reaches
     * the `thunk_FUN_005041f6()` on EVERY path to the function tail (the gates at
     * L412-416 only decide whether `FUN_004427f1` runs *before* it), so the
     * overlay LCG step fires once per render frame regardless of player movement.
     *
     * §95 REVISED-AGAIN 2026-06-05: a 2026-06-04 pass briefly **movement-gated**
     * this, mis-reading a `house-idle rngcalls DESYNC` as "retail consumes 0 when
     * idle".  That measurement was **confounded by the un-pinned background-window
     * NPCs** (which share this LCG and were freely desyncing port↔retail until the
     * 2026-06-05 bg-NPC {phasepin}).  With the NPCs pinned, a clean
     * `house-idle-npc-drift --target both` rng-callsite drill shows retail
     * consuming `0x443606` **every idle frame** (per-frame caller tally = 1, not
     * 0) — and the decompile is unconditional.  Reverted to unconditional; both
     * idle AND walk stay rngcalls-ALIGNED (walk was unaffected — every walk frame
     * moved, so the gate was open there anyway).  engine-quirks §95;
     * docs/findings/freeroam-rng-consumption.md (Lead C). */
    scene1_debug_overlay_consume_rng();
}

/* §95 dev-overlay LCG step (442cef.c L421 / ret 0x443606), UNCONDITIONAL: the
 * engine consumes exactly one raw LCG step at the FUN_00442cef tail every render
 * frame, idle or moving (the call sits at LAB_004435f7, past every gate).
 * Extracted so it is unit-testable in isolation. */
void scene1_debug_overlay_consume_rng(void)
{
    (void)rng_next15();
}

void scene1_ingame_tick(void)
{
    /* Engine FUN_004536cb L50555-50568 — state-1 sub-dispatch:
     *
     *   if (transition_flag != 0)  → transition arm
     *   else if (skip_flag != 0)   → skip (no sim call)
     *   else if (paused_flag == 0) → default-running arm
     *   else                       → paused arm (= transition arm body)
     */
    if (g_scene1_ingame_transition_flag != 0) {
        scene1_ingame_transition_arm_tick();
    } else if (g_scene1_ingame_skip_flag != 0) {
        /* skip — no sim call */
    } else if (g_scene1_ingame_paused_flag == 0) {
        scene1_ingame_default_arm_tick();
    } else {
        scene1_ingame_transition_arm_tick();
    }
}
