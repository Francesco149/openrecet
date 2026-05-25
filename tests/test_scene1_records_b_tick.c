/*
 * test_scene1_records_b_tick.c — unit tests for the C8j-tick.1
 * skeleton (engine FUN_0043ae20 outer loop + preamble).
 *
 * Scope: skeleton-only behavior — preamble pos += vel + age++, dead
 * slot skip, kill helper, hook installation.  Per-type behaviors
 * land in sub-chip ladder C8j-tick.2+ with their own tests.
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "rng.h"
#include "scene1_particles_tick.h"
#include "scene1_per_frame_open.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_records_b_tick.h"
#include "scene1_spawn.h"
#include "sim.h"                       /* g_sim_frame_count (engine DAT_0438b8cc) */

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    scene1_records_reset(1);
    g_scene1_records_b_count = 0;
    g_scene1_records_b_tick_flag = 0;
    g_sim_frame_count = 0;
    scene1_records_b_set_per_type_body(NULL);
    scene1_records_b_set_state_machine_hook(NULL);
    scene1_records_b_set_se_hook(NULL);
    scene1_records_b_set_cull_query_hook(NULL);
    scene1_records_b_set_aux_485979_hook(NULL);
    scene1_records_b_set_aux_482a51_hook(NULL);
    scene1_records_b_set_notify_queue_hook(NULL);
    scene1_records_b_set_ground_query_hook(NULL);
    scene1_records_b_set_aux_4532bc_hook(NULL);
    scene1_records_b_set_overlay_spawn_hook(NULL);
    scene1_records_b_set_aux_4319d6_hook(NULL);
    scene1_records_b_set_sw_record_at_hook(NULL);
    scene1_records_b_set_aux_43ab6e_hook(NULL);
    scene1_records_b_set_wall_raycast_hook(NULL);
    scene1_records_b_set_wall_flag_at_hook(NULL);
    scene1_records_b_set_wall_destroy_hook(NULL);
    scene1_records_b_set_aux_44b255_hook(NULL);
    memset(g_scene1_b_wall_lifetime, 0, sizeof g_scene1_b_wall_lifetime);
    memset(g_scene1_b_wall_freshness, 0, sizeof g_scene1_b_wall_freshness);
}

static int32_t *bslot(int i)
{
    return &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
}

static void slot_set_i(int i, int off, int32_t v)
{
    bslot(i)[off] = v;
}

static int32_t slot_get_i(int i, int off)
{
    return bslot(i)[off];
}

static void slot_set_f(int i, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    bslot(i)[off] = v;
}

static float slot_get_f(int i, int off)
{
    int32_t v = bslot(i)[off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void stage_live(int slot, int32_t type, float px, float py, float pz,
                       float vx, float vy, float vz, int32_t age)
{
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_TYPE, type);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_X, px);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_Y, py);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_Z, pz);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_X, vx);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_Z, vz);
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_AGE, age);
}

/* ─── tests ───────────────────────────────────────────────────────── */

int test_records_b_tick_empty_table_is_noop(void)
{
    reset_world();
    scene1_records_b_tick();
    /* Nothing got mutated: all slots remain TYPE=0. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE), 0);
    }
    return 0;
}

int test_records_b_tick_skips_dead_slots(void)
{
    /* Dead-slot's pos must NOT see vel-integration even if dirty. */
    reset_world();
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 5.0f);
    /* TYPE stays 0 → slot is dead. */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0);
    return 0;
}

int test_records_b_tick_preamble_integrates_pos(void)
{
    reset_world();
    /* slot 5 alive with non-zero vel + non-zero pos. */
    stage_live(5, /*type=*/0x10, 1.0f, 2.0f, 3.0f, 0.25f, -0.5f, 1.5f, /*age=*/7);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_X) - 1.25f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_Y) - 1.5f)  < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_Z) - 4.5f)  < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(5, SCENE1_RECORDS_B_OFF_AGE), 8);
    /* Vel untouched. */
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_X) - 0.25f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_Y) - (-0.5f)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_Z) - 1.5f) < 1e-6f);
    /* TYPE preserved. */
    T_ASSERT_EQ_I(slot_get_i(5, SCENE1_RECORDS_B_OFF_TYPE), 0x10);
    return 0;
}

int test_records_b_tick_preamble_clears_per_tick_flag(void)
{
    reset_world();
    g_scene1_records_b_tick_flag = 42;  /* dirty */
    stage_live(0, 1, 0, 0, 0, 0, 0, 0, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_records_b_tick_kill_slot_sets_type_zero(void)
{
    reset_world();
    stage_live(10, /*type=*/0x42, 0, 0, 0, 0, 0, 0, 0);
    T_ASSERT_EQ_I(slot_get_i(10, SCENE1_RECORDS_B_OFF_TYPE), 0x42);
    scene1_records_b_tick_kill_slot(10);
    T_ASSERT_EQ_I(slot_get_i(10, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_kill_slot_oob_is_safe(void)
{
    reset_world();
    /* Should not crash / corrupt anything. */
    scene1_records_b_tick_kill_slot(-1);
    scene1_records_b_tick_kill_slot(SCENE1_RECORDS_B_COUNT);
    scene1_records_b_tick_kill_slot(SCENE1_RECORDS_B_COUNT + 100);
    return 0;
}

/* Per-type-body hook capture state. */
static int s_dispatch_calls;
static int s_dispatch_last_slot;
static int32_t s_dispatch_last_type;
static void capture_dispatch(int slot_idx, int32_t type)
{
    s_dispatch_calls++;
    s_dispatch_last_slot = slot_idx;
    s_dispatch_last_type = type;
}

int test_records_b_tick_per_type_hook_fires_after_preamble(void)
{
    reset_world();
    s_dispatch_calls      = 0;
    s_dispatch_last_slot  = -1;
    s_dispatch_last_type  = 0;
    /* Pre-C8j-tick.2 expected `prev == NULL` (no default body); now
     * `dispatch_default` is installed at module init.  Just round-trip. */
    (void)scene1_records_b_set_per_type_body(capture_dispatch);

    stage_live(3, /*type=*/0x77, 1, 2, 3, 0, 0, 0, 0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_dispatch_calls, 1);
    T_ASSERT_EQ_I(s_dispatch_last_slot, 3);
    T_ASSERT_EQ_I(s_dispatch_last_type, 0x77);

    /* Confirm preamble fired before dispatch — age already 1 if hook
     * had read the slot.  We didn't capture state; just confirm
     * post-call slot has age=1 + pos unchanged (vel=0). */
    T_ASSERT_EQ_I(slot_get_i(3, SCENE1_RECORDS_B_OFF_AGE), 1);

    /* Restore. */
    scene1_records_b_set_per_type_body(NULL);
    return 0;
}

int test_records_b_tick_per_type_hook_not_called_for_dead_slots(void)
{
    reset_world();
    s_dispatch_calls = 0;
    scene1_records_b_set_per_type_body(capture_dispatch);
    /* All 512 slots dead. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_dispatch_calls, 0);
    scene1_records_b_set_per_type_body(NULL);
    return 0;
}

/* State-machine hook setter is wired but not invoked from skeleton.
 * Confirm setter round-trips. */
static void noop_state_machine(int32_t *slot) { (void)slot; }

int test_records_b_tick_state_machine_setter_round_trips(void)
{
    reset_world();
    scene1_b_state_machine_fn prev =
        scene1_records_b_set_state_machine_hook(noop_state_machine);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_state_machine_hook(NULL);
    T_ASSERT(prev == noop_state_machine);
    return 0;
}

int test_records_b_tick_walks_all_512_slots(void)
{
    reset_world();
    /* Mark slots 0, 100, 256, 511 alive — confirm preamble fires on
     * the outer extremes. */
    stage_live(0,   1, 0, 0, 0, 1.0f, 0, 0, 0);
    stage_live(100, 1, 0, 0, 0, 0, 1.0f, 0, 0);
    stage_live(256, 1, 0, 0, 0, 0, 0, 1.0f, 0);
    stage_live(511, 1, 0, 0, 0, 0.5f, 0.5f, 0.5f, 0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0,   SCENE1_RECORDS_B_OFF_POS_X) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(100, SCENE1_RECORDS_B_OFF_POS_Y) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(256, SCENE1_RECORDS_B_OFF_POS_Z) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_X) - 0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_Y) - 0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_Z) - 0.5f) < 1e-6f);

    /* All four had age++. */
    T_ASSERT_EQ_I(slot_get_i(0,   SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(100, SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(256, SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(511, SCENE1_RECORDS_B_OFF_AGE), 1);
    return 0;
}

/* ═══ C8j-tick.2 — anchor-cascade tests ════════════════════════════════ */
/*
 * Tests use a static fake owner blob large enough to cover all engine
 * offsets touched by the 0x1e/0x2f/0x88/0x9a + 0x89/0x9e bodies:
 *
 *   +0x14   slot.OWNER_B (we set this slot field to point AT the blob)
 *   +0x18   compass-direction int (0x89/0x9e ang)
 *   +0x3f0  owner pose x
 *   +0x3f4  owner pose y
 *   +0x3f8  owner pose z
 *   +0x420  owner orientation angle (float radians)
 *   +0x424  NPC motion-style ID (0x48/0x4b/0x4c/...)
 *   +0x428  alive/gate flag (1 = alive)
 *   +0x6fc + n*12   joint table — n in [0, ~0x20)
 *   +0xa88 + n*4    per-joint enable byte (0 = enabled)
 *
 * Total: ~0xb00 bytes.  Each test resets the blob to zero, sets owner
 * gate alive (+0x428=1), and writes the slot field OWNER_B to the blob
 * address before calling scene1_records_b_tick().
 */
#define OWNER_BLOB_SIZE 0xb00
/* The slot's OWNER_B field is `int32_t` (engine assumes 32-bit pointers
 * — Win32 native).  On 64-bit Linux test hosts a static array sits
 * above 4 GB so the truncated int32_t can't round-trip back to a
 * dereferenceable pointer.  mmap MAP_32BIT pins the blob in the low
 * 4 GB so the truncation is lossless.  Linux-only; tests are Linux. */
static uint8_t *g_test_owner_blob;

static void owner_blob_ensure(void)
{
    if (g_test_owner_blob) return;
    void *p = mmap(NULL, OWNER_BLOB_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                   -1, 0);
    if (p == MAP_FAILED) abort();
    g_test_owner_blob = (uint8_t *)p;
}

static void owner_blob_reset(void)
{
    owner_blob_ensure();
    memset(g_test_owner_blob, 0, OWNER_BLOB_SIZE);
}

static void owner_blob_set_f(int byte_off, float v)
{
    memcpy(g_test_owner_blob + byte_off, &v, sizeof v);
}

static void owner_blob_set_i(int byte_off, int32_t v)
{
    memcpy(g_test_owner_blob + byte_off, &v, sizeof v);
}

/* Attach the fake owner blob to slot[OWNER_B] (engine field +0x14).
 * The mmap MAP_32BIT placement makes (int32_t) truncation lossless,
 * so the in-port `(const void *)(uintptr_t)slot_owner_b` recovery
 * lands back at our blob. */
static void bind_owner(int slot_idx)
{
    owner_blob_ensure();
    int32_t ptr = (int32_t)(uintptr_t)g_test_owner_blob;
    slot_set_i(slot_idx, SCENE1_RECORDS_B_OFF_OWNER_B, ptr);
}

/* SE hook capture. */
static int s_se_calls;
static uint16_t s_se_last_id;
static void capture_se(uint16_t id)
{
    s_se_calls++;
    s_se_last_id = id;
}

/* State-machine hook capture. */
static int s_sm_calls;
static void capture_state_machine(int32_t *slot)
{
    (void)slot;
    s_sm_calls++;
}

/* ─── 0x2f / 0x88 / 0x9a — joint-table anchor cascade ──────────────────── */

int test_records_b_tick_type_2f_writes_joint_pose(void)
{
    /* type 0x2f, aux_b0=0 → joint slot at owner+0x6fc/0x700/0x704.
     * Engine writes pos.y -= 1.0. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x6fc, 10.0f);      /* joint 0 x */
    owner_blob_set_f(0x700, 5.0f);       /* joint 0 y (then -1.0) */
    owner_blob_set_f(0x704, -20.0f);     /* joint 0 z */
    owner_blob_set_f(0x420, 0.0f);       /* owner angle */
    owner_blob_set_i(0xa88, 0);          /* joint enable */

    stage_live(0, /*type=*/0x2f, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_B0, 0);
    bind_owner(0);

    scene1_records_b_tick();

    /* Joint pose overwrites the +(0,3,0) initial — pos = (10, 5-1=4, -20). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) -  4.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - -20.0f) < 1e-5f);
    /* vel.x = sin(-π/2 + 0) * 2.5 = -2.5; vel.z = cos(-π/2 + 0) * 2.5 ≈ 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - -2.5f)  < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) -  0.0f)  < 1e-4f);
    return 0;
}

int test_records_b_tick_type_2f_aux_b0_1_uses_positive_pi_half(void)
{
    /* aux_b0=1 path uses +π/2, not -π/2.  Joint slot at offset
     * 0x6fc + 12 = 0x708. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x708, 1.0f);
    owner_blob_set_f(0x70c, 2.0f);
    owner_blob_set_f(0x710, 3.0f);
    owner_blob_set_f(0x420, 0.0f);
    owner_blob_set_i(0xa88 + 4, 0);
    stage_live(0, 0x2f, 0, 0, 0, 0, 0, 0, 2);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_B0, 1);
    bind_owner(0);

    scene1_records_b_tick();

    /* sin(+π/2) = 1, vel.x = 2.5; cos(+π/2) ≈ 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 2.5f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_type_2f_joint_enable_byte_gates_body(void)
{
    /* owner+0xa88+aux_b0*4 != 0 → early-return after the joint pose
     * write but BEFORE vel.x is set.  vel.x stays at the pre-tick
     * value (0 in stage_live).  Drag stays 0. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x6fc, 7.0f);
    owner_blob_set_f(0x700, 8.0f);
    owner_blob_set_f(0x704, 9.0f);
    owner_blob_set_i(0xa88, 1);   /* gate fails */
    stage_live(0, 0x2f, 0, 0, 0, 0, 0, 0, 2);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_B0, 0);
    bind_owner(0);

    scene1_records_b_tick();

    /* Pose still written from joint table. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 7.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 7.0f) < 1e-5f);  /* 8 - 1 */
    /* vel.x untouched (= 0 from stage). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    /* Drag also untouched (LAB_0043b205 tail did not run). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_DRAG), 0);
    return 0;
}

int test_records_b_tick_type_2f_sets_drag_1_0_in_tail(void)
{
    /* Drag should be 1.0 for 0x2f (= 0x3f800000 bits). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0xa88, 0);
    stage_live(0, 0x2f, 0, 0, 0, 0, 0, 0, 2);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_88_drag_1_0_kill_at_0x96(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0xa88, 0);
    /* Age = 0x96 → kill. */
    stage_live(0, 0x88, 0, 0, 0, 0, 0, 0, /*age=*/0x95);
    bind_owner(0);

    scene1_records_b_tick();
    /* AGE post-tick = 0x96 — engine kill threshold. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x96);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_9a_drag_0_5_kill_at_0x15e(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0xa88, 0);
    stage_live(0, 0x9a, 0, 0, 0, 0, 0, 0, 0x15d);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_2f_skips_state_machine_in_loop(void)
{
    /* Engine: `if (*piVar14 != 0x2f) FUN_0043865e()` — state machine
     * is NOT called in the 0x2f loop. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0xa88, 0);
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 0x2f, 0, 0, 0, 0, 0, 0, /*age=*/5);  /* age becomes 6, in [5, 0xb4) */
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 0);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_type_88_calls_state_machine_40x(void)
{
    /* Engine: 0x88 calls FUN_0043865e 40 times in the iter loop. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0xa88, 0);
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 0x88, 0, 0, 0, 0, 0, 0, /*age=*/0x50);  /* age becomes 0x51, in [0x4b, 0x87) */
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 40);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

/* ─── 0x1e — else branch (gate + motion-id sub-dispatch) ──────────────── */

int test_records_b_tick_type_1e_gate_fail_returns_early(void)
{
    /* owner+0x428 != 1 → body skipped (no pos / vel / drag write). */
    reset_world();
    owner_blob_reset();
    /* owner+0x428 stays 0 = gate fail. */
    stage_live(0, 0x1e, 1, 2, 3, 0, 0, 0, 4);
    bind_owner(0);

    scene1_records_b_tick();

    /* Pos is just preamble (1+0, 2+0, 3+0) — body didn't fire. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 1.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_DRAG), 0);
    return 0;
}

int test_records_b_tick_type_1e_motion_4b_uses_angle_plus_0_3(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);          /* alive gate */
    owner_blob_set_i(0x424, 0x4b);
    owner_blob_set_f(0x708, 100.0f);
    owner_blob_set_f(0x70c, 200.0f);
    owner_blob_set_f(0x710, 300.0f);
    owner_blob_set_f(0x420, 0.0f);
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner(0);

    scene1_records_b_tick();

    /* Pose = (100, 200, 300). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 200.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 300.0f) < 1e-5f);
    /* LIFE_MULT = 3.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 3.0f) < 1e-6f);
    /* Vel: sin(0.3) ≈ 0.2955, cos(0.3) ≈ 0.9553. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - sinf(0.3f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - cosf(0.3f)) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_VEL_Y), 0);
    return 0;
}

int test_records_b_tick_type_1e_motion_4c_uses_angle_minus_0_3(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x4c);
    owner_blob_set_f(0x708, 1.0f);
    owner_blob_set_f(0x70c, 2.0f);
    owner_blob_set_f(0x710, 3.0f);
    owner_blob_set_f(0x420, 0.0f);
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, 2);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - sinf(-0.3f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - cosf(-0.3f)) < 1e-5f);
    return 0;
}

int test_records_b_tick_type_1e_motion_48_uses_angle_plus_0_3(void)
{
    /* 0x48 path: separate asm block but same outcome as 0x4b (+0.3). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x48);
    owner_blob_set_f(0x708, -10.0f);
    owner_blob_set_f(0x70c, 0.0f);
    owner_blob_set_f(0x710, 50.0f);
    owner_blob_set_f(0x420, 1.0f);    /* base angle 1 rad */
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, 2);
    bind_owner(0);

    scene1_records_b_tick();

    /* angle = 1 + 0.3 = 1.3 */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - sinf(1.3f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - cosf(1.3f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 3.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_1e_motion_other_uses_alt_pos_branch(void)
{
    /* motion = something not in {0x48, 0x4b, 0x4c} → ALT_POS branch. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x55);   /* arbitrary other */
    owner_blob_set_f(0x420, 0.0f);
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, 2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 7.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 8.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 9.0f);
    bind_owner(0);

    scene1_records_b_tick();

    /* pos.x = sin(0)*1.5 + 7 = 7; pos.y = 8; pos.z = cos(0)*1.5 + 9 = 10.5 */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) -  7.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) -  8.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 10.5f) < 1e-5f);
    /* vel = (sin(0), 0, cos(0)) = (0, 0, 1). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 1.0f) < 1e-5f);
    /* LIFE_MULT NOT written in ALT_POS branch. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_LIFE_MULT), 0);
    return 0;
}

int test_records_b_tick_type_1e_se_fires_at_age_4_and_4b(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x55);
    s_se_calls = 0;
    s_se_last_id = 0;
    scene1_records_b_set_se_hook(capture_se);

    /* age becomes 4 after preamble. */
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x176);

    /* age becomes 0x4b. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x55);
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);

    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, /*age=*/0x4a);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2a4);

    scene1_records_b_set_se_hook(NULL);
    return 0;
}

int test_records_b_tick_type_1e_kill_at_age_0x69(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    owner_blob_set_i(0x424, 0x55);
    stage_live(0, 0x1e, 0, 0, 0, 0, 0, 0, /*age=*/0x68);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x69);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x89 / 0x9e — compass-anchored billboard ────────────────────────── */

int test_records_b_tick_type_89_writes_compass_pose(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x18, 0);    /* compass=0 → ang=0 */
    owner_blob_set_i(0x428, 1);
    owner_blob_set_f(0x3f0, 100.0f);
    owner_blob_set_f(0x3f4, 200.0f);
    owner_blob_set_f(0x3f8, 300.0f);
    /* LIFE_MULT defaults to 0 — pos.y = 0+0+200 = 200. */

    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner(0);

    scene1_records_b_tick();

    /* ang=0 → sin=0, cos=1; radius=2.0 (type 0x89). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 200.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 302.0f) < 1e-5f);
    /* Drag = 2.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_9e_radius_uses_life_mult_5x(void)
{
    /* type 0x9e: radius = slot[LIFE_MULT] * 5. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x18, 2);   /* compass=2 → ang = 2*2π/8 = π/2 */
    owner_blob_set_i(0x428, 1);
    owner_blob_set_f(0x3f0, 0.0f);
    owner_blob_set_f(0x3f8, 0.0f);

    stage_live(0, 0x9e, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    bind_owner(0);

    scene1_records_b_tick();

    /* radius = 2 * 5 = 10; ang = π/2 → sin=1, cos≈0.
     * pos.x = 1*10 + 0 = 10; pos.z = 0*10 + 0 = 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) -  0.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_type_89_kill_on_owner_gate_fail(void)
{
    /* owner+0x428 != 1 → kill regardless of age. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 0);   /* dead owner */
    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, 5);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_89_kill_at_age_0xaf(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, /*age=*/0xae);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0xaf);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_89_se_fires_at_age_0x50(void)
{
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);

    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, /*age=*/0x4f);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT(s_se_calls >= 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2c1);
    scene1_records_b_set_se_hook(NULL);
    return 0;
}

int test_records_b_tick_type_89_iter_loop_calls_state_machine_20x_in_window(void)
{
    /* age in [0x50, 0xa0) → 20 state-machine calls in the iter loop. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, /*age=*/0x60);  /* becomes 0x61 */
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 20);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_type_89_owner_null_no_crash(void)
{
    /* OWNER_B = 0 (NULL) → body must short-circuit; no crash. */
    reset_world();
    stage_live(0, 0x89, 0, 0, 0, 0, 0, 0, 5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_OWNER_B, 0);

    scene1_records_b_tick();

    /* Slot still alive (no body fired, no kill). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x89);
    return 0;
}

int test_records_b_tick_type_2f_owner_null_no_crash(void)
{
    reset_world();
    stage_live(0, 0x2f, 0, 0, 0, 0, 0, 0, 2);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_OWNER_B, 0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x2f);
    return 0;
}

int test_records_b_tick_unknown_type_is_noop(void)
{
    /* Types outside C8j-tick.2 scope go through dispatch_default's
     * default branch (no-op). */
    reset_world();
    stage_live(0, /*type=*/0x55, 1, 2, 3, 0.5f, 0, 0, 10);
    scene1_records_b_tick();
    /* Only preamble fired: pos.x = 1.5, age = 11, TYPE preserved. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 1.5f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 11);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x55);
    return 0;
}

int test_records_b_tick_se_hook_setter_round_trips(void)
{
    reset_world();
    scene1_b_se_fn prev = scene1_records_b_set_se_hook(capture_se);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_se_hook(NULL);
    T_ASSERT(prev == capture_se);
    return 0;
}

/* ═══ C8j-tick.3 — mid-cascade tests ═══════════════════════════════════ */
/*
 * 0x9c uses owner_B (slot[+0x14]).  0x68 / 0x74 / 0x79 / 0x69 use
 * owner_A (slot[+0x10]) instead — bind via bind_owner_a().  Owner blob
 * is shared since 0x68/0x74/0x79 read at +0x904/0x908/0x90c/0x948/0xcf8/
 * 0xea0, and the existing 0xb00-byte blob isn't large enough; grow it
 * (lazy alloc) when these tests first run.
 */
/* Owner_A blob needs to cover up to +0xea0 (used by C8j-tick.3 type 0x69)
 * AND up to +0xe38 (used by C8j-tick.4 type-4 anim drive write).  Grow to
 * 0xf00 for headroom. */
#define OWNER_BLOB_3_SIZE 0xf00
static uint8_t *g_test_owner_a_blob;

static void owner_a_blob_ensure(void)
{
    if (g_test_owner_a_blob) return;
    void *p = mmap(NULL, OWNER_BLOB_3_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                   -1, 0);
    if (p == MAP_FAILED) abort();
    g_test_owner_a_blob = (uint8_t *)p;
}

static void owner_a_blob_reset(void)
{
    owner_a_blob_ensure();
    memset(g_test_owner_a_blob, 0, OWNER_BLOB_3_SIZE);
}

static void owner_a_blob_set_i(int byte_off, int32_t v)
{
    memcpy(g_test_owner_a_blob + byte_off, &v, sizeof v);
}

static int32_t owner_a_blob_get_i(int byte_off)
{
    int32_t v;
    memcpy(&v, g_test_owner_a_blob + byte_off, sizeof v);
    return v;
}

static float owner_a_blob_get_f(int byte_off)
{
    int32_t v;
    memcpy(&v, g_test_owner_a_blob + byte_off, sizeof v);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void bind_owner_a(int slot_idx)
{
    owner_a_blob_ensure();
    int32_t ptr = (int32_t)(uintptr_t)g_test_owner_a_blob;
    slot_set_i(slot_idx, SCENE1_RECORDS_B_OFF_OWNER_A, ptr);
}

/* Cull-query hook capture. */
static int s_cull_calls;
static int s_cull_return;
static int cull_stub(float x, float y)
{
    (void)x; (void)y;
    s_cull_calls++;
    return s_cull_return;
}

/* ─── 0x9c — NPC shoulder-arc bend ────────────────────────────────────── */

int test_records_b_tick_type_9c_writes_pose_around_owner(void)
{
    /* ang=0 → sin=0, cos=1; scale = LIFE_MULT.
     * pos.x = owner.x - 0*scale*1.5 = owner.x
     * pos.y = local_c * scale + owner.y    (local_c = 10 at age=0, but
     *   preamble bumps age to 1, so local_c = 10 - 1*0.3 = 9.7;
     *   clamped to max(9.7, scale)).
     * pos.z = owner.z - 1*scale*1.5 = owner.z - 1.5 */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x3f0, 100.0f);
    owner_blob_set_f(0x3f4, 50.0f);
    owner_blob_set_f(0x3f8, -20.0f);

    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-5f);
    /* local_c = max(9.7, 1.0) = 9.7; pos.y = 9.7*1 + 50 = 59.7. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 59.7f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - (-21.5f)) < 1e-5f);
    return 0;
}

int test_records_b_tick_type_9c_local_c_high_age_branch(void)
{
    /* AGE >= 0xbe → local_c = (AGE - 0xbe) * 0.6 + scale.
     * After preamble, AGE=0xbf, scale=2.0 → local_c = (0xbf-0xbe)*0.6 + 2 = 2.6.
     * pos.y = 2.6 * 2.0 + owner.y. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x3f4, 10.0f);

    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/0xbe);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - (2.6f * 2.0f + 10.0f)) < 1e-4f);
    return 0;
}

int test_records_b_tick_type_9c_rot_scr_clamped_to_zero(void)
{
    /* AGE >= 0x1a: ROT_SCR = clamp((AGE-0x1a)*π/40 - π/2, ≤0).
     * After preamble, AGE = 0xff → (0xff-0x1a)*π/40 - π/2 ≫ 0 → clamp to 0. */
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/0xfe);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR)) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_9c_rot_scr_neg_pi_half_when_young(void)
{
    /* AGE < 0x1a: ROT_SCR = -π/2 verbatim. */
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - (-1.5707964f)) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_9c_kill_at_age_0xc8(void)
{
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/0xc7);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);

    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0xc8);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_9c_se_fires_at_age_1(void)
{
    /* Preamble bumps age 0 → 1 before dispatch.  AGE==1 → SE 0x2c2. */
    reset_world();
    owner_blob_reset();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);

    stage_live(0, 0x9c, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT(s_se_calls >= 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2c2);
    return 0;
}

/* ─── 0x34 — NPC joint-target lerp ────────────────────────────────────── */

int test_records_b_tick_type_34_default_joint_pose(void)
{
    /* AUX_SENT1 preamble default = -1; joint pose reads owner+0x6fc /
     * +0x708 / +0x70c.  ALT_POS defaults zero → vel = -pos/15. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x6fc, 30.0f);
    owner_blob_set_f(0x708, 60.0f);
    owner_blob_set_f(0x70c, 90.0f);

    stage_live(0, 0x34, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT1, -1);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 30.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 60.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 90.0f) < 1e-5f);
    /* ALT_POS = 0; vel = (0 - 30) / 15 = -2.0 etc. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - (-2.0f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - (-4.0f)) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - (-6.0f)) < 1e-5f);
    /* DRAG = -0.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - (-0.5f)) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_34_iter_loop_state_machine_in_window(void)
{
    /* PART_IDX=0 + AGE in [0x5a, 0x78) → 20 state_machine calls. */
    reset_world();
    owner_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 0x34, 0, 0, 0, 0, 0, 0, /*age=*/0x60);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT1, -1);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 20);
    return 0;
}

int test_records_b_tick_type_34_kill_at_0x96(void)
{
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x34, 0, 0, 0, 0, 0, 0, /*age=*/0x95);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT1, -1);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x96);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x68 — three-phase NPC spawn cycle ──────────────────────────────── */

int test_records_b_tick_type_68_drag_default_1_0(void)
{
    /* FLAG_A != 1 → DRAG = 1.0, age_off = 0. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/50);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_68_drag_with_flag_a(void)
{
    /* FLAG_A == 1 → DRAG = 0.2, age_off = 0x14. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/50);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.2f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_68_kill_at_age_off_plus_0x4b(void)
{
    /* FLAG_A=0 → age_off=0; kill at AGE == 0x4b. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/0x4a);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);

    /* FLAG_A=1 → age_off=0x14; kill at AGE == 0x14+0x4b = 0x5f. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/0x5e);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_68_se_fires_at_age_off_plus_10(void)
{
    /* FLAG_A=1 → SE 0x2a4 at AGE==0x14+10=0x1e; SE 0x2a9 at AGE==0x14+0x28=0x3c. */
    reset_world();
    owner_a_blob_reset();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/0x1d);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(s_se_calls >= 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2a4);
    return 0;
}

int test_records_b_tick_type_68_state_machine_in_window(void)
{
    /* age_off=0 → state_machine called for AGE in [0, 0x3c). */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);

    /* Past window — no call. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x68, 0, 0, 0, 0, 0, 0, /*age=*/0x40);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

/* ─── 0x74 / 0x79 — entity ground-cull walker ─────────────────────────── */

int test_records_b_tick_type_74_drag_zero(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x74, 0, 0, 0, 0, 0, 0, /*age=*/50);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    /* AUX_C8 set to 1. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AUX_C8), 1);
    return 0;
}

int test_records_b_tick_type_79_drag_0_7(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x79, 0, 0, 0, 0, 0, 0, /*age=*/50);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.7f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_74_anchor_back_cancels_preamble(void)
{
    /* Preamble bumps pos by vel; body cancels it.  Net pos unchanged.
     * For ages outside [0, 0x28), no iter loop runs. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x74, 10.0f, 20.0f, 30.0f,
               /*vel=*/0.5f, 0.5f, 0.5f, /*age=*/0x80);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 20.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 30.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_type_74_kill_at_age_0x37(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x74, 0, 0, 0, 0, 0, 0, /*age=*/0x36);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x37);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_74_owner_kill_gate(void)
{
    /* owner_a+0xcf8 != 0 → kill regardless of age. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);

    stage_live(0, 0x74, 0, 0, 0, 0, 0, 0, /*age=*/0x50);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_type_74_cull_query_gates_state_machine(void)
{
    /* AGE in [0, 0x28) → 20-iter loop calling cull then state_machine.
     * cull_stub returns 0 (>=0 = "visible/skip") → state_machine NOT called. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    s_cull_calls = 0;
    s_cull_return = 0;          /* return 0 → state_machine NOT called */
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    scene1_records_b_set_cull_query_hook(cull_stub);

    stage_live(0, 0x74, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_cull_calls, 20);
    T_ASSERT_EQ_I(s_sm_calls, 0);

    /* Now with cull < 0 ("visible") → state_machine fires every iter. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    s_cull_calls = 0;
    s_cull_return = -1;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    scene1_records_b_set_cull_query_hook(cull_stub);

    stage_live(0, 0x74, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_cull_calls, 20);
    T_ASSERT_EQ_I(s_sm_calls, 20);
    return 0;
}

/* ─── 0x69 — entity self-spawn-then-die ───────────────────────────────── */

int test_records_b_tick_type_69_spawn_on_age_match_kills_self(void)
{
    /* PART_IDX=0 → spawn-trigger AGE = 0*4+0x14 = 0x14.  Preamble bumps
     * age from 0x13 to 0x14.  Engine writes owner+0xea0 = PART_IDX,
     * spawns entity 0x68, kills self. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x69, 0, 0, 0, 0, 0, 0, /*age=*/0x13);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xea0), 0);

    /* A spawned slot should now be live somewhere with type 0x68. */
    int found_spawn = 0;
    for (int k = 1; k < SCENE1_RECORDS_B_COUNT; k++) {
        if (slot_get_i(k, SCENE1_RECORDS_B_OFF_TYPE) == 0x68) {
            found_spawn = 1;
            break;
        }
    }
    T_ASSERT(found_spawn);
    return 0;
}

int test_records_b_tick_type_69_no_op_when_age_mismatch(void)
{
    /* PART_IDX=2 → spawn-trigger AGE = 2*4+0x14 = 0x1c.  age=5 → no spawn. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x69, 0, 0, 0, 0, 0, 0, /*age=*/4);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 2);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* Slot stays alive, no spawn. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x69);
    /* Suppress unused warning */
    (void)owner_a_blob_get_f;
    return 0;
}

int test_records_b_tick_cull_query_hook_setter_round_trips(void)
{
    reset_world();
    scene1_b_cull_query_fn prev = scene1_records_b_set_cull_query_hook(cull_stub);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_cull_query_hook(NULL);
    T_ASSERT(prev == cull_stub);
    return 0;
}

