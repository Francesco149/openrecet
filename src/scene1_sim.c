/*
 * scene1_sim.c — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004536cb @ 0x4536cb state-1 sub-dispatch
 * (L50555-50568) — picks one of three arms based on flag globals,
 * then runs that arm's per-tick body.
 *
 * FUN_004427d3 (30 B transition/paused/EVENT wrapper — the arm retail runs on
 * every dialogue frame, see scene1_ingame_tick) call list:
 *
 *   FUN_0048407f  (  795 B) — scene1_conversation_pose_tick +
 *                             scene1_event_actor_tail_tick (bg-NPC pump,
 *                             companion follow + wing, db054++)
 *   FUN_00430c00  (  109 B) — stub (town-NPC anim pump; empty in HOUSE)
 *   FUN_0043ae20  (25750 B) — scene1_records_b_tick (skeleton)
 *   FUN_0043a5d9  ( 1429 B) — stub (slide-panel updater; no RNG)
 *   FUN_0040fb3a            — scene1_particles_tick()
 *   FUN_004426a7  (  300 B) — stub (cinematic camera ramp; inert, no RNG)
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
#include "scene1_intro_dialogue.h"
#include "scene1_player_ctrl.h"
#include "scene1_records_b_tick.h"
#include "scene1_records_c_tick.h"
#include "scene1_tutorial_dispatch.h"
#include "rng.h"
#include "call_trace.h"

/* Engine flag stand-ins.  All BSS-zero default → HOUSE selects the
 * default-running arm.  See header for engine-globals mapping.
 *
 * DAT_0438b1c8 (the "busy/paused" scene gate) has TWO surfaces in the port:
 * this explicit flag (for writers the engine has that we haven't ported —
 * none today) and the dialogue runtime's lifecycle, scene1_intro_dialogue_busy()
 * (retail's live writers: FUN_0044bd0d arms a dialogue with b1c8=2, the
 * LAB_00452aab load worker holds 1, the script end clears 0).  The dispatch
 * below ORs them — they are the SAME engine global. */
int g_scene1_ingame_transition_flag = 0;  /* DAT_0438b1d0 */
int g_scene1_ingame_skip_flag       = 0;  /* DAT_0438b1d8 */
int g_scene1_ingame_paused_flag     = 0;  /* DAT_0438b1c8 (explicit-writer mirror) */

void scene1_ingame_transition_arm_tick(void)
{
    /* E.2 probe — FUN_004427d3 @ 0x4427d3.  STUB→partial: 4 of the engine's 6
     * calls run; FUN_00430c00 / FUN_0043a5d9 / FUN_004426a7 stay stubbed (see
     * below) so the row keeps its ≈ marker.
     *
     * This is the arm retail dispatches EVERY dialogue/event frame to
     * (FUN_004536cb routes b1c8 != 0 here INSTEAD of FUN_00442cef — verified on
     * item-display-2: during the two tutorial dialogues the retail call-trace
     * has 0x4427d3 + 0x48407f + 0x46f621 once per frame and NO 0x442cef /
     * 0x48670f rows).  Until 2026-06-09 the port kept running the DEFAULT arm
     * during dialogues, so the dev-overlay LCG step (+1/frame) and the
     * 目玉-sparkle emitter (+6 per 8 frames) kept consuming shared LCG that
     * retail does not — the +3245-call rngcalls DESYNC across the dialogue
     * window (the "bg-NPC desync after dialogue start" of
     * findings/shop-display-menu-RE.md follow-up #2). */
    CALL_TRACE_ENTER_STUB(0x4427d3u);

    /* FUN_004427d3 L8 — FUN_0048407f, the event-actor driver: the pose/release
     * branch (lean tick, shared with the default arm's release frame) + the
     * tail the lean tick deliberately omits (bg-NPC pump, companion
     * spring-follow + wing emit, the unconditional db054++). */
    scene1_conversation_pose_tick();
    scene1_event_actor_tail_tick();

    /* FUN_004427d3 L9 — FUN_00430c00: town-NPC anim pump (iterates the
     * 0x2e9-dword townfolk records; chr_anim_tick per live slot, then
     * FUN_0042ea35).  PORT-DEBT(focused, FUN_00430c00): no town NPCs exist in
     * the HOUSE scenes we verify; the loop body is a no-op there and consumes
     * no RNG. */

    /* FUN_004427d3 L10 — FUN_0043ae20 (table B tick; the same skeleton the
     * default arm runs). */
    scene1_records_b_tick();

    /* FUN_004427d3 L11 — FUN_0043a5d9: slide-panel/banner offset updater
     * (0x2a0-stride array at DAT_0695ef78, ±0.1 ramps, clamp −13).
     * PORT-DEBT(focused, FUN_0043a5d9): no consumer wired; no RNG. */

    /* FUN_004427d3 L12 — FUN_0040fb3a (table A particle integrator). */
    scene1_particles_tick();

    /* FUN_004427d3 L13 — FUN_004426a7: cinematic camera ramp, gated on
     * DAT_06a46fa0 > 0 (a cutscene counter, 0 in our windows); sin/cos only,
     * no RNG.  PORT-DEBT(focused, FUN_004426a7). */
}

