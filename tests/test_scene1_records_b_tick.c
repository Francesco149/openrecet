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

#include "scene1_particles_tick.h"
#include "scene1_per_frame_open.h"
#include "scene1_records.h"
#include "scene1_records_b_spawn.h"
#include "scene1_records_b_tick.h"
#include "scene1_spawn.h"

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    scene1_records_reset(1);
    g_scene1_records_b_count = 0;
    g_scene1_records_b_tick_flag = 0;
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