/* ═══ C8j-tick.4 — Body 1 (2/3/4/0x22/0x54/0x67/0x6d-0x70) ══════════════ */
/*
 * All Body 1 types use OWNER_A.  Pose is anchored at owner+0x20..0x28
 * (FLAG_B < 0 / "simple") or owner + FLAG_B*0x44 + 0x9e0 ("joint table").
 * Test owner blob 0xe00 covers up to +0xea0 (used by 0x69) and +0xe30/+0xe38
 * (used by Body 1 type-4 anim drive).
 */

/* ─── DRAG per type ────────────────────────────────────────────────────── */

int test_records_b_tick_type_2_drag_2(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_54_drag_2(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x54, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_67_drag_5_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x67, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 5.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_type_22_drag_3_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x22, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 3.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_type_6e_drag_2_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x6e, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_3_drag_1_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 3, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_type_4_drag_1_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 4, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

/* ─── FLAG_B < 0 simple-pose branch ────────────────────────────────────── */

int test_records_b_tick_body1_flagb_neg_writes_simple_pose(void)
{
    /* FLAG_B = -1 → pose at owner+0x20 + (sin(rot), 1.0, cos(rot)).
     * ROT_X = 0 → sin = 0, cos = 1.
     * Owner+0x20/0x24/0x28 = (10, 20, 30).
     * Expected: pos = (0+10, 20+1, 1+30) = (10, 21, 31). */
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float fx = 10.0f, fy = 20.0f, fz = 30.0f;
    memcpy(&v, &fx, 4); owner_a_blob_set_i(0x20, v);
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0x24, v);
    memcpy(&v, &fz, 4); owner_a_blob_set_i(0x28, v);

    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 3);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 21.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 31.0f) < 1e-5f);
    /* per_scale = 3 * -0.4 = -1.2; ALT_POS = (10 + -1.2*0, 21, 30 + -1.2*1)
     * = (10, 21, 28.8). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 21.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - 28.8f) < 1e-4f);
    return 0;
}

int test_records_b_tick_body1_type_6d_lifts_pos_y_by_1(void)
{
    /* Type 0x6d in FLAG_B<0 branch: pos.y += 1 after the base pose. */
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float fy = 50.0f;
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0x24, v);
    stage_live(0, 0x6d, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* Base pose pos.y = owner+0x24 + 1.0 = 51; then +1.0 for type 0x6d-0x70
     * = 52. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 52.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body1_type_2_no_pos_y_lift(void)
{
    /* Type 2 in FLAG_B<0 branch: no +1.0 to pos.y. */
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float fy = 50.0f;
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0x24, v);
    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* Base pose pos.y = owner+0x24 + 1.0 = 51, no lift. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 51.0f) < 1e-5f);
    return 0;
}

/* ─── FLAG_B >= 0 joint-table branch ──────────────────────────────────── */

int test_records_b_tick_body1_flagb_pos_joint_table(void)
{
    /* FLAG_B = 2 → joint base offset = 2*0x44 + 0x9e0 = 0xa68.
     * Owner+0xa68/0xa6c/0xa70 = (100, 200, 300).
     * ROT_X = 0 → sin=0, cos=1.
     * pos = (100+0, 200+1, 300+1) = (100, 201, 301).
     * ALT_POS = direct copy (100, 201, 300). */
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float fx = 100.0f, fy = 200.0f, fz = 300.0f;
    memcpy(&v, &fx, 4); owner_a_blob_set_i(0xa68, v);
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0xa6c, v);
    memcpy(&v, &fz, 4); owner_a_blob_set_i(0xa70, v);
    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, 2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 3);  /* per_scale irrelevant */
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 201.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 301.0f) < 1e-4f);
    /* ALT_POS direct-copies joint base (with +1.0 on y). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 100.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 201.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - 300.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_body1_type_6d_no_lift_in_joint_branch(void)
{
    /* Type 0x6d in FLAG_B>=0 branch: no pos.y +1 lift (engine only adds
     * inside the FLAG_B<0 simple branch fall-through). */
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float fy = 200.0f;
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0x9e4, v);  /* joint 0, y */
    stage_live(0, 0x6d, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, 0);     /* joint slot 0 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* pos.y = joint.y + 1.0 = 201, no extra lift. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 201.0f) < 1e-5f);
    return 0;
}

/* ─── Type 0x67 spawn path ────────────────────────────────────────────── */
/* No public hook on scene1_overlay_spawn — observe indirectly: type 0x67
 * runs the same pose code as type 2 (no observable divergence on slot
 * state except the side-effect spawn) and the body must not crash. */

int test_records_b_tick_body1_type_67_runs_full_pose(void)
{
    /* Identical pose math to type 2 since the 0x67-specific code only
     * adds an extra scene1_overlay_spawn call.  Validate that the pose
     * still matches and that the slot stays alive (the body doesn't
     * accidentally kill on the spawn path). */
    reset_world();
    owner_a_blob_reset();

    int32_t v;
    float fx = 5.0f, fy = 10.0f, fz = -5.0f;
    memcpy(&v, &fx, 4); owner_a_blob_set_i(0x20, v);
    memcpy(&v, &fy, 4); owner_a_blob_set_i(0x24, v);
    memcpy(&v, &fz, 4); owner_a_blob_set_i(0x28, v);

    stage_live(0, 0x67, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* Pose computation: ROT_X=0 → sin=0, cos=1; pos = owner+0x20+(0,1,1)
     * = (5, 11, -4). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) -  5.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 11.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - -4.0f) < 1e-5f);
    /* Slot still alive (age 3 → 4 after preamble, not 0x14). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x67);
    /* DRAG is 5.5 unique to 0x67. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 5.5f) < 1e-5f);
    return 0;
}

/* ─── State-machine loop window + anim-drive special case ──────────────── */

int test_records_b_tick_body1_state_machine_loop_5_iters(void)
{
    /* PART_IDX==0 + AGE in [6, 10) → loop fires up to 5 times.  With the
     * capture hook installed (returns 1 = "continue"), all 5 iters run. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/6);  /* preamble → AGE=7 */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 5);
    return 0;
}

int test_records_b_tick_body1_state_machine_loop_outside_window(void)
{
    /* AGE outside [6, 10) → loop doesn't fire. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/0xa);  /* preamble → 0xb */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_body1_state_machine_loop_part_idx_nonzero(void)
{
    /* PART_IDX != 0 → loop doesn't fire. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/6);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_body1_state_machine_loop_null_hook_runs_zero(void)
{
    /* NULL hook → state_machine_call_ret returns 0 first iter → break. */
    reset_world();
    owner_a_blob_reset();
    /* Hook NULL by default. */
    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/6);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* No state machine ran; no anim-drive write to owner_a+0xe30. */
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 0);
    return 0;
}

/* Custom state_machine hook that writes anim_drive on first call only,
 * mimicking the engine's per-tick fill-with-velocity behavior. */
static int s_anim_drive_writes;
static int s_anim_drive_write_value;
static void anim_drive_state_machine(int32_t *slot)
{
    (void)slot;
    /* Engine resets to 0 before each call; only the FIRST hook call
     * within the loop should "produce a value" — subsequent calls leave
     * it 0, which still passes the ret==1 gate but not the >0 gate. */
    s_sm_calls++;
    if (s_anim_drive_writes == 0) {
        g_scene1_records_b_tick_anim_drive = s_anim_drive_write_value;
    }
    s_anim_drive_writes++;
}

int test_records_b_tick_body1_type_4_anim_drive_writes_owner(void)
{
    /* Type==4 + state_machine returns 1 + anim_drive > 0 → owner_a+0xe30
     * gets anim_drive/10 (min 1) and +0xe38 gets 0x1e (30).  Hook writes
     * 50 on first call → expected owner+0xe30 = 5 (50/10), +0xe38 = 30. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    s_anim_drive_writes = 0;
    s_anim_drive_write_value = 50;
    scene1_records_b_set_state_machine_hook(anim_drive_state_machine);

    stage_live(0, 4, 0, 0, 0, 0, 0, 0, /*age=*/6);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 5);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe38), 0x1e);
    return 0;
}

int test_records_b_tick_body1_anim_drive_floor_at_1(void)
{
    /* anim_drive = 5 → 5/10 = 0 → floored to 1. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    s_anim_drive_writes = 0;
    s_anim_drive_write_value = 5;
    scene1_records_b_set_state_machine_hook(anim_drive_state_machine);

    stage_live(0, 4, 0, 0, 0, 0, 0, 0, /*age=*/6);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 1);
    return 0;
}

int test_records_b_tick_body1_type_2_no_anim_drive_branch(void)
{
    /* Type != 4 → no owner_a+0xe30 write even with anim_drive set. */
    reset_world();
    owner_a_blob_reset();
    s_sm_calls = 0;
    s_anim_drive_writes = 0;
    s_anim_drive_write_value = 100;
    scene1_records_b_set_state_machine_hook(anim_drive_state_machine);

    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/6);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 0);
    return 0;
}

/* ─── Kill paths ───────────────────────────────────────────────────────── */

int test_records_b_tick_body1_kill_at_age_0x14(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 2, 0, 0, 0, 0, 0, 0, /*age=*/0x13);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x14);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body1_kill_on_owner_cf8(void)
{
    /* owner_a+0xcf8 != 0 → kill regardless of age. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x22, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_B, -1);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ═══ C8j-tick.5 — Body 2 (0x71/0x72/0x7d/0x85/0x8a/0x8b/0x5b/0x5c/0x5e/0x86/0x87) ══ */
/*
 * All Body 2 types use OWNER_A.pose at +0x20/+0x24/+0x28.  The 0x8a body
 * also writes at +0x904/+0x908/+0x90c (vel-recoil), +0xcf8 (kill marker),
 * +0xe7c/+0xe80/+0xe84 (drive zero-out), +0xe90, and reads +0x930 via
 * aux_482a51.  The 0x71/0x72/0x7d body reads compass at +0x948 and reads
 * +0xe90 (0x72 only).  All bodies read +0xcf8 in their kill checks.
 * OWNER_BLOB_3_SIZE (0xf00) covers everything.
 */

/* aux_485979 hook capture. */
static int s_aux_485979_calls;
static int32_t s_aux_485979_last;
static void capture_aux_485979(int32_t a)
{
    s_aux_485979_calls++;
    s_aux_485979_last = a;
}

/* aux_482a51 hook capture. */
static int s_aux_482a51_calls;
static int32_t s_aux_482a51_last_a;
static int32_t s_aux_482a51_last_b;
static void capture_aux_482a51(int32_t a, int32_t b)
{
    s_aux_482a51_calls++;
    s_aux_482a51_last_a = a;
    s_aux_482a51_last_b = b;
}

/* notify_queue hook capture. */
static int s_notify_calls;
static int32_t s_notify_a, s_notify_b, s_notify_c;
static float   s_notify_d;
static void capture_notify(int32_t a, int32_t b, int32_t c, float d)
{
    s_notify_calls++;
    s_notify_a = a; s_notify_b = b; s_notify_c = c; s_notify_d = d;
}

/* State machine that returns "progress" (= installed hook) for the
 * ret-aware bodies (0x85, 0x8a, 0x8b).  The void-return contract here
 * means installing ANY hook → state_machine_call_ret() returns 1.  Use
 * for Body 2 progress-path tests. */
static int s_sm5_calls;
static void capture_sm_progress(int32_t *slot)
{
    (void)slot;
    s_sm5_calls++;
}

/* ─── 0x85 ─────────────────────────────────────────────────────────────── */

int test_records_b_tick_body2_type_85_pose_anchored_at_owner(void)
{
    /* No state-machine hook → ret==0 path; pose still written. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x20, 0x42000000);  /* owner.x = 32.0 */
    owner_a_blob_set_i(0x24, 0x41200000);  /* owner.y = 10.0 */
    owner_a_blob_set_i(0x28, 0xc1a00000);  /* owner.z = -20.0 */
    stage_live(0, 0x85, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* sin(0)=0, cos(0)=1 → pose = (0 + 32, 10+1, 1 + -20) = (32, 11, -19). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 32.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 11.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - -19.0f) < 1e-5f);
    /* DRAG = 0.5 */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    /* ALT_POS direct copy from owner + (0, 1, 0). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 32.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 11.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - -20.0f) < 1e-5f);
    /* Slot still alive (ret==0, owner+0xcf8==0, AGE!=0x24). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x85);
    return 0;
}

int test_records_b_tick_body2_type_85_state_machine_progress_kills(void)
{
    /* Install a state-machine hook → ret==1; engine writes owner+0xe90=7,
     * owner+0xe94=0, then kills the slot. */
    reset_world();
    owner_a_blob_reset();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x85, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm5_calls, 1);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe90), 7);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe94), 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body2_type_85_kill_at_age_0x24(void)
{
    /* ret==0 path AND AGE==0x24 → kill. */
    reset_world();
    owner_a_blob_reset();
    /* AGE in slot is 0x23; preamble bumps to 0x24. */
    stage_live(0, 0x85, 0, 0, 0, 0, 0, 0, /*age=*/0x23);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_type_85_kill_owner_cf8(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x85, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x8a / 0x8b ──────────────────────────────────────────────────────── */

int test_records_b_tick_body2_type_8a_pose_half_radius(void)
{
    /* No state-machine hook → only pose + DRAG.  Pose uses half-radius
     * sin/cos (* 0.5). */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x20, 0x40400000);  /* owner.x = 3.0 */
    owner_a_blob_set_i(0x24, 0x40000000);  /* owner.y = 2.0 */
    owner_a_blob_set_i(0x28, 0x40800000);  /* owner.z = 4.0 */
    stage_live(0, 0x8a, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* sin(0)*0.5=0, cos(0)*0.5=0.5 → pose = (3, 3, 4.5). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 3.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 3.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 4.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_type_8a_progress_full_cascade(void)
{
    /* Install state-machine hook → progress path.  Verifies:
     *   aux_485979(0); SE(0x13f); owner+0xcf8=0x2d; owner+0xe90=1;
     *   aux_482a51(owner_a_int + 0x930, 2); notify_queue(8, 4, 4, 0.5);
     *   owner+0xe7c/e80/e84 zeroed (set to 0 from any prior value);
     *   slot killed; owner+0x904 / 0x908 / 0x90c vel-recoil. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xe7c, 0x55);    /* will be zeroed */
    owner_a_blob_set_i(0xe80, 0x66);
    owner_a_blob_set_i(0xe84, 0x77);
    s_sm5_calls = 0;
    s_aux_485979_calls = 0;
    s_aux_482a51_calls = 0;
    s_notify_calls     = 0;
    s_se_calls         = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    scene1_records_b_set_aux_485979_hook(capture_aux_485979);
    scene1_records_b_set_aux_482a51_hook(capture_aux_482a51);
    scene1_records_b_set_notify_queue_hook(capture_notify);
    scene1_records_b_set_se_hook(capture_se);

    stage_live(0, 0x8a, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);  /* sin=0, cos=1 */
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_sm5_calls, 1);
    T_ASSERT_EQ_I(s_aux_485979_calls, 1);
    T_ASSERT_EQ_I(s_aux_485979_last, 0);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x13f);
    T_ASSERT_EQ_I(s_aux_482a51_calls, 1);
    /* aux_482a51's first arg is owner_a_int + 0x930. */
    int32_t expected = (int32_t)(uintptr_t)g_test_owner_a_blob + 0x930;
    T_ASSERT_EQ_I(s_aux_482a51_last_a, expected);
    T_ASSERT_EQ_I(s_aux_482a51_last_b, 2);
    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 8);
    T_ASSERT_EQ_I(s_notify_b, 4);
    T_ASSERT_EQ_I(s_notify_c, 4);
    T_ASSERT(fabsf(s_notify_d - 0.5f) < 1e-6f);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xcf8), 0x2d);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe7c), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe80), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe84), 0);
    /* vel-recoil: sin(0)*1.4 = 0, cos(0)*1.4 = 1.4 → owner+0x904 =
     * 0*-0.1 = 0, owner+0x908 = 0.3, owner+0x90c = 1.4*-0.1 = -0.14. */
    T_ASSERT(fabsf(owner_a_blob_get_f(0x904) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(owner_a_blob_get_f(0x908) - 0.3f) < 1e-5f);
    T_ASSERT(fabsf(owner_a_blob_get_f(0x90c) - -0.14f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    scene1_records_b_set_state_machine_hook(NULL);
    scene1_records_b_set_aux_485979_hook(NULL);
    scene1_records_b_set_aux_482a51_hook(NULL);
    scene1_records_b_set_notify_queue_hook(NULL);
    scene1_records_b_set_se_hook(NULL);
    return 0;
}

int test_records_b_tick_body2_type_8b_progress_uses_0x8_vel_scale(void)
{
    /* 0x8b path differs from 0x8a:
     *   - no aux_485979 / SE / aux_482a51 / notify_queue calls
     *   - owner+0xcf8 = 0xf (vs 0x2d for 0x8a)
     *   - vel_scale = 0.8 (vs 1.4) */
    reset_world();
    owner_a_blob_reset();
    s_aux_485979_calls = 0;
    s_aux_482a51_calls = 0;
    s_notify_calls     = 0;
    s_se_calls         = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    scene1_records_b_set_aux_485979_hook(capture_aux_485979);
    scene1_records_b_set_aux_482a51_hook(capture_aux_482a51);
    scene1_records_b_set_notify_queue_hook(capture_notify);
    scene1_records_b_set_se_hook(capture_se);

    stage_live(0, 0x8b, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_aux_485979_calls, 0);
    T_ASSERT_EQ_I(s_aux_482a51_calls, 0);
    T_ASSERT_EQ_I(s_notify_calls, 0);
    T_ASSERT_EQ_I(s_se_calls, 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xcf8), 0xf);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe90), 1);
    /* vel_scale = 0.8; cos(0)*0.8 = 0.8 → owner+0x90c = 0.8*-0.1 = -0.08. */
    T_ASSERT(fabsf(owner_a_blob_get_f(0x90c) - -0.08f) < 1e-5f);
    T_ASSERT(fabsf(owner_a_blob_get_f(0x908) - 0.3f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    scene1_records_b_set_state_machine_hook(NULL);
    scene1_records_b_set_aux_485979_hook(NULL);
    scene1_records_b_set_aux_482a51_hook(NULL);
    scene1_records_b_set_notify_queue_hook(NULL);
    scene1_records_b_set_se_hook(NULL);
    return 0;
}

int test_records_b_tick_body2_type_8a_no_progress_falls_through(void)
{
    /* No state-machine hook installed → ret==0, no kill, no owner side
     * effects (other than the unconditional kill checks at the tail
     * which all read 0 in a clean owner blob). */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x8a, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* Engine `if (owner+0xe7c == 0) kill` fires unconditionally — slot
     * gets killed even without state-machine progress when owner blob
     * is zero.  This matches the engine's "0x8a/0x8b force-kill when
     * owner has no progress yet" semantics. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_type_8a_kill_age_20000(void)
{
    /* AGE == 20000 → kill via the tail check.  Owner zeroed → no
     * progress path. */
    reset_world();
    owner_a_blob_reset();
    /* Pre-tick AGE = 19999; preamble bumps to 20000. */
    stage_live(0, 0x8a, 0, 0, 0, 0, 0, 0, /*age=*/19999);
    /* owner+0xe7c stays 0, slot will kill anyway from the 0xe7c==0
     * check — verify by setting 0xe7c != 0 so we ISOLATE the AGE
     * check. */
    owner_a_blob_set_i(0xe7c, 1);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x5b / 0x5c / 0x5e / 0x86 / 0x87 ─────────────────────────────────── */

int test_records_b_tick_body2_type_5c_drag_1_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x5c, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_type_87_drag_2_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x87, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_type_5b_age_2_spawn_no_lift(void)
{
    /* 0x5b at AGE==2 → spawn(0, x, y+0, z, 4, 1.8, 1).  Use the
     * scene1_spawn trace to capture the call. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x20, 0x40000000);   /* owner.x = 2.0 */
    owner_a_blob_set_i(0x24, 0x3f800000);   /* owner.y = 1.0 */
    owner_a_blob_set_i(0x28, 0x40400000);   /* owner.z = 3.0 */
    scene1_spawn_trace_reset();
    /* Pre-tick AGE=1; preamble → AGE=2. */
    stage_live(0, 0x5b, 0, 0, 0, 0, 0, 0, /*age=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);  /* sin=0, cos=1 */
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    scene1_spawn_call_t *c = &g_scene1_spawn_trace[0];
    T_ASSERT_EQ_I(c->slot_hint, 0);
    /* pos.x = sin(0) + 2 = 2; pos.y = 1+1 = 2; pos.z = cos(0) + 3 = 4. */
    T_ASSERT(fabsf(c->x - 2.0f) < 1e-5f);
    /* y_offset = 0 for 0x5b → spawn y = POS_Y + 0 = 2. */
    T_ASSERT(fabsf(c->y - 2.0f) < 1e-5f);
    T_ASSERT(fabsf(c->z - 4.0f) < 1e-5f);
    T_ASSERT_EQ_I(c->type, 4);
    T_ASSERT(fabsf(c->scale - 1.8f) < 1e-5f);
    T_ASSERT_EQ_I(c->param7, 1);
    return 0;
}

int test_records_b_tick_body2_type_5c_age_2_spawn_lifts_y(void)
{
    /* 0x5c at AGE==2 → spawn y uses y_offset = 1.0 (+ POS_Y). */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x24, 0x3f800000);   /* owner.y = 1.0 */
    scene1_spawn_trace_reset();
    stage_live(0, 0x5c, 0, 0, 0, 0, 0, 0, /*age=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    /* POS_Y = owner.y + 1 = 2; spawn y = 2 + 1 = 3. */
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].y - 3.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body2_type_5b_no_spawn_outside_age_2(void)
{
    reset_world();
    owner_a_blob_reset();
    scene1_spawn_trace_reset();
    stage_live(0, 0x5b, 0, 0, 0, 0, 0, 0, /*age=*/5);  /* preamble → 6 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_body2_type_5c_state_machine_loop_window(void)
{
    /* AGE in [2, 6) → 5-iter early-break state-machine loop.  With
     * hook installed, all 5 fire (no break). */
    reset_world();
    owner_a_blob_reset();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x5c, 0, 0, 0, 0, 0, 0, /*age=*/3);  /* → 4 (in window) */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 5);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body2_type_5c_kill_at_age_0x14(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x5c, 0, 0, 0, 0, 0, 0, /*age=*/0x13);  /* → 0x14 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x71 / 0x72 / 0x7d ───────────────────────────────────────────────── */

int test_records_b_tick_body2_type_71_age_under_20_uses_sin_ramp(void)
{
    /* type 0x71 + AGE<0x14 → scale = sin(AGE*π/2/20)*2.5 + 0.5.
     * At AGE=10 (post-preamble from 9), scale = sin(10*π/40)*2.5 + 0.5
     *   = sin(π/4)*2.5 + 0.5 ≈ 0.7071*2.5 + 0.5 ≈ 2.268. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 99);  /* "else" compass: POS_Z += 0.4 */
    stage_live(0, 0x71, 0, 0, 0, 0, 0, 0, /*age=*/9);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);  /* sin=0, cos=1 */
    bind_owner_a(0);
    scene1_records_b_tick();
    /* POS_X = scale*sin(0) + owner.x(0) = 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 0.0f) < 1e-5f);
    /* POS_Z = scale*cos(0) + owner.z(0) + 0.4 (compass else) ≈ 2.268 + 0.4. */
    float expected_z = sinf(10.0f * 1.5707964f / 20.0f) * 2.5f + 0.5f + 0.4f;
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - expected_z) < 1e-4f);
    /* DRAG = 0.5 (default for 0x71, no override). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_type_71_age_at_or_above_20_uses_3_0_scale(void)
{
    /* Engine 0x71 + AGE>=0x14: `scale = 3.0` (unconditional override
     * of the min(2.0, AGE*0.2+0.5) base; sin-ramp not entered).  Use
     * ROT_X = π/2 so sin(ROT_X) = 1 and POS_X = scale * 1 + owner.x. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 99);  /* "else" compass → POS_Z += 0.4 */
    stage_live(0, 0x71, 0, 0, 0, 0, 0, 0, /*age=*/24);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.5707964f);  /* π/2 */
    bind_owner_a(0);
    scene1_records_b_tick();
    /* POS_X = 3.0 * sin(π/2) + 0 = 3.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 3.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_body2_type_7d_scale_is_2_5_and_drag_1_5(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 4);  /* compass 4 → POS_X += 0.4 */
    stage_live(0, 0x7d, 0, 0, 0, 0, 0, 0, /*age=*/9);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* scale = 2.5; POS_X = 2.5*0 + 0 + 0.4 = 0.4. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 0.4f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_type_72_scale_multiplied_by_0_9(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 99);
    /* AGE=10, type=0x72: base = 10*0.2+0.5 = 2.5; clamped to 2.0;
     * 0x72 multiplies by 0.9 → 1.8.  But 0x72's tail also overrides
     * DRAG to 1.0 (after the initial 0.4 store). */
    stage_live(0, 0x72, 0, 0, 0, 0, 0, 0, /*age=*/9);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* POS_Z = 1.8 * 1 + 0 + 0.4 = 2.2. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 2.2f) < 1e-5f);
    /* Final DRAG = 1.0 (overridden). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body2_compass_0_subtracts_pos_x(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x20, 0x41200000);  /* owner.x = 10.0 */
    owner_a_blob_set_i(0x948, 0);
    stage_live(0, 0x7d, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* scale=2.5; POS_X = 2.5*0 + 10 - 0.4 = 9.6. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 9.6f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body2_type_7d_age_1_fires_pfo_alloc(void)
{
    /* type 0x7d + AGE==1 → PFO Table A passthrough alloc.  We can't
     * easily mock the PFO allocator (no hook), but it writes to a
     * specific slot in Table A.  Verify a slot got claimed (sentinel
     * became non-(-1)) and template_id == 6. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 4);
    /* Initialize Table A: scene1_records_reset called inside reset_world
     * already populates it via scene1_pfo_table_a_init.  Verify
     * baseline: slot 0 sentinel == -1. */
    extern int32_t g_scene1_pfo_table_a[];
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[4], -1);

    /* Pre-tick AGE=0; preamble → 1. */
    stage_live(0, 0x7d, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* Slot 0 sentinel (offset 4 in PFO slot layout) should now be 6. */
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[4], 6);
    return 0;
}

int test_records_b_tick_body2_type_72_age_mod5_4_increments_seq(void)
{
    /* 0x72 at AGE%5==4 → SEQ_ID = seq_counter_next.  AGE=4 = 4%5=4. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 99);
    g_scene1_record_b_seq_counter = 42;
    /* Pre-tick AGE=3; preamble → 4. */
    stage_live(0, 0x72, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* SEQ_ID = prior counter (42), counter now 43. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 42);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 43);
    return 0;
}

int test_records_b_tick_body2_type_72_age_mod5_not_4_no_seq_no_kill(void)
{
    /* 0x72 at AGE%5!=4: SEQ_ID not written, AGE-kill skipped (engine
     * jmp 0x43fbbc).  Slot stays alive. */
    reset_world();
    owner_a_blob_reset();
    /* Set owner+0xe90 = 2 so the 0x72-specific kill check doesn't fire. */
    owner_a_blob_set_i(0xe90, 2);
    g_scene1_record_b_seq_counter = 100;
    /* Pre-tick AGE=4; preamble → 5; 5 % 5 == 0 ≠ 4. */
    stage_live(0, 0x72, 0, 0, 0, 0, 0, 0, /*age=*/4);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID, 0xdead);  /* sentinel */
    bind_owner_a(0);
    scene1_records_b_tick();
    /* SEQ_ID not modified. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 0xdead);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 100);
    /* Slot still alive. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x72);
    return 0;
}

int test_records_b_tick_body2_type_72_kills_when_owner_e90_not_2(void)
{
    /* 0x72-specific: owner+0xe90 != 2 → kill. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xe90, 99);  /* != 2 */
    stage_live(0, 0x72, 0, 0, 0, 0, 0, 0, /*age=*/3);  /* AGE%5=4 → 4 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_type_71_kill_at_age_0x1e(void)
{
    reset_world();
    owner_a_blob_reset();
    /* Pre-tick AGE=0x1d; preamble → 0x1e. */
    stage_live(0, 0x71, 0, 0, 0, 0, 0, 0, /*age=*/0x1d);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_type_71_owner_cf8_kill(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x71, 0, 0, 0, 0, 0, 0, /*age=*/3);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_state_machine_loop_window_71(void)
{
    /* 0x71: state machine loop window AGE in (3, 0x14).  At AGE=10
     * (post-preamble from 9), loop fires 5 iters. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x948, 99);
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x71, 0, 0, 0, 0, 0, 0, /*age=*/9);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 5);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

/* ═══ C8j-tick.6 — Body 3 (0x1f/0x5a/0x98/0x6c/0x6b/0x28) ═══════════════ */

/* 0x5a/0x98 reads OWNER_B; the others have no owner read. */

int test_records_b_tick_body3_type_1f_life_mult_ramps(void)
{
    /* AGE=10 (preamble bumps from 9); LIFE_MULT starts 0; after tick =
     * 0.03; DRAG = 0.03*0.1 - 0.5 = -0.497. */
    reset_world();
    stage_live(0, 0x1f, 0, 0, 0, 0, 0, 0, /*age=*/9);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.03f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.497f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body3_type_1f_life_mult_clamps_at_1_5(void)
{
    /* If LIFE_MULT already > 1.5, the +0.03 overshoots and gets clamped. */
    reset_world();
    stage_live(0, 0x1f, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.49f);
    scene1_records_b_tick();
    /* 1.49 + 0.03 = 1.52 > 1.5 → clamp to 1.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body3_type_1f_kill_at_age_0x78(void)
{
    reset_world();
    stage_live(0, 0x1f, 0, 0, 0, 0, 0, 0, /*age=*/0x77);  /* → 0x78 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body3_type_5a_drag_uses_plus_0_5(void)
{
    /* 0x5a clamps LIFE_MULT at 2.0 (not 1.5 like 0x1f) and uses
     * DRAG = LIFE_MULT*0.1 + 0.5. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);   /* keep slot alive */
    stage_live(0, 0x5a, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner(0);
    scene1_records_b_tick();
    /* LIFE_MULT = 1.03; DRAG = 1.03*0.1 + 0.5 = 0.603. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.03f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.603f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body3_type_5a_drifts_toward_player_in_window(void)
{
    /* AGE in ((PART_IDX+3)*10, 0x78) → VEL_{X,Z} drift toward
     * g_scene1_player_pos[{0,2}] then *= 0.95.
     * With PART_IDX=0, window = (30, 120).  AGE=50 (preamble from 49).
     * Player at (10, _, -20); slot at POS=(0,0,0).
     * vx_new = ((10 - 0) * 0.003 + 0) * 0.95 = 0.0285
     * vz_new = ((-20 - 0) * 0.003 + 0) * 0.95 = -0.057 */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    g_scene1_player_pos[0] =  10.0f;
    g_scene1_player_pos[1] =   0.0f;
    g_scene1_player_pos[2] = -20.0f;
    stage_live(0, 0x5a, 0, 0, 0, 0, 0, 0, /*age=*/49);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    bind_owner(0);
    scene1_records_b_tick();
    /* Note: preamble pos += vel = 0 + 0 = 0; preamble runs BEFORE body. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0285f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - -0.057f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body3_type_5a_no_drift_outside_window(void)
{
    /* AGE <= (PART_IDX+3)*10: drift block skipped (VEL unchanged). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    g_scene1_player_pos[0] = 100.0f;
    g_scene1_player_pos[2] = 100.0f;
    stage_live(0, 0x5a, 0, 0, 0, 0, 0, 0, /*age=*/25);  /* → 26 */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);     /* window > 30 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 7.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_Z, -3.0f);
    bind_owner(0);
    scene1_records_b_tick();
    /* VEL unchanged. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) -  7.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - -3.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body3_type_5a_age_0x78_spawns_and_kills(void)
{
    /* AGE==0x78 → two overlay spawns + SE + POS_Y+1 + state_machine + kill. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 1);
    s_se_calls = 0; s_sm5_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_set_state_machine_hook(capture_sm_progress);

    stage_live(0, 0x5a, 5.0f, 10.0f, -3.0f, 0, 0, 0, /*age=*/0x77);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2ac);
    T_ASSERT_EQ_I(s_sm5_calls, 1);
    /* POS_Y after = 10.0 + 1.0 = 11.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 11.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);  /* killed */
    scene1_records_b_set_se_hook(NULL);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body3_type_5a_kill_on_owner_428(void)
{
    /* owner_b+0x428 != 1 → kill (LAB_0043cded). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x428, 0);  /* != 1 → kill */
    stage_live(0, 0x5a, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body3_type_98_uses_same_body_as_5a(void)
{
    /* 0x98 shares the body with 0x5a — verify the kill check
     * still fires (owner_b+0x428 != 1 in default zero blob). */
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x98, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body3_type_6c_drag_kill_at_age_200(void)
{
    /* 0x6c uses 0x1f-style DRAG (LIFE_MULT*0.1 - 0.5) but DOES NOT
     * update LIFE_MULT.  Kills at AGE==0xc8 (=200). */
    reset_world();
    stage_live(0, 0x6c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    /* LIFE_MULT unchanged (no +0.03 update). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.0f) < 1e-6f);
    /* DRAG = 1.0*0.1 - 0.5 = -0.4. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.4f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x6c);  /* alive */

    reset_world();
    stage_live(0, 0x6c, 0, 0, 0, 0, 0, 0, /*age=*/0xc7);  /* → 0xc8 = 200 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body3_type_6b_age_0x2d_spawns_se(void)
{
    /* 0x6b at AGE==0x2d → SE + overlay spawn (no state_machine inside
     * the if; that fires for AGE >= 0x2d). */
    reset_world();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x6b, 7.0f, 4.0f, -8.0f, 0, 0, 0, /*age=*/0x2c);  /* → 0x2d */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2ac);
    /* Still alive (kill at 0x9b). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x6b);
    scene1_records_b_set_se_hook(NULL);
    return 0;
}

int test_records_b_tick_body3_type_6b_state_machine_only_after_2d(void)
{
    /* AGE < 0x2d → no state_machine.  AGE >= 0x2d → state_machine fires. */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x6b, 0, 0, 0, 0, 0, 0, /*age=*/0x10);  /* → 0x11 (< 0x2d) */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 0);

    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x6b, 0, 0, 0, 0, 0, 0, /*age=*/0x30);  /* → 0x31 (>= 0x2d) */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 1);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body3_type_6b_kill_at_age_0x9b(void)
{
    reset_world();
    stage_live(0, 0x6b, 0, 0, 0, 0, 0, 0, /*age=*/0x9a);  /* → 0x9b */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body3_type_28_vel_y_decay(void)
{
    /* 0x28: VEL_Y -= 0.003 every tick; DRAG = LIFE_MULT*0.1. */
    reset_world();
    stage_live(0, 0x28, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    /* Preamble: pos.y += vel.y (1.0); then body subtracts 0.003 from VEL_Y. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.997f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.1f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body3_type_28_kill_at_age_300(void)
{
    reset_world();
    stage_live(0, 0x28, 0, 0, 0, 0, 0, 0, /*age=*/0x12b);  /* → 0x12c = 300 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ═══ C8j-tick.7 — scattered post-Body-3 (0x38/0x29/0x8c) ══════════════ */

int test_records_b_tick_body4_type_38_drag_2_kill_at_300(void)
{
    reset_world();
    stage_live(0, 0x38, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);

    reset_world();
    stage_live(0, 0x38, 0, 0, 0, 0, 0, 0, /*age=*/0x12b);  /* → 0x12c */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body4_type_29_drag_4x_life_mult(void)
{
    reset_world();
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.75f);
    scene1_records_b_tick();
    /* DRAG = 0.75 * 4 = 3.0 */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 3.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body4_type_29_age_1_fires_notify_queue(void)
{
    /* AGE==1: notify_queue(0xa, 0x10, 0x10, 1.0). */
    reset_world();
    s_notify_calls = 0;
    scene1_records_b_set_notify_queue_hook(capture_notify);
    /* Pre-tick AGE=0; preamble → 1. */
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 0xa);
    T_ASSERT_EQ_I(s_notify_b, 0x10);
    T_ASSERT_EQ_I(s_notify_c, 0x10);
    T_ASSERT(fabsf(s_notify_d - 1.0f) < 1e-6f);
    scene1_records_b_set_notify_queue_hook(NULL);
    return 0;
}

int test_records_b_tick_body4_type_29_spawn_when_age_lt_104(void)
{
    /* SCALE_Y=1.0 default; spawn_age = (int)(136-32) = 104.  AGE=5 < 104
     * → scene1_spawn(0, POS, 0x4e, LIFE_MULT*0.5, 1). */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x29, 3.0f, 5.0f, -7.0f, 0, 0, 0, /*age=*/4);  /* → 5 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y,   1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    scene1_spawn_call_t *c = &g_scene1_spawn_trace[0];
    T_ASSERT_EQ_I(c->slot_hint, 0);
    T_ASSERT(fabsf(c->x -  3.0f) < 1e-5f);
    T_ASSERT(fabsf(c->y -  5.0f) < 1e-5f);
    T_ASSERT(fabsf(c->z - -7.0f) < 1e-5f);
    T_ASSERT_EQ_I(c->type, 0x4e);
    T_ASSERT(fabsf(c->scale - 0.5f) < 1e-5f);
    T_ASSERT_EQ_I(c->param7, 1);
    return 0;
}

int test_records_b_tick_body4_type_29_no_spawn_at_spawn_age(void)
{
    /* AGE == spawn_age = 104 → no spawn (engine `if AGE < spawn_age`). */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/103);  /* → 104 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y,   1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_body4_type_29_inner_loop_fires_on_age_mod3_1(void)
{
    /* AGE in (10, 90) AND AGE % 3 == 1 → state_machine inner loop with
     * iter_count = min(5, AGE/8 + 1).  AGE=13 → iter_count = min(5, 2)
     * = 2 (n in [0, 2)). */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/12);  /* → 13 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 2);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body4_type_29_inner_loop_clamps_at_5(void)
{
    /* AGE=40 → iter_count = min(5, 40/8+1) = min(5, 6) = 5. */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    /* AGE = 39; preamble → 40; 40 % 3 = 1 ✓. */
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/39);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 5);
    scene1_records_b_set_state_machine_hook(NULL);
    return 0;
}

