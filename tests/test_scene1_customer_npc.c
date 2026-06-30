/*
 * test_scene1_customer_npc.c — in-shop browsing-customer chibi NPCs
 * (FUN_0046f8ba / FUN_0046f914 / FUN_0046fbb7 / FUN_0046fa31 / FUN_0046fbee /
 *  FUN_0047019f — the cc08==4 wandering crowd).
 *
 * RNG-EXACTNESS is the contract: the per-spawn LCG draw count (4) and the
 * per-tick retarget-burst draw count (2 LCG per try, break on first walkable
 * cell) must match retail bit-for-bit.  These assert the draw accounting and
 * the roster/cap builder.
 *
 * Walkability uses the SAME grid the display chip exposes (shop_display_grid_
 * cell over s_grid); shop_display_reset() zeros it → every cell walkable, so
 * the retarget burst breaks on its first try (a deterministic 2 draws) with no
 * furniture neighbour → the NPC stays in the retarget state and re-bursts each
 * frame.  That gives a closed-form per-frame draw count to assert against.
 */

#include "t.h"

#include <string.h>

#include "rng.h"
#include "scene1_shop_display.h"
#include "scene1_shop_walker.h"
#include "scene1_particles_tick.h"   /* g_scene1_camera_yaw (_DAT_073de39c) */

/* ── FUN_0046f8ba roster/cap builder ─────────────────────────────────────── */

int test_cs_npc_roster_tutorial_single(void)
{
    scene1_customer_npc_reset();
    /* tutorial session list = {13, -2, …} (kyaku 13 = key 0x0d → table idx 7). */
    int32_t list[0x14];
    for (int i = 0; i < 0x14; i++) list[i] = -1;
    list[0] = 0xd;
    list[1] = -2;
    scene1_customer_npc_roster_build(list);
    T_ASSERT_EQ_I(scene1_customer_npc_cap(), 1);
    return 0;
}

int test_cs_npc_roster_multi_and_order(void)
{
    scene1_customer_npc_reset();
    /* keys: 0x0c→idx0, 0x04→idx4, 0x14→idx17.  An unmatched id (0x99) is
     * skipped (no table key); the scan stops at the first negative entry. */
    int32_t list[0x14];
    for (int i = 0; i < 0x14; i++) list[i] = -1;
    list[0] = 0x0c;
    list[1] = 0x99;   /* no key match → skipped, cap unchanged */
    list[2] = 0x04;
    list[3] = 0x14;
    list[4] = -1;     /* terminator */
    scene1_customer_npc_roster_build(list);
    T_ASSERT_EQ_I(scene1_customer_npc_cap(), 3);
    return 0;
}

int test_cs_npc_roster_easydisp_gate(void)
{
    /* easydisp default 0 → builds; there is no public setter, so this asserts
     * the DEFAULT path produces a cap (the gate is closed only when the config
     * is non-zero, which the pinned scenarios never are). */
    scene1_customer_npc_reset();
    int32_t list[0x14];
    for (int i = 0; i < 0x14; i++) list[i] = -1;
    list[0] = 0x0d;
    list[1] = -1;
    scene1_customer_npc_roster_build(list);
    T_ASSERT_EQ_I(scene1_customer_npc_cap(), 1);
    return 0;
}

int test_cs_npc_roster_null_list(void)
{
    scene1_customer_npc_reset();
    scene1_customer_npc_roster_build(NULL);
    T_ASSERT_EQ_I(scene1_customer_npc_cap(), 0);
    return 0;
}

/* ── FUN_0047019f pump: spawn cadence + RNG draw accounting ───────────────── */

/* Build a 1-customer roster + an all-walkable grid (zeroed s_grid). */
static void setup_one_customer_all_walkable(void)
{
    shop_display_reset();              /* zero s_grid → every cell walkable */
    scene1_customer_npc_reset();
    int32_t list[0x14];
    for (int i = 0; i < 0x14; i++) list[i] = -1;
    list[0] = 0xd;                     /* kyaku 13 → 1 roster entry */
    list[1] = -1;
    scene1_customer_npc_roster_build(list);
}

int test_cs_npc_pump_no_spawn_before_frame30(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    /* frames 1..29: %30 != 0, no active NPCs → ZERO LCG draws each. */
    for (int f = 1; f <= 29; f++) {
        unsigned d = scene1_customer_npc_pump(1 /*sell_inactive*/, 0 /*tier*/);
        if (d != 0)
            T_FAIL("frame %d drew %u rng, expected 0 (no spawn, no NPC)", f, d);
    }
    T_ASSERT_EQ_I(scene1_customer_npc_spawned(), 0);
    return 0;
}

