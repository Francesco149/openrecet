/*
 * test_scene1_sim.c — Cs1 wiring tests for scene1_ingame_tick + the
 * SCENE_STATE_INGAME arm of sim_step_a.
 *
 * The behavior these tests pin down:
 *
 *   1. `scene1_ingame_tick()` calls `scene1_particles_tick()`.
 *   2. `sim_step_a()` with `g_scene_state == SCENE_STATE_INGAME` runs
 *      the integrator.
 *   3. `sim_step_a()` with `g_scene_state == SCENE_STATE_TITLE` does
 *      NOT run the integrator (smoke check the switch doesn't fall
 *      through into INGAME by accident).
 *
 * Behavior is observed via a hand-injected type-0x92 particle (same
 * helper as `--show-pass-f-test`): after one tick the AGE field bumps
 * from 0 → 1 and ROT_X bumps by π/200 (≈ 0.015708).
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "input.h"
#include "scene.h"
#include "scene1_companion_ctrl.h"
#include "scene1_conversation_pose.h"
#include "scene1_particles_tick.h"
#include "scene1_records.h"
#include "scene1_records_c_tick.h"
#include "scene1_sim.h"
#include "scene1_player_ctrl.h"
#include "title_save_dialog.h"
#include "scene1_spawn.h"
#include "rng.h"
#include "sim.h"
#include "worker_load.h"
#include "scene1_intro_dialogue.h"

static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();
    g_scene1_records_c_count = 0;
    /* Revert C-tick hooks to defaults (other tests may have installed
     * captures; we don't want them firing here). */
    scene1_records_c_set_ground_query(NULL);
    scene1_records_c_set_raycast(NULL);
    scene1_records_c_set_commit_pickup(NULL);

    /* C8j.3 state-1 sub-dispatch flags — BSS-zero = default arm. */
    g_scene1_ingame_transition_flag = 0;
    g_scene1_ingame_skip_flag       = 0;
    g_scene1_ingame_paused_flag     = 0;

    sim_init();
    worker_load_reset();
    /* The opening-prologue dialogue (src/scene1_intro_dialogue.c) is armed by
     * scene_post_fade_init; sim_step_a ticks it on INGAME frames. A prior test
     * may have left it armed, so reset it here for a hermetic sim_step_a aging
     * count. (Its inter-script loading bracket is Win32-only — start_script is
     * a no-op in the host build — so it stays dormant here either way.) */
    scene1_intro_dialogue_reset();
    g_input_state[0].buttons = 0;
    g_input_state[1].buttons = 0;
}

/* Stage a STATE=2 (pickup-bob) table-C record at the given slot, with
 * AGE preset (so the trailing age++ advances visibly).  Tracks
 * g_scene1_records_c_count so the integrator's outer loop reaches the
 * slot. */
static void stage_c_pickup(int slot, int type, int age)
{
    int base = slot * SCENE1_RECORDS_C_STRIDE;
    float zero = 0.0f, one = 1.0f;
    int32_t bz, bo;
    memcpy(&bz, &zero, sizeof bz);
    memcpy(&bo, &one,  sizeof bo);
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_X]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_Y]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_Z]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_TYPE]    = type;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_AGE]     = age;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_SCALE]   = bo;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_STATE]   = 2;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_AUX]     = 0;
    if (slot >= g_scene1_records_c_count)
        g_scene1_records_c_count = slot + 1;
}

static int c_slot_get_i(int slot, int off)
{
    return g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
}

static int slot_read_i(int slot, int off)
{
    return g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
}

static float slot_read_f(int slot, int off)
{
    int32_t v = g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof v);
    return f;
}

int test_scene1_ingame_tick_runs_integrator(void)
{
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == 0x92);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE)  == 0);

    scene1_ingame_tick();

    /* Type-0x92 integrator bumps AGE by 1 and ROT_X by π/200 per tick. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);

    float rot_x = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    if (fabsf(rot_x - 0.015707964f) > 1e-6f)
        T_FAIL("rot.x = %f, expected ~0.015708 (π/200)", (double)rot_x);
    return 0;
}

int test_sim_step_a_state_ingame_runs_integrator(void)
{
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    sim_step_a();

    /* The INGAME switch arm should have called scene1_ingame_tick,
     * which calls scene1_particles_tick, which bumps AGE. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    return 0;
}

int test_sim_step_a_state_title_skips_integrator(void)
{
    reset_world();
    g_scene_state = SCENE_STATE_TITLE;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    sim_step_a();

    /* TITLE arm runs scene_title_sim_default; the integrator should
     * be untouched. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    return 0;
}

int test_sim_step_a_state_ingame_100_ticks(void)
{
    /* Drive 100 ticks and verify AGE accumulates + ROT_X reaches the
     * expected sum of 100 * π/200 = π/2 ≈ 1.5708.  This is the
     * smoke-test for "the integrator runs every frame", not just once. */
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < 100; i++) sim_step_a();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 100);

    float rot_x = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    float expected = 100.0f * 0.015707964f;
    if (fabsf(rot_x - expected) > 1e-4f)
        T_FAIL("rot.x = %f after 100 ticks, expected %f",
               (double)rot_x, (double)expected);
    return 0;
}