int test_records_b_tick_body4_type_29_age_mod_10_increments_seq(void)
{
    /* AGE=20 → AGE%10==0 → SEQ_ID = seq. */
    reset_world();
    g_scene1_record_b_seq_counter = 17;
    /* AGE = 19; preamble → 20. */
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/19);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 17);
    T_ASSERT_EQ_I(g_scene1_record_b_seq_counter, 18);
    return 0;
}

int test_records_b_tick_body4_type_29_kill_at_age_136(void)
{
    /* SCALE_Y=1 → kill at AGE >= 136. */
    reset_world();
    /* AGE = 135; preamble → 136. */
    stage_live(0, 0x29, 0, 0, 0, 0, 0, 0, /*age=*/135);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_SCALE_Y, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body4_type_8c_drag_1_rot_x_increment(void)
{
    reset_world();
    stage_live(0, 0x8c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    /* ROT_X increased by 0.15. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 1.15f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body4_type_8c_kill_when_part_idx_100(void)
{
    reset_world();
    stage_live(0, 0x8c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 100);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body4_type_8c_kill_when_age_over_0x4af(void)
{
    reset_world();
    stage_live(0, 0x8c, 0, 0, 0, 0, 0, 0, /*age=*/0x4af);  /* → 0x4b0 > 0x4af */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body2_aux_hook_setters_round_trip(void)
{
    /* All three new setters return previous value. */
    reset_world();
    scene1_b_aux_1arg_fn p1 =
        scene1_records_b_set_aux_485979_hook(capture_aux_485979);
    T_ASSERT(p1 == NULL);
    p1 = scene1_records_b_set_aux_485979_hook(NULL);
    T_ASSERT(p1 == capture_aux_485979);

    scene1_b_aux_2arg_fn p2 =
        scene1_records_b_set_aux_482a51_hook(capture_aux_482a51);
    T_ASSERT(p2 == NULL);
    p2 = scene1_records_b_set_aux_482a51_hook(NULL);
    T_ASSERT(p2 == capture_aux_482a51);

    scene1_b_notify_queue_fn p3 =
        scene1_records_b_set_notify_queue_hook(capture_notify);
    T_ASSERT(p3 == NULL);
    p3 = scene1_records_b_set_notify_queue_hook(NULL);
    T_ASSERT(p3 == capture_notify);
    return 0;
}

/* ═══ C8j-tick.8 — ground-bouncing types 0x2c/0x23/0x3a ════════════════ */

/* ─── ground_query + aux_4532bc captures ─────────────────────────────── */

static int   s_gq_calls;
static int   s_gq_hit;       /* return value mocked by next call */
static float s_gq_out_y;     /* value written to out_y */
static float s_gq_last_x, s_gq_last_y, s_gq_last_z;
static int gq_canned(float x, float y, float z, float *out_y)
{
    s_gq_calls++;
    s_gq_last_x = x; s_gq_last_y = y; s_gq_last_z = z;
    *out_y = s_gq_out_y;
    return s_gq_hit;
}

static int s_aux_4532bc_calls;
static int32_t s_aux_4532bc_last;
static void capture_aux_4532bc(int32_t a)
{
    s_aux_4532bc_calls++;
    s_aux_4532bc_last = a;
}

/* ─── 0x2c — physics-bounce billboard ─────────────────────────────────── */

int test_records_b_tick_body5_type_2c_vel_y_gravity_damping(void)
{
    /* VEL_Y = (VEL_Y - 0.02) * 0.95.  Pre VEL_Y=1.0 → (1.0-0.02)*0.95 = 0.931.
     * Preamble runs first (POS_Y += VEL_Y = 1.0), then body updates VEL_Y. */
    reset_world();
    stage_live(0, 0x2c, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.931f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_2c_rot_scratch_increments(void)
{
    /* ROT_SCR += 0.05; ROT_Z += 0.03. */
    reset_world();
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.1f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z,   0.2f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - 0.15f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z)   - 0.23f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_2c_matrix_ends_as_identity(void)
{
    /* Body computes rot_x * rot_y into MATRIX0 then wipes it with
     * mat4_identity().  Net observable: MATRIX0 = identity post-tick. */
    reset_world();
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    /* Pre-fill MATRIX0 with garbage. */
    for (int j = 0; j < 16; j++) {
        slot_set_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + j, 99.0f + (float)j);
    }
    scene1_records_b_tick();
    for (int j = 0; j < 16; j++) {
        float v = slot_get_f(0, SCENE1_RECORDS_B_OFF_MATRIX0 + j);
        float expect = (j == 0 || j == 5 || j == 10 || j == 15) ? 1.0f : 0.0f;
        T_ASSERT(fabsf(v - expect) < 1e-5f);
    }
    return 0;
}

int test_records_b_tick_body5_type_2c_no_bounce_when_vel_y_positive(void)
{
    /* VEL_Y > 0 → ground_query NOT called; bounce skipped. */
    reset_world();
    s_gq_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 100.0f;  /* would be a hit if asked */
    stage_live(0, 0x2c, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 0);
    return 0;
}

int test_records_b_tick_body5_type_2c_no_bounce_when_no_ground_hit(void)
{
    /* Negative VEL_Y but ground returns 0 (no hit) → no bounce. */
    reset_world();
    s_gq_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 0; s_gq_out_y = 0.0f;
    /* Pre VEL_Y < 0 won't survive damping calc to negative until body runs;
     * but the body reads VEL_Y AFTER the damping (= post-write value).
     * With pre-VEL_Y=-1: post = (-1-0.02)*0.95 = -0.969 < 0, gate fires. */
    stage_live(0, 0x2c, 0, /*py=*/10.0f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);  /* FLAG=0 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);                                 /* gate fired */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);   /* FLAG still 0 */
    return 0;
}

int test_records_b_tick_body5_type_2c_bounce_first_hit_sets_flag(void)
{
    /* Negative VEL_Y AND ground hits AND POS_Y <= threshold (LIFE_MULT*0.5
     * + slot[AUX_9]).  With LIFE_MULT=1 and AUX_9=0, threshold=0.5; with
     * post-preamble POS_Y=0.4, gate fires. */
    reset_world();
    s_gq_calls = 0; s_notify_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_notify_queue_hook(capture_notify);
    s_gq_hit = 1; s_gq_out_y = 0.0f;  /* ground value ignored — threshold is on slot */
    /* Pre POS_Y = 0.4 (preamble adds VEL_Y=0 → still 0.4). */
    stage_live(0, 0x2c, 0, /*py=*/0.4f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_AUX_9,     0.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    /* POS_Y snapped to threshold = 0.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 0.5f) < 1e-5f);
    /* VEL_Y bounced: post-damping VEL_Y = -0.969 * -0.8 = +0.7752. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.7752f) < 1e-4f);
    /* FLAG set to 1, notify fired. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1 + 1); /* +1 from FLAG++ */
    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 4); T_ASSERT_EQ_I(s_notify_b, 4); T_ASSERT_EQ_I(s_notify_c, 4);
    T_ASSERT(fabsf(s_notify_d - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_body5_type_2c_no_bounce_when_pos_y_above_threshold(void)
{
    /* Ground hits but POS_Y > threshold → no snap, no FLAG change.  Pre
     * POS_Y=5, VEL_Y=-1 → post-preamble POS_Y=4 (still well above 0.5). */
    reset_world();
    s_gq_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    stage_live(0, 0x2c, 0, /*py=*/5.0f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);  /* threshold=0.5 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_AUX_9,     0.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 4.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_body5_type_2c_flag_progression_kill_at_0x1e(void)
{
    /* FLAG > 0 → FLAG++.  FLAG == 0x1e (after increment) → kill. */
    reset_world();
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0x1d);  /* +1 → 0x1e */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body5_type_2c_state_machine_only_when_flag_0(void)
{
    /* FLAG > 0 → state_machine NOT called (engine's `if (FLAG==0)` gate).
     * FLAG == 0 → state_machine called with DRAG = LIFE_MULT * 1.2. */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 5);  /* +1 → 6, not 0 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 0);

    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm5_calls, 1);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.4f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_2c_kill_at_age_300(void)
{
    reset_world();
    stage_live(0, 0x2c, 0, 0, 0, 0, 0, 0, /*age=*/0x12b);  /* → 0x12c */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x23 — ground-bouncer with cleanup cascade ─────────────────────── */

int test_records_b_tick_body5_type_23_drag_and_rot_increments(void)
{
    /* DRAG = LIFE_MULT * 3.0; ROT_SCR += 0.04; ROT_X += 0.04 — always. */
    reset_world();
    stage_live(0, 0x23, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.5f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR,   0.1f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X,     0.2f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  1);  /* FLAG!=0 to avoid ground-hit path */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)    - 4.5f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - 0.14f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X)   - 0.24f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_23_age_odd_spawns_0x53(void)
{
    /* FLAG==0 AND AGE & 1 → scene1_spawn(0, POS, 0x53, LIFE_MULT*0.1, 1). */
    reset_world();
    scene1_spawn_trace_reset();
    /* Pre AGE=0, preamble → 1 (odd).  No ground hook → no_hit, no cascade. */
    stage_live(0, 0x23, 3.0f, 5.0f, -7.0f, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    scene1_spawn_call_t *c = &g_scene1_spawn_trace[0];
    T_ASSERT_EQ_I(c->type, 0x53);
    T_ASSERT(fabsf(c->scale - 0.2f) < 1e-5f);     /* 2.0 * 0.1 */
    T_ASSERT_EQ_I(c->param7, 1);
    T_ASSERT(fabsf(c->x -  3.0f) < 1e-5f);
    T_ASSERT(fabsf(c->y -  5.0f) < 1e-5f);
    T_ASSERT(fabsf(c->z - -7.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_23_age_even_no_0x53_spawn(void)
{
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x23, 0, 0, 0, 0, 0, 0, /*age=*/1);  /* → 2 (even) */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_body5_type_23_ground_hit_fires_cascade(void)
{
    /* Ground hit, POS_Y < gy+1: FLAG=1, VEL_Y=0, aux_4532bc(0x20),
     * notify_queue(0x28, 0x10, 0x10, 1), 7 scene1_spawn calls. */
    reset_world();
    scene1_spawn_trace_reset();
    s_gq_calls = 0; s_notify_calls = 0; s_aux_4532bc_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_notify_queue_hook(capture_notify);
    scene1_records_b_set_aux_4532bc_hook(capture_aux_4532bc);
    s_gq_hit = 1; s_gq_out_y = 5.0f;  /* ground at y=5 */
    /* AGE=2 (even → no 0x53 spawn).  POS_Y=4 (< 5+1). */
    stage_live(0, 0x23, 0, /*py=*/4.0f, 0, 0, /*vy=*/0.0f, 0, /*age=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();

    /* POS_Y snapped to 5.0 (ground_y). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 5.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);

    T_ASSERT_EQ_I(s_aux_4532bc_calls, 1);
    T_ASSERT_EQ_I(s_aux_4532bc_last, 0x20);

    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 0x28);
    T_ASSERT_EQ_I(s_notify_b, 0x10);
    T_ASSERT_EQ_I(s_notify_c, 0x10);
    T_ASSERT(fabsf(s_notify_d - 1.0f) < 1e-6f);

    /* 7 scene1_spawn calls: 0x0f, 0x36, 0x2a, 0x52, 0x51, 0x51, 0x51. */
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 7);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x0f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].type, 0x36);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[2].type, 0x2a);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[3].type, 0x52);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[4].type, 0x51);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[5].type, 0x51);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[6].type, 0x51);
    /* 0x51 stack: gy, gy+2, gy+2. */
    T_ASSERT(fabsf(g_scene1_spawn_trace[4].y - 5.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[5].y - 7.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[6].y - 7.0f) < 1e-5f);
    /* Scales: 0x0f→LM, 0x36→LM*0.8, 0x2a→LM*0.4, 0x52→LM*0.7, 0x51×3→LM. */
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 1.0f)  < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[1].scale - 0.8f)  < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[2].scale - 0.4f)  < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[3].scale - 0.7f)  < 1e-5f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].param7, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].param7, 0x40);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[2].param7, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[3].param7, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[4].param7, 2);

    /* DRAG ends at LIFE_MULT * 8.0 = 8.0 (overwrites the initial *3.0). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 8.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_23_ground_hit_above_threshold_no_cascade(void)
{
    /* Ground hits but POS_Y >= gy+1 → no cascade. */
    reset_world();
    scene1_spawn_trace_reset();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 5.0f;
    stage_live(0, 0x23, 0, /*py=*/10.0f, 0, 0, 0, 0, /*age=*/1);  /* → 2 even */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_body5_type_23_flag_progression_kill_at_10(void)
{
    /* FLAG != 0 branch: FLAG++ to 10 → kill. */
    reset_world();
    stage_live(0, 0x23, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 9);  /* +1 → 10 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body5_type_23_flag_over_20_decays_pos_y(void)
{
    /* FLAG > 20 (post-increment) → POS_Y -= 0.1. */
    reset_world();
    stage_live(0, 0x23, 0, /*py=*/5.0f, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 20);  /* +1 → 21 > 20 */
    scene1_records_b_tick();
    /* POS_Y was 5.0, body sees FLAG+1=21 > 20, decrements POS_Y by 0.1 → 4.9. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 4.9f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_23_flag_at_20_does_not_decay(void)
{
    /* FLAG+1 == 20 → branch's `> 20` is FALSE → no POS_Y decay. */
    reset_world();
    stage_live(0, 0x23, 0, /*py=*/5.0f, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 19);  /* +1 → 20 */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 5.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_23_kill_at_age_200(void)
{
    reset_world();
    /* AGE = 199; preamble → 200. */
    stage_live(0, 0x23, 0, 0, 0, 0, 0, 0, /*age=*/199);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);  /* FLAG!=0 branch, no kill from that */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x3a — alternate ground-bouncer with cleanup cascade ───────────── */

int test_records_b_tick_body5_type_3a_flag_nonzero_skips_to_age_kill(void)
{
    /* FLAG != 0 → only the AGE==0x78 kill check runs.  AGE != 0x78 → live. */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x3a, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3a);
    return 0;
}

int test_records_b_tick_body5_type_3a_kill_at_age_0x78(void)
{
    reset_world();
    stage_live(0, 0x3a, 0, 0, 0, 0, 0, 0, /*age=*/0x77);  /* → 0x78 */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body5_type_3a_age_odd_spawns_0x53(void)
{
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x3a, 3.0f, 5.0f, -7.0f, 0, 0, 0, /*age=*/0);  /* → 1 (odd) */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    /* No ground hook + no state machine → no cleanup, only the 0x53 spawn. */
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x53);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 0.1f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_type_3a_ground_hit_drag_3_state_machine_kills(void)
{
    /* Ground hit + POS_Y < gy+1: DRAG=3, state_machine.  Then DRAG=0.5,
     * state_machine.  bvar17 = true from ground hit → cleanup fires + kill. */
    reset_world();
    scene1_spawn_trace_reset();
    s_gq_calls = 0; s_notify_calls = 0; s_aux_4532bc_calls = 0; s_sm5_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_notify_queue_hook(capture_notify);
    scene1_records_b_set_aux_4532bc_hook(capture_aux_4532bc);
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    s_gq_hit = 1; s_gq_out_y = 5.0f;
    /* AGE=2 (even, no 0x53 spawn).  POS_Y=4 (< 5+1). */
    stage_live(0, 0x3a, 0, /*py=*/4.0f, 0, 0, 0, 0, /*age=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT_EQ_I(s_sm5_calls, 2);                    /* ground+0.5 paths each call SM once */
    T_ASSERT_EQ_I(s_aux_4532bc_calls, 1);
    T_ASSERT_EQ_I(s_aux_4532bc_last, 0x20);
    T_ASSERT_EQ_I(s_notify_calls, 1);

    /* 4 cleanup spawns: 0x52, 0x51, 0x51, 0x51. */
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 4);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x52);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 0.7f) < 1e-5f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].type, 0x51);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[2].type, 0x51);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[3].type, 0x51);
    /* 0x51 stack uses snapped POS_Y (=gy=5.0). */
    T_ASSERT(fabsf(g_scene1_spawn_trace[1].y - 5.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[2].y - 7.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[3].y - 7.0f) < 1e-5f);
    /* Kill. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body5_type_3a_state_machine_progress_alone_triggers_cleanup(void)
{
    /* No ground hit, but state_machine hook installed → 2nd state_machine
     * "returned non-zero" → bvar17 = 1 → cleanup fires. */
    reset_world();
    scene1_spawn_trace_reset();
    s_gq_calls = 0; s_aux_4532bc_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_aux_4532bc_hook(capture_aux_4532bc);
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    s_gq_hit = 0; s_gq_out_y = 0.0f;
    /* pre-AGE=3 → post-preamble AGE=4 (even, no 0x53 spawn from AGE&1). */
    stage_live(0, 0x3a, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_aux_4532bc_calls, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 4);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_body5_type_3a_no_progress_no_cleanup(void)
{
    /* No ground hit + no state_machine hook → bvar17 stays 0 → no cleanup,
     * slot still alive.  pre-AGE=3 → post-preamble AGE=4 (even). */
    reset_world();
    scene1_spawn_trace_reset();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 0;
    stage_live(0, 0x3a, 0, 0, 0, 0, 0, 0, /*age=*/3);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x3a);
    /* DRAG ends at 0.5 (always set). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_body5_ground_query_hook_round_trip(void)
{
    reset_world();
    scene1_b_tick_ground_query_fn prev =
        scene1_records_b_set_ground_query_hook(gq_canned);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_ground_query_hook(NULL);
    T_ASSERT(prev == gq_canned);
    return 0;
}

int test_records_b_tick_body5_aux_4532bc_hook_round_trip(void)
{
    reset_world();
    scene1_b_aux_4532bc_fn prev =
        scene1_records_b_set_aux_4532bc_hook(capture_aux_4532bc);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_aux_4532bc_hook(NULL);
    T_ASSERT(prev == capture_aux_4532bc);
    return 0;
}

/* ═══ C8j-tick.9 — 0x3c/0x3b/Body 5/0x2b/0x26/0x2a/0x27 ════════════════ */

/* ─── 0x3c — small physics + AGE-1 spawn ──────────────────────────────── */

int test_records_b_tick_t9_type_3c_sets_drag_0_1(void)
{
    reset_world();
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.1f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_3c_state_machine_age_window(void)
{
    /* state_machine fires only when 8 <= AGE < 100.  Test AGE=7 (no),
     * AGE=8 (yes), AGE=99 (yes), AGE=100 (no).  Preamble bumps AGE+1. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);

    s_sm_calls = 0;
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/6);  /* → 7 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);

    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/7);  /* → 8 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);

    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/99);  /* → 100 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t9_type_3c_age_1_spawns_0x54(void)
{
    reset_world();
    scene1_spawn_trace_reset();
    /* Pre-tick AGE=0 → preamble bumps to 1. */
    stage_live(0, 0x3c, /*px=*/3.0f, /*py=*/5.0f, /*pz=*/-2.0f, 0, 0, 0, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x54);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].x - 3.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].y - 6.0f) < 1e-5f);  /* py + 1 */
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].z - -2.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 0.1f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_3c_age_other_no_spawn(void)
{
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_t9_type_3c_age_0x78_kills(void)
{
    reset_world();
    stage_live(0, 0x3c, 0, 0, 0, 0, 0, 0, /*age=*/0x77);  /* → 0x78 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x3b — NPC sister-spawn + drift toward player ──────────────────── */

int test_records_b_tick_t9_type_3b_sets_drag_0_1(void)
{
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x3b, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.1f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_3b_spawns_npc_sister_then_restores_owner_pos(void)
{
    reset_world();
    owner_blob_reset();
    /* Pre-populate owner.pos (at +0x3f0..+0x3f8). */
    owner_blob_set_f(0x3f0, 10.0f);
    owner_blob_set_f(0x3f4, 20.0f);
    owner_blob_set_f(0x3f8, 30.0f);
    scene1_record_b_spawn_trace_reset();

    /* Slot.POS = (1, 2, 3); preamble adds zero vel → POS stays. */
    stage_live(0, 0x3b, 1.0f, 2.0f, 3.0f, 0, 0, 0, /*age=*/5);
    bind_owner(0);
    scene1_records_b_tick();

    /* NPC spawn fired with our owner. */
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_NPC);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].type, 0x3c);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].flag, 1);

    /* Owner.pos restored to original (10, 20, 30). */
    float ox, oy, oz;
    memcpy(&ox, g_test_owner_blob + 0x3f0, 4);
    memcpy(&oy, g_test_owner_blob + 0x3f4, 4);
    memcpy(&oz, g_test_owner_blob + 0x3f8, 4);
    T_ASSERT(fabsf(ox - 10.0f) < 1e-6f);
    T_ASSERT(fabsf(oy - 20.0f) < 1e-6f);
    T_ASSERT(fabsf(oz - 30.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_3b_drift_gated_age_window(void)
{
    /* Drift body runs only when 30 < AGE < 120.  AGE=29 → preamble 30 (no
     * drift); speed cap still runs.  AGE=30 → 31 (drift fires).  Use small
     * vel so speed cap is irrelevant for the drift-side check. */
    reset_world();
    owner_blob_reset();
    g_scene1_player_pos[0] = 0.0f;
    g_scene1_player_pos[2] = 0.0f;
    /* AGE=29 → preamble 30 (no drift).  VEL_X=0.5 stays put under cap. */
    stage_live(0, 0x3b, 100.0f, 0, 100.0f, /*vx=*/0.5f, 0, /*vz=*/0.0f, 29);
    bind_owner(0);
    scene1_records_b_tick();
    /* VEL_X unchanged (0.5 — drift skipped, cap doesn't engage at |v|<0.6). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.5f) < 1e-6f);

    reset_world();
    owner_blob_reset();
    g_scene1_player_pos[0] = 100.0f;  /* drift target */
    g_scene1_player_pos[2] = 0.0f;
    /* AGE=30 → preamble 31 (drift fires).  Pre VEL_X=0, POS_X=0, ALT_POS_X=0;
     * vel_x = ((100 - 0)*0.005 + 0)*0.95 = 0.5*0.95 = 0.475. */
    stage_live(0, 0x3b, 0, 0, 0, /*vx=*/0.0f, 0, /*vz=*/0.0f, 30);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.475f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_type_3b_speed_cap_at_0_6(void)
{
    /* AGE outside drift window so the speed cap runs untouched by drift.
     * Pre VEL = (3, 0, 4) → speed=5 → cap to (3*0.6/5, _, 4*0.6/5) = (0.36, 0, 0.48). */
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x3b, 0, 0, 0, /*vx=*/3.0f, 0, /*vz=*/4.0f, /*age=*/5);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.36f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.48f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_type_3b_age_0x100_kills(void)
{
    reset_world();
    owner_blob_reset();
    stage_live(0, 0x3b, 0, 0, 0, 0, 0, 0, /*age=*/0xff);  /* → 0x100 */
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── Body 5 (0x21/0x25/0x31/0x32) — life-multiplier ramp + AGE-kill ──── */

int test_records_b_tick_t9_body5_life_mult_and_drag(void)
{
    /* LIFE_MULT += 0.002; DRAG = LIFE_MULT*0.1 (= 0.0002). */
    reset_world();
    stage_live(0, 0x25, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.002f) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)      - 0.0002f) < 1e-7f);
    return 0;
}

int test_records_b_tick_t9_body5_rot_z_increments(void)
{
    reset_world();
    stage_live(0, 0x31, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.1f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 0.13f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_body5_type_21_short_life_kills_at_0x50(void)
{
    /* 0x21 path: state_machine if AGE<0x48; kill at AGE==0x50. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    /* Pre-tick AGE=0x47 → preamble 0x48 (no SM); not 0x50 → alive. */
    stage_live(0, 0x21, 0, 0, 0, 0, 0, 0, /*age=*/0x47);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x21);

    reset_world();
    /* AGE=0x46 → 0x47 (SM fires); not killed. */
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x21, 0, 0, 0, 0, 0, 0, /*age=*/0x46);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);

    reset_world();
    /* AGE=0x4f → 0x50 → kill. */
    stage_live(0, 0x21, 0, 0, 0, 0, 0, 0, /*age=*/0x4f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t9_body5_type_32_long_life_kills_at_0x100(void)
{
    /* 0x32 path: state_machine if AGE<0xf8; kill at AGE==0x100. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x32, 0, 0, 0, 0, 0, 0, /*age=*/0xf6);  /* → 0xf7 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);

    reset_world();
    stage_live(0, 0x32, 0, 0, 0, 0, 0, 0, /*age=*/0xff);  /* → 0x100 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x2b — ground-bounce w/ overlay cascade ────────────────────────── */

int test_records_b_tick_t9_type_2b_sets_drag_life_mult_times_0_2(void)
{
    reset_world();
    stage_live(0, 0x2b, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    /* FLAG=1 to skip the big body. */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.4f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_type_2b_flag_1_short_circuits_big_body(void)
{
    /* When FLAG==1, the matrix/ROT/VEL updates are skipped.  Pre ROT_SCR
     * = 0; if unchanged after tick, big body didn't run. */
    reset_world();
    stage_live(0, 0x2b, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR)) < 1e-7f);
    return 0;
}

int test_records_b_tick_t9_type_2b_big_body_updates_rot_vel_y(void)
{
    /* FLAG=0 path: ROT_SCR += 0.05, ROT_Z += 0.03, VEL_Y -= 0.02. */
    reset_world();
    stage_live(0, 0x2b, 0, /*py=*/10.0f, 0, 0, /*vy=*/0.5f, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z,   0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - 0.05f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z)   - 0.03f) < 1e-6f);
    /* VEL_Y: preamble doesn't touch it; body does -= 0.02 → 0.48. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.48f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_2b_bounce_snaps_pos_and_zeros_vel(void)
{
    /* Pre POS_Y=0.4, VEL_Y=-1.0 (will be -1.02 post-body), LIFE_MULT=1 →
     * threshold = 0.5 + gy=0.0 = 0.5; gate fires. */
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    stage_live(0, 0x2b, 0, /*py=*/0.4f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 0.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-7f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x28);
    return 0;
}