int test_cs_npc_pump_spawn_frame_draws_4_plus_burst(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    /* run to the spawn frame (30th). */
    unsigned spawn_draws = 0;
    for (int f = 1; f <= 30; f++)
        spawn_draws = scene1_customer_npc_pump(1, 0);

    /* frame 30 = FUN_0046f914 spawn (EXACTLY 4 LCG) + the just-spawned NPC's
     * state-(-1) retarget burst.  An all-walkable grid breaks the burst on its
     * first try = 2 LCG.  So the spawn frame draws exactly 4 + 2 = 6. */
    T_ASSERT_EQ_U(spawn_draws, 6u);
    T_ASSERT_EQ_I(scene1_customer_npc_spawned(), 1);

    /* the slot is occupied (active != -1) and still in the retarget state
     * (no furniture neighbour in the zeroed grid → WSTATE stays -1). */
    int32_t *slot = scene1_customer_npc_slot(0);
    T_ASSERT(slot != NULL);
    T_ASSERT(slot[CS_NPC_OFF_ACTIVE] != -1);
    T_ASSERT_EQ_I(slot[CS_NPC_OFF_TYPE_IDX], 7);   /* roster[0] = table idx 7 */
    T_ASSERT_EQ_I(slot[CS_NPC_OFF_WSTATE], -1);
    return 0;
}

int test_cs_npc_pump_reburst_2_per_frame(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    for (int f = 1; f <= 30; f++)
        scene1_customer_npc_pump(1, 0);

    /* frames 31..40: spawned==cap (no new spawn); the lone NPC stays in the
     * retarget state (no furniture neighbour) and re-bursts = EXACTLY 2 LCG
     * draws/frame, deterministically. */
    for (int f = 31; f <= 40; f++) {
        unsigned d = scene1_customer_npc_pump(1, 0);
        if (d != 2u)
            T_FAIL("frame %d drew %u rng, expected 2 (re-burst)", f, d);
    }
    T_ASSERT_EQ_I(scene1_customer_npc_spawned(), 1);
    return 0;
}

int test_cs_npc_pump_sell_active_blocks_spawn(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    /* sell_inactive==0 (the f404==1 scripted tutorial): NO NPC ever spawns, so
     * the pump draws ZERO RNG on every frame (incl. the 30-frame boundary). */
    for (int f = 1; f <= 90; f++) {
        unsigned d = scene1_customer_npc_pump(0 /*sell_active*/, 0);
        if (d != 0u)
            T_FAIL("frame %d drew %u rng under sell-active, expected 0", f, d);
    }
    T_ASSERT_EQ_I(scene1_customer_npc_spawned(), 0);
    return 0;
}

int test_cs_npc_pump_spawn_consumes_exactly_4(void)
{
    /* Isolate the spawn's draw count from the tick: cap the roster at 1, then
     * compare the spawn-frame total (4 + burst) against a no-NPC baseline burst
     * count taken from the SAME seed.  The burst is grid+RNG-driven, so we
     * instead assert the spawn delta directly via the closed-form all-walkable
     * burst (2): spawn_total - burst(2) == 4. */
    setup_one_customer_all_walkable();
    rng_seed(0xC0FFEE);
    unsigned spawn_total = 0;
    for (int f = 1; f <= 30; f++)
        spawn_total = scene1_customer_npc_pump(1, 0);
    /* all-walkable → burst is exactly 2; the remainder is the spawn's draws. */
    T_ASSERT_EQ_U(spawn_total - 2u, 4u);
    return 0;
}

/* ── FUN_0046fbb7 walkability (via the shared grid) ───────────────────────── */

int test_cs_npc_grid_walkable_bounds(void)
{
    shop_display_reset();              /* all-zero grid = all walkable */
    /* The pump only ever probes cells in [1,8]×[1,7] (the burst range) — all
     * in-bounds; the grid bounds check rejects out-of-range cells.  We exercise
     * the public grid accessor the walker uses for those values. */
    T_ASSERT_EQ_I(shop_display_grid_cell(0, 0), 0);   /* walkable (empty) */
    T_ASSERT_EQ_I(shop_display_grid_cell(8, 7), 0);
    /* a furniture cell (set via the rebuild path is covered elsewhere); here we
     * just confirm the zeroed baseline the burst test relies on. */
    return 0;
}