/* ─── Cs2: LAB_00453bed mass dispatch ────────────────────────────────── */

static int ticks_advance_age(int state)
{
    reset_world();
    g_scene_state = state;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    sim_step_a();
    return slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE);
}

int test_sim_step_a_state_2_runs_integrator(void)
{
    /* State 2 (cutscene) hits engine LAB_00453bed → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(2), 1);
    return 0;
}

int test_sim_step_a_state_3_runs_integrator(void)
{
    /* State 3 (dialog) hits engine LAB_00453bed → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(3), 1);
    return 0;
}

int test_sim_step_a_state_b_runs_integrator(void)
{
    /* State 0xb (the largest >=9 LAB_00453bed entry) → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(0xb), 1);
    return 0;
}

int test_sim_step_a_state_10_runs_integrator(void)
{
    /* State 0x10 (last ending arm) → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(0x10), 1);
    return 0;
}

int test_sim_step_a_state_4_skips_integrator(void)
{
    /* State 4 — engine block 21 goto LAB_00453cfb directly, no tick. */
    T_ASSERT_EQ_I(ticks_advance_age(4), 0);
    return 0;
}

int test_sim_step_a_state_5_skips_integrator(void)
{
    /* State 5 (worldmap) — engine calls FUN_0046c039 then LAB_00453cfb. */
    T_ASSERT_EQ_I(ticks_advance_age(5), 0);
    return 0;
}

int test_sim_step_a_state_a_skips_integrator(void)
{
    /* State 0xa — engine calls FUN_0047e711 then LAB_00453cfb. */
    T_ASSERT_EQ_I(ticks_advance_age(0xa), 0);
    return 0;
}

int test_sim_step_a_state_c_skips_integrator(void)
{
    /* State 0xc — engine falls into the (>=9 && !=0xb && (<0xd || >0x10))
     * exclusion in block 21; goto LAB_00453cfb directly. */
    T_ASSERT_EQ_I(ticks_advance_age(0xc), 0);
    return 0;
}

/* ─── C8j.3: state-1 sub-dispatch + default-arm wiring ──────────────── */

int test_scene1_ingame_default_arm_ticks_particles_and_c(void)
{
    /* All gate flags BSS-zero → scene1_ingame_tick routes through the
     * default-running arm, which must tick BOTH tables. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);  /* table A */
    stage_c_pickup(0, /*type*/ 2, /*age*/ 10);             /* table C */

    scene1_ingame_tick();

    /* Particle (table A) tick fired → AGE 0→1. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    /* Table C tick fired → pickup-bob AGE 10→11. */
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}

int test_scene1_ingame_default_arm_consumes_debug_overlay_rng(void)
{
    /* engine-quirks §95 (REVISED-AGAIN 2026-06-05): FUN_00442cef's tail
     * dev-overlay (442cef.c L421 / ret 0x443606) burns one raw LCG step
     * **UNCONDITIONALLY** — the call sits at LAB_004435f7, past every gate, so
     * retail consumes it once per render frame whether the player is idle or
     * moving.  A 2026-06-04 pass briefly movement-gated this on a confounded
     * house-idle measurement (the un-pinned bg-NPCs were desyncing the shared
     * LCG); the clean post-bg-NPC-pin drill shows retail consuming 0x443606
     * every idle frame.  See freeroam-rng-consumption.md Lead C.
     *
     * (a) the default arm consumes exactly ONE more step than the transition arm
     * (the overlay step), idle OR moving. */
    reset_world();                       /* player not posed → idle */
    unsigned long c0 = rng_call_count();
    scene1_ingame_transition_arm_tick();
    unsigned long trans = rng_call_count() - c0;

    reset_world();
    c0 = rng_call_count();
    scene1_ingame_default_arm_tick();
    unsigned long deflt_idle = rng_call_count() - c0;
    T_ASSERT_EQ_U(deflt_idle, trans + 1u);   /* overlay step fires even when idle */

    /* (b) the step itself, in isolation: exactly ONE LCG step, idle... */
    reset_world();
    T_ASSERT_EQ_I(player_ctrl_is_moving(), 0);
    c0 = rng_call_count();
    scene1_debug_overlay_consume_rng();
    T_ASSERT_EQ_U(rng_call_count() - c0, 1u);   /* idle → still 1 step */

    /* ...AND when moving (unconditional — movement is irrelevant). */
    title_save_dialog_reset();           /* clear the FUN_0048670f save-gate so the tick runs */
    player_ctrl_pose_house_standing(0);
    g_input_state[0].buttons = 0x0008u;
    scene1_player_ctrl_tick();           /* sets s_player_moving from the d-pad */
    T_ASSERT_EQ_I(player_ctrl_is_moving(), 1);
    c0 = rng_call_count();
    scene1_debug_overlay_consume_rng();
    T_ASSERT_EQ_U(rng_call_count() - c0, 1u);   /* moving → 1 step */

    g_input_state[0].buttons = 0;
    scene1_player_ctrl_tick();
    return 0;
}