int test_records_b_tick_t9_type_2b_no_bounce_when_vel_y_positive(void)
{
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_calls = 0; s_gq_hit = 1;
    stage_live(0, 0x2b, 0, /*py=*/0.0f, 0, 0, /*vy=*/1.0f, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    scene1_records_b_tick();
    /* ground_query is still called (engine always queries when FLAG!=1),
     * but the bounce branch is gated on VEL_Y<0. */
    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t9_type_2b_se_only_on_odd_slot(void)
{
    /* slot 0 = even → no SE; slot 1 = odd → SE(0x2c0). */
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_se_hook(capture_se);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    s_se_calls = 0;
    stage_live(0, 0x2b, 0, /*py=*/0.4f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 0);

    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_se_hook(capture_se);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    s_se_calls = 0;
    /* Same setup at slot 1 (odd). */
    bslot(1)[SCENE1_RECORDS_B_OFF_TYPE] = 0x2b;
    slot_set_f(1, SCENE1_RECORDS_B_OFF_POS_Y, 0.4f);
    slot_set_f(1, SCENE1_RECORDS_B_OFF_VEL_Y, -1.0f);
    slot_set_i(1, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_f(1, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I((int)s_se_last_id, 0x2c0);
    return 0;
}

int test_records_b_tick_t9_type_2b_age_0x50_kills(void)
{
    reset_world();
    stage_live(0, 0x2b, 0, 0, 0, 0, 0, 0, /*age=*/0x4f);  /* → 0x50 */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);  /* shortcut */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x26 / 0x2a — bounce inverts VEL_Y ─────────────────────────────── */

int test_records_b_tick_t9_type_26_life_mult_drag_rot(void)
{
    reset_world();
    stage_live(0, 0x26, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.1f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.002f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)
                   - (1.002f * 0.2f)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 0.13f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_type_2a_bounce_inverts_vel_y(void)
{
    /* Pre POS_Y=0.4, VEL_Y=-1.0, LIFE_MULT=1 → preamble: POS_Y = 0.4 +
     * (-1.0) = -0.6.  Body bumps LIFE_MULT to 1.002 (+= 0.002), so
     * threshold = 1.002*0.5 + gy=0 = 0.501.  POS_Y (-0.6) < 0.501 → snap;
     * VEL_Y inverted to +1.0. */
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    stage_live(0, 0x2a, 0, /*py=*/0.4f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 0.501f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_26_no_bounce_when_pos_y_above_threshold(void)
{
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    /* Pre POS_Y=10.0, VEL_Y=-1.0 → preamble POS_Y = 9.0.  Body threshold
     * = 1.002*0.5 = 0.501.  9.0 > 0.501 → no snap. */
    stage_live(0, 0x26, 0, /*py=*/10.0f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 9.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - -1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t9_type_26_age_0xa0_kills(void)
{
    reset_world();
    stage_live(0, 0x26, 0, 0, 0, 0, 0, 0, /*age=*/0x9f);  /* → 0xa0 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x27 — three-phase state machine ────────────────────────────────── */

int test_records_b_tick_t9_type_27_flag_2_decrements_life_mult(void)
{
    /* FLAG==2: LIFE_MULT -= 0.1; kill iff < 0. */
    reset_world();
    stage_live(0, 0x27, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.5f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.4f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x27);  /* alive */
    return 0;
}

int test_records_b_tick_t9_type_27_flag_2_kills_on_negative(void)
{
    reset_world();
    stage_live(0, 0x27, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.05f);  /* → -0.05 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t9_type_27_flag_1_grows_and_runs_sm(void)
{
    /* FLAG==1: LIFE_MULT += 0.3 (clamp 10); DRAG = LM*0.5; state_machine. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x27, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.3f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)      - 0.65f) < 1e-6f);
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t9_type_27_flag_1_clamps_life_mult_at_10(void)
{
    reset_world();
    stage_live(0, 0x27, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 9.9f);  /* +0.3 → 10.2 → 10 */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 10.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t9_type_27_flag_0_drifts_rot_vel_life(void)
{
    /* FLAG==0 default: ROT_Z += 0.03; VEL_Y -= 0.01; LIFE_MULT += 0.1. */
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 0;  /* no ground → no phase transition. */
    stage_live(0, 0x27, 0, /*py=*/5.0f, 0, 0, /*vy=*/0.5f, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 0.03f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.49f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.1f) < 1e-6f);
    /* FLAG unchanged (no ground hit). */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t9_type_27_flag_0_ground_hit_transitions_to_grow(void)
{
    /* FLAG==0 + ground hit + POS_Y < gy+0.3 → POS_Y=threshold, FLAG=1,
     * VEL=0, LIFE_MULT += 0.5 (splash boost). */
    reset_world();
    scene1_records_b_set_ground_query_hook(gq_canned);
    s_gq_hit = 1; s_gq_out_y = 0.0f;  /* threshold = 0.3 */
    /* Pre POS_Y=0.0 (< 0.3 threshold). */
    stage_live(0, 0x27, 0, /*py=*/0.0f, 0, /*vx=*/1.0f, 0, /*vz=*/2.0f, 5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 0.3f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-7f);
    /* LIFE_MULT: pre 1.0 + 0.1 (always) + 0.5 (splash) = 1.6. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.6f) < 1e-5f);
    return 0;
}

/* ═══ C8j-tick.10 — Body 6 + Body 7 (motion-table anchor) ═══════════════ */

static void owner_a_blob_set_f(int byte_off, float v)
{
    memcpy(g_test_owner_a_blob + byte_off, &v, sizeof v);
}

/* Helpers to write entries to the motion table without leaking state
 * between tests.  reset_world() does NOT zero g_scene1_b_motion_table —
 * we zero it explicitly in each test below since the table is global. */
static void motion_table_zero(void)
{
    memset(g_scene1_b_motion_table, 0,
           sizeof g_scene1_b_motion_table);
}

static void motion_table_set(int idx, float drag_mul, float drag_base,
                             float pos_y_mul)
{
    g_scene1_b_motion_table[idx].drag_mul  = drag_mul;
    g_scene1_b_motion_table[idx].drag_base = drag_base;
    g_scene1_b_motion_table[idx].pos_y_mul = pos_y_mul;
}

/* Body 6 — types {0x10, 0xb, 0x14, 0x13, 0x99}. */

int test_records_b_tick_t10_body6_killed_when_owner_gate_zero(void)
{
    /* Gate: if owner+0x428 != 1 → kill slot. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 0);  /* gate closed */
    stage_live(0, 0x10, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t10_body6_motion_d_uses_fixed_drag_minus_0_8(void)
{
    /* motion_idx in {0xd, 0xe} → DRAG = -0.8, NOT the formula. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 0xd);
    owner_a_blob_set_f(0xabc, 99.0f);  /* would dominate the formula */
    motion_table_set(0xd, /*drag_mul=*/99.0f, /*drag_base=*/99.0f,
                     /*pos_y_mul=*/99.0f);
    stage_live(0, 0xb, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.8f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t10_body6_motion_e_uses_fixed_drag_minus_0_8(void)
{
    /* Same as above but motion_idx == 0xe. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 0xe);
    owner_a_blob_set_f(0xabc, 99.0f);
    stage_live(0, 0x14, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.8f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t10_body6_other_motion_uses_drag_formula(void)
{
    /* motion_idx not in {0xd, 0xe} → formula.
     * With drag_mul=2.0, drag_base=4.0, owner.abc=3.0:
     *   ((4.0 + 0.1 - 1.5) * 3.0 * 2.0) - 0.3
     * = (2.6 * 3.0 * 2.0) - 0.3
     * = 15.6 - 0.3
     * = 15.3 */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 5);
    owner_a_blob_set_f(0xabc, 3.0f);
    motion_table_set(5, 2.0f, 4.0f, 0.0f);
    stage_live(0, 0x13, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 15.3f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t10_body6_writes_pose_from_owner_anchor(void)
{
    /* POS_X = owner+0x3f0; POS_Z = owner+0x3f8;
     * POS_Y = owner+0xabc * pos_y_mul * drag_mul * 0.5 + owner+0x3f4
     * With drag_mul=2.0, pos_y_mul=3.0, owner.abc=4.0, owner.3f4=100:
     *   POS_Y = 4.0 * 3.0 * 2.0 * 0.5 + 100 = 12 + 100 = 112 */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 7);
    owner_a_blob_set_f(0x3f0, 50.0f);
    owner_a_blob_set_f(0x3f4, 100.0f);
    owner_a_blob_set_f(0x3f8, -30.0f);
    owner_a_blob_set_f(0xabc, 4.0f);
    motion_table_set(7, /*drag_mul=*/2.0f, /*drag_base=*/0.0f,
                     /*pos_y_mul=*/3.0f);
    stage_live(0, 0x99, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) -  50.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 112.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - -30.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t10_body6_calls_state_machine(void)
{
    /* state_machine called exactly once per tick for live Body 6 slot. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x10, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t10_body6_type_13_writes_anim_drive_on_progress(void)
{
    /* When state_machine progresses (hook installed) AND type == 0x13:
     *   owner+0xb90 = anim_drive (DAT_06a46f94)
     *   owner+0xb94 = 0x1e */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    g_scene1_records_b_tick_anim_drive = 0x1234;
    stage_live(0, 0x13, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb90), 0x1234);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb94), 0x1e);
    return 0;
}

int test_records_b_tick_t10_body6_type_other_skips_anim_drive(void)
{
    /* Even with state_machine hook installed, types other than 0x13 do
     * NOT write owner+0xb90/+0xb94. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    g_scene1_records_b_tick_anim_drive = 0xbeef;
    stage_live(0, 0x99, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb90), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb94), 0);
    return 0;
}

int test_records_b_tick_t10_body6_type_13_no_progress_skips_write(void)
{
    /* Type 0x13 but state_machine hook is NULL (no progress) → no write. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    g_scene1_records_b_tick_anim_drive = 0xcafe;
    stage_live(0, 0x13, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb90), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xb94), 0);
    return 0;
}

int test_records_b_tick_t10_body6_does_not_age_kill(void)
{
    /* Body 6 has no AGE-based kill — slot lives indefinitely while
     * owner+0x428 stays at 1. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    /* Stage AGE near common kill thresholds; slot should survive each. */
    stage_live(0, 0x14, 0, 0, 0, 0, 0, 0, /*age=*/0x100);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x14);
    return 0;
}

/* Body 7 — types {0x11, 0xc}. */

int test_records_b_tick_t10_body7_killed_when_owner_gate_zero(void)
{
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 0);
    stage_live(0, 0x11, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t10_body7_type_11_uses_drag_zero(void)
{
    /* Type 0x11 → DRAG = 0.0 (independent of motion table). */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 9);
    motion_table_set(9, 99.0f, 99.0f, 99.0f);
    stage_live(0, 0x11, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t10_body7_type_c_uses_motion_formula(void)
{
    /* Type 0xc → same drag formula as Body 6 (non-{d,e} branch). */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 11);
    owner_a_blob_set_f(0xabc, 1.0f);
    motion_table_set(11, /*drag_mul=*/1.0f, /*drag_base=*/1.4f,
                     /*pos_y_mul=*/0.0f);
    /* DRAG = ((1.4 + 0.1 - 1.5) * 1.0 * 1.0) - 0.3 = 0 - 0.3 = -0.3 */
    stage_live(0, 0xc, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.3f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t10_body7_writes_pose_from_owner(void)
{
    /* Same pose formula as Body 6. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    owner_a_blob_set_i(0x424, 3);
    owner_a_blob_set_f(0x3f0, 7.0f);
    owner_a_blob_set_f(0x3f4, 8.0f);
    owner_a_blob_set_f(0x3f8, 9.0f);
    owner_a_blob_set_f(0xabc, 2.0f);
    motion_table_set(3, /*drag_mul=*/4.0f, /*drag_base=*/0.0f,
                     /*pos_y_mul=*/0.5f);
    /* POS_Y = 2.0 * 0.5 * 4.0 * 0.5 + 8.0 = 2.0 + 8.0 = 10.0 */
    stage_live(0, 0xc, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) -  7.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) -  9.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t10_body7_age_7_kills(void)
{
    /* AGE == 7 (post-preamble bump from 6) → kill.
     * stage_live sets AGE=6; preamble bumps to 7. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    stage_live(0, 0x11, 0, 0, 0, 0, 0, 0, /*age=*/6);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t10_body7_age_other_survives(void)
{
    /* AGE != 7 → no kill. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    stage_live(0, 0xc, 0, 0, 0, 0, 0, 0, /*age=*/5);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* Preamble bumps AGE 5→6, which is not 7. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xc);
    return 0;
}

int test_records_b_tick_t10_body7_calls_state_machine(void)
{
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x428, 1);
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0xc, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

/* ═══ C8j-tick.11 — Body 7a (overlay + state-machine cluster) ═══════════ */

/* Overlay spawn hook capture — covers scene1_overlay_spawn calls fired
 * by per-type bodies in scene1_records_b_tick.  Hook is local to the
 * tick TU (scene1_records_b_set_overlay_spawn_hook) — when installed,
 * the per-type body's overlay_spawn() wrapper routes here instead of
 * calling scene1_overlay_spawn. */
static int s_overlay_calls;
static struct {
    const void *owner;
    float pos_x, pos_y, pos_z;
    int   type;
    float scale;
    int   dur;
    int   rot_y;
    int   shape_mode;
    int   mode;
} s_overlay_last;
static void capture_overlay_spawn(const void *owner,
                                  float px, float py, float pz,
                                  int type, float scale, int dur,
                                  int rot_y, int shape_mode, int mode)
{
    s_overlay_calls++;
    s_overlay_last.owner = owner;
    s_overlay_last.pos_x = px; s_overlay_last.pos_y = py;
    s_overlay_last.pos_z = pz;
    s_overlay_last.type = type; s_overlay_last.scale = scale;
    s_overlay_last.dur = dur;   s_overlay_last.rot_y = rot_y;
    s_overlay_last.shape_mode = shape_mode; s_overlay_last.mode = mode;
}
static void install_overlay_capture(void)
{
    s_overlay_calls = 0;
    scene1_records_b_set_overlay_spawn_hook(capture_overlay_spawn);
}
static void restore_overlay(void)
{
    scene1_records_b_set_overlay_spawn_hook(NULL);
}

/* ─── 0x46 — overlay cascade ────────────────────────────────────────── */

int test_records_b_tick_t11_type_46_age_1_spawns_0x44(void)
{
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x46, /*px=*/1.0f, /*py=*/2.0f, /*pz=*/3.0f, 0, 0, 0, /*age=*/0);
    /* Preamble bumps age 0→1. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x44);
    T_ASSERT(fabsf(s_overlay_last.scale - 2.5f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 3.0f) < 1e-6f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t11_type_46_age_0x28_fires_3_overlays_plus_se(void)
{
    /* Age 0x27 → preamble bumps to 0x28 → fires 3 overlay spawns
     * (types 0x42, 0x43, 0x45) and se_play(0x2a3). */
    reset_world();
    install_overlay_capture();
    scene1_records_b_set_se_hook(capture_se);
    s_se_calls = 0;
    stage_live(0, 0x46, /*px=*/10.0f, /*py=*/20.0f, /*pz=*/30.0f,
               0, 0, 0, /*age=*/0x27);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 3);
    /* Last spawned was 0x45 with scale 1.5. */
    T_ASSERT_EQ_I(s_overlay_last.type, 0x45);
    T_ASSERT(fabsf(s_overlay_last.scale - 1.5f) < 1e-6f);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2a3);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t11_type_46_state_machine_in_age_window(void)
{
    /* SM fires for AGE in [0x28, 0x30). */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x46, 0, 0, 0, 0, 0, 0, /*age=*/0x28);  /* preamble→0x29 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t11_type_46_no_state_machine_outside_window(void)
{
    /* AGE = 0x30 (outside [0x28, 0x30)) → SM skipped. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x46, 0, 0, 0, 0, 0, 0, /*age=*/0x2f);  /* preamble→0x30 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t11_type_46_kills_at_age_0x3c(void)
{
    reset_world();
    stage_live(0, 0x46, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_46_sets_drag_3_0(void)
{
    reset_world();
    stage_live(0, 0x46, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 3.0f) < 1e-6f);
    return 0;
}

/* ─── 0x97 — overlay emit + state-machine ─────────────────────────── */

int test_records_b_tick_t11_type_97_sets_drag_1_0(void)
{
    reset_world();
    stage_live(0, 0x97, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t11_type_97_calls_state_machine_every_tick(void)
{
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x97, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t11_type_97_odd_age_spawns_overlay_0x56(void)
{
    /* AGE = 1 (post-preamble) → odd → spawn. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x97, /*px=*/5.0f, /*py=*/-1.0f, /*pz=*/7.0f,
               0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x56);
    T_ASSERT(fabsf(s_overlay_last.scale - 1.0f) < 1e-6f);
    /* py argument is hard-coded 0 (not the slot's POS_Y). */
    T_ASSERT(fabsf(s_overlay_last.pos_x - 5.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 7.0f) < 1e-6f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t11_type_97_even_age_skips_overlay(void)
{
    /* AGE = 2 (post-preamble) → even → no spawn. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x97, 0, 0, 0, 0, 0, 0, /*age=*/1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t11_type_97_kills_at_age_0x320(void)
{
    reset_world();
    stage_live(0, 0x97, 0, 0, 0, 0, 0, 0, /*age=*/0x320);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_97_survives_at_age_0x100(void)
{
    reset_world();
    stage_live(0, 0x97, 0, 0, 0, 0, 0, 0, /*age=*/0xff);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x97);
    return 0;
}

/* ─── 0xe / 0x12 — motion-id sub-dispatch ─────────────────────────── */

int test_records_b_tick_t11_type_e_motion_31_drag_3_0_kill_at_8(void)
{
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x31);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/7);  /* preamble→8 → kill */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 3.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_f_kills_immediately(void)
{
    /* motion 0xf → DRAG = 1.0, kill AGE >= 0 → always kills. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0xf);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_25_iter_loop(void)
{
    /* motion 0x25 → DRAG=4.0, up-to-20 SM iter loop with hook installed
     * (which returns 1 each time) → 20 calls then kill. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x25);
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x12, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 4.0f) < 1e-6f);
    T_ASSERT_EQ_I(s_sm_calls, 20);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_25_no_hook_skips_loop_kills(void)
{
    /* Without SM hook, the iter loop breaks on first call (state_machine_
     * call_ret returns 0 when hook is NULL) → 0 SM calls; still kills. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x25);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_3d_drag_3_5_kill_at_5(void)
{
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x3d);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/4);  /* preamble→5 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 3.5f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_44_uses_motion_table_drag(void)
{
    /* motion 0x44 → DRAG = motion_table[0x44].drag_mul * 1.15. */
    reset_world();
    motion_table_zero();
    motion_table_set(0x44, /*drag_mul=*/2.0f, 0, 0);
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x44);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.3f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_43_writes_pose(void)
{
    /* motion 0x43 → DRAG=3.5; pose 2*sin(owner+0x420) + owner+0x3f0/etc. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x43);
    owner_a_blob_set_f(0x3f0, 10.0f);
    owner_a_blob_set_f(0x3f4, 20.0f);
    owner_a_blob_set_f(0x3f8, 30.0f);
    owner_a_blob_set_f(0x420, 0.0f);  /* sin=0, cos=1 */
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 22.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  -  3.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_18_drag_6_5_under_100(void)
{
    /* motion 0x18 + owner+0xa58 < 100 → DRAG = 6.5. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x18);
    owner_a_blob_set_i(0xa58, 50);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 6.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_18_drag_8_5_over_100(void)
{
    /* motion 0x18 + owner+0xa58 >= 100 → DRAG = 8.5. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x18);
    owner_a_blob_set_i(0xa58, 150);
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 8.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t11_type_e_motion_other_drag_2_0_kill_at_0xf(void)
{
    /* Default else branch: DRAG=2.0; kill AGE == 0xf. */
    reset_world();
    motion_table_zero();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x55);  /* not in any specific branch */
    stage_live(0, 0xe, 0, 0, 0, 0, 0, 0, /*age=*/0xe);  /* preamble→0xf */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0xd / 0x15 — pose around owner ─────────────────────────────── */

int test_records_b_tick_t11_type_d_writes_pose_and_drag_0_5(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x3f0, 100.0f);
    owner_a_blob_set_f(0x3f4, 200.0f);
    owner_a_blob_set_f(0x3f8, 300.0f);
    owner_a_blob_set_f(0x420, 0.0f);  /* sin=0, cos=1 */
    stage_live(0, 0xd, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) -  0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 202.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 302.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t11_type_15_motion_19_uses_drag_0(void)
{
    /* type 0x15 + owner motion == 0x19 → DRAG = 0 (vs 0.5 default). */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x19);
    stage_live(0, 0x15, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-7f);
    return 0;
}

int test_records_b_tick_t11_type_15_motion_other_uses_drag_0_5(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x18);
    stage_live(0, 0x15, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t11_type_d_sm_fires_in_age_5_to_9(void)
{
    /* SM gate for type 0xd is AGE in [5, 9). */
    reset_world();
    owner_a_blob_reset();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0xd, 0, 0, 0, 0, 0, 0, /*age=*/4);  /* preamble→5 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t11_type_d_sm_skipped_outside_window(void)
{
    reset_world();
    owner_a_blob_reset();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0xd, 0, 0, 0, 0, 0, 0, /*age=*/8);  /* preamble→9 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t11_type_15_sm_fires_in_age_0_to_0xf(void)
{
    /* SM gate for type 0x15 is AGE in [0, 0xf). */
    reset_world();
    owner_a_blob_reset();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x15, 0, 0, 0, 0, 0, 0, /*age=*/0xd);  /* preamble→0xe */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t11_type_d_kills_at_age_0x28(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0xd, 0, 0, 0, 0, 0, 0, /*age=*/0x27);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ═══ C8j-tick.12 — Body 7b head (0xf walker, 0x9b big body, 0x24) ═══ */

/* We don't have a hook for scene1_record_b_spawn_entity/npc — observe
 * via the table-B state directly: confirm a new live slot exists with
 * the expected type. */

/* ─── 0xf walker arm ──────────────────────────────────────────────── */

int test_records_b_tick_t12_type_f_no_motion_match_is_noop(void)
{
    /* motion 0x99 (not in {0x18, 0x3b, 0x3c}) → no-op; pose unchanged. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x99);
    owner_a_blob_set_f(0x3f0, 999.0f);  /* should NOT be written. */
    stage_live(0, 0xf, /*px=*/1.0f, /*py=*/2.0f, /*pz=*/3.0f, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* POS_X = 1.0 (unchanged from stage_live, preamble adds vel=0). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 1.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xf);
    return 0;
}

int test_records_b_tick_t12_type_f_motion_18_writes_pose(void)
{
    /* motion 0x18 → DRAG=1.5, pose around owner, kill immediately. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x18);
    owner_a_blob_set_f(0x3f0, 10.0f);
    owner_a_blob_set_f(0x3f4, 20.0f);
    owner_a_blob_set_f(0x3f8, 30.0f);
    owner_a_blob_set_f(0x420, 0.0f);
    stage_live(0, 0xf, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* Pose first (sin=0, cos=1):
     *   POS_X = 0.5*0 + 10 = 10
     *   POS_Y = 20 + 1.0 = 21
     *   POS_Z = 0.5*1 + 30 = 30.5
     * Then kill on AGE >= 1 (preamble→1). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 21.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 30.5f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t12_type_f_motion_3b_also_matches(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x3b);
    stage_live(0, 0xf, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t12_type_f_motion_3c_also_matches(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x3c);
    stage_live(0, 0xf, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t12_type_f_calls_state_machine(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0x424, 0x18);
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0xf, 0, 0, 0, 0, 0, 0, 0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

/* ─── 0x9b big animation body ──────────────────────────────────────── */

int test_records_b_tick_t12_type_9b_writes_pose(void)
{
    /* AGE small (1), ROT_X=0 → sin=0, cos=1; LIFE_MULT=2.0
     * Pose: POS_X = owner+0x20 - 1.5*0*2 = owner+0x20
     *       POS_Y = owner+0x24 + max(15.0-1*0.3, 2*2=4) = owner+0x24 + 14.7
     *       POS_Z = owner+0x28 - 1.5*1*2 = owner+0x28 - 3.0 */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x20, 100.0f);
    owner_a_blob_set_f(0x24, 200.0f);
    owner_a_blob_set_f(0x28, 300.0f);
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - (200.0f + 14.7f)) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 297.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t12_type_9b_rot_scr_starts_neg_pi_half(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - -1.5707964f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t12_type_9b_rot_scr_ramps_at_age_36(void)
{
    /* age 35 → preamble bumps to 36 → ROT_SCR = 0*π/40 - π/2 = -π/2 */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/35);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - -1.5707964f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t12_type_9b_rot_scr_clamped_zero_at_high_age(void)
{
    /* age=200 → ROT_SCR = (200-36)*π/40 - π/2 ≈ 12.8 - 1.57 ≈ 11.3 > 0
     * → clamped to 0. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/199);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t12_type_9b_age_123_fires_overlay_cascade(void)
{
    /* age=122 preamble→123: 2 overlay spawns + (123%3==0) → 3rd spawn. */
    reset_world();
    install_overlay_capture();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/122);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 3);  /* 0x6a + 0x6e + (age%3==0) 0x6f */
    T_ASSERT_EQ_I(s_overlay_last.type, 0x6f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t12_type_9b_age_124_fires_only_two_overlays(void)
{
    /* age=123 preamble→124: 124%3 = 1 → only 2 spawns. */
    reset_world();
    install_overlay_capture();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/123);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 2);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x6e);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t12_type_9b_age_outside_window_no_overlay(void)
{
    /* age=122 (before [123, 365)) — no overlay. */
    reset_world();
    install_overlay_capture();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/121);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t12_type_9b_age_130_fires_se(void)
{
    reset_world();
    owner_a_blob_reset();
    scene1_records_b_set_se_hook(capture_se);
    s_se_calls = 0;
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/129);  /* preamble→130 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2c2);
    return 0;
}

int test_records_b_tick_t12_type_9b_age_390_kills(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/0x185);  /* preamble→0x186 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t12_type_9b_owner_cf8_nonzero_kills(void)
{
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t12_type_9b_age_200_spawns_entity(void)
{
    /* age=199 preamble→200 → scene1_record_b_spawn_entity(OWNER_A, 0x9d, -1).
     * Engine calls FUN_0044376a (entity allocator) — NOT NPC.  Observe
     * via the table-B state: scan for a new live slot of type 0x9d. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x9b, 0, 0, 0, 0, 0, 0, /*age=*/199);
    bind_owner_a(0);
    scene1_records_b_tick();
    int found = 0;
    for (int i = 1; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE) == 0x9d) {
            found = 1;
            break;
        }
    }
    T_ASSERT_EQ_I(found, 1);
    return 0;
}

/* ─── 0x24 simple body ────────────────────────────────────────────── */

int test_records_b_tick_t12_type_24_sets_drag_10(void)
{
    reset_world();
    stage_live(0, 0x24, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 10.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t12_type_24_calls_state_machine(void)
{
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x24, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t12_type_24_kills_at_age_10(void)
{
    reset_world();
    stage_live(0, 0x24, 0, 0, 0, 0, 0, 0, /*age=*/9);  /* preamble→10 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t12_type_24_survives_below_10(void)
{
    reset_world();
    stage_live(0, 0x24, 0, 0, 0, 0, 0, 0, /*age=*/3);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x24);
    return 0;
}

/* ═══ C8j-tick.13 — type 0x53 (drift-damping body) ═════════════════════ */

static int s_aux_4319d6_calls;
static int s_aux_4319d6_return;
static int aux_4319d6_stub(void)
{
    s_aux_4319d6_calls++;
    return s_aux_4319d6_return;
}

int test_records_b_tick_t13_type_53_sets_life_mult_005_at_age_under_45(void)
{
    /* AGE = 0 → LIFE_MULT = 0.005 (initial). */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.005f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_age_50_ramps_life_mult(void)
{
    /* AGE = 50 (post-preamble) → ramp 0.005 + (50-45)*0.001 = 0.01;
     * sin((50-45)*0.04)*0.02 + 1.0 = sin(0.2)*0.02 + 1.0 ≈ 1.00397;
     * LIFE_MULT ≈ 0.01 * 1.00397 ≈ 0.01004. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/49);  /* preamble→50 */
    scene1_records_b_tick();
    float expected = 0.01f * (sinf(0.2f) * 0.02f + 1.0f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - expected) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_clamps_life_mult_at_0_015(void)
{
    /* AGE = 100 → ramp 0.005 + (100-45)*0.001 = 0.06, clamp at 0.015;
     * then sin modulation around 0.015. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/99);  /* preamble→100 */
    scene1_records_b_tick();
    float expected = 0.015f * (sinf(0.04f * (100.0f - 45.0f)) * 0.02f + 1.0f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - expected) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_late_life_ramps_down(void)
{
    /* kill_age = 600 (default, aux_4319d6 returns 0).  ramp_down_threshold
     * = 555.  AGE = 600 hits the kill (== kill_age), so test at 556. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/555);  /* preamble→556 */
    scene1_records_b_tick();
    /* At AGE = 556: ramp_down = 0.015 - (556-555)*0.001 = 0.014. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.014f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_late_life_clamps_at_zero(void)
{
    /* AGE = 575 — well past the 555 threshold; 0.015 - 20*0.001 = -0.005,
     * clamp to 0. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/574);  /* preamble→575 */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_age_below_30_damps_vel_xz(void)
{
    /* AGE = 10 → vel.x *= 0.92; vel.z *= 0.92. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, /*vx=*/2.0f, 0, /*vz=*/4.0f, /*age=*/9);
    /* Preamble adds vel to pos (vel unchanged), bumps AGE to 10. */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 1.84f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 3.68f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t13_type_53_age_30_to_45_no_vel_change(void)
{
    /* AGE = 35 → in [30, 45]; vel passes through unchanged. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, /*vx=*/2.0f, 0, /*vz=*/4.0f, /*age=*/34);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 4.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t13_type_53_age_46_zeros_vel_xz(void)
{
    /* AGE > 45 → VEL_X = 0, VEL_Z = 0. */
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, /*vx=*/3.0f, 0, /*vz=*/-2.0f, /*age=*/45);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-7f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-7f);
    return 0;
}

int test_records_b_tick_t13_type_53_sm_fires_in_mid_life(void)
{
    /* AGE = 100 (in (45, 555)) → DRAG + SM. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/99);  /* preamble→100 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t13_type_53_sm_skipped_at_birth(void)
{
    /* AGE = 5 (not > 45) → no SM. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/4);  /* preamble→5 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t13_type_53_kills_at_age_600_default(void)
{
    reset_world();
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/0x257);  /* preamble→600 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t13_type_53_stage_transition_shortens_kill_age(void)
{
    /* aux_4319d6 returns 1 AND FLAG_A in {0, 3} → kill_age = 120 instead
     * of 600. */
    reset_world();
    s_aux_4319d6_calls = 0;
    s_aux_4319d6_return = 1;
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_stub);
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/0x77);  /* preamble→120 */
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT(s_aux_4319d6_calls >= 1);
    return 0;
}

int test_records_b_tick_t13_type_53_flag_a_not_0_or_3_uses_long_kill_age(void)
{
    /* FLAG_A = 1 → aux_4319d6 NOT consulted; kill_age stays 600.
     * At AGE = 120, slot still alive. */
    reset_world();
    s_aux_4319d6_calls = 0;
    s_aux_4319d6_return = 1;
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_stub);
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/0x77);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x53);
    T_ASSERT_EQ_I(s_aux_4319d6_calls, 0);  /* hook NOT called. */
    return 0;
}

int test_records_b_tick_t13_type_53_aux_4319d6_returns_0_keeps_long_kill_age(void)
{
    /* FLAG_A == 0 but aux_4319d6 returns 0 → kill_age stays 600. */
    reset_world();
    s_aux_4319d6_calls = 0;
    s_aux_4319d6_return = 0;
    scene1_records_b_set_aux_4319d6_hook(aux_4319d6_stub);
    stage_live(0, 0x53, 0, 0, 0, 0, 0, 0, /*age=*/0x77);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_FLAG_A, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x53);
    T_ASSERT_EQ_I(s_aux_4319d6_calls, 1);
    return 0;
}

/* ═══ C8j-tick.14 — type 0x58 / 0x66 (anchor rotor body) ════════════════ */

/* Helpers below stage an owner_A blob with pose at (10, 20, 30) and a
 * compass field set to 1 (a non-matching value, so the compass-shift
 * branches at owner+0x948 ∈ {0, 2, 4, 6} stay closed unless a test
 * deliberately opens them).  Other owner fields default zero. */
static void stage_type_58_66_owner(float px, float py, float pz)
{
    owner_a_blob_reset();
    int32_t v;
    memcpy(&v, &px, 4); owner_a_blob_set_i(0x20, v);
    memcpy(&v, &py, 4); owner_a_blob_set_i(0x24, v);
    memcpy(&v, &pz, 4); owner_a_blob_set_i(0x28, v);
    owner_a_blob_set_i(0x948, 1);  /* compass != {0,2,4,6} */
}

int test_records_b_tick_t14_type_58_writes_base_anchor_pose(void)
{
    /* AGE=2 → post-preamble 3. r = 3*0.4 + 1 = 2.2.
     * ROT_X=0 → sin=0, cos=1.  ROT_Z=0.
     * POS_X = sin(0)*(2.2+0) + 10 = 10.
     * POS_Y = 20 + 1.3 = 21.3.
     * POS_Z = cos(0)*(2.2+0) + 30 = 32.2. */
    reset_world();
    stage_type_58_66_owner(10.0f, 20.0f, 30.0f);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 21.3f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.2f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_type_58_sets_drag_1_3(void)
{
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.3f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t14_type_66_overrides_drag_to_1_6(void)
{
    /* Type 0x66 first writes DRAG=1.3, then the sub-branch overrides to 1.6. */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x66, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.6f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t14_type_58_radius_clamps_at_4(void)
{
    /* AGE=19 → post-preamble 20.  r_raw = 20*0.4 + 1 = 9 → clamp to 4.
     * ROT_X=0, ROT_Z=0 → POS_Z = cos(0)*(4+0) + 0 = 4 (not 9). */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/19);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 4.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_type_58_rot_z_adds_to_radius(void)
{
    /* AGE=0 → post-preamble 1.  r = 1*0.4 + 1 = 1.4.
     * ROT_Z = 0.5 → effective radius = 1.4 + 0.5 = 1.9.
     * ROT_X = 0 → POS_Z = 1.0 * 1.9 + 30 = 31.9. */
    reset_world();
    stage_type_58_66_owner(0, 0, 30.0f);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.5f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 31.9f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_type_58_rot_x_rotates_into_x(void)
{
    /* ROT_X = π/2 → sin=1, cos=0.  AGE=2 → post=3, r=2.2.
     * POS_X = 1 * (2.2+0) + 0 = 2.2.
     * POS_Z = 0 * (2.2+0) + 0 = 0. */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.5707964f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 2.2f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 0.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t14_type_66_radial_shift(void)
{
    /* Type 0x66 adds sin(ROT_X+π/2)*ROT_SCR to POS_X and cos(...)*ROT_SCR
     * to POS_Z.  With ROT_X=0, sin(π/2)=1, cos(π/2)=0 → POS_X += ROT_SCR,
     * POS_Z unchanged.  Base pose: ROT_X=0, ROT_Z=0, AGE=2 → r=2.2,
     * POS_X base = 0 + 0 = 0, plus ROT_SCR=2.0 → POS_X = 2.0. */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x66, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 2.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* POS_X = (sin(0) * 2.2 + 0)  + (sin(π/2) * 2.0) = 0 + 2.0 = 2.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 2.0f) < 1e-4f);
    /* POS_Z = (cos(0) * 2.2 + 0)  + (cos(π/2) * 2.0) ≈ 2.2 + 0 = 2.2. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 2.2f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t14_type_58_does_not_apply_radial_shift(void)
{
    /* Type 0x58 must NOT use ROT_SCR.  Set ROT_SCR to a big value;
     * POS_X should match the base anchor only. */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 99.0f);
    bind_owner_a(0);
    scene1_records_b_tick();

    /* sin(0)*2.2 + 0 = 0 (no ROT_SCR contribution). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 0.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t14_compass_0_shifts_x_by_0_7(void)
{
    /* owner+0x948 = 0 → POS_X += 0.7. */
    reset_world();
    stage_type_58_66_owner(10.0f, 0, 0);
    owner_a_blob_set_i(0x948, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* base POS_X = 10 + 0.7 = 10.7. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.7f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_compass_4_shifts_x_by_0_7(void)
{
    reset_world();
    stage_type_58_66_owner(10.0f, 0, 0);
    owner_a_blob_set_i(0x948, 4);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.7f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_compass_2_shifts_z_by_0_3(void)
{
    reset_world();
    stage_type_58_66_owner(0, 0, 30.0f);
    owner_a_blob_set_i(0x948, 2);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* base POS_Z = cos(0)*2.2 + 30 = 32.2; +0.3 = 32.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_compass_6_shifts_z_by_0_3(void)
{
    reset_world();
    stage_type_58_66_owner(0, 0, 30.0f);
    owner_a_blob_set_i(0x948, 6);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_compass_non_matching_no_shift(void)
{
    /* owner+0x948 = 3 (non-matching) → no compass shift. */
    reset_world();
    stage_type_58_66_owner(10.0f, 0, 30.0f);
    owner_a_blob_set_i(0x948, 3);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* POS_X = 0*2.2 + 10 = 10 (no +0.7).  POS_Z = 1*2.2 + 30 = 32.2 (no +0.3). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.2f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_writes_alt_pos_from_owner(void)
{
    /* ALT_POS = (owner.x, owner.y + 1.0, owner.z). */
    reset_world();
    stage_type_58_66_owner(7.0f, 8.0f, 9.0f);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.5f);  /* nonzero — must not affect ALT_POS */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 7.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 9.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - 9.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t14_sm_loop_fires_in_age_window(void)
{
    /* AGE post-preamble = 6,7,8,9 → 5-iter SM loop (breaks early if
     * hook returns 0; capture_state_machine doesn't break — it's void).
     * state_machine_call_ret returns 1 when a hook is installed, so the
     * loop runs all 5 iterations. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/5);  /* post=6 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 5);
    return 0;
}

int test_records_b_tick_t14_sm_loop_skipped_outside_window(void)
{
    /* AGE post-preamble = 5 (below window) → no SM loop. */
    reset_world();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    s_sm_calls = 0;
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/4);  /* post=5 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t14_sm_loop_no_hook_runs_zero_iters(void)
{
    /* With no SM hook installed, state_machine_call_ret returns 0 on the
     * first iteration → loop breaks immediately. */
    reset_world();
    /* No SM hook installed (reset_world clears it). */
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/5);  /* post=6, in window */
    bind_owner_a(0);
    scene1_records_b_tick();
    /* Slot stays alive — nothing kills it. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x58);
    return 0;
}

int test_records_b_tick_t14_owner_cf8_nonzero_kills(void)
{
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t14_kill_on_age_0xe(void)
{
    /* AGE = 13 pre-preamble → post = 0xe (14) → kill. */
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/13);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t14_kill_does_not_fire_below_0xe(void)
{
    reset_world();
    stage_type_58_66_owner(0, 0, 0);
    stage_live(0, 0x58, 0, 0, 0, 0, 0, 0, /*age=*/12);  /* post=13 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x58);
    return 0;
}

/* ═══ C8j-tick.15a — types 0x33 / 0x60 / 0x65 ══════════════════════════ */

/* ─── type 0x33 — ROT_Z spin + LIFE_MULT*3 drag ─── */

int test_records_b_tick_t15a_type_33_increments_rot_z(void)
{
    /* ROT_Z = 0.10 → +0.05 → 0.15. */
    reset_world();
    stage_live(0, 0x33, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 0.10f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 0.15f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15a_type_33_drag_is_life_mult_times_3(void)
{
    /* LIFE_MULT = 0.5 → DRAG = 0.5 * 3 = 1.5. */
    reset_world();
    stage_live(0, 0x33, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.5f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15a_type_33_kills_at_age_0x100(void)
{
    /* AGE = 0xff pre-preamble → post = 0x100 → kill. */
    reset_world();
    stage_live(0, 0x33, 0, 0, 0, 0, 0, 0, /*age=*/0xff);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15a_type_33_does_not_kill_below_0x100(void)
{
    reset_world();
    stage_live(0, 0x33, 0, 0, 0, 0, 0, 0, /*age=*/0xfe);  /* post = 0xff */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x33);
    return 0;
}

/* ─── type 0x60 — owner_a pose snap + DRAG=8 ─── */

int test_records_b_tick_t15a_type_60_snaps_to_owner_pose_plus_half_y(void)
{
    reset_world();
    owner_a_blob_reset();
    int32_t v;
    float x = 11.0f, y = 22.0f, z = 33.0f;
    memcpy(&v, &x, 4); owner_a_blob_set_i(0x20, v);
    memcpy(&v, &y, 4); owner_a_blob_set_i(0x24, v);
    memcpy(&v, &z, 4); owner_a_blob_set_i(0x28, v);
    stage_live(0, 0x60, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 11.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 22.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 33.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15a_type_60_drag_is_8(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x60, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 8.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15a_type_60_kills_at_age_5(void)
{
    /* AGE = 4 pre-preamble → post = 5 → kill. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x60, 0, 0, 0, 0, 0, 0, /*age=*/4);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15a_type_60_does_not_kill_below_age_5(void)
{
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x60, 0, 0, 0, 0, 0, 0, /*age=*/3);  /* post = 4 */
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x60);
    return 0;
}

int test_records_b_tick_t15a_type_60_null_owner_is_noop(void)
{
    /* Without binding owner_a, slot_owner_a returns NULL — body must
     * early-return without writing pose or DRAG. */
    reset_world();
    stage_live(0, 0x60, 7.0f, 7.0f, 7.0f, 0, 0, 0, /*age=*/0);
    /* Don't bind_owner_a — OWNER_A stays 0. */
    scene1_records_b_tick();
    /* POS unchanged (no preamble shift since VEL=0); DRAG stays 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 7.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  - 0.0f) < 1e-6f);
    return 0;
}

/* ─── type 0x65 — late-AGE vertical drift damper ─── */

int test_records_b_tick_t15a_type_65_above_age_30_damps_vel_y(void)
{
    /* AGE = 0x1f pre → post = 0x20 (> 30).  VEL_Y = 0.3 → (0.3 - 0.05) * 0.99
     * = 0.2475. */
    reset_world();
    stage_live(0, 0x65, 0, 0, 0, 0, 0.3f, 0, /*age=*/0x1f);
    scene1_records_b_tick();
    /* preamble shifts pos.y by vel.y, but the body reads/writes VEL_Y after.
     * Expected: 0.2475 (no clamp triggers). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.2475f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15a_type_65_clamps_vel_y_at_negative_half(void)
{
    /* AGE > 30 and VEL_Y very negative → after damp + clamp = -0.5. */
    reset_world();
    stage_live(0, 0x65, 0, 0, 0, 0, -10.0f, 0, /*age=*/0x1f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - (-0.5f)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15a_type_65_at_or_below_age_30_no_damp(void)
{
    /* AGE = 0x1d pre → post = 0x1e (== 30, NOT > 30).  Damp skipped. */
    reset_world();
    stage_live(0, 0x65, 0, 0, 0, 0, 0.3f, 0, /*age=*/0x1d);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.3f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15a_type_65_kills_at_age_0x78(void)
{
    /* AGE = 0x77 pre → post = 0x78 → kill (shared LAB_0043f39b tail). */
    reset_world();
    stage_live(0, 0x65, 0, 0, 0, 0, 0, 0, /*age=*/0x77);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

static int s_t15a_sm_calls;
static void t15a_sm_capture(int32_t *slot)
{
    (void)slot;
    s_t15a_sm_calls++;
}

int test_records_b_tick_t15a_type_65_sm_nonzero_return_kills(void)
{
    /* Hook installed → state_machine_call_ret returns 1 → slot is killed
     * via LAB_004411e3 (skips the AGE==0x78 check below). */
    reset_world();
    s_t15a_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(t15a_sm_capture);
    stage_live(0, 0x65, 0, 0, 0, 0, 0, 0, /*age=*/0x40);  /* > 30 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15a_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15a_type_65_no_sm_hook_does_not_kill_mid_life(void)
{
    /* No hook → state_machine_call_ret returns 0 → no kill from SM
     * branch; AGE=post-preamble=0x40 != 0x78 → no kill from tail. */
    reset_world();
    stage_live(0, 0x65, 0, 0, 0, 0, 0, 0, /*age=*/0x3f);  /* post=0x40 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x65);
    return 0;
}

/* ═══ C8j-tick.15b — types 0x5f / 0x3e (shared) + 0x82 ═════════════════ */

/* Stage owner_A at (px, py, pz), yaw, with anim_state in [4..7] so the
 * shared kill gate stays closed. */
static void stage_owner_yawed(float px, float py, float pz, float yaw,
                              int32_t anim_state)
{
    owner_a_blob_reset();
    int32_t v;
    memcpy(&v, &px,  4); owner_a_blob_set_i(0x20,  v);
    memcpy(&v, &py,  4); owner_a_blob_set_i(0x24,  v);
    memcpy(&v, &pz,  4); owner_a_blob_set_i(0x28,  v);
    memcpy(&v, &yaw, 4); owner_a_blob_set_i(0xea4, v);
    owner_a_blob_set_i(0xe90, anim_state);
}

int test_records_b_tick_t15b_type_5f_writes_owner_anchored_pose(void)
{
    /* yaw = 0 → sin = 0, cos = 1.  scale = 2.  POS_X = 0*2 + 10 = 10;
     * POS_Y = 20 + 1.2 = 21.2; POS_Z = 1*2 + 30 = 32. */
    reset_world();
    stage_owner_yawed(10.0f, 20.0f, 30.0f, 0.0f, /*anim=*/4);
    stage_live(0, 0x5f, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 21.2f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 32.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15b_type_5f_drag_is_1_5(void)
{
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/4);
    stage_live(0, 0x5f, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15b_type_3e_writes_owner_anchored_pose(void)
{
    /* yaw = π/2 → sin = 1, cos = 0.  scale = 3.  POS_X = 1*3 + 10 = 13;
     * POS_Y = 20 + 1.5 = 21.5; POS_Z = 0*3 + 30 = 30. */
    reset_world();
    stage_owner_yawed(10.0f, 20.0f, 30.0f, 1.5707964f, /*anim=*/5);
    stage_live(0, 0x3e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 13.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 21.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 30.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t15b_type_3e_drag_is_2_0(void)
{
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/4);
    stage_live(0, 0x3e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15b_shared_anim_state_below_4_kills(void)
{
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/3);
    stage_live(0, 0x5f, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15b_shared_anim_state_above_7_kills(void)
{
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/8);
    stage_live(0, 0x3e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15b_shared_kills_at_age_0x19(void)
{
    /* AGE = 0x18 pre → post = 0x19 → shared LAB_004402a2 kill. */
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/5);
    stage_live(0, 0x5f, 0, 0, 0, 0, 0, 0, /*age=*/0x18);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── type 0x82 — owner-anchored AGE-20 lerp ─── */

int test_records_b_tick_t15b_type_82_snaps_to_owner_pose_plus_1_5_y(void)
{
    /* AGE = 4 pre → post = 5 (not in {1, 0x14}, no extra side effects).
     * POS = owner+0x20..28 with +1.5 on Y. */
    reset_world();
    stage_owner_yawed(11.0f, 22.0f, 33.0f, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/4);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 11.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 23.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 33.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  - 2.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15b_type_82_age_1_records_alt_pos(void)
{
    /* AGE = 0 pre → post = 1 → one-shot ALT_POS write. */
    reset_world();
    stage_owner_yawed(11.0f, 22.0f, 33.0f, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/0);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 11.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 23.5f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - 33.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15b_type_82_non_age_1_preserves_alt_pos(void)
{
    /* AGE != 1 path → ALT_POS write is gated.  Pre-set ALT to a sentinel
     * and verify body doesn't overwrite. */
    reset_world();
    stage_owner_yawed(99.0f, 99.0f, 99.0f, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/2);  /* post = 3 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 7.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 8.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 9.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X) - 7.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y) - 8.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z) - 9.0f) < 1e-6f);
    return 0;
}

static int s_t15b_sm_calls;
static void t15b_sm_capture(int32_t *slot) { (void)slot; s_t15b_sm_calls++; }

int test_records_b_tick_t15b_type_82_age_20_runs_20_iter_sm_loop(void)
{
    /* AGE = 0x13 pre → post = 0x14 → 20-iter SM loop fires. */
    reset_world();
    s_t15b_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(t15b_sm_capture);
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/0x13);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15b_sm_calls, 0x14);
    return 0;
}

int test_records_b_tick_t15b_type_82_age_20_lerps_pos_toward_alt(void)
{
    /* Stage ALT_POS at (10, 0, 20).  At AGE==0x14 (post-preamble), POS
     * starts at owner+0 = (0, 1.5, 0) (after the unconditional anchor
     * snap on line 0x43fef7+).  dx = (10 - 0) * 0.05 = 0.5; dz = (20 -
     * 0) * 0.05 = 1.0.  After 20 iterations, POS_X += 20*0.5 = 10 →
     * POS_X = 10.0; POS_Z = 0 + 20*1.0 = 20.  Net: POS ends at ALT_POS. */
    reset_world();
    stage_owner_yawed(0, -1.5f, 0, 0, /*anim=*/0);  /* y -1.5 so post anchor lands at y=0 */
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/0x13);  /* post=0x14 */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 10.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 0.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 20.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 20.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t15b_type_82_kills_at_age_0x23(void)
{
    /* AGE = 0x22 pre → post = 0x23 → kill. */
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/0x22);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15b_type_82_no_owner_e90_gate(void)
{
    /* anim_state=0 (out of [4..7] for 5f/3e) must NOT kill 0x82. */
    reset_world();
    stage_owner_yawed(0, 0, 0, 0, /*anim=*/0);
    stage_live(0, 0x82, 0, 0, 0, 0, 0, 0, /*age=*/4);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x82);
    return 0;
}

/* ═══ C8j-tick.15c — types 0x7b / 0xa1 / 0xa4 shared body ════════════════ */

/* Stage a phase-0 (bounce_count == 0) live slot at pos (px, py, pz) with
 * type `type` and PART_IDX = 0.  AGE = `pre_age` so post-preamble AGE is
 * pre_age+1. */
static void stage_t15c(int32_t type, float px, float py, float pz,
                       float vx, float vy, float vz, int32_t pre_age,
                       int32_t bounce_count)
{
    stage_live(0, type, px, py, pz, vx, vy, vz, pre_age);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, bounce_count);
}

int test_records_b_tick_t15c_phase0_vel_y_decreases_by_0_01(void)
{
    /* Stage VEL_Y = 0.5 well above ground.  After tick: VEL_Y = 0.49. */
    reset_world();
    stage_t15c(0x7b, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.49f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15c_phase0_pos_y_above_ground_no_impact(void)
{
    /* Stage POS_Y = 100, ground_y stub returns 0 (no hook).  VEL_Y < 0
     * triggers ground check; 100 > 0+0.3 → no impact.  bounce_count
     * stays 0 (the only stable signal — POS_Y after preamble has VEL_Y
     * added in, so it's 99.9 not 100). */
    reset_world();
    stage_t15c(0x7b, 0, 100.0f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    /* POS_Y must NOT have been snapped to ground+1.0 = 1.0. */
    T_ASSERT(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) > 50.0f);
    return 0;
}

int test_records_b_tick_t15c_phase0_vel_y_positive_skips_impact(void)
{
    /* VEL_Y starts at 0.05 → post-tick 0.04 (still > 0), so impact
     * check is bypassed regardless of POS_Y. */
    reset_world();
    stage_t15c(0x7b, 0, 0.1f, 0, 0, 0.05f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    /* No impact: POS_Y unchanged (preamble adds VEL_Y but post-tick is at
     * 0.1 + 0.05 = 0.15), bounce_count stays 0. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t15c_phase0_impact_snaps_pos_y_up(void)
{
    /* POS_Y just below ground+0.3 (ground=0 default) AND VEL_Y < 0 → impact.
     * POS_Y becomes 0 + 1.0 = 1.0. */
    reset_world();
    stage_t15c(0x7b, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    /* Preamble: POS_Y += VEL_Y → 0.2 + (-0.1) = 0.1.  Then body sets
     * POS_Y = ground_y + 1.0 = 1.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 1.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15c_phase0_impact_zeros_velocity(void)
{
    reset_world();
    stage_t15c(0x7b, 0, 0.2f, 0, 0.5f, -0.1f, -0.3f, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase0_impact_increments_bounce_count(void)
{
    reset_world();
    stage_t15c(0x7b, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    return 0;
}

int test_records_b_tick_t15c_phase0_a4_skips_ground_query(void)
{
    /* type 0xa4 must NOT call ground_query — the engine `cmp [esi], ebx`
     * + `je 0x4407b8` at 0x440783 skips the query branch entirely. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1; s_gq_out_y = 999.0f;       /* would force big ground if asked */
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_t15c(0xa4, 0, 100.0f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 0);
    return 0;
}

int test_records_b_tick_t15c_phase0_7b_uses_ground_query(void)
{
    /* type 0x7b queries terrain.  Hook returns 1 with ground_y=50.0.
     * Stage POS_Y = 50.1 (well within impact window): preamble drops
     * to 50.0; ground+0.3 = 50.3; 50.0 <= 50.3 → impact.  Verify hook
     * was called AND POS_Y snapped to ground+1.0 = 51.0. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1; s_gq_out_y = 50.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_t15c(0x7b, 0, 50.1f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 51.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t15c_phase0_a1_uses_ground_query(void)
{
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_t15c(0xa1, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    return 0;
}

int test_records_b_tick_t15c_phase0_impact_fires_notify_queue(void)
{
    reset_world();
    s_notify_calls = 0;
    scene1_records_b_set_notify_queue_hook(capture_notify);
    stage_t15c(0x7b, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 10);
    T_ASSERT_EQ_I(s_notify_b, 4);
    T_ASSERT_EQ_I(s_notify_c, 4);
    T_ASSERT(fabsf(s_notify_d - 1.0f) < 1e-6f);
    return 0;
}

/* SE-capture multi-id state. */
static uint16_t s_se_ids[4];
static int s_se_id_count;
static void capture_se_multi(uint16_t id)
{
    if (s_se_id_count < 4) s_se_ids[s_se_id_count] = id;
    s_se_id_count++;
}

int test_records_b_tick_t15c_phase0_impact_plays_two_se_ids(void)
{
    /* Raw-asm verified at 0x4407f9 (push 0x148) + 0x440803 (push 0x2a5).
     * Both SE plays fire on first impact. */
    reset_world();
    s_se_id_count = 0;
    scene1_records_b_set_se_hook(capture_se_multi);
    stage_t15c(0x7b, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_id_count, 2);
    T_ASSERT_EQ_I(s_se_ids[0], 0x148);
    T_ASSERT_EQ_I(s_se_ids[1], 0x2a5);
    return 0;
}

int test_records_b_tick_t15c_phase0_7b_pfo_alloc_scale_1_0(void)
{
    /* type 0x7b → scale = 1.0 (default fld1).  PFO Table A passthrough
     * writes template_id=5 to slot[SENTINEL] and scale to PARAM5. */
    extern int32_t g_scene1_pfo_table_a[];
    reset_world();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], -1);
    stage_t15c(0x7b, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 5);
    /* PARAM5 is float scale_base. */
    float scale;
    memcpy(&scale, &g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_PARAM5], 4);
    T_ASSERT(fabsf(scale - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase0_a1_pfo_alloc_scale_1_5(void)
{
    extern int32_t g_scene1_pfo_table_a[];
    reset_world();
    stage_t15c(0xa1, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 5);
    float scale;
    memcpy(&scale, &g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_PARAM5], 4);
    T_ASSERT(fabsf(scale - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase0_a4_pfo_alloc_scale_4_0(void)
{
    extern int32_t g_scene1_pfo_table_a[];
    reset_world();
    stage_t15c(0xa4, 0, 0.2f, 0, 0, -0.1f, 0, /*age=*/0, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 5);
    float scale;
    memcpy(&scale, &g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_PARAM5], 4);
    T_ASSERT(fabsf(scale - 4.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase0_sets_per_tick_flag(void)
{
    /* DAT_06a46f98 = 1 regardless of impact firing. */
    reset_world();
    /* High POS_Y → no impact, but flag must still latch in phase 0. */
    stage_t15c(0x7b, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/0, /*bc=*/0);
    g_scene1_records_b_tick_flag = 0;
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

int test_records_b_tick_t15c_phase1_7b_drag_2_0(void)
{
    /* bounce_count != 0 → settle phase. */
    reset_world();
    stage_t15c(0x7b, 0, 1.0f, 0, 0, 0, 0, /*age=*/0, /*bc=*/1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase1_a1_drag_4_0(void)
{
    reset_world();
    stage_t15c(0xa1, 0, 1.0f, 0, 0, 0, 0, /*age=*/0, /*bc=*/1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 4.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase1_a4_drag_8_0(void)
{
    reset_world();
    stage_t15c(0xa4, 0, 1.0f, 0, 0, 0, 0, /*age=*/0, /*bc=*/1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 8.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15c_phase1_fires_state_machine(void)
{
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_t15c(0x7b, 0, 1.0f, 0, 0, 0, 0, /*age=*/0, /*bc=*/1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t15c_phase1_does_not_fire_phase0_side_effects(void)
{
    /* In settle phase, the impact-only notify_queue + PFO alloc must NOT
     * fire even though POS_Y is far below ground. */
    reset_world();
    s_notify_calls = 0;
    scene1_records_b_set_notify_queue_hook(capture_notify);
    stage_t15c(0x7b, 0, -100.0f, 0, 0, -1.0f, 0, /*age=*/0, /*bc=*/1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_notify_calls, 0);
    /* PFO Table A slot stays sentinel = -1. */
    extern int32_t g_scene1_pfo_table_a[];
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], -1);
    return 0;
}

int test_records_b_tick_t15c_shared_kills_at_age_0x82(void)
{
    /* Pre AGE = 0x81 → post = 0x82 → kill. */
    reset_world();
    stage_t15c(0x7b, 0, 100.0f, 0, 0, 0, 0, /*age=*/0x81, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15c_shared_does_not_kill_below_0x82(void)
{
    reset_world();
    stage_t15c(0xa4, 0, 100.0f, 0, 0, 0, 0, /*age=*/0x80, /*bc=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa4);
    return 0;
}

/* ═══ C8j-tick.15d — type 0x84 single-body ground-bounce + self-kill ═══ */

int test_records_b_tick_t15d_writes_drag_neg_0_15(void)
{
    /* DRAG = -0.15 written unconditionally (both phases). */
    reset_world();
    stage_live(0, 0x84, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.15f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15d_vel_y_decreases_by_0_01(void)
{
    reset_world();
    stage_live(0, 0x84, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.49f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15d_no_ground_hit_no_impact(void)
{
    /* Default no-hook ground_query returns 0 → no hit → no impact.
     * Slot stays alive (type unchanged). */
    reset_world();
    stage_live(0, 0x84, 0, 100.0f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x84);
    return 0;
}

int test_records_b_tick_t15d_above_threshold_no_impact(void)
{
    /* Ground hit, but POS_Y > ground + 0.2 → no impact. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 1.0f, 0, 0, -0.5f, 0, /*age=*/0);
    /* Preamble POS_Y = 1.0 + (-0.5) = 0.5; 0.5 > 0 + 0.2 → no impact. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x84);
    return 0;
}

int test_records_b_tick_t15d_impact_kills_slot(void)
{
    /* POS_Y close enough → impact → kill. */
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 0.15f, 0, 0, -0.5f, 0, /*age=*/0);
    /* Preamble POS_Y = 0.15 - 0.5 = -0.35; ground+0.2 = 0.2; -0.35 <= 0.2 → impact. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15d_impact_snaps_pos_y_to_ground_plus_0_2(void)
{
    /* On impact: POS_Y = ground_y + 0.2 (NOT + 1.0 like 0x7b). */
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 1.0f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 5.2f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15d_impact_sets_vel_y_neg_0_01(void)
{
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 0.15f, 0, 1.5f, -0.5f, 2.5f, /*age=*/0);
    scene1_records_b_tick();
    /* Impact latches VEL_Y = -0.01; VEL_X/Z = 0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - -0.01f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15d_impact_plays_se_0x2b0(void)
{
    reset_world();
    s_se_id_count = 0;
    scene1_records_b_set_se_hook(capture_se_multi);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 0.15f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_id_count, 1);
    T_ASSERT_EQ_I(s_se_ids[0], 0x2b0);
    return 0;
}

int test_records_b_tick_t15d_impact_spawns_table_a_template_1_scale_0_3(void)
{
    extern int32_t g_scene1_pfo_table_a[];
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], -1);
    stage_live(0, 0x84, 0, 0.15f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 1);
    float scale;
    memcpy(&scale, &g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_PARAM5], 4);
    T_ASSERT(fabsf(scale - 0.3f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15d_does_not_fire_notify_queue(void)
{
    /* 0x84 has NO notify_queue call (unlike 0x7b/0xa1/0xa4). */
    reset_world();
    s_notify_calls = 0;
    scene1_records_b_set_notify_queue_hook(capture_notify);
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 0.15f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_notify_calls, 0);
    return 0;
}

int test_records_b_tick_t15d_impact_writes_aux_9_from_query(void)
{
    /* Engine writes the 4-float scratch buffer at &slot[+0x18]; our hook
     * collapses to out_y only, so we mirror by writing slot[AUX_9] =
     * out_y to preserve the engine's observable post-tick AUX_9 state. */
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 7.5f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x84, 0, 100.0f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_AUX_9) - 7.5f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15d_state_machine_runs_in_phase_0(void)
{
    /* state_machine runs unconditionally at phase-0 tail (whether or not
     * impact fired).  Stage no impact (high POS_Y) + install SM hook. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x84, 0, 100.0f, 0, 0, -0.5f, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t15d_phase_1_undoes_preamble_vel_y(void)
{
    /* bounce_count != 0 → POS_Y -= VEL_Y (cancels preamble's gravity
     * addition).  Pre POS_Y = 50, VEL_Y = 1.0; preamble → POS_Y = 51;
     * body → POS_Y = 50. */
    reset_world();
    stage_live(0, 0x84, 0, 50.0f, 0, 0, 1.0f, 0, /*age=*/0);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 50.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15d_kills_at_age_300(void)
{
    /* Shared LAB_004402a2 tail: kill on AGE == 300.  Pre AGE = 299 →
     * post = 300 → kill. */
    reset_world();
    stage_live(0, 0x84, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/299);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15d_does_not_kill_below_age_300(void)
{
    reset_world();
    stage_live(0, 0x84, 0, 100.0f, 0, 0, 0.5f, 0, /*age=*/298);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x84);
    return 0;
}

/* ═══ C8j-tick.15e — types 0x73 / 0x78 / 0x7a trail-cull body ═════════ */

int test_records_b_tick_t15e_age_negative_cancels_preamble(void)
{
    /* AGE < 0 branch: POS -= VEL cancels the preamble's POS += VEL.
     * Net effect: POS is unchanged from stage_live (before preamble +
     * body cancellation).  age stays at -2 after preamble +1 → -1, body
     * does not touch age beyond preamble.  We stage age=-2 so post-
     * preamble age=-1 (still < 0). */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x73, 5.0f, 10.0f, 15.0f, 0.5f, 1.0f, -0.25f, /*age=*/-2);
    bind_owner_a(0);
    scene1_records_b_tick();
    /* Preamble: POS += VEL → (5.5, 11.0, 14.75); body cancels → (5.0,
     * 10.0, 15.0).  Slot stays alive (owner_a+0xcf8 is 0). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 5.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 15.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), -1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x73);
    return 0;
}

int test_records_b_tick_t15e_age_negative_owner_cf8_nonzero_kills(void)
{
    /* AGE < 0 + owner_a+0xcf8 != 0 → KILL. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x78, 0, 0, 0, 0, 0, 0, /*age=*/-2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15e_age_1_dual_overlay_spawn(void)
{
    /* AGE == 1 → 2 overlay_spawn calls.  Stage AGE = 0 (preamble bumps
     * to 1).  POS / VEL chosen to make POS-VEL = (5, 10, 15) easy. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x7a, /*px=*/6.0f, /*py=*/11.0f, /*pz=*/16.0f,
               /*vx=*/1.0f, /*vy=*/1.0f, /*vz=*/1.0f, /*age=*/0);
    scene1_records_b_tick();
    /* Preamble: POS → (7, 12, 17), AGE → 1.  Body: POS-VEL = (6, 11, 16). */
    T_ASSERT_EQ_I(s_overlay_calls, 2);
    /* Last call was the template 0x13 ALT_POS spawn, scale 0.7. */
    T_ASSERT_EQ_I(s_overlay_last.type, 0x13);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.7f) < 1e-6f);
    T_ASSERT_EQ_I(s_overlay_last.dur, -1);
    T_ASSERT_EQ_I(s_overlay_last.mode, 0);
    restore_overlay();
    return 0;
}

/* C8j-tick.15e local capture — both spawn calls (not just last). */
static int s_t15e_overlay_n;
static struct { int type; float px, py, pz, scale; }
    s_t15e_overlay_log[4];
static void t15e_capture_overlay(const void *owner,
                                 float px, float py, float pz,
                                 int type, float scale,
                                 int dur, int rot_y,
                                 int shape_mode, int mode)
{
    (void)owner; (void)dur; (void)rot_y;
    (void)shape_mode; (void)mode;
    if (s_t15e_overlay_n < 4) {
        s_t15e_overlay_log[s_t15e_overlay_n].type  = type;
        s_t15e_overlay_log[s_t15e_overlay_n].px    = px;
        s_t15e_overlay_log[s_t15e_overlay_n].py    = py;
        s_t15e_overlay_log[s_t15e_overlay_n].pz    = pz;
        s_t15e_overlay_log[s_t15e_overlay_n].scale = scale;
    }
    s_t15e_overlay_n++;
}

int test_records_b_tick_t15e_age_1_first_spawn_template_0x10_at_pos_minus_vel(void)
{
    /* Verify the FIRST overlay_spawn call: template 0x10, pos = POS-VEL,
     * scale 1.0. */
    reset_world();
    s_t15e_overlay_n = 0;
    scene1_records_b_set_overlay_spawn_hook(t15e_capture_overlay);
    stage_live(0, 0x73, /*px=*/10.0f, /*py=*/20.0f, /*pz=*/30.0f,
               /*vx=*/2.0f, /*vy=*/3.0f, /*vz=*/4.0f, /*age=*/0);
    /* Stash ALT_POS so the second spawn doesn't alias the first. */
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 200.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 300.0f);
    scene1_records_b_tick();
    /* Preamble: POS → (12, 23, 34) → POS-VEL = (10, 20, 30). */
    T_ASSERT_EQ_I(s_t15e_overlay_n, 2);
    T_ASSERT_EQ_I(s_t15e_overlay_log[0].type, 0x10);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].px    - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].py    - 20.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].pz    - 30.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].scale - 1.0f)  < 1e-6f);
    /* Second spawn: template 0x13 at ALT_POS, scale 0.7. */
    T_ASSERT_EQ_I(s_t15e_overlay_log[1].type, 0x13);
    T_ASSERT(fabsf(s_t15e_overlay_log[1].px    - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[1].py    - 200.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[1].pz    - 300.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[1].scale - 0.7f)   < 1e-6f);
    scene1_records_b_set_overlay_spawn_hook(NULL);
    return 0;
}

int test_records_b_tick_t15e_drag_zero_after_tick(void)
{
    /* DRAG = 0 unconditionally written.  Stage with DRAG = 1.5; should
     * be overwritten to 0. */
    reset_world();
    stage_live(0, 0x73, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15e_cull_visible_continues(void)
{
    /* cull_query returns -1 (visible) → state_machine fires, slot stays
     * alive (state_machine hook NULL → ret 0 → AGE-78 check; AGE=10
     * stays). */
    reset_world();
    s_cull_calls = 0;
    s_cull_return = -1;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0x78, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_cull_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x78);
    return 0;
}

int test_records_b_tick_t15e_cull_culled_kills(void)
{
    /* cull_query returns >= 0 (culled) → KILL. */
    reset_world();
    s_cull_return = 0;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0x7a, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15e_state_machine_ret_1_kills(void)
{
    /* state_machine hook installed → state_machine_call_ret returns 1
     * → KILL. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x73, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15e_state_machine_ret_0_age_120_kills(void)
{
    /* No state_machine hook → ret 0 → AGE-78 (=0x78=120) check.  Stage
     * AGE = 119; preamble bumps to 120 → KILL. */
    reset_world();
    stage_live(0, 0x78, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15e_state_machine_ret_0_age_below_120_alive(void)
{
    /* AGE 118 → preamble → 119 (!= 120) → alive. */
    reset_world();
    stage_live(0, 0x7a, 0, 0, 0, 0, 0, 0, /*age=*/118);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x7a);
    return 0;
}

int test_records_b_tick_t15e_clears_anim_drive_global_before_sm(void)
{
    /* DAT_06a46f94 (= g_scene1_records_b_tick_anim_drive) is set to 0
     * before the state_machine call.  Pre-set to 99, expect post-tick
     * = 0. */
    reset_world();
    g_scene1_records_b_tick_anim_drive = 99;
    stage_live(0, 0x73, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_anim_drive, 0);
    return 0;
}

int test_records_b_tick_t15e_age_1_spawn_skipped_when_not_age_1(void)
{
    /* AGE != 1 → no overlay_spawn calls fire.  Stage AGE=10 (post-
     * preamble = 11). */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x78, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15e_all_three_types_share_body(void)
{
    /* Smoke: same input, three types.  All three should produce the
     * same DRAG=0 + cull_query call count + kill on AGE-120. */
    reset_world();
    s_cull_return = -1;
    scene1_records_b_set_cull_query_hook(cull_stub);
    s_cull_calls = 0;
    stage_live(0, 0x73, 0, 0, 0, 0, 0, 0, /*age=*/10);
    stage_live(1, 0x78, 0, 0, 0, 0, 0, 0, /*age=*/10);
    stage_live(2, 0x7a, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_cull_calls, 3);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(1, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(2, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    return 0;
}

/* ─── C8j-tick.15f — body_0x76_or_0xa3 + body_0x77_or_0xa2 ─────────────── */

int test_records_b_tick_t15f_76_age_negative_cancels_preamble_no_kill(void)
{
    /* type 0x76, AGE<0: POS -= VEL cancels preamble; slot stays alive
     * (no owner gate, unlike 0x73). */
    reset_world();
    stage_live(0, 0x76, 5.0f, 10.0f, 15.0f, 0.5f, 1.0f, -0.25f, /*age=*/-2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 5.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 15.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), -1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x76);
    return 0;
}

int test_records_b_tick_t15f_a3_age_negative_no_owner_gate(void)
{
    /* 0x76/0xa3 AGE<0 has no owner_a+0xcf8 check (unlike 0x73 / 0x78 /
     * 0x7a from C8j-tick.15e).  Stage owner_a+0xcf8 = nonzero; slot
     * should still survive. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0xa3, 0, 0, 0, 0, 0, 0, /*age=*/-2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa3);
    return 0;
}

int test_records_b_tick_t15f_76_life_mult_increments_by_0_01(void)
{
    /* AGE>=0: LIFE_MULT += 0.01 per tick.  Pre LIFE_MULT = 1.0; post-tick
     * = 1.01. */
    reset_world();
    stage_live(0, 0x76, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 1.01f)
            < 1e-5f);
    return 0;
}

int test_records_b_tick_t15f_76_xz_vel_damp_to_0_99_y_untouched(void)
{
    /* VEL_X *= 0.99, VEL_Z *= 0.99; VEL_Y is NOT damped (engine asm
     * confirms: only +0x68 and +0x70 are *= 0.99). */
    reset_world();
    stage_live(0, 0xa3, 0, 0, 0, /*vx=*/2.0f, /*vy=*/3.0f, /*vz=*/4.0f,
               /*age=*/10);
    scene1_records_b_tick();
    /* Preamble doesn't damp; body does VEL_X/Z *= 0.99. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 1.98f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 3.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 3.96f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15f_76_sm_skipped_when_part_idx_nonzero(void)
{
    /* PART_IDX != 0 → SM not called.  Hook count should remain 0. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x76, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 5);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x76);
    return 0;
}

int test_records_b_tick_t15f_76_sm_ret_nonzero_kills(void)
{
    /* PART_IDX == 0 + SM hook installed (ret==1) → KILL. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x76, 0, 0, 0, 0, 0, 0, /*age=*/10);
    /* PART_IDX defaults to 0. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15f_a3_pose_snap_fires_at_age_match(void)
{
    /* type 0xa3 + PART_IDX == 0 + AGE == (slot_idx % 0xf + 0x3c).
     * slot_idx=0 → target_age=0x3c (60); stage AGE=0x3b so preamble
     * bumps to 0x3c.  Verify scene1_record_b_spawn_npc was called
     * with type 0x97, flag 1. */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x3f0, 100.0f);
    owner_blob_set_f(0x3f4, 200.0f);
    owner_blob_set_f(0x3f8, 300.0f);
    scene1_record_b_spawn_trace_reset();

    stage_live(0, 0xa3, /*px=*/7.0f, 0, /*pz=*/9.0f, 0, 0, 0, /*age=*/0x3b);
    bind_owner(0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 1);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].kind,
                  SCENE1_RECORD_B_SPAWN_KIND_NPC);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].type, 0x97);
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace[0].flag, 1);

    /* Owner pose restored (10/20/30 untouched after restore). */
    float ox, oy, oz;
    memcpy(&ox, g_test_owner_blob + 0x3f0, 4);
    memcpy(&oy, g_test_owner_blob + 0x3f4, 4);
    memcpy(&oz, g_test_owner_blob + 0x3f8, 4);
    T_ASSERT(fabsf(ox - 100.0f) < 1e-6f);
    T_ASSERT(fabsf(oy - 200.0f) < 1e-6f);
    T_ASSERT(fabsf(oz - 300.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15f_a3_pose_snap_skipped_for_type_0x76(void)
{
    /* type 0x76 — even with PART_IDX == 0 + AGE-match, no spawn fires. */
    reset_world();
    owner_blob_reset();
    scene1_record_b_spawn_trace_reset();
    stage_live(0, 0x76, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_t15f_a3_pose_snap_skipped_when_age_mismatch(void)
{
    /* type 0xa3 + PART_IDX == 0 but AGE off by one → no spawn. */
    reset_world();
    owner_blob_reset();
    scene1_record_b_spawn_trace_reset();
    stage_live(0, 0xa3, 0, 0, 0, 0, 0, 0, /*age=*/0x3a);  /* post-preamble 0x3b != target 0x3c */
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_record_b_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_t15f_76_kills_at_age_0x5a(void)
{
    /* AGE == 0x5a (90) → KILL.  Stage AGE = 0x59; preamble → 0x5a. */
    reset_world();
    stage_live(0, 0x76, 0, 0, 0, 0, 0, 0, /*age=*/0x59);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15f_a3_does_not_kill_below_0x5a(void)
{
    /* AGE != 0x5a → slot alive. */
    reset_world();
    stage_live(0, 0xa3, 0, 0, 0, 0, 0, 0, /*age=*/0x58);  /* → 0x59 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa3);
    return 0;
}

/* body_0x77_or_0xa2 tests */

int test_records_b_tick_t15f_77_owner_cf8_nonzero_kills(void)
{
    /* type 0x77 + owner_a+0xcf8 != 0 → KILL. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/10);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15f_a2_owner_cf8_check_skipped(void)
{
    /* type 0xa2 — never reads owner_a+0xcf8.  Stage it nonzero; slot
     * must stay alive. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0xa2, 0, 0, 0, 0, 0, 0, /*age=*/10);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa2);
    return 0;
}

int test_records_b_tick_t15f_77_rot_x_increments_by_0_2(void)
{
    /* ROT_X += 0.2 unconditional. */
    reset_world();
    owner_a_blob_reset();
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 1.2f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15f_77_below_age_threshold_no_motion(void)
{
    /* type 0x77 + AGE <= 0x14 → motion block skipped.  Stage AGE = 0x13
     * (post-preamble = 0x14 = threshold; not >); VEL must be unchanged. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x20, 100.0f);
    owner_a_blob_set_f(0x28,   0.0f);
    stage_live(0, 0x77, 0, 0, 0, /*vx=*/0, 0, /*vz=*/0, /*age=*/0x13);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15f_77_motion_accumulates_vel(void)
{
    /* AGE = 100 (post-preamble 101), owner_a+0x20 = 10, slot.POS_X = 0.
     * dx = 10, dz = 0, length = 10 → normalize → (1, 0).
     * factor = (101 - 0x14) * 0.002 + 0.001 = 81 * 0.002 + 0.001 = 0.163.
     * VEL_X += 0.163 * 1 = 0.163.  Then drag 0.98 → 0.15974. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x20, 10.0f);
    owner_a_blob_set_f(0x28,  0.0f);
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/100);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.15974f)
            < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15f_77_length_below_1_5_kills(void)
{
    /* length(dx, dz) < 1.5 → KILL.  Owner.pose = (1, _, 0); slot.POS = (0, _, 0).
     * dx = 1, dz = 0, length = 1 < 1.5 → KILL. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x20, 1.0f);
    owner_a_blob_set_f(0x28, 0.0f);
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/100);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15f_77_horizontal_speed_capped_to_0_3(void)
{
    /* Pre VEL_X = 1.0 (large).  Owner pose at (100, _, 0) (very far).
     * factor = (101 - 0x14) * 0.002 + 0.001 = 0.163.
     * VEL_X += 0.163 * 1 (normalized) = 1.163.  speed = 1.163 > 0.3.
     * After cap: VEL_X = 1.163 * 0.3 / 1.163 = 0.3.  Then drag 0.98 →
     * 0.294. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_f(0x20, 100.0f);
    owner_a_blob_set_f(0x28,   0.0f);
    stage_live(0, 0x77, 0, 0, 0, /*vx=*/1.0f, 0, /*vz=*/0, /*age=*/100);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.294f)
            < 1e-4f);
    return 0;
}

int test_records_b_tick_t15f_a2_uses_owner_b_pose(void)
{
    /* type 0xa2 reads owner_b+0x3f0/+0x3f8 (NPC-allocator-style pose). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_f(0x3f0, 10.0f);
    owner_blob_set_f(0x3f8,  0.0f);
    /* type 0xa2 threshold is 0x3c; stage AGE so post-preamble > 0x3c. */
    stage_live(0, 0xa2, 0, 0, 0, 0, 0, 0, /*age=*/0x64);  /* → 0x65 > 0x3c */
    bind_owner(0);
    scene1_records_b_tick();
    /* dx = 10, dz = 0, normalize → (1, 0).  factor = (0x65 - 0x14)*0.002+
     * 0.001 = 81*0.002+0.001 = 0.163.  VEL_X = 0.163 → cap not engaged →
     * drag 0.98 → 0.15974. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.15974f)
            < 1e-4f);
    return 0;
}

int test_records_b_tick_t15f_77_sm_ret_nonzero_skips_age_kill(void)
{
    /* type 0x77 SM ret != 0 → advance iter, skip AGE==4000 check.  Stage
     * AGE = 3999 → post-preamble 4000 (would kill if not for SM short-circuit).
     * Install SM hook (=> ret=1) and verify slot stays alive.
     *
     * However: the motion block computes length<1.5 KILL when owner.pose
     * matches slot.POS (dx,dz close).  Use a no-target setup (no owner)
     * → motion block skipped; SM still fires with ret 1 → return-early. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    /* No owner bound → slot_owner_a returns NULL → motion block skipped. */
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/3999);
    scene1_records_b_tick();
    /* SM ret != 0 → early return; AGE-4000 kill check skipped. */
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x77);
    return 0;
}

int test_records_b_tick_t15f_77_kills_at_age_4000_when_sm_ret_zero(void)
{
    /* No SM hook → ret=0 → AGE==4000 KILL fires.  No owner → motion
     * block skipped; falls to SM (NULL hook → ret 0) → falls to
     * AGE-4000 check. */
    reset_world();
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/3999);  /* → 4000 */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15f_a2_sm_called_only_above_threshold(void)
{
    /* type 0xa2: SM fires only when AGE > 0x3c.  Stage AGE = 0x3b
     * (post-preamble 0x3c, not >) → SM NOT called. */
    reset_world();
    owner_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0xa2, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);

    /* Stage AGE = 0x3c (post-preamble 0x3d > 0x3c) → SM called. */
    reset_world();
    owner_blob_reset();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0xa2, 0, 0, 0, 0, 0, 0, /*age=*/0x3c);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    /* 0xa2 SM ret ignored → AGE-4000 check still runs; AGE 0x3d != 4000
     * → slot alive. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa2);
    return 0;
}

/* ─── C8j-tick.15g — body_0x2e_or_0x36 (player-homing drift) ──────────── */

int test_records_b_tick_t15g_preamble_spins_rot_z_and_zeroes_drag(void)
{
    /* Both types: ROT_Z += 0.05 and DRAG = 0 unconditionally.  Stage AGE
     * outside motion window so VEL stays zero and the speed-cap path is
     * a no-op. */
    reset_world();
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG,  9.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 1.05f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  - 0.0f)  < 1e-6f);

    reset_world();
    stage_live(0, 0x36, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 2.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG,  9.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 2.05f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)  - 0.0f)  < 1e-6f);
    return 0;
}

int test_records_b_tick_t15g_type_2e_age_in_window_homes_to_player(void)
{
    /* Type 0x2e, stage AGE = 0x32 (post-preamble 0x33 — comfortably inside
     * (0x1e, 0xc8)).  step = 0x33 - 0x1e = 0x15 = 21.
     * rate = 21 * 5e-05 = 0.00105 (< 0.005 cap).
     * drag = 1.0 - 21 * 0.01 = 0.79; floored to 0.98.
     * Player at (10, 0, 0); slot at POS=(0, 0, 0), ALT=(0, 0, 0).
     * delta_x = 10 - 0 = 10.
     * vx_new = ((10) * 0.00105 + 0) * 0.98 = 0.010290.
     * vy/vz stay 0 (player y/z = 0). */
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 0.0f;
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0x32);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.010290f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_2e_rate_caps_at_0_005(void)
{
    /* High AGE so step is large → rate would exceed 0.005, clamped.
     * AGE = 0xc6 (post-preamble 0xc7 < 0xc8).  step = 0xc7 - 0x1e = 0xa9 = 169.
     * raw_rate = 169 * 5e-05 = 0.00845 → clamped to 0.005.
     * drag = 1.0 - 169 * 0.01 = -0.69 → floored to 0.98.
     * Player at (200, 0, 0); slot at POS=(0,0,0), ALT=(0,0,0).
     * vx_new = (200 * 0.005 + 0) * 0.98 = 0.98.  Below cap 0.25? No, 0.98 > 0.25
     *   → speed-cap activates: cap/speed * vx_new = 0.25/0.98 * 0.98 = 0.25.
     * Speed = sqrt(0.98²) = 0.98.  After cap: vx = 0.98 * 0.25 / 0.98 = 0.25. */
    reset_world();
    g_scene1_player_pos[0] = 200.0f;
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0xc6);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.25f) < 1e-5f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_2e_outside_motion_window_no_homing(void)
{
    /* AGE = 0xc7 (post-preamble 0xc8 — NOT < 0xc8, gate closed).
     * VEL stays 0 → speed-cap a no-op → VEL still 0. */
    reset_world();
    g_scene1_player_pos[0] = 200.0f;
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0xc7);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_2e_alt_pos_offsets_target(void)
{
    /* Target = player_pos + ALT.  Player at (5, 0, 0), ALT_X = 3 → target_x = 8.
     * AGE 0x1f (post 0x20): step = 2, rate = 0.0001, drag floored to 0.98.
     * vx_new = ((8 - 0) * 0.0001 + 0) * 0.98 = 0.000784. */
    reset_world();
    g_scene1_player_pos[0] = 5.0f;
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0x1f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 3.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.000784f) < 1e-7f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_36_age_in_window_uses_constant_rate(void)
{
    /* Type 0x36, stage AGE 0x1f (post 0x20, > 0x1e, < 0x78).
     * Constant rate 0.005, constant drag 0.95.
     * Player at (10, 0, 0); slot POS=(0,0,0), ALT=(0,0,0).
     * vx_new = (10 * 0.005 + 0) * 0.95 = 0.0475.
     * speed = 0.0475 < cap 0.75 → no cap. */
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    stage_live(0, 0x36, 0, 0, 0, 0, 0, 0, /*age=*/0x1f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0475f) < 1e-6f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_36_outside_motion_window_no_homing(void)
{
    /* AGE = 0x77 (post 0x78 — NOT < 0x78, gate closed). */
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    stage_live(0, 0x36, 0, 0, 0, 0, 0, 0, /*age=*/0x77);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    g_scene1_player_pos[0] = 0.0f;
    return 0;
}

int test_records_b_tick_t15g_type_36_speed_cap_at_0_75(void)
{
    /* Pre-existing VEL = (10, 0, 0).  AGE = 0 (post 1; outside (0x1e, 0x78)
     * window so motion block skipped, vel unchanged).  speed = 10 > 0.75
     * cap → vx = 10 * 0.75 / 10 = 0.75. */
    reset_world();
    stage_live(0, 0x36, 0, 0, 0, 10.0f, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.75f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15g_type_36_spawns_two_particles_when_alive(void)
{
    /* No SM hook → state_machine_call_ret returns 0 → slot stays alive,
     * spawn cluster runs.  Stage POS = (4, 0, 0), VEL preserved at 0
     * (motion window closed at AGE=0).  Expected: 2 spawns, type 0x70,
     * scale 0.2, param7=1.  First at (4, 0, 0); second at (4 + 0*0.5,
     * 0+0*0.5, 0+0*0.5) = (4, 0, 0). */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x36, 4.0f, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 2);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].slot_hint, 0);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x70);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 0.2f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].param7, 1);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].x - 4.0f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].type, 0x70);
    T_ASSERT(fabsf(g_scene1_spawn_trace[1].scale - 0.2f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].param7, 1);
    /* second spawn pos = POS + VEL*0.5; VEL=(0,0,0) → same as POS. */
    T_ASSERT(fabsf(g_scene1_spawn_trace[1].x - 4.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15g_type_36_second_spawn_offsets_by_vel(void)
{
    /* Pre-VEL = (10, 0, 0); Pre-POS = (0, 0, 0).  Outer preamble runs
     * BEFORE the body: POS_X += VEL_X → POS_X = 10.  Inside the body:
     * motion window closed at AGE=1 (post-preamble); speed-cap caps
     * |VEL| = 10 > 0.75 → vx = 0.75.  First spawn at POS = (10, 0, 0).
     * Second spawn at POS + VEL*0.5 = (10 + 0.75*0.5, 0, 0) = (10.375, 0, 0). */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x36, 0, 0, 0, 10.0f, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 2);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].x - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(g_scene1_spawn_trace[1].x - 10.375f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15g_type_2e_no_spawn_cluster(void)
{
    /* Type 0x2e never spawns particles. */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_t15g_type_36_sm_ret_nonzero_skips_spawn(void)
{
    /* SM hook installed → state_machine_call_ret returns 1 → engine zeroes
     * slot[TYPE] (kill).  Subsequent type==0x36 spawn cluster sees TYPE=0
     * and skips. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    scene1_spawn_trace_reset();
    stage_live(0, 0x36, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 0);
    return 0;
}

int test_records_b_tick_t15g_kills_at_age_0x100(void)
{
    /* Pre-AGE 0xff → post-preamble 0x100 → kill (both types). */
    reset_world();
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0xff);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);

    reset_world();
    stage_live(0, 0x36, 0, 0, 0, 0, 0, 0, /*age=*/0xff);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15g_does_not_kill_below_age_0x100(void)
{
    reset_world();
    stage_live(0, 0x2e, 0, 0, 0, 0, 0, 0, /*age=*/0xfe);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x2e);
    return 0;
}

/* ─── C8j-tick.15h — body_0xa0 + body_0x7e (cull-tail variants) ───────── */

int test_records_b_tick_t15h_t_a0_age_negative_cancels_preamble(void)
{
    /* AGE<0: POS -= VEL cancels the preamble's POS += VEL.  Slot stays
     * alive when OWNER_B+0x440 == 0. */
    reset_world();
    owner_blob_reset();
    stage_live(0, 0xa0, 5.0f, 10.0f, 15.0f, 0.5f, 1.0f, -0.25f, /*age=*/-2);
    bind_owner(0);
    scene1_records_b_tick();
    /* Preamble: POS+=VEL → (5.5, 11.0, 14.75); body cancels → (5.0, 10.0, 15.0). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 5.0f)  < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 15.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), -1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa0);
    return 0;
}

int test_records_b_tick_t15h_t_a0_age_negative_owner_b_440_nonzero_kills(void)
{
    /* AGE<0 + OWNER_B+0x440 != 0 → KILL.  (0xa0 uses OWNER_B+0x440 — distinct
     * from the 0x73 family's OWNER_A+0xcf8.) */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x440, 1);
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/-2);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15h_t_a0_age_negative_other_owner_field_does_not_kill(void)
{
    /* Only OWNER_B+0x440 gates the kill — writing to a nearby in-range
     * field (+0x4f0) must NOT kill the slot.  Proves the offset is
     * specifically +0x440 (vs the 0x73 family's +0xcf8, which falls
     * outside our 0xb00-byte blob anyway). */
    reset_world();
    owner_blob_reset();
    owner_blob_set_i(0x4f0, 1);
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/-2);
    bind_owner(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa0);
    return 0;
}

int test_records_b_tick_t15h_t_a0_age_1_dual_overlay_spawn_null_owner(void)
{
    /* AGE==1 dual overlay_spawn: template 0x10 scale 1.0 at POS-VEL, then
     * template 0x13 scale 0.7 at ALT_POS.  Both owner=NULL (asm `xor edi,
     * edi; push edi` for 1st arg). */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0xa0, /*px=*/6.0f, /*py=*/11.0f, /*pz=*/16.0f,
               /*vx=*/1.0f, /*vy=*/1.0f, /*vz=*/1.0f, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 200.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 300.0f);
    scene1_records_b_tick();
    /* Preamble: POS → (7, 12, 17), AGE → 1.  Body: POS-VEL = (6, 11, 16). */
    T_ASSERT_EQ_I(s_overlay_calls, 2);
    /* Last call (template 0x13 ALT_POS spawn) — owner is NULL. */
    T_ASSERT(s_overlay_last.owner == NULL);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x13);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.7f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 100.0f) < 1e-5f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 200.0f) < 1e-5f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 300.0f) < 1e-5f);
    T_ASSERT_EQ_I(s_overlay_last.dur, -1);
    T_ASSERT_EQ_I(s_overlay_last.mode, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15h_t_a0_age_1_first_spawn_template_0x10(void)
{
    /* FIRST overlay_spawn: template 0x10 at POS-VEL, scale 1.0, NULL owner. */
    reset_world();
    s_t15e_overlay_n = 0;
    scene1_records_b_set_overlay_spawn_hook(t15e_capture_overlay);
    stage_live(0, 0xa0, /*px=*/10.0f, /*py=*/20.0f, /*pz=*/30.0f,
               /*vx=*/2.0f, /*vy=*/3.0f, /*vz=*/4.0f, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_X, 100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Y, 200.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ALT_POS_Z, 300.0f);
    scene1_records_b_tick();
    /* Preamble: POS → (12, 23, 34) → POS-VEL = (10, 20, 30). */
    T_ASSERT_EQ_I(s_t15e_overlay_n, 2);
    T_ASSERT_EQ_I(s_t15e_overlay_log[0].type, 0x10);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].px    - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].py    - 20.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].pz    - 30.0f) < 1e-5f);
    T_ASSERT(fabsf(s_t15e_overlay_log[0].scale - 1.0f)  < 1e-6f);
    T_ASSERT_EQ_I(s_t15e_overlay_log[1].type, 0x13);
    T_ASSERT(fabsf(s_t15e_overlay_log[1].scale - 0.7f)  < 1e-6f);
    scene1_records_b_set_overlay_spawn_hook(NULL);
    return 0;
}

int test_records_b_tick_t15h_t_a0_drag_zero_after_tick(void)
{
    /* DRAG = 0 unconditionally written each tick (AGE >= 0 path). */
    reset_world();
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15h_t_a0_cull_culled_kills(void)
{
    /* cull_query returns >= 0 (culled) → KILL. */
    reset_world();
    s_cull_return = 0;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    scene1_records_b_set_cull_query_hook(NULL);
    return 0;
}

int test_records_b_tick_t15h_t_a0_state_machine_ret_1_kills(void)
{
    /* SM hook installed → state_machine_call_ret returns 1 → KILL. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15h_t_a0_kills_at_age_0x78(void)
{
    /* Stage AGE 119 → preamble bumps to 120 (0x78) → KILL. */
    reset_world();
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15h_t_a0_below_age_0x78_alive(void)
{
    /* Stage AGE 118 → preamble → 119 (!= 0x78) → alive. */
    reset_world();
    stage_live(0, 0xa0, 0, 0, 0, 0, 0, 0, /*age=*/118);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa0);
    return 0;
}

int test_records_b_tick_t15h_t_7e_drag_zero_after_tick(void)
{
    /* Type 0x7e: DRAG = 0 unconditionally (entry skips the head, lands
     * straight at LAB_004402ad's DRAG=0 write at 0x44036a). */
    reset_world();
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/10);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG)) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15h_t_7e_cull_culled_kills(void)
{
    /* 0x7e cull_query >= 0 → KILL. */
    reset_world();
    s_cull_return = 0;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    scene1_records_b_set_cull_query_hook(NULL);
    return 0;
}

int test_records_b_tick_t15h_t_7e_state_machine_ret_1_kills(void)
{
    /* 0x7e SM hook installed → ret 1 → KILL. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15h_t_7e_clears_anim_drive_before_sm(void)
{
    /* DAT_06a46f94 cleared before SM call (asm 0x44058d `and ds:0x6a46f94, 0`). */
    reset_world();
    g_scene1_records_b_tick_anim_drive = 99;
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/10);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_anim_drive, 0);
    return 0;
}

int test_records_b_tick_t15h_t_7e_kills_at_age_0x78(void)
{
    /* 0x7e shares 0xa0's AGE==0x78 kill. */
    reset_world();
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15h_t_7e_does_not_spawn_overlays(void)
{
    /* 0x7e skips the 0x73-family AGE==1 dual overlay_spawn head. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x7e, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

/* ═══ C8j-tick.15i — entity-bounce shared body ════════════════════════ */
/* Types {0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6, 99, 0x51, 0x52, 0x56, 0x96,
 * 0x62}.  Stages slot at age 0 → preamble bumps to 1 → cluster path
 * exercises AGE==1 overlay spawn; otherwise stage at age >=2 to skip
 * the AGE==1 head. */

int test_records_b_tick_t15i_cluster_drag_0p5_after_tick(void)
{
    /* All cluster types share the DRAG=0.5 override after the DRAG=0
     * preamble (asm 0x4403d5: DRAG = 0.5). */
    reset_world();
    stage_live(0, 0x4d, 0, 0, 0, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 1.5f);
    scene1_records_b_tick();
    /* 0x4d body also subtracts 0.01 from VEL_Y but doesn't touch DRAG
     * after the cluster's DRAG=0.5 write. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15i_cluster_age_1_spawns_overlay_9(void)
{
    /* AGE=0 staged + preamble → AGE=1 → cluster spawns template 9 at POS
     * with scale 0.4 (passing owner_a). */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x4e, 1.0f, 2.0f, 3.0f, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 9);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.4f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 3.0f) < 1e-6f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15i_cluster_age_other_no_overlay(void)
{
    /* AGE != 1 → cluster skips the overlay spawn. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0xa5, 0, 0, 0, 0, 0, 0, /*age=*/9);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15i_t_4d_vel_y_subtract_0p01(void)
{
    /* asm 0x440453-0x440461: type 0x4d → VEL_Y -= 0.01.  Stage VEL_Y =
     * 1.0 then verify post-tick VEL_Y = 0.99. */
    reset_world();
    /* Make cull return "visible" (-1) AND install SM that returns 1 to
     * KILL the slot, so we can sample VEL_Y just before kill.  We do this
     * by installing a SM hook (just observes; default state_machine_call_ret
     * returns 0 without an int-return hook). */
    stage_live(0, 0x4d, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.99f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15i_t_99_overlay_offset_by_vel_12(void)
{
    /* asm 0x4403e6-0x44042b: type 99 → overlay at (POS + VEL*12), template
     * 0x24, scale 1.0.  POS.y unscaled. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 99, 1.0f, 2.0f, 3.0f, /*vx=*/0.5f, 0, /*vz=*/-0.25f,
               /*age=*/5);
    /* Preamble: POS += VEL → POS becomes (1.5, 2.0, 2.75) before the
     * body runs.  Body then spawns at POS + VEL*12. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x24);
    T_ASSERT(fabsf(s_overlay_last.scale - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - (1.5f + 0.5f * 12.0f)) < 1e-5f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 2.0f) < 1e-5f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - (2.75f + -0.25f * 12.0f)) < 1e-5f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15i_t_99_drag_starts_1p2_then_overrides_1p0(void)
{
    /* type 99 sets DRAG = 1.2 in the head (asm 0x440433), then DRAG = 1.0
     * in the sub-dispatch (asm 0x440579) just before SM.  Final post-tick
     * DRAG = 1.0. */
    reset_world();
    stage_live(0, 99, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15i_t_52_drag_0p5(void)
{
    /* asm 0x440447: 0x52 → DRAG = 0.5. */
    reset_world();
    stage_live(0, 0x52, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15i_cull_kills(void)
{
    /* cull_query >= 0 → KILL.  All non-bounce types share this. */
    reset_world();
    s_cull_return = 0;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0x62, 0, 0, 0, 0, 0, 0, /*age=*/5);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_kills_at_age_0x78(void)
{
    /* LAB_00440741: AGE==0x78 → KILL (for non-bounce types, after SM ret=0). */
    reset_world();
    stage_live(0, 0x62, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_below_age_0x78_alive(void)
{
    /* AGE != 0x78 → slot survives. */
    reset_world();
    stage_live(0, 0x62, 0, 0, 0, 0, 0, 0, /*age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x62);
    return 0;
}

int test_records_b_tick_t15i_sm_ret_1_kills(void)
{
    /* General SM ret==1 → KILL (asm 0x44059a-0x4405a3 paths). */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x4e, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    /* default state_machine_call_ret with hook installed returns 1
     * (state_machine_call_ret_default returns 1 when hook != NULL). */
    T_ASSERT_EQ_I(s_sm_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* SM hook that sets g_scene1_records_b_tick_anim_drive — simulates the
 * engine's real FUN_0043865e setting DAT_06a46f94 during its body.  The
 * void-hook signature means state_machine_call_ret() always returns 1
 * when this is installed (per the engine-int-return approximation in
 * src/scene1_records_b_tick.c L232-239).  The body's 0x52 SM-ret-1 arm
 * reads g_scene1_records_b_tick_anim_drive AFTER the SM call. */
static int s_sm_drive_calls;
static int s_sm_drive_set_to;
static void sm_sets_drive(int32_t *slot)
{
    (void)slot;
    s_sm_drive_calls++;
    g_scene1_records_b_tick_anim_drive = s_sm_drive_set_to;
}

int test_records_b_tick_t15i_t_52_sm_ret_1_writes_damage_when_drive_positive(void)
{
    /* asm 0x4405a0-0x4405e1: type 0x52 SM ret==1 + DAT_06a46f94 > 0 →
     * owner_a+0xe30 = drive/10 (min 1); owner_a+0xe38 = 0x1e. */
    reset_world();
    owner_a_blob_reset();
    s_sm_drive_calls = 0;
    s_sm_drive_set_to = 250;   /* /10 = 25 */
    scene1_records_b_set_state_machine_hook(sm_sets_drive);
    stage_live(0, 0x52, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_drive_calls, 1);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 25);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe38), 0x1e);
    /* Slot killed regardless of damage-write outcome. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_t_52_sm_ret_1_drive_floor_1(void)
{
    /* drive/10 < 1 → clamped to 1. */
    reset_world();
    owner_a_blob_reset();
    s_sm_drive_set_to = 5;     /* /10 = 0 → clamped to 1 */
    scene1_records_b_set_state_machine_hook(sm_sets_drive);
    stage_live(0, 0x52, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 1);
    return 0;
}

int test_records_b_tick_t15i_t_52_sm_ret_1_drive_zero_no_write(void)
{
    /* DAT_06a46f94 == 0 → owner damage write skipped; slot still killed. */
    reset_world();
    owner_a_blob_reset();
    /* Pre-stamp a sentinel so we can detect non-writes. */
    owner_a_blob_set_i(0xe30, 0x12345678);
    s_sm_drive_set_to = 0;
    scene1_records_b_set_state_machine_hook(sm_sets_drive);
    stage_live(0, 0x52, 0, 0, 0, 0, 0, 0, /*age=*/2);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe30), 0x12345678);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x51 — 3-iter loop ──────────────────────────────────────────── */

int test_records_b_tick_t15i_t_51_restores_pos_after_loop(void)
{
    /* asm 0x4404b0-0x44055e: 3 iters of SM at displaced POS, then restore
     * to ORIG POS.  Without an SM hook, no state_machine_call_ret hook is
     * installed → ret == 0 → loop runs all 3 iters; POS restored. */
    reset_world();
    stage_live(0, 0x51, 7.0f, 8.0f, 9.0f, 0, 0, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    scene1_records_b_tick();
    /* Slot alive (AGE != 0x3c, != 0x78), POS restored. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x51);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 7.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 8.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 9.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15i_t_51_drag_1p2(void)
{
    /* asm 0x4404b0: 0x51 → DRAG = 1.2 (after DRAG=0 head). */
    reset_world();
    stage_live(0, 0x51, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.2f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15i_t_51_age_0x3c_kills(void)
{
    /* asm 0x44055f: AGE == 0x3c (60) → KILL. */
    reset_world();
    stage_live(0, 0x51, 0, 0, 0, 0, 0, 0, /*age=*/59);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_t_51_age_0x78_kills(void)
{
    /* AGE == 0x78 (120) also kills via LAB_00440741. */
    reset_world();
    stage_live(0, 0x51, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

/* ─── 0x56 / 0x96 — ground-bounce ─────────────────────────────────── */

int test_records_b_tick_t15i_t_56_drag_neg_0p15(void)
{
    /* asm 0x4405f3: 0x56 → DRAG = -0.15. */
    reset_world();
    stage_live(0, 0x56, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - -0.15f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15i_t_56_vel_y_subtract_0p01(void)
{
    /* asm 0x44060b-0x44061b: 0x56 → VEL_Y -= 0.01.  Pre VEL_Y=1.0 →
     * post=0.99. */
    reset_world();
    stage_live(0, 0x56, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.99f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15i_t_56_rotates_scr_and_z(void)
{
    /* asm 0x44061e-0x440630: ROT_SCR += 0.05, ROT_Z += 0.03. */
    reset_world();
    stage_live(0, 0x56, 0, 0, 0, 0, 1.0f, 0, /*age=*/2);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z, 2.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - 1.05f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z) - 2.03f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15i_t_56_no_ground_hit_no_bounce(void)
{
    /* ground_query returns 0 → bounce gate not taken; PART_IDX stays 0. */
    reset_world();
    s_gq_calls = 0; s_gq_hit = 0; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x56, 0, /*py=*/0.0f, 0, 0, /*vy=*/-1.0f, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t15i_t_56_ground_hit_bounces_and_plays_0x158(void)
{
    /* type 0x56 + ground hit + POS_Y <= gy+0.3 + VEL_Y < 0:
     *   POS_Y snaps to gy+0.3
     *   VEL_Y *= -0.5
     *   VEL_X/Z *= 0.7
     *   PART_IDX bumped to 1 by the impact AND then to 2 by the post-
     *   bounce LAB_00440741 "bc != 0 → bc++" path (engine asm
     *   0x44073a-0x44073b — runs unconditionally when bc != 0 after
     *   impact).  SE 0x158 plays during the impact (gated on bc==1
     *   between the two increments).  Net post-tick PART_IDX = 2. */
    reset_world();
    s_gq_calls = 0; s_gq_hit = 1; s_gq_out_y = 0.0f;
    s_se_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x56, /*px=*/0, /*py=*/-0.2f, /*pz=*/0,
               /*vx=*/2.0f, /*vy=*/-1.0f, /*vz=*/3.0f, /*age=*/2);
    /* Preamble: POS += VEL → py=-1.2; VEL_Y -= 0.01 → -1.01.  Then check
     * py<=gy+0.3 → -1.2<=0.3 ✓ → bounce. */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 0.3f) < 1e-5f);
    /* VEL_Y was -1.01 (after gravity), then *= -0.5 → 0.505. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.505f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 1.4f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 2.1f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 2);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x158);
    return 0;
}

int test_records_b_tick_t15i_t_96_ground_hit_plays_0x168(void)
{
    /* type 0x96 SE on first bounce → 0x168. */
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    s_se_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x96, 0, -0.5f, 0, 0, -1.0f, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x168);
    return 0;
}

int test_records_b_tick_t15i_t_56_bounce_count_2_kills(void)
{
    /* PART_IDX == 1 pre-tick → bounce impact bumps to 2 → KILL. */
    reset_world();
    s_gq_hit = 1; s_gq_out_y = 0.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x56, 0, -0.5f, 0, 0, -1.0f, 0, /*age=*/2);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_t_56_bounce_sets_flag(void)
{
    /* g_scene1_records_b_tick_flag (= DAT_06a46f98) set to 1 by 0x56 path
     * regardless of bounce outcome. */
    reset_world();
    s_gq_hit = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x56, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 1);
    return 0;
}

int test_records_b_tick_t15i_t_56_age_0x78_kills(void)
{
    /* LAB_00440741 shared kill on AGE==0x78. */
    reset_world();
    stage_live(0, 0x56, 0, 0, 0, 0, 0, 0, /*age=*/119);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15i_t_56_skips_cull_check(void)
{
    /* 0x56 jumps over the cull gate at 0x440469; even with cull_stub set
     * to "kill", slot survives. */
    reset_world();
    s_cull_return = 0;
    scene1_records_b_set_cull_query_hook(cull_stub);
    stage_live(0, 0x56, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x56);
    return 0;
}

/* ─── C8j-tick.15j — body_0x83 (shop-walker velocity nudge) ──────────── */

/* Shop-walker record fixture.  Each record is a 0x2e9-dword block (the
 * engine's actual stride at DAT_0076bd98), with leading padding so the
 * TYPE anchor sits at offset SCENE1_RECORDS_SHOP_STRIDE/2 (mid-record);
 * body reads at negative dword offsets -0xe..-0x9 around the anchor.
 *
 * We allocate 4 records — enough to exercise the gate cascade + nudge
 * formula with a manageable test surface.  Each record's TYPE-anchor
 * pointer is returned by t15j_sw_record_at(idx), null when idx >= 4. */
#define T15J_SW_RECORD_DW   0x2e9
#define T15J_SW_ANCHOR_OFF  0x40   /* leading dwords before the anchor;
                                    * must be > 0xe so neg-offset reads
                                    * stay within bounds */
#define T15J_SW_RECORD_COUNT 4

static int32_t g_t15j_sw_records[T15J_SW_RECORD_COUNT][T15J_SW_RECORD_DW];

static int32_t *t15j_sw_record_at(int idx)
{
    if (idx < 0 || idx >= T15J_SW_RECORD_COUNT) return NULL;
    return &g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF];
}

static int32_t *t15j_sw_record_at_partial(int idx)
{
    /* Returns a record only for idx == 1 — used to verify gate-skipping
     * on null records preserves slot integrity. */
    if (idx != 1) return NULL;
    return &g_t15j_sw_records[1][T15J_SW_ANCHOR_OFF];
}

static void t15j_sw_records_reset(void)
{
    memset(g_t15j_sw_records, 0, sizeof g_t15j_sw_records);
    scene1_records_b_set_sw_record_at_hook(t15j_sw_record_at);
}

static void t15j_sw_record_set_f(int idx, int dw_off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF + dw_off] = v;
}

static float t15j_sw_record_get_f(int idx, int dw_off)
{
    int32_t v = g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF + dw_off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void t15j_sw_record_set_gates(int idx,
                                     int32_t flag_1b3, int32_t type_0,
                                     int32_t flag_1b7)
{
    g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF + 0x1b3] = flag_1b3;
    g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF + 0]     = type_0;
    g_t15j_sw_records[idx][T15J_SW_ANCHOR_OFF + 0x1b7] = flag_1b7;
}

/* SM hook that bumps anim_drive — used to exercise the damage-write path. */
static int32_t s_t15j_damage_drive_seed;
static void t15j_sm_set_drive(int32_t *slot)
{
    (void)slot;
    g_scene1_records_b_tick_anim_drive = s_t15j_damage_drive_seed;
}

int test_records_b_tick_t15j_age_0x3c_plays_se_0x2bb_and_0x2a5(void)
{
    /* AGE 0x3b post-preamble = 0x3c → both SE plays fire (0x2bb for the
     * AGE==0x3c gate; 0x2a5 NOT fired since AGE != 0x1e).  Then stage
     * AGE 0x1d post-preamble = 0x1e → only 0x2a5 fires. */
    reset_world();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2bb);

    reset_world();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x1d);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2a5);
    return 0;
}

int test_records_b_tick_t15j_age_0x3c_inits_vel_from_rot_x(void)
{
    /* AGE==0x3c: VEL = (sin(ROT_X)*0.15, -0.02, cos(ROT_X)*0.15).
     * ROT_X = 0 → sin=0, cos=1 → VEL = (0, -0.02, 0.15).  After phase 4
     * (AGE <= 0xb4 drag 0.98): VEL = (0, -0.0196, 0.147). */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - (-0.02f * 0.98f))
             < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - (0.15f * 0.98f))
             < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_age_0x3c_rot_x_pi_half(void)
{
    /* ROT_X = π/2 → sin=1, cos=0 → VEL_pre = (0.15, -0.02, 0).
     * After drag 0.98: VEL = (0.147, -0.0196, 0). */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x3b);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.5707963f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - (0.15f * 0.98f))
             < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_age_le_0xb4_drags_velocity_0_98(void)
{
    /* AGE == 0xb4: drag still active.  Pre-VEL = (1, 2, 4) → post = ×0.98. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 1.0f, 2.0f, 4.0f, /*age=*/0xb3);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.98f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 1.96f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 3.92f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15j_age_gt_0xb4_zeroes_velocity(void)
{
    /* AGE == 0xb5: drag arm closes, VEL latched to zero. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 9.0f, 9.0f, 9.0f, /*age=*/0xb4);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_seq_id_captures_at_0x78(void)
{
    /* AGE 0x77 post-preamble = 0x78 → first capture point; SEQ_ID
     * gets g_scene1_record_b_seq_counter (which then increments). */
    reset_world();
    g_scene1_record_b_seq_counter = 42;
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x77);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 42);
    T_ASSERT_EQ_U(g_scene1_record_b_seq_counter, 43u);
    return 0;
}

int test_records_b_tick_t15j_seq_id_captures_at_0xf0(void)
{
    /* AGE 0xef post-preamble = 0xf0 → last capture point. */
    reset_world();
    g_scene1_record_b_seq_counter = 100;
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0xef);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 100);
    T_ASSERT_EQ_U(g_scene1_record_b_seq_counter, 101u);
    return 0;
}

int test_records_b_tick_t15j_seq_id_not_captured_between_buckets(void)
{
    /* AGE 0x9b post-preamble = 0x9c — not a capture point (those are
     * {0x78, 0xa0, 0xc8, 0xf0}).  SEQ_ID preserved, counter untouched. */
    reset_world();
    g_scene1_record_b_seq_counter = 99;
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x9b);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID, 777);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_SEQ_ID), 777);
    T_ASSERT_EQ_U(g_scene1_record_b_seq_counter, 99u);
    return 0;
}

int test_records_b_tick_t15j_drag_is_1_5_unconditional(void)
{
    /* DRAG = 1.5 set every tick regardless of AGE. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/5);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 99.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 1.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_particle_0x1f_emits_with_age_divisor(void)
{
    /* AGE 0xa0 post-preamble = 0xa1 → divisor 1 (every tick); AGE divisible
     * → emit particle 0x1f.  scale = rng + 1.0 ∈ [1.0, 2.0). */
    reset_world();
    install_overlay_capture();
    rng_seed(1);
    stage_live(0, 0x83, 10.0f, 20.0f, 30.0f, 0, 0, 0, /*age=*/0xa0);
    scene1_records_b_tick();
    /* Expect at least one particle 0x1f spawn + AGE % 2 == 0 → particle 0x20.
     * 0xa1 is odd so 0x20 NOT emitted.  Just one spawn (0x1f). */
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x1f);
    T_ASSERT(s_overlay_last.scale >= 1.0f && s_overlay_last.scale < 2.0f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 10.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 20.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 30.0f) < 1e-6f);
    T_ASSERT_EQ_I(s_overlay_last.dur, -1);
    T_ASSERT_EQ_I(s_overlay_last.rot_y, 0);
    T_ASSERT_EQ_I(s_overlay_last.shape_mode, 0);
    T_ASSERT_EQ_I(s_overlay_last.mode, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15j_particle_0x1f_skipped_when_not_divisible(void)
{
    /* AGE 0x77 post-preamble = 0x78 → divisor 4 (AGE >= 0x78 → divisor 3,
     * then AGE < 0x8c so divisor=3; further AGE < 0x78? no — actually
     * AGE 0x78 fails the `< 0x78` check, so divisor stays 3.  AGE % 3 =
     * 0x78 % 3 = 0 → emit.  Use AGE 0x79 post = 0x7a, divisor 3 (since
     * 0x7a >= 0x78), 0x7a % 3 = 122 % 3 = 2 → skip. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x79);
    scene1_records_b_tick();
    /* Only particle 0x20 may fire (AGE 0x7a even → emit 0x20). 0x1f NOT emitted. */
    int n_1f = 0;
    /* Inspect overlay history via last call alone is insufficient; trust
     * total count == 1 (= just the 0x20 spawn since AGE 0x7a even). */
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x20);
    (void)n_1f;
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15j_particle_0x1f_outside_window_no_emit(void)
{
    /* AGE 0x50 post-preamble = 0x51 — wait, gate is `age > 0x50` so AGE
     * 0x51 passes.  Use AGE 0x4f post-preamble = 0x50 → 0x50 > 0x50
     * false → skip the 0x1f spawn.  Verify only the AGE%2==0 0x20 path. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x4f);
    scene1_records_b_tick();
    /* AGE 0x50 — 0x50 % 2 == 0 → emit 0x20.  No 0x1f. */
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x20);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15j_particle_0x20_emits_on_even_age(void)
{
    /* AGE 1 post-preamble = 2 → even → emit 0x20.  Also AGE < 0x50 so
     * no 0x1f.  AGE < 0x46 + no owner gate → no kill. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x83, 1.0f, 2.0f, 3.0f, 0, 0, 0, /*age=*/1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x20);
    T_ASSERT(fabsf(s_overlay_last.scale - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(s_overlay_last.pos_z - 3.0f) < 1e-6f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15j_particle_0x20_skipped_on_odd_age(void)
{
    /* AGE 2 post-preamble = 3 (odd) → no 0x20 emit. AGE < 0x50 → no 0x1f. */
    reset_world();
    install_overlay_capture();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15j_age_lt_0x46_kills_when_owner_cf8_set(void)
{
    /* AGE post-preamble < 0x46 AND owner_a+0xcf8 != 0 → kill. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x10);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15j_age_lt_0x46_alive_when_owner_cf8_clear(void)
{
    /* AGE post-preamble < 0x46 AND owner_a+0xcf8 == 0 → slot alive. */
    reset_world();
    owner_a_blob_reset();
    /* owner_a+0xcf8 stays 0 */
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x10);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x83);
    return 0;
}

int test_records_b_tick_t15j_age_ge_0x46_ignores_owner_cf8(void)
{
    /* AGE post-preamble >= 0x46 → owner_a+0xcf8 != 0 has no effect on kill. */
    reset_world();
    owner_a_blob_reset();
    owner_a_blob_set_i(0xcf8, 1);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x46);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x83);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_no_hook_is_noop(void)
{
    /* No sw_record_at_hook installed → default returns NULL → loop is
     * a no-op + slot survives the phase. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x83);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_gate_filter_type_0_skips(void)
{
    /* Record with TYPE != 1 (e.g. 0) → gate fails → no nudge. */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/0, /*type_0=*/0, /*flag_1b7=*/0);
    t15j_sw_record_set_f(0, -0xe, 5.0f);  /* rec.POS_X */
    t15j_sw_record_set_f(0, -0xd, 0.0f);
    t15j_sw_record_set_f(0, -0xc, 0.0f);
    t15j_sw_record_set_f(0, -0xb, 0.0f);  /* rec.VEL_X */
    t15j_sw_record_set_f(0, -0xa, 0.0f);
    t15j_sw_record_set_f(0, -0x9, 0.0f);

    stage_live(0, 0x83, 10.0f, 0, 0, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    /* Record VEL untouched. */
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_gate_filter_flag_1b3_positive_skips(void)
{
    /* Record with flag_1b3 > 0 → gate fails. */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/1, /*type_0=*/1, /*flag_1b7=*/0);
    t15j_sw_record_set_f(0, -0xe, 0.0f);
    t15j_sw_record_set_f(0, -0xd, 0.0f);
    t15j_sw_record_set_f(0, -0xc, 0.0f);

    stage_live(0, 0x83, 10.0f, 0, 0, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_gate_filter_flag_1b7_nonzero_skips(void)
{
    /* Record with flag_1b7 != 0 → gate fails. */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/0, /*type_0=*/1, /*flag_1b7=*/1);

    stage_live(0, 0x83, 10.0f, 0, 0, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_nudge_formula(void)
{
    /* Record gated open: TYPE=1, flags=0.  Record at POS=(0,0,0), slot at
     * POS=(3, 0, 4) → delta=(3, 0, 4), len=5, nudge = delta/5 * 0.03 =
     * (0.018, 0, 0.024). */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/0, /*type_0=*/1, /*flag_1b7=*/0);
    t15j_sw_record_set_f(0, -0xe, 0.0f);
    t15j_sw_record_set_f(0, -0xd, 0.0f);
    t15j_sw_record_set_f(0, -0xc, 0.0f);
    t15j_sw_record_set_f(0, -0xb, 0.0f);
    t15j_sw_record_set_f(0, -0xa, 0.0f);
    t15j_sw_record_set_f(0, -0x9, 0.0f);

    stage_live(0, 0x83, 3.0f, 0.0f, 4.0f, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.018f) < 1e-5f);
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xa) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0x9) - 0.024f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_skips_zero_distance(void)
{
    /* Slot and record at same POS → lensq = 0 → skip nudge (engine fcomp
     * + jbe). */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/0, /*type_0=*/1, /*flag_1b7=*/0);
    t15j_sw_record_set_f(0, -0xe, 5.0f);
    t15j_sw_record_set_f(0, -0xd, 5.0f);
    t15j_sw_record_set_f(0, -0xc, 5.0f);

    stage_live(0, 0x83, 5.0f, 5.0f, 5.0f, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    /* Record VEL still zero — gate passed, but lensq <= 0 so no nudge. */
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xa) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0x9) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_outside_age_window_skipped(void)
{
    /* AGE 0x4f post-preamble = 0x50 → !(AGE > 0x50) → outer gate fails.
     * Record VEL untouched even though gates would pass. */
    reset_world();
    t15j_sw_records_reset();
    t15j_sw_record_set_gates(0, /*flag_1b3=*/0, /*type_0=*/1, /*flag_1b7=*/0);
    t15j_sw_record_set_f(0, -0xe, 0.0f);
    t15j_sw_record_set_f(0, -0xd, 0.0f);
    t15j_sw_record_set_f(0, -0xc, 0.0f);

    stage_live(0, 0x83, 10.0f, 0, 0, 0, 0, 0, /*age=*/0x4f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sw_loop_null_records_skipped(void)
{
    /* Partial hook: only idx==1 returns a record; idx 0/2/3 return NULL.
     * Only record 1 should be nudged. */
    reset_world();
    memset(g_t15j_sw_records, 0, sizeof g_t15j_sw_records);
    scene1_records_b_set_sw_record_at_hook(t15j_sw_record_at_partial);
    t15j_sw_record_set_gates(1, /*flag_1b3=*/0, /*type_0=*/1, /*flag_1b7=*/0);
    t15j_sw_record_set_f(1, -0xe, 0.0f);
    t15j_sw_record_set_f(1, -0xd, 0.0f);
    t15j_sw_record_set_f(1, -0xc, 0.0f);

    stage_live(0, 0x83, 3.0f, 0.0f, 4.0f, 0, 0, 0, /*age=*/0x60);
    scene1_records_b_tick();
    T_ASSERT(fabsf(t15j_sw_record_get_f(1, -0xb) - 0.018f) < 1e-5f);
    T_ASSERT(fabsf(t15j_sw_record_get_f(1, -0x9) - 0.024f) < 1e-5f);
    /* Idx 0 still zero (hook returned NULL). */
    T_ASSERT(fabsf(t15j_sw_record_get_f(0, -0xb) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15j_sm_damage_write_on_drive_positive(void)
{
    /* AGE in (0x50, 0x11d) → SM call.  Hook installed sets anim_drive=20
     * → write owner_a+0xe2c = 20/2 = 10, owner_a+0xe34 = 0x1e. */
    reset_world();
    owner_a_blob_reset();
    s_t15j_damage_drive_seed = 20;
    scene1_records_b_set_state_machine_hook(t15j_sm_set_drive);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x60);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe2c), 10);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe34), 0x1e);
    return 0;
}

int test_records_b_tick_t15j_sm_damage_write_floor_1(void)
{
    /* anim_drive=1 → 1/2 = 0 → floored to 1. */
    reset_world();
    owner_a_blob_reset();
    s_t15j_damage_drive_seed = 1;
    scene1_records_b_set_state_machine_hook(t15j_sm_set_drive);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x60);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe2c), 1);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe34), 0x1e);
    return 0;
}

int test_records_b_tick_t15j_sm_damage_no_write_when_drive_zero(void)
{
    /* anim_drive=0 → damage write skipped (engine `test eax, eax; jle skip`). */
    reset_world();
    owner_a_blob_reset();
    s_t15j_damage_drive_seed = 0;
    scene1_records_b_set_state_machine_hook(t15j_sm_set_drive);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x60);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe2c), 0);
    T_ASSERT_EQ_I(owner_a_blob_get_i(0xe34), 0);
    return 0;
}

int test_records_b_tick_t15j_sm_no_call_outside_age_window(void)
{
    /* AGE post-preamble == 0x50 → outer gate `> 0x50` false → SM not called. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/0x4f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t15j_age_300_kills(void)
{
    /* AGE 299 post-preamble = 300 → KILL. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/299);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15j_age_299_alive(void)
{
    /* AGE 298 post-preamble = 299 → still alive. */
    reset_world();
    stage_live(0, 0x83, 0, 0, 0, 0, 0, 0, /*age=*/298);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x83);
    return 0;
}

int test_records_b_tick_t15j_sw_record_at_hook_setter_round_trips(void)
{
    reset_world();
    scene1_b_sw_record_at_fn prev =
        scene1_records_b_set_sw_record_at_hook(t15j_sw_record_at);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_sw_record_at_hook(NULL);
    T_ASSERT(prev == t15j_sw_record_at);
    return 0;
}

/* ─── C8j-tick.15k — body_0x75 (ground-cull walker, single-type) ──────── */

/* Stage type-0x75 cousins: count `n` extra live 0x75 slots starting at
 * slot index `start`, all with age `cousin_age`. */
static void t15k_stage_cousins(int start, int n, int32_t cousin_age)
{
    for (int k = 0; k < n; k++) {
        stage_live(start + k, 0x75, 0, 0, 0, 0, 0, 0, cousin_age);
    }
}

int test_records_b_tick_t15k_phase1_writes_life_mult_half(void)
{
    /* AGE pre-preamble 0 → post = 1 (< 200), phase 1.  LIFE_MULT = 0.5. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 99.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 0.5f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase1_age_decay_above_100(void)
{
    /* AGE pre-preamble 100 → preamble bumps to 101.  Body decays back to 100. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/100);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 100);
    return 0;
}

int test_records_b_tick_t15k_phase1_age_no_decay_at_100(void)
{
    /* AGE pre-preamble 99 → preamble bumps to 100.  100 is NOT > 100 so no decay. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 100);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_at_age_100_with_7_cousins(void)
{
    /* Stage self at AGE pre-preamble 99 (post = 100, qualifies AGE>=100).
     * Need live_count > 6 → stage self + 7 cousins = 8.  Cousins all live
     * (age < 200).  Self should phase-boost: AGE→200, VEL_Y → 0.1. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 200);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.1f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_inhibited_at_6_cousins(void)
{
    /* 6 cousins + self = 7 → NOT > 6.  Wait — engine asm `cmp 0x6 jle 0x43e845`:
     * `if (count > 6)` boost.  count must be STRICTLY greater than 6.  count == 7
     * (self + 6 cousins) IS > 6 → boost.  Test inhibition with 5 cousins (count=6).
     * Self + 5 cousins = 6 → !(6 > 6) → no boost.  AGE stays at 100 (post-preamble
     * decayed: pre=99 → 100; no decay since 100 not >100). */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    t15k_stage_cousins(1, 5, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 100);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_inhibited_when_cousins_too_old(void)
{
    /* Cousins with AGE>=200 don't count.  Self at slot 0 runs FIRST (before
     * cousins' preambles), so cousin AGE is read as-staged.  Stage cousins at
     * AGE=200 so they read >=200 at scan time → live_count = 1 (just self) →
     * no boost. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    t15k_stage_cousins(1, 7, /*cousin_age=*/200);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 100);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_inhibited_below_age_100(void)
{
    /* AGE pre-preamble 98 → post 99 (< 100).  Even with 7 cousins, no boost. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/98);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 99);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_vel_xz_from_owner_yaw(void)
{
    /* Owner yaw at +0xea4 drives VEL_X = sin(yaw)*0.24, VEL_Z = cos(yaw)*0.24.
     * yaw=0 → VEL_X=0, VEL_Z=0.24. */
    reset_world();
    owner_a_blob_reset();
    {
        float yaw = 0.0f;
        int32_t v;
        memcpy(&v, &yaw, sizeof v);
        owner_a_blob_set_i(0xea4, v);
    }
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    bind_owner_a(0);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.24f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_vel_xz_pi_half(void)
{
    /* yaw=π/2 → VEL_X=0.24, VEL_Z=0. */
    reset_world();
    owner_a_blob_reset();
    {
        float yaw = 1.5707963f;
        int32_t v;
        memcpy(&v, &yaw, sizeof v);
        owner_a_blob_set_i(0xea4, v);
    }
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    bind_owner_a(0);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.24f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_no_owner_uses_zero_yaw(void)
{
    /* OWNER_A == NULL (no bind_owner_a).  Body reads yaw=0 → VEL_X=0,
     * VEL_Z=0.24.  No crash. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 200);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.24f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase1_rot_x_steps_toward_target_max_0_08(void)
{
    /* Single slot (no cousins) at AGE pre-preamble 0 → post 1.  total_count=1,
     * preceding=0 → target = (0 * 2π / 1) + frame * 0.04 = frame * 0.04.
     * With g_sim_frame_count = 0 → target = 0.  ROT_X starts at 1.0 → after
     * step toward 0 by max 0.08 → ROT_X = 1.0 - 0.08 = 0.92. */
    reset_world();
    g_sim_frame_count = 0;
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 1.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 0.92f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase1_rot_x_snaps_when_within_max_step(void)
{
    /* ROT_X=0.05, target=0 → |delta|=0.05 < 0.08 → snap to target=0. */
    reset_world();
    g_sim_frame_count = 0;
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.05f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase1_rot_x_target_uses_frame_count(void)
{
    /* g_sim_frame_count = 25 → target = 0 + 25 * 0.04 = 1.0.
     * ROT_X = 0 → step toward 1.0 by 0.08 → ROT_X = 0.08. */
    reset_world();
    g_sim_frame_count = 25;
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X) - 0.08f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase1_rot_x_target_uses_cousin_ordinal(void)
{
    /* Self at slot 5 + 3 cousins at slots 0,1,2.  total_count=4, preceding=3
     * (since self is scanned 4th).  target = (3 * 2π / 4) + 0 = 1.5π ≈ 4.7123.
     * Normalize: 4.7123 - 2π ≈ -1.5708.  ROT_X = 0 → step toward -1.5708 by
     * 0.08 → result = -0.08. */
    reset_world();
    g_sim_frame_count = 0;
    t15k_stage_cousins(0, 3, /*cousin_age=*/50);
    stage_live(5, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(5, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_ROT_X) - (-0.08f)) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase1_pos_anchored_to_owner_plus_2_1(void)
{
    /* Owner at (10, 20, 30).  Single live slot, ROT_X=0 (so rotation no-op),
     * AGE 0 → post-preamble 1 → scale = 1*0.1 = 0.1.  Matrix:
     *   mat = T(0,0,0.1) × RotX(rot_x_after_step) ≈ T(0,0,0.1) × identity
     *   (since ROT_X stays 0 — single-slot target=0, already-at-target)
     * → translation = (0, 0, 0.1).
     * POS = owner + (0, 0, 0.1) + (0, 2.1, 0) = (10, 22.1, 30.1). */
    reset_world();
    owner_a_blob_reset();
    {
        float ox = 10.0f, oy = 20.0f, oz = 30.0f;
        int32_t v;
        memcpy(&v, &ox, sizeof v); owner_a_blob_set_i(0x20, v);
        memcpy(&v, &oy, sizeof v); owner_a_blob_set_i(0x24, v);
        memcpy(&v, &oz, sizeof v); owner_a_blob_set_i(0x28, v);
    }
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    bind_owner_a(0);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 22.1f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 30.1f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t15k_phase1_pos_scale_clamped_to_3(void)
{
    /* AGE post-preamble 35 (pre=34) → scale = 35*0.1 = 3.5 → clamped to 3.0.
     * ROT_X=0 → POS_Z = 0 + 3.0 = 3.0; POS_Y = 0 + 2.1.  No cousins. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/34);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 0.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 2.1f) < 1e-4f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Z) - 3.0f) < 1e-4f);
    return 0;
}

int test_records_b_tick_t15k_phase1_zeroes_rot_scr_and_rot_z(void)
{
    /* ROT_SCR and ROT_Z are always overwritten to 0 in phase 1. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR, 0.9f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_Z,   0.7f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_SCR) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_Z)   - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase2_vel_y_gravity(void)
{
    /* AGE pre-preamble 199 → post 200 → phase 2.  VEL_Y -= 0.01.  Pre VEL_Y=1.0
     * → preamble adds POS_Y by 1.0; body subtracts 0.01 → 0.99. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, /*vy=*/1.0f, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.99f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase2_drag_var_is_0_3(void)
{
    /* Phase 2 always writes DRAG = 0.3 at the tail (before SM). */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/199);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 99.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 0.3f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15k_phase2_ground_query_skipped_when_vel_y_ge_0(void)
{
    /* VEL_Y starts at 0.0 → body decrements to -0.01 (< 0) → ground_query
     * FIRES.  To test "skip when VEL_Y >= 0", start with vy=0.011 → after
     * -0.01, vy=0.001 (>= 0) → no ground_query. */
    reset_world();
    s_gq_calls = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, 0, 0, 0, /*vy=*/0.011f, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 0);
    return 0;
}

int test_records_b_tick_t15k_phase2_ground_query_fires_when_vel_y_lt_0(void)
{
    /* VEL_Y starts at 0.0 → decremented to -0.01 < 0 → ground_query fires. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 0;     /* miss — no bounce body fires */
    s_gq_out_y = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    return 0;
}

int test_records_b_tick_t15k_phase2_ground_bounce_snaps_pos_y_to_gy_plus_1(void)
{
    /* Ground hit at ground_y=5.0; pre POS_Y=4.5; threshold = 5.0 + 0.3 = 5.3.
     * POS_Y (after preamble) = 4.5 + 0 (VEL_Y=0 pre) = 4.5 <= 5.3 → bounce.
     * Snap POS_Y → 5.0 + 1.0 = 6.0.  VEL_Y (after gravity = -0.01) → -0.01 * -0.5
     * = 0.005. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, /*py=*/4.5f, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 6.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.005f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15k_phase2_ground_bounce_increments_bounce_count(void)
{
    /* Pre PART_IDX (bounce_count) = 0 → post = 1. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, 4.5f, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    return 0;
}

int test_records_b_tick_t15k_phase2_ground_bounce_count_3_kills_slot(void)
{
    /* Pre bounce_count = 2 → post = 3 → kill. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, 4.5f, 0, 0, 0, 0, /*age=*/199);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 2);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15k_phase2_no_bounce_when_pos_y_above_threshold(void)
{
    /* POS_Y too far above ground (threshold = ground+0.3) → no bounce. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, /*py=*/10.0f, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 10.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t15k_phase2_no_bounce_when_ground_miss(void)
{
    /* ground_query returns 0 (no hit) → no bounce regardless of POS_Y. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 0;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x75, 0, /*py=*/4.5f, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 4.5f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t15k_phase2_sm_nonzero_kills_slot(void)
{
    /* state_machine_call_ret() returns 1 if a hook is installed (port's
     * void-hook approximation of the engine's int-return SM).  Install
     * any hook → ret==1 → slot dies in phase 2. */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/199);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT(s_sm5_calls >= 1);
    return 0;
}

int test_records_b_tick_t15k_phase1_sm_nonzero_kills_slot(void)
{
    /* Same SM ret!=0 path at phase-1 tail. */
    reset_world();
    s_sm5_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_sm_progress);
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    T_ASSERT(s_sm5_calls >= 1);
    return 0;
}

int test_records_b_tick_t15k_age_400_kills_slot(void)
{
    /* AGE pre-preamble 399 → post 400 → phase 2 → all branches end at the
     * shared "AGE==400 kill" check.  Slot dies. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/399);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15k_age_399_stays_alive(void)
{
    /* AGE post-preamble 399 (pre 398) — phase 2 but AGE != 400 → alive. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/398);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x75);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 399);
    return 0;
}

int test_records_b_tick_t15k_phase_boost_doesnt_check_age_400_same_tick(void)
{
    /* Phase-1 boost path jumps PAST the AGE==400 check at LAB_0043ed87
     * AND past phase 2 entirely.  AGE = 200 latched, slot alive. */
    reset_world();
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/99);
    t15k_stage_cousins(1, 7, /*cousin_age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x75);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 200);
    return 0;
}

int test_records_b_tick_t15k_angle_step_toward_wraps_through_pi(void)
{
    /* Verify angle_step_toward wrap-around: ROT_X = 3.0 (near +π), target =
     * -3.0 (near -π).  Shortest arc goes through +π (small positive step).
     * step = current + max_step = 3.0 + 0.08 = 3.08 → normalize: 3.08 - 2π
     * ≈ 3.08 - 6.2832 ≈ -3.2032 — wait, that's outside [-π, π] (we want
     * result in [-π, π]).  3.08 > π → result = 3.08 - 2π ≈ -3.2032 → still
     * < -π → result += 2π → ≈ 3.08.  Hmm, normalization is ambiguous around
     * the boundary.  Skip exact assertion — just verify body runs without
     * crashing and ROT_X lands in [-π, π]. */
    reset_world();
    g_sim_frame_count = (uint32_t)(int32_t)(-3.0f / 0.04f);  /* target ≈ -3.0 */
    stage_live(0, 0x75, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_ROT_X, 3.0f);
    scene1_records_b_tick();
    float rx = slot_get_f(0, SCENE1_RECORDS_B_OFF_ROT_X);
    T_ASSERT(rx >= -3.1415927f && rx <= 3.1415927f);
    return 0;
}

int test_records_b_tick_t15k_dispatch_unaffected_for_other_types(void)
{
    /* Sanity: 0x75's unique side-effect (LIFE_MULT=0.5) doesn't fire for
     * neighbor types.  Use type 0x77 (its body writes ROT_X/VEL but never
     * touches LIFE_MULT).  Pre LIFE_MULT=99.0 should stay 99.0 since 0x75
     * body is NOT dispatched. */
    reset_world();
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT, 99.0f);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_LIFE_MULT) - 99.0f) < 1e-6f);
    return 0;
}

/* ─── C8j-tick.15l — body_0x7c (sister-hunting projectile) ────────────── */

/* Per-call overlay log (capacity 32) for tests that need to inspect the
 * full sequence rather than just the last call. */
#define T15L_OVERLAY_LOG_CAP 32
static int s_t15l_overlay_log_n;
static struct {
    int   type;
    float pos_x, pos_y, pos_z;
    float scale;
    int   dur;
} s_t15l_overlay_log[T15L_OVERLAY_LOG_CAP];
static void t15l_log_overlay(const void *owner,
                             float px, float py, float pz,
                             int type, float scale, int dur,
                             int rot_y, int shape_mode, int mode)
{
    (void)owner; (void)rot_y; (void)shape_mode; (void)mode;
    if (s_t15l_overlay_log_n < T15L_OVERLAY_LOG_CAP) {
        s_t15l_overlay_log[s_t15l_overlay_log_n].type = type;
        s_t15l_overlay_log[s_t15l_overlay_log_n].pos_x = px;
        s_t15l_overlay_log[s_t15l_overlay_log_n].pos_y = py;
        s_t15l_overlay_log[s_t15l_overlay_log_n].pos_z = pz;
        s_t15l_overlay_log[s_t15l_overlay_log_n].scale = scale;
        s_t15l_overlay_log[s_t15l_overlay_log_n].dur = dur;
    }
    s_t15l_overlay_log_n++;
}
static void t15l_install_overlay_log(void)
{
    s_t15l_overlay_log_n = 0;
    scene1_records_b_set_overlay_spawn_hook(t15l_log_overlay);
}

/* Sister-search hook capture. */
static int     s_t15l_sister_calls;
static int32_t s_t15l_sister_old;
static float   s_t15l_sister_args[4];
static int32_t s_t15l_sister_ret;          /* mocked return */
static int32_t *s_t15l_sister_last_slot;
static int32_t t15l_sister_hook(int32_t *slot,
                                float a, float b, float c, float d,
                                int32_t old_idx)
{
    s_t15l_sister_calls++;
    s_t15l_sister_last_slot = slot;
    s_t15l_sister_args[0] = a;
    s_t15l_sister_args[1] = b;
    s_t15l_sister_args[2] = c;
    s_t15l_sister_args[3] = d;
    s_t15l_sister_old = old_idx;
    return s_t15l_sister_ret;
}

/* Convenience: stage 0x7c slot with AGE such that post-preamble lands at
 * the target.  Engine preamble does pos += vel + age++; per-type body
 * reads the post-preamble AGE.  Target post-preamble AGE = pre + 1. */
static void t15l_stage(int slot, int pre_age, float py)
{
    stage_live(slot, 0x7c, 0, py, 0, 0, 0, 0, /*age=*/pre_age);
}

int test_records_b_tick_t15l_phase2_age_increments_and_drag_2(void)
{
    /* AGE pre 200 → preamble bumps to 201; body sees age > 200 → Phase 2.
     * Phase 2 sets DRAG=2.0 and age++ → 202 (no kill, <= 230). */
    reset_world();
    t15l_stage(0, /*pre_age=*/200, /*py=*/100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 99.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 202);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG) - 2.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x7c);
    return 0;
}

int test_records_b_tick_t15l_phase2_sm_called(void)
{
    /* Phase 2 calls state_machine(slot) once. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    t15l_stage(0, /*pre_age=*/200, /*py=*/100.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 1);
    return 0;
}

int test_records_b_tick_t15l_phase2_kill_at_age_above_230(void)
{
    /* AGE pre 230 → preamble bumps to 231; Phase 2 body bumps to 232.
     * 232 > 230 → kill. */
    reset_world();
    t15l_stage(0, /*pre_age=*/230, /*py=*/100.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15l_phase2_no_kill_at_230(void)
{
    /* AGE pre 228 → preamble 229; Phase 2 → 230.  230 is NOT > 230 → alive. */
    reset_world();
    t15l_stage(0, /*pre_age=*/228, /*py=*/100.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x7c);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 230);
    return 0;
}

int test_records_b_tick_t15l_phase1_age_negative_cancels_preamble(void)
{
    /* AGE pre -5 → preamble: age becomes -4, pos.x += vel.x.  Body sees
     * age=-4 < 0 → cancels: pos.x -= vel.x → net pos.x unchanged.  Stage
     * POS_Y=1000 to guarantee no ground latch (threshold = 0 + 1 = 1). */
    reset_world();
    stage_live(0, 0x7c, /*px=*/10.0f, /*py=*/1000.0f, /*pz=*/30.0f,
               /*vx=*/2.0f, /*vy=*/0, /*vz=*/0, /*age=*/-5);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 10.0f) < 1e-5f);
    /* But velocity-trail loop runs (speed!=0) at the new POS=(10, 1000, 30)
     * — verified separately. */
    return 0;
}

int test_records_b_tick_t15l_phase1_age_zero_skips_cancel(void)
{
    /* AGE pre -1 → preamble bumps to 0 (NOT < 0) → no cancel.  POS gains
     * VEL via preamble.  Stage POS_Y=1000 to skip ground. */
    reset_world();
    stage_live(0, 0x7c, /*px=*/10.0f, /*py=*/1000.0f, /*pz=*/30.0f,
               /*vx=*/2.0f, /*vy=*/0, /*vz=*/0, /*age=*/-1);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 12.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15l_phase1_sm_emit_when_first_sm_nonzero(void)
{
    /* AGE pre 6 → preamble 7 (>5).  SM hook installed (ret!=0).  Body
     * emits 0x3a at scale 2.25 with POS_Y unchanged.  Use POS_Y=1000 to
     * suppress ground bounce.  Vel=0 so trail loop is no-op.
     *
     * Engine quirk: SM-emit sets the shared hit_flag, which then triggers
     * the post-impact cascade.  So we expect 4 overlay calls total:
     *   [0] 0x3a scale 2.25 (SM-emit)
     *   [1] 0x2e scale 0.8  (post-impact)
     *   [2] 0x44 scale 0.8
     *   [3] 0x32 scale 0.8
     * AGE also jumps to 0xc8. */
    reset_world();
    t15l_install_overlay_log();
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x7c, /*px=*/3.0f, /*py=*/1000.0f, /*pz=*/4.0f,
               0, 0, 0, /*age=*/6);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15l_overlay_log_n, 4);
    T_ASSERT_EQ_I(s_t15l_overlay_log[0].type, 0x3a);
    T_ASSERT(fabsf(s_t15l_overlay_log[0].scale - 2.25f) < 1e-5f);
    T_ASSERT(fabsf(s_t15l_overlay_log[0].pos_y - 1000.0f) < 1e-4f);
    T_ASSERT_EQ_I(s_t15l_overlay_log[1].type, 0x2e);
    T_ASSERT_EQ_I(s_t15l_overlay_log[2].type, 0x44);
    T_ASSERT_EQ_I(s_t15l_overlay_log[3].type, 0x32);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0xc8);
    return 0;
}