void scene1_ingame_default_arm_tick(void)
{
    /* E.2 probe — FUN_00442cef @ 0x442cef. */
    CALL_TRACE_ENTER(0x442cefu);

    /* DAY-2 actor re-place one-shot (retail FUN_0048526d scene-entry re-seat).
     * FIRST, before the conversation-pose tick derives the face-each-other
     * octants and before render — the default arm runs only on non-dialogue
     * free-roam frames, so this fires on the first post-iv2_5 free-roam frame
     * (the beat's first counting frame, retail day2 @15470).  Inert (pending
     * flag 0) on every other frame. */
    scene1_tutorial_dispatch_consume_day2_replace();

    /* cc04 (the display-stand menu sub-state) SAMPLED AT DISPATCH: the engine's
     * db054 increment sits at the FUN_0048b850 tail, reached only when the
     * frame's cc08/cc04 dispatch took a non-menu path.  On the menu-CLOSE frame
     * the menu arm clears cc04 MID-frame and b850 never runs → retail does NOT
     * advance db054 that frame; the port's fallback below used to re-read cc04
     * after the clear and advanced anyway — +1 db054 per pause vs retail, which
     * skewed every db054-aligned diff after the first menu (the phantom
     * house_update.px/py/dust "DRIFT @202" on item-display-2: the walk values
     * are bit-identical under a 1-frame shift). */
    int cc04_at_dispatch = player_ctrl_cc04();

    /* cc08 SAMPLED AT DISPATCH — the cc04 story above, for the cs mode boundary
     * (RE §21.23).  db054 advances ONLY when the engine reaches the FUN_0048b850
     * tail (the free-roam MOVE), and retail's FUN_0048670f skips that move on
     * BOTH cc08==4 boundary frames — so its db054 stays FROZEN across the whole
     * cc08==4 span AND its two edge frames:
     *   - the 4→1 LEAVE (frame 631): retail's `if (cc08==4){…master tick sets
     *     cc08=1 when the b520 dissolve completes…} else {…walk→FUN_0048b850…}`
     *     is an if/else on the FRAME-START cc08 — the leave frame takes the
     *     cc08==4 branch and never reaches the walk/db054 path, even though cc08
     *     flipped to 1 mid-frame.  ⇒ gate on cc08_at_dispatch (the dispatch value
     *     was still 4).
     *   - the 1→4 ENTRY (frame 304): the walk arm (cc08==1 branch) detects the
     *     customer-service trigger, sets cc08=4, and `goto`s the tail SKIPPING
     *     FUN_0048b850 (all.c:87485-89) ⇒ no db054++.  ⇒ gate on the LIVE cc08
     *     (now 4), since the dispatch value was 1.
     * The port splits that single if/else across scene1_player_ctrl_tick (the
     * cc08==4 arm / freeroam arm flip cc08) and THIS fallback.  Gating on the
     * LIVE cc08 alone (the pre-§21.23 code) caught the ENTRY but re-read the
     * flipped-to-1 LEAVE value and advanced db054 a frame early — the +1 drift
     * from CONV_POSE_START (632); gating on the DISPATCH value alone catches the
     * leave but wrongly advances on the entry (dispatch still 1).  Require BOTH
     * != 4 = "no cc08==4 involvement this frame" = FUN_0048b850 actually ran.
     * (The +1 drift cascaded into the late 目玉 sparkle / chibi walk phase via
     * the shared LCG — viewer notes #20/#22.) */
    int cc08_at_dispatch = player_ctrl_cc08();

    /* FUN_0048407f conversation branch (the event-actor tick): hold the iv1_2
     * face-to-face pose on the freeroam chibis (Recette look-up/blink anim 6,
     * Tear talk anim 4) and advance their sprite anim.  Runs FIRST, before the
     * freeroam controllers, mirroring the branch's position ahead of the
     * per-actor anim step + the spring-follow.  Inert outside the conversation;
     * the companion controller reads scene1_conversation_pose_active() to yield
     * its anim/facing while the pose is held.
     *
     * NOTE the engine's FUN_00442cef does NOT call FUN_0048407f — that driver
     * belongs to the EVENT arm (FUN_004427d3).  This default-arm call is the
     * port's release-frame compensation: our dialogue runtime drops busy() and
     * posing() on the SAME frame the script completes, so the release branch
     * (anim 6→0) must run from the first post-busy default frame.  Inert on
     * every other default frame (the !posing && !s_pose_active early-out). */
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
     * dust); this fallback ticks+advances it on every OTHER frame — the
     * display-menu / pre-move paths the engine reaches through FUN_0048b850's
     * other callers but the port's stubbed arms do not.  (Dialogue frames no
     * longer come through here at all — scene1_ingame_tick routes busy frames
     * to the event arm, whose tail owns the companion + db054.)  Gated on the
     * latch so the companion ticks exactly once per frame (engine-quirks §114);
     * the db054 advance additionally requires the frame to have DISPATCHED
     * outside the menu (cc04_at_dispatch — see above; advance_phase itself
     * still re-checks the live cc04 for the menu-OPEN frame, set mid-frame). */
    if (!player_ctrl_companion_ticked()) {
        scene1_companion_ctrl_tick();
        /* db054 advances ONLY in free-roam: the engine bumps it at the
         * FUN_0048b850 tail, which does NOT run in cc08==4 (customer service) —
         * so retail's db054 FREEZES there (verified: frozen at 156).  The
         * companion wing-glow sparkle gate is `db054 % 4 == 0`, so a frozen db054
         * (156 % 4 == 0) emits the type-0x1f particle EVERY frame, vs every 4th
         * while it keeps incrementing — this is the cc08==4 ambient-particle rng
         * the port was under-drawing (~5.5/f vs retail ~10/f).  Freeze it here to
         * match (RE §8.8 — the resolved §8.5 rng-rate gap). */
        if (cc04_at_dispatch == 0 && cc08_at_dispatch != 4 &&
            player_ctrl_cc08() != 4)
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
    /* RE §21.28 cc08 boundary markers live for exactly ONE frame — clear at
     * the frame top, before any arm can set or read them (the player tick
     * can't own this: it does not run on event frames). */
    player_ctrl_cc08_markers_frame_clear();

    /* Engine FUN_004536cb L50555-50568 — state-1 sub-dispatch:
     *
     *   if (transition_flag != 0)  → transition arm
     *   else if (skip_flag != 0)   → skip (no sim call)
     *   else if (paused_flag == 0) → default-running arm
     *   else                       → paused arm (= transition arm body)
     *
     * paused_flag is DAT_0438b1c8, the dialogue/event "busy" gate: retail sets
     * it 2 when FUN_0044bd0d arms a dialogue, 1 through the load worker, and
     * clears it when the script ends — so EVERY dialogue frame runs the
     * event/paused arm, never FUN_00442cef (verified on the item-display-2
     * retail call-trace: zero 0x442cef/0x48670f rows during both tutorial
     * dialogues, 0x4427d3 once per frame).  The port's live writer of that
     * lifecycle is the dialogue runtime — read its busy() alongside the
     * explicit flag (same engine global, see the definitions above). */
    if (g_scene1_ingame_transition_flag != 0) {
        scene1_ingame_transition_arm_tick();
    } else if (g_scene1_ingame_skip_flag != 0) {
        /* skip — no sim call */
    } else if (g_scene1_ingame_paused_flag == 0 &&
               !scene1_intro_dialogue_busy()) {
        scene1_ingame_default_arm_tick();
    } else {
        scene1_ingame_transition_arm_tick();
    }
}