/* ── facing recompute (FUN_0046fbee wstate==2 idle + FUN_0047019f velocity) ───
 *
 * The chibi facing octant (slot[6]) is camera-relative: the engine recomputes
 * it from the walk/idle direction with the b850 converter
 * (player_ctrl_facing_octant) at cam yaw = g_scene1_camera_yaw.  The asserted
 * octants below are the exact result of the (atan2 → (a+yaw+π/8)/2π·8+8 → ftol
 * → &7) chain at the HOUSE shop yaw (π). */
int test_cs_npc_facing_idle_from_facedir(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    for (int f = 1; f <= 30; f++) scene1_customer_npc_pump(1, 0);  /* spawn slot 0 */
    int32_t *slot = scene1_customer_npc_slot(0);
    T_ASSERT(slot != NULL && slot[CS_NPC_OFF_ACTIVE] != -1);

    g_scene1_camera_yaw = 3.1415927f;          /* HOUSE shop yaw (_DAT_073de39c) */

    /* FACE_DIR → idle octant (wstate==2 zeroes vel, the velocity arm then skips,
     * so the FACE_DIR facing persists past chr_anim_tick): 0→4, 1→2, 2→0, 3→6. */
    static const int expect[4] = { 4, 2, 0, 6 };
    for (int d = 0; d < 4; d++) {
        slot[CS_NPC_OFF_WSTATE]   = 2;          /* idle/dwell */
        slot[CS_NPC_OFF_WTIMER]   = 0;          /* WTIMER 0→1, never hits >0x78 */
        slot[CS_NPC_OFF_FACE_DIR] = d;          /* +0x70 */
        slot[CS_NPC_OFF_FACING]   = 99;         /* poison → must be overwritten */
        scene1_customer_npc_pump(1, 0);
        if (slot[CS_NPC_OFF_FACING] != expect[d])
            T_FAIL("idle FACE_DIR %d → facing %d, expected %d",
                   d, slot[CS_NPC_OFF_FACING], expect[d]);
    }
    return 0;
}

int test_cs_npc_facing_walk_from_velocity(void)
{
    setup_one_customer_all_walkable();
    rng_seed(0x12345);
    for (int f = 1; f <= 30; f++) scene1_customer_npc_pump(1, 0);  /* spawn slot 0 */
    int32_t *slot = scene1_customer_npc_slot(0);
    T_ASSERT(slot != NULL && slot[CS_NPC_OFF_ACTIVE] != -1);

    g_scene1_camera_yaw = 3.1415927f;

    /* Force wstate==1 (dwell-anim arm): it leaves velocity untouched, so the
     * pump-tail velocity recompute (atan2(VEL_X, VEL_Z)) runs on the values we
     * set.  +z→4, -x→2, -z→0, +x→6 (same octant set as the cardinals). */
    struct { float vx, vz; int oct; } cases[] = {
        {  0.0f,  0.1f, 4 },   /* atan2(0,+z)  =  0    */
        { -0.1f,  0.0f, 2 },   /* atan2(-x,0)  = -π/2  */
        {  0.0f, -0.1f, 0 },   /* atan2(0,-z)  =  π    */
        {  0.1f,  0.0f, 6 },   /* atan2(+x,0)  =  π/2  */
    };
    for (int i = 0; i < 4; i++) {
        slot[CS_NPC_OFF_WSTATE] = 1;            /* dwell-anim: vel preserved */
        slot[CS_NPC_OFF_WTIMER] = 0;            /* no transition this frame */
        memcpy(&slot[CS_NPC_OFF_VEL_X], &cases[i].vx, sizeof(float));
        memcpy(&slot[CS_NPC_OFF_VEL_Z], &cases[i].vz, sizeof(float));
        slot[CS_NPC_OFF_FACING] = 99;           /* poison */
        scene1_customer_npc_pump(1, 0);
        if (slot[CS_NPC_OFF_FACING] != cases[i].oct)
            T_FAIL("walk vel (%.1f,%.1f) → facing %d, expected %d",
                   cases[i].vx, cases[i].vz, slot[CS_NPC_OFF_FACING], cases[i].oct);
    }

    /* zero velocity → the recompute SKIPS (keeps the prior facing). */
    slot[CS_NPC_OFF_WSTATE] = 1;
    slot[CS_NPC_OFF_WTIMER] = 0;
    { float z = 0.0f; memcpy(&slot[CS_NPC_OFF_VEL_X], &z, 4);
                      memcpy(&slot[CS_NPC_OFF_VEL_Z], &z, 4); }
    slot[CS_NPC_OFF_FACING] = 5;                 /* sentinel that must survive */
    scene1_customer_npc_pump(1, 0);
    T_ASSERT_EQ_I(slot[CS_NPC_OFF_FACING], 5);
    return 0;
}