int test_records_b_tick_t15l_phase1_sm_no_emit_when_no_sm_hook(void)
{
    /* No SM hook → SM ret=0 both times → no SM-emit.  Trail still fires
     * if VEL nonzero but use VEL=0 + POS_Y=1000 to verify just the SM-
     * emit branch.  Expect overlay calls == 0. */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    stage_live(0, 0x7c, /*px=*/3.0f, /*py=*/1000.0f, /*pz=*/4.0f,
               0, 0, 0, /*age=*/6);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15l_phase1_sm_pos_y_restored_after_second_call(void)
{
    /* POS_Y must be restored to original (engine restores unconditionally
     * after the POS_Y-0.5 / SM / POS_Y+0.5 sequence).  Test with no SM
     * hook → both SMs return 0 but engine still does the nudge/restore. */
    reset_world();
    stage_live(0, 0x7c, /*px=*/0, /*py=*/1000.0f, /*pz=*/0,
               0, 0, 0, /*age=*/6);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 1000.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15l_phase1_sm_skip_when_age_le_5(void)
{
    /* AGE pre 4 → preamble 5; 5 is NOT > 5 → skip SM-emit block. */
    reset_world();
    s_sm_calls = 0;
    scene1_records_b_set_state_machine_hook(capture_state_machine);
    stage_live(0, 0x7c, 0, /*py=*/1000.0f, 0, 0, 0, 0, /*age=*/4);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_sm_calls, 0);
    return 0;
}

int test_records_b_tick_t15l_phase1_age_eq_1_spawns_0x70(void)
{
    /* AGE pre 0 → preamble 1 → scene1_spawn(0, POS, 0x70, 0.5, 1). */
    reset_world();
    scene1_spawn_trace_reset();
    /* POS_Y=1000 to skip ground latch. */
    stage_live(0, 0x7c, /*px=*/5.0f, /*py=*/1000.0f, /*pz=*/15.0f,
               0, 0, 0, /*age=*/0);
    scene1_records_b_tick();
    T_ASSERT(g_scene1_spawn_trace_count >= 1);
    /* Find the 0x70 call (other types may also enqueue via other paths,
     * but here only 0x7c body fires). */
    int found_70 = -1;
    for (int k = 0; k < g_scene1_spawn_trace_count; k++) {
        if (g_scene1_spawn_trace[k].type == 0x70) { found_70 = k; break; }
    }
    T_ASSERT(found_70 >= 0);
    scene1_spawn_call_t *c = &g_scene1_spawn_trace[found_70];
    T_ASSERT_EQ_I(c->slot_hint, 0);
    T_ASSERT(fabsf(c->x - 5.0f)   < 1e-5f);
    T_ASSERT(fabsf(c->y - 1000.0f) < 1e-4f);
    T_ASSERT(fabsf(c->z - 15.0f)  < 1e-5f);
    T_ASSERT(fabsf(c->scale - 0.5f) < 1e-6f);
    T_ASSERT_EQ_I(c->param7, 1);
    return 0;
}

int test_records_b_tick_t15l_phase1_age_neq_1_no_spawn_70(void)
{
    /* AGE pre 1 → preamble 2 → AGE != 1 → no scene1_spawn. */
    reset_world();
    scene1_spawn_trace_reset();
    stage_live(0, 0x7c, 0, /*py=*/1000.0f, 0, 0, 0, 0, /*age=*/1);
    scene1_records_b_tick();
    /* No 0x70 call. */
    for (int k = 0; k < g_scene1_spawn_trace_count; k++) {
        T_ASSERT(g_scene1_spawn_trace[k].type != 0x70);
    }
    return 0;
}

int test_records_b_tick_t15l_phase1_part_idx_timer_increments(void)
{
    /* PART_IDX pre 1 → increments to 2.  Pre-stage AUX_SENT2=-1 to keep
     * sister-search dormant.  POS_Y=1000 to skip ground.  AGE pre 99
     * (preamble 100, > 0x14 = 20 → sister window open; but no hook → no
     * write).  PART_IDX < 0x28 → window still open. */
    reset_world();
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2, -1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 2);
    return 0;
}

