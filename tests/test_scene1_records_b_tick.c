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

#include "scene1_records.h"
#include "scene1_records_b_tick.h"

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
#define OWNER_BLOB_3_SIZE 0xe00
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