int test_scene1_ingame_transition_flag_picks_transition_arm(void)
{
    /* Transition flag != 0 → routes to transition arm, which ticks
     * particles but NOT table C. */
    reset_world();
    g_scene1_ingame_transition_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_skip_flag_skips_all_ticks(void)
{
    /* Skip flag != 0 (transition flag stays 0) → no sim call at all. */
    reset_world();
    g_scene1_ingame_skip_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_paused_flag_picks_transition_arm(void)
{
    /* Paused flag != 0 (transition+skip both 0) → paused arm body,
     * which is the transition-arm body in the engine.  Particles tick,
     * table C does not. */
    reset_world();
    g_scene1_ingame_paused_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_transition_arm_tick_direct(void)
{
    /* Direct call to transition arm — ticks particles, not C. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_transition_arm_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_default_arm_tick_direct(void)
{
    /* Direct call to default arm — ticks BOTH particles and C. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_default_arm_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}

int test_scene1_ingame_dialogue_busy_routes_to_event_arm(void)
{
    /* Retail dispatches every dialogue frame to the EVENT arm (FUN_004536cb:
     * DAT_0438b1c8 != 0 → FUN_004427d3, never FUN_00442cef — verified on the
     * item-display-2 retail call-trace).  The port's live b1c8 writer is the
     * dialogue runtime: armed/loading/active ⇒ busy ⇒ event arm.  Observable:
     * table C (a default-arm-only tick) freezes while a dialogue is armed, and
     * resumes the frame it ends. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_intro_dialogue_start_single(1, 5);     /* D_TUT_LOAD → busy() */
    T_ASSERT_EQ_I(scene1_intro_dialogue_busy(), 1);

    scene1_ingame_tick();

    /* Event arm ran: particles ticked, table C did NOT. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);

    /* Dialogue ends → busy drops → the default arm resumes. */
    scene1_intro_dialogue_skip_to_end();
    T_ASSERT_EQ_I(scene1_intro_dialogue_busy(), 0);

    scene1_ingame_tick();
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);

    /* Teardown: the event-arm pose tick latched the conversation pose on the
     * (suite-inherited) live actor; release it so later tests' companion anim
     * law isn't gated off. */
    scene1_conversation_pose_reset();
    return 0;
}

int test_scene1_event_arm_advances_db054_with_live_actor(void)
{
    /* The FUN_0048407f tail increments db054 UNCONDITIONALLY (all.c:84658) —
     * retail's db054 advanced through every item-display-2 dialogue frame while
     * house_update never ran.  With a live player actor and a busy dialogue,
     * each ingame tick must advance the db054 clock by exactly 1. */
    reset_world();
    title_save_dialog_reset();
    player_ctrl_pose_house_standing(0);           /* live actor 0 */
    scene1_intro_dialogue_start_single(1, 5);     /* busy → event arm */

    int d0 = scene1_companion_db054();
    scene1_ingame_tick();
    scene1_ingame_tick();
    T_ASSERT_EQ_I(scene1_companion_db054(), d0 + 2);

    scene1_intro_dialogue_skip_to_end();
    scene1_conversation_pose_reset();   /* release the latched pose (teardown) */
    return 0;
}

int test_scene1_event_actor_tail_inert_without_actor(void)
{
    /* The tail is guarded on a live player actor (the engine's event driver is
     * gated on the scene's actor context): a bare transition/paused frame with
     * no actors must neither warm the bg-NPC pump (RNG!) nor tick db054.
     * pose_house_standing(-1) clears every actor slot (char[0] = -1) — earlier
     * tests in this file leave actor 0 live, and reset_world does not despawn. */
    reset_world();
    player_ctrl_pose_house_standing(-1);
    int d0 = scene1_companion_db054();
    unsigned long c0 = rng_call_count();

    scene1_ingame_transition_arm_tick();

    T_ASSERT_EQ_I(scene1_companion_db054(), d0);
    T_ASSERT_EQ_U(rng_call_count() - c0, 0u);
    return 0;
}

int test_scene1_ingame_transition_flag_takes_precedence_over_skip(void)
{
    /* Engine order: transition checked first, then skip.  With both
     * set, transition wins → particle tick fires. */
    reset_world();
    g_scene1_ingame_transition_flag = 1;
    g_scene1_ingame_skip_flag       = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    return 0;
}

int test_sim_step_a_state_ingame_default_arm_also_ticks_c(void)
{
    /* End-to-end: sim_step_a(SCENE_STATE_INGAME) with default flags
     * routes through scene1_ingame_tick → default arm → C tick fires. */
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    stage_c_pickup(0, 2, 10);

    sim_step_a();

    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}