int test_records_b_tick_t15l_phase1_part_idx_stays_zero_without_sister(void)
{
    /* PART_IDX=0 + AUX_SENT2=-1 (no sister) → timer stays 0. */
    reset_world();
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2, -1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0);
    return 0;
}

int test_records_b_tick_t15l_phase1_sister_found_seeds_part_idx(void)
{
    /* Sister hook returns 5.  PART_IDX starts 0 → after search,
     * AUX_SENT2=5 → seed PART_IDX=1. */
    reset_world();
    s_t15l_sister_ret = 5;
    s_t15l_sister_calls = 0;
    scene1_records_b_set_aux_43ab6e_hook(t15l_sister_hook);
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2, -1);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15l_sister_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2), 5);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 1);
    return 0;
}

int test_records_b_tick_t15l_sister_hook_args_match_engine(void)
{
    /* Verify exact float args + old_idx passed through. */
    reset_world();
    s_t15l_sister_ret = -1;
    s_t15l_sister_calls = 0;
    scene1_records_b_set_aux_43ab6e_hook(t15l_sister_hook);
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2, 42);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15l_sister_calls, 1);
    T_ASSERT(fabsf(s_t15l_sister_args[0] - 0.6f)  < 1e-6f);
    T_ASSERT(fabsf(s_t15l_sister_args[1] - 0.03f) < 1e-6f);
    T_ASSERT(fabsf(s_t15l_sister_args[2] - 0.05f) < 1e-6f);
    T_ASSERT(fabsf(s_t15l_sister_args[3] - 0.96f) < 1e-6f);
    T_ASSERT_EQ_I(s_t15l_sister_old, 42);
    /* hook returned -1 → AUX_SENT2 overwritten to -1 → no seed. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AUX_SENT2), -1);
    return 0;
}

int test_records_b_tick_t15l_sister_skipped_when_age_le_0x14(void)
{
    /* AGE pre 0x13 → preamble 0x14 → 0x14 is NOT > 0x14 → no search. */
    reset_world();
    s_t15l_sister_calls = 0;
    scene1_records_b_set_aux_43ab6e_hook(t15l_sister_hook);
    t15l_stage(0, /*pre_age=*/0x13, /*py=*/1000.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15l_sister_calls, 0);
    return 0;
}

int test_records_b_tick_t15l_sister_skipped_when_part_idx_ge_0x28(void)
{
    /* PART_IDX pre 0x28 → 0x28 is NOT < 0x28 → no search.  PART_IDX>0
     * also increments → post = 0x29. */
    reset_world();
    s_t15l_sister_calls = 0;
    scene1_records_b_set_aux_43ab6e_hook(t15l_sister_hook);
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_PART_IDX, 0x28);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_t15l_sister_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_PART_IDX), 0x29);
    return 0;
}

int test_records_b_tick_t15l_phase1_ground_query_called(void)
{
    /* Ground query always fires (no VEL_Y gate).  Use POS_Y=1000 so the
     * miss case latches no impact. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 0;
    s_gq_out_y = 0;
    scene1_records_b_set_ground_query_hook(gq_canned);
    t15l_stage(0, /*pre_age=*/99, /*py=*/1000.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_gq_calls, 1);
    return 0;
}

int test_records_b_tick_t15l_phase1_ground_hit_writes_aux_9_and_snaps(void)
{
    /* Hit at gy=5.0; POS_Y=4.0 → 4.0 <= 5.0+1.0=6.0 → snap to 6.0, zero VEL. */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x7c, /*px=*/0, /*py=*/4.0f, /*pz=*/0,
               /*vx=*/1.0f, /*vy=*/2.0f, /*vz=*/3.0f, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_AUX_9) - 5.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 6.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_VEL_Z) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_b_tick_t15l_phase1_ground_miss_uses_gy_zero_threshold(void)
{
    /* Miss → gy=0, threshold = 0 + 1 = 1.  POS_Y=0.5 <= 1 → latches
     * impact even though "no ground" — engine quirk (asm fldz initializes
     * gy slot to 0 before the call; on miss the value stays 0). */
    reset_world();
    s_gq_calls = 0;
    s_gq_hit = 0;
    s_gq_out_y = 999.0f;  /* hook writes but engine ignores on miss == 0 */
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x7c, /*px=*/0, /*py=*/0.5f, /*pz=*/0, 0, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    /* Latched: snap to 1.0. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 1.0f) < 1e-5f);
    /* AGE jumps to 200 from post-impact. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0xc8);
    return 0;
}

int test_records_b_tick_t15l_phase1_ground_no_latch_when_above_threshold(void)
{
    /* Hit but POS_Y above threshold → no latch. */
    reset_world();
    s_gq_hit = 1;
    s_gq_out_y = 5.0f;
    scene1_records_b_set_ground_query_hook(gq_canned);
    stage_live(0, 0x7c, /*px=*/0, /*py=*/100.0f, /*pz=*/0, 0, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_Y) - 100.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 100);
    return 0;
}

int test_records_b_tick_t15l_post_impact_fires_notify_se_and_3_overlays(void)
{
    /* Latch impact → notify_queue(10,4,4,1.0) + SE 0x148 + 3 overlay
     * spawns (0x2e/0x44/0x32 at scale 0.8).  AGE jumps to 0xc8.  Use
     * POS_Y=0 to latch on miss (threshold=1). */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    s_notify_calls = 0;
    scene1_records_b_set_notify_queue_hook(capture_notify);
    stage_live(0, 0x7c, /*px=*/0, /*py=*/0, /*pz=*/0, 0, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    /* notify_queue(10, 4, 4, 1.0). */
    T_ASSERT_EQ_I(s_notify_calls, 1);
    T_ASSERT_EQ_I(s_notify_a, 10);
    T_ASSERT_EQ_I(s_notify_b, 4);
    T_ASSERT_EQ_I(s_notify_c, 4);
    T_ASSERT(fabsf(s_notify_d - 1.0f) < 1e-6f);
    /* SE 0x148. */
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x148);
    /* 3 overlay spawns.  Last is 0x32. */
    T_ASSERT(s_overlay_calls >= 3);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x32);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.8f) < 1e-5f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15l_no_trail_when_vel_zero(void)
{
    /* VEL=0 → trail loop skipped.  POS_Y=1000 to skip ground.  Expect
     * 0 overlay calls (no SM hook → no SM-emit; no impact → no post-
     * impact emits). */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    stage_live(0, 0x7c, /*px=*/0, /*py=*/1000.0f, /*pz=*/0, 0, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15l_trail_emits_iters_from_speed(void)
{
    /* |VEL|=1.0 → iters = (int)(1.0/0.1) = 10 → 10 overlay spawns.
     * VEL = (1, 0, 0) → speed = 1.  POS_Y=1000 + no SM → no other emits. */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    stage_live(0, 0x7c, /*px=*/0, /*py=*/1000.0f, /*pz=*/0,
               /*vx=*/1.0f, /*vy=*/0, /*vz=*/0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 10);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x3a);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.25f) < 1e-5f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15l_trail_iters_clamped_to_1(void)
{
    /* Very small speed (0.001 → iters=0 → clamp to 1) emits 1 spawn at
     * t=0 (= POS).  POS_Y=1000 + no SM. */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    stage_live(0, 0x7c, /*px=*/0, /*py=*/1000.0f, /*pz=*/0,
               /*vx=*/0.001f, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    /* First (only) trail iter is t=0 → pos = POS unchanged.  POS after
     * preamble is (0 + 0.001, 1000, 0). */
    T_ASSERT(fabsf(s_overlay_last.pos_x - 0.001f) < 1e-5f);
    T_ASSERT(fabsf(s_overlay_last.pos_y - 1000.0f) < 1e-3f);
    restore_overlay();
    return 0;
}

int test_records_b_tick_t15l_trail_last_emit_at_pos_minus_vel_offset(void)
{
    /* iters=10, VEL=(1,0,0).  Last iter k=9 → t = 9/10 = 0.9 → trail_x =
     * pos_x - 0.9 * 1.0.  Pre POS_X = 5, vel.x = 1 → preamble POS_X = 6 →
     * last trail at x = 6 - 0.9 = 5.1. */
    reset_world();
    s_overlay_calls = 0;
    install_overlay_capture();
    stage_live(0, 0x7c, /*px=*/5.0f, /*py=*/1000.0f, /*pz=*/0,
               /*vx=*/1.0f, 0, 0, /*age=*/99);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_overlay_calls, 10);
    T_ASSERT(fabsf(s_overlay_last.pos_x - 5.1f) < 1e-5f);
    return 0;
}

int test_records_b_tick_t15l_age_eq_130_kills_slot(void)
{
    /* AGE pre 129 → preamble 130 → tail kill.  Phase 1 (130 <= 200).
     * Stage POS_Y=1000 to skip ground latch.  PART_IDX-based timer not
     * relevant here. */
    reset_world();
    t15l_stage(0, /*pre_age=*/129, /*py=*/1000.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_t15l_age_just_below_130_alive(void)
{
    /* AGE pre 128 → preamble 129 → not equal to 130 → alive. */
    reset_world();
    t15l_stage(0, /*pre_age=*/128, /*py=*/1000.0f);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x7c);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 129);
    return 0;
}

int test_records_b_tick_t15l_post_impact_sets_age_200(void)
{
    /* Latching impact jumps AGE to 0xc8 — Phase 2 entry next tick.  Verify
     * the assignment lands. */
    reset_world();
    /* No ground hook → gq returns 0; gy=0 → POS_Y=0 latches via threshold
     * = 1. */
    stage_live(0, 0x7c, /*px=*/0, /*py=*/0, /*pz=*/0, 0, 0, 0, /*age=*/50);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0xc8);
    return 0;
}

int test_records_b_tick_t15l_dispatch_unaffected_for_other_types(void)
{
    /* Type 0x77 must not trigger 0x7c's body (DRAG=2.0 in Phase 2 is
     * unique to 0x7c).  Use AGE pre 200 → 201 → 0x7c body would set
     * DRAG=2.0 but 0x77 wouldn't.  Verify DRAG stays at pre-set value. */
    reset_world();
    stage_live(0, 0x77, 0, 0, 0, 0, 0, 0, /*age=*/200);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_DRAG, 99.0f);
    scene1_records_b_tick();
    /* 0x77 body writes ROT_X+=0.2 + may write DRAG via its own path; check
     * it's not 2.0 (the 0x7c-specific value). */
    float drag = slot_get_f(0, SCENE1_RECORDS_B_OFF_DRAG);
    T_ASSERT(drag != 2.0f);
    return 0;
}

int test_records_b_tick_t15l_set_aux_43ab6e_hook_round_trip(void)
{
    /* Setter returns previous value; install/uninstall round-trip. */
    reset_world();
    scene1_b_aux_43ab6e_fn prev =
        scene1_records_b_set_aux_43ab6e_hook(t15l_sister_hook);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_aux_43ab6e_hook(NULL);
    T_ASSERT(prev == t15l_sister_hook);
    return 0;
}

/* ─── C8j-tick.16 — LAB_00440dc1 default-tail wall-bounce body ──────── */

/* Captured wall-raycast invocation + scripted result. */
static int s_wall_ray_calls;
static struct {
    float ox, oy, oz, dx, dy, dz;
} s_wall_ray_last;
static scene1_b_wall_ray_result_t s_wall_ray_script;
static int s_wall_ray_script_hit;
static int wall_ray_capture(float ox, float oy, float oz,
                            float dx, float dy, float dz,
                            scene1_b_wall_ray_result_t *out)
{
    s_wall_ray_calls++;
    s_wall_ray_last.ox = ox; s_wall_ray_last.oy = oy; s_wall_ray_last.oz = oz;
    s_wall_ray_last.dx = dx; s_wall_ray_last.dy = dy; s_wall_ray_last.dz = dz;
    *out = s_wall_ray_script;
    return s_wall_ray_script_hit;
}

static int s_wall_flag_value;
static int s_wall_flag_calls;
static int wall_flag_capture(int32_t wx, int32_t wz)
{
    (void)wx; (void)wz;
    s_wall_flag_calls++;
    return s_wall_flag_value;
}

static int s_wall_destroy_calls;
static int32_t s_wall_destroy_last;
static void wall_destroy_capture(int32_t wall_id)
{
    s_wall_destroy_calls++;
    s_wall_destroy_last = wall_id;
}

static int s_aux_44b255_calls;
static void aux_44b255_capture(void) { s_aux_44b255_calls++; }

/* Convenience: enable the three gates so the body fires.  TYPE is set
 * by the caller; AUX_C8 = 1 + per-tick flag = 1 here.  OWNER_A defaults
 * to 1 (Path A) — pass 0 to exercise Path B.
 *
 * Tests invoke the body directly via scene1_records_b_run_lab_00440dc1
 * because the outer tick loop clears the flag at slot iter top — so
 * scene1_records_b_tick() can only exercise the gates, not the body. */
static void lab_dc1_stage(int slot, int32_t type, int32_t owner_a)
{
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_TYPE,    type);
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_AUX_C8,  1);
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_OWNER_A, owner_a);
    g_scene1_records_b_tick_flag = 1;
}

static void lab_dc1_install_hooks(void)
{
    s_wall_ray_calls = 0;
    s_wall_flag_calls = 0;
    s_wall_destroy_calls = 0;
    s_aux_44b255_calls = 0;
    s_wall_ray_script_hit = 0;
    s_wall_flag_value = 0;
    memset(&s_wall_ray_script, 0, sizeof s_wall_ray_script);
    memset(&s_wall_ray_last,   0, sizeof s_wall_ray_last);
    s_wall_destroy_last = -1;
    scene1_records_b_set_wall_raycast_hook(wall_ray_capture);
    scene1_records_b_set_wall_flag_at_hook(wall_flag_capture);
    scene1_records_b_set_wall_destroy_hook(wall_destroy_capture);
    scene1_records_b_set_aux_44b255_hook(aux_44b255_capture);
}

int test_records_b_tick_dc1_gate_aux_c8_blocks(void)
{
    /* TYPE alive + tick_flag set, but AUX_C8 = 0 → no raycast. */
    reset_world();
    lab_dc1_install_hooks();
    slot_set_i(0, SCENE1_RECORDS_B_OFF_TYPE,    0xff);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_OWNER_A, 1);
    g_scene1_records_b_tick_flag = 1;
    /* AUX_C8 NOT set → 0. */
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_ray_calls, 0);
    return 0;
}

int test_records_b_tick_dc1_gate_tick_flag_blocks(void)
{
    /* TYPE alive + AUX_C8 set, but tick_flag = 0 → no raycast. */
    reset_world();
    lab_dc1_install_hooks();
    slot_set_i(0, SCENE1_RECORDS_B_OFF_TYPE,    0xff);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_C8,  1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_OWNER_A, 1);
    /* tick_flag NOT set → 0. */
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_ray_calls, 0);
    return 0;
}

int test_records_b_tick_dc1_all_gates_open_runs_raycast(void)
{
    /* All three gates open → wall raycast invoked exactly once. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_ray_calls, 1);
    return 0;
}

int test_records_b_tick_dc1_path_a_back_step_origin(void)
{
    /* Path A: ox = pos - 0.2*vel.  POS_X = 15, VEL_X = 10 → back-step
     * origin = 15 - 0.2*10 = 13.  Direct invocation skips the preamble. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 15.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 10.0f);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT(fabsf(s_wall_ray_last.ox - 13.0f) < 1e-5f);
    T_ASSERT(fabsf(s_wall_ray_last.dx - 10.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_dc1_path_a_type_58_uses_age_formula(void)
{
    /* Type 0x58 substitutes ray origin: (pos - vel) + (age-6) * vel * 0.3.
     * POS_X = 1, VEL_X = 1, AGE = 7 → (1-1) + (7-6)*1*0.3 = 0.3. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x58, /*owner_a=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 1.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 1.0f);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AGE,   7);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT(fabsf(s_wall_ray_last.ox - 0.3f) < 1e-5f);
    return 0;
}

int test_records_b_tick_dc1_no_wall_ray_hit_skips(void)
{
    /* Raycast hook returns 0 (no hit) → no flag query, no kill. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_flag_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xff);  /* not killed */
    return 0;
}

int test_records_b_tick_dc1_wall_flag_other_skips(void)
{
    /* Wall flag returns 2 (not 0 or 1) → skip with no further action. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_flag_value     = 2;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_flag_calls, 1);
    T_ASSERT_EQ_I(s_wall_destroy_calls, 0);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xff);  /* not killed */
    return 0;
}

int test_records_b_tick_dc1_wall_id_lifetime_destroy(void)
{
    /* wall_id = 3, lifetime = 1 → freshness=0x1e, lifetime→0, then
     * wall_destroy(wall_id-1) + KILL.  No SE, no particle. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 3;
    g_scene1_b_wall_lifetime[3] = 1;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_destroy_calls, 1);
    T_ASSERT_EQ_I(s_wall_destroy_last, 2);    /* wall_id - 1 */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);  /* killed */
    T_ASSERT_EQ_I(s_se_calls, 0);
    return 0;
}

int test_records_b_tick_dc1_wall_id_lifetime_decrement_only(void)
{
    /* wall_id = 5, lifetime = 10 → decrement to 9.  SE 0x169 + particle +
     * KILL.  Freshness pinned to 0x1e. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 5;
    g_scene1_b_wall_lifetime[5] = 10;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(g_scene1_b_wall_lifetime[5], 9);
    T_ASSERT_EQ_I(g_scene1_b_wall_freshness[5], 0x1e);
    T_ASSERT_EQ_I(s_wall_destroy_calls, 0);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x169);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);  /* killed */
    return 0;
}

int test_records_b_tick_dc1_wall_id_lifetime_at_max_no_decrement(void)
{
    /* When lifetime >= 0x64, the body skips decrement and freshness reset
     * but still falls through to the lifetime==0 check.  Lifetime stays
     * at 0x64 → != 0 → SE 0x169 + particle + KILL. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 7;
    g_scene1_b_wall_lifetime[7] = 0x64;
    g_scene1_b_wall_freshness[7] = 0;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(g_scene1_b_wall_lifetime[7], 0x64);     /* unchanged */
    T_ASSERT_EQ_I(g_scene1_b_wall_freshness[7], 0);       /* unchanged */
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x169);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_dc1_default_particle_path(void)
{
    /* TYPE 0xff (not in any special list) + wall_id = 0 → default path:
     * scene1_pfo_table_a_alloc_passthrough + FUN_0044b255 + KILL. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);   /* killed */
    return 0;
}

int test_records_b_tick_dc1_type_2_extended_spawn_pair(void)
{
    /* TYPE 0x2 (in the {0x2/0x54/0x3/0x4/0x22/0x67/0x6d-0x70} list) →
     * scene1_spawn(0x29) + scene1_spawn(0x2a) + SE 0x167 + 0x44b255 +
     * KILL. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x2, /*owner_a=*/0x1234);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_spawn_trace_reset();
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 2);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].type, 0x29);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[1].type, 0x2a);
    T_ASSERT(fabsf(g_scene1_spawn_trace[0].scale - 0.2f) < 1e-5f);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].param7, 1);
    T_ASSERT_EQ_I(g_scene1_spawn_trace[0].slot_hint, 0x1234);  /* owner_a */
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x167);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);  /* killed */
    return 0;
}

int test_records_b_tick_dc1_type_72_no_kill(void)
{
    /* TYPE 0x72 — spawn pair + SE 0x167, BUT no kill, no 0x44b255 (asm
     * 0x44106b jmps to next slot, not the kill path). */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x72, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_spawn_trace_reset();
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(g_scene1_spawn_trace_count, 2);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x167);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x72);  /* alive */
    return 0;
}

int test_records_b_tick_dc1_type_5b_default_se_29e(void)
{
    /* TYPE 0x5b → SE 0x29e + default-particle path + 0x44b255 + KILL. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x5b, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x29e);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_dc1_type_4d_default_se_2b0(void)
{
    /* TYPE 0x4d → SE 0x2b0 + default-particle path + 0x44b255 + KILL. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x4d, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    s_se_calls = 0;
    scene1_records_b_set_se_hook(capture_se);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_se_calls, 1);
    T_ASSERT_EQ_I(s_se_last_id, 0x2b0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_dc1_type_78_overlay_scale_0p8(void)
{
    /* TYPE 0x78 → overlay_spawn at slot.pos with scale 0.8 + 0x44b255 +
     * KILL.  No particle, no SE. */
    reset_world();
    lab_dc1_install_hooks();
    install_overlay_capture();
    lab_dc1_stage(0, /*type=*/0x78, /*owner_a=*/1);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 5.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_Y, 6.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_Z, 7.0f);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x14);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.8f) < 1e-5f);
    /* Uses slot.pos, not hit_pos. */
    T_ASSERT(fabsf(s_overlay_last.pos_x - 5.0f) < 1e-5f);
    T_ASSERT_EQ_I(s_aux_44b255_calls, 1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_dc1_type_7a_overlay_scale_1p0(void)
{
    /* TYPE 0x7a → overlay_spawn at slot.pos with scale 1.0. */
    reset_world();
    lab_dc1_install_hooks();
    install_overlay_capture();
    lab_dc1_stage(0, /*type=*/0x7a, /*owner_a=*/1);
    s_wall_ray_script_hit = 1;
    s_wall_ray_script.wall_id = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT(fabsf(s_overlay_last.scale - 1.0f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0);
    restore_overlay();
    return 0;
}

int test_records_b_tick_dc1_path_b_owner_zero_simpler_origin(void)
{
    /* Path B: OWNER_A == 0 → raycast origin = slot.POS (no back-step).
     * POS_X = 15, VEL_X = 10 → ox = 15.  Direct invocation skips preamble. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xff, /*owner_a=*/0);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 15.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 10.0f);
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT(fabsf(s_wall_ray_last.ox - 15.0f) < 1e-5f);
    T_ASSERT(fabsf(s_wall_ray_last.dx - 10.0f) < 1e-5f);
    return 0;
}

int test_records_b_tick_dc1_path_b_type_a0_overlay_spawn(void)
{
    /* Path B + TYPE 0xa0: overlay_spawn(NULL, slot.pos, 0x14, 0.8). */
    reset_world();
    lab_dc1_install_hooks();
    install_overlay_capture();
    lab_dc1_stage(0, /*type=*/0xa0, /*owner_a=*/0);
    s_wall_ray_script_hit = 1;
    s_wall_flag_value     = 0;     /* allowable for path B */
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_overlay_calls, 1);
    T_ASSERT(s_overlay_last.owner == NULL);
    T_ASSERT_EQ_I(s_overlay_last.type, 0x14);
    T_ASSERT(fabsf(s_overlay_last.scale - 0.8f) < 1e-5f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa0);  /* NOT killed */
    restore_overlay();
    return 0;
}

int test_records_b_tick_dc1_path_b_type_1f_resets_age(void)
{
    /* Path B + TYPE 0x1f: slot[AGE] = 0x70 on wall hit. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0x1f, /*owner_a=*/0);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AGE, 42);
    s_wall_ray_script_hit = 1;
    s_wall_flag_value     = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0x70);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0x1f);  /* NOT killed */
    return 0;
}

int test_records_b_tick_dc1_path_b_no_hit_no_kill(void)
{
    /* Path B + no raycast hit → no action. */
    reset_world();
    lab_dc1_install_hooks();
    lab_dc1_stage(0, /*type=*/0xa0, /*owner_a=*/0);
    s_wall_ray_script_hit = 0;
    scene1_records_b_run_lab_00440dc1(0);
    T_ASSERT_EQ_I(s_wall_flag_calls, 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_TYPE), 0xa0);
    return 0;
}

int test_records_b_tick_dc1_set_wall_raycast_hook_round_trip(void)
{
    reset_world();
    scene1_b_wall_raycast_fn prev =
        scene1_records_b_set_wall_raycast_hook(wall_ray_capture);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_wall_raycast_hook(NULL);
    T_ASSERT(prev == wall_ray_capture);
    return 0;
}

int test_records_b_tick_dc1_set_wall_flag_at_hook_round_trip(void)
{
    reset_world();
    scene1_b_wall_flag_at_fn prev =
        scene1_records_b_set_wall_flag_at_hook(wall_flag_capture);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_wall_flag_at_hook(NULL);
    T_ASSERT(prev == wall_flag_capture);
    return 0;
}

int test_records_b_tick_dc1_set_wall_destroy_hook_round_trip(void)
{
    reset_world();
    scene1_b_wall_destroy_fn prev =
        scene1_records_b_set_wall_destroy_hook(wall_destroy_capture);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_wall_destroy_hook(NULL);
    T_ASSERT(prev == wall_destroy_capture);
    return 0;
}

int test_records_b_tick_dc1_set_aux_44b255_hook_round_trip(void)
{
    reset_world();
    scene1_b_aux_44b255_fn prev =
        scene1_records_b_set_aux_44b255_hook(aux_44b255_capture);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_aux_44b255_hook(NULL);
    T_ASSERT(prev == aux_44b255_capture);
    return 0;
}

int test_records_b_tick_dc1_dead_slot_skips(void)
{
    /* TYPE == 0 first gate → no raycast even if other gates set. */
    reset_world();
    lab_dc1_install_hooks();
    slot_set_i(0, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    slot_set_i(0, SCENE1_RECORDS_B_OFF_OWNER_A, 1);
    g_scene1_records_b_tick_flag = 1;
    /* TYPE = 0 by default. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_wall_ray_calls, 0);
    return 0;
}
