/*
 * test_stage_palette.c — C7d stage_palette_init_house().
 *
 * The stub is small. Tests cover:
 *
 *   1. Struct layout: size and offset of every typed field. The
 *      header already has _Static_asserts for these, so the runtime
 *      assertions here are belt-and-suspenders — they catch any
 *      misalignment that creeps in if someone disables the static
 *      asserts (e.g. older compiler) and lets the struct compile
 *      with a different layout.
 *
 *   2. Pointer wiring: after init, `g_stage_palette` points at
 *      `g_stage_palette_house` (not NULL, not somewhere else).
 *
 *   3. Field defaults: every typed field reads as zero after init —
 *      the engine BSS-zero contract for HOUSE.
 *
 *   4. Idempotence + overwrite: a second call re-zeroes mutated
 *      fields and keeps the pointer stable. Mirrors the
 *      stage_init_house contract from stage_state.c.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "stage_palette.h"

int test_stage_palette_size(void)
{
    T_ASSERT_EQ_U(sizeof(stage_palette_t), (unsigned)STAGE_PALETTE_SIZE);
    T_ASSERT_EQ_U((unsigned)STAGE_PALETTE_SIZE, 0x1b3cu);
    return 0;
}

int test_stage_palette_offsets(void)
{
    T_ASSERT_EQ_U(offsetof(stage_palette_t, mode),               0x0000u);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, gravity_x),          0x1a7cu);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, gravity_y),          0x1a80u);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, gravity_z),          0x1a84u);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, lighting_flag_1a88), 0x1a88u);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, lighting_flag_1a8c), 0x1a8cu);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, clear_r),            0x1aa8u);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, clear_g),            0x1aacu);
    T_ASSERT_EQ_U(offsetof(stage_palette_t, clear_b),            0x1ab0u);
    return 0;
}

int test_stage_palette_init_house_pointer(void)
{
    g_stage_palette = NULL;
    stage_palette_init_house();
    T_ASSERT(g_stage_palette == &g_stage_palette_house);
    return 0;
}

int test_stage_palette_init_house_zeroes(void)
{
    /* Smudge every typed field so a no-op init would visibly fail. */
    g_stage_palette_house.mode               = 7;
    g_stage_palette_house.gravity_x          = 1.5f;
    g_stage_palette_house.gravity_y          = -2.25f;
    g_stage_palette_house.gravity_z          = 9.81f;
    g_stage_palette_house.lighting_flag_1a88 = 2;
    g_stage_palette_house.lighting_flag_1a8c = 1;
    g_stage_palette_house.clear_r            = 0xff;
    g_stage_palette_house.clear_g            = 0x80;
    g_stage_palette_house.clear_b            = 0x40;

    stage_palette_init_house();

    T_ASSERT_EQ_I(g_stage_palette_house.mode,               0);
    T_ASSERT(g_stage_palette_house.gravity_x == 0.0f);
    T_ASSERT(g_stage_palette_house.gravity_y == 0.0f);
    T_ASSERT(g_stage_palette_house.gravity_z == 0.0f);
    T_ASSERT_EQ_I(g_stage_palette_house.lighting_flag_1a88, 0);
    T_ASSERT_EQ_I(g_stage_palette_house.lighting_flag_1a8c, 0);
    T_ASSERT_EQ_I(g_stage_palette_house.clear_r,            0);
    T_ASSERT_EQ_I(g_stage_palette_house.clear_g,            0);
    T_ASSERT_EQ_I(g_stage_palette_house.clear_b,            0);
    return 0;
}

int test_stage_palette_init_house_zeroes_padding(void)
{
    /* Padding zones get scrubbed too — the engine treats the whole
     * 0x1b3c-byte record as one BSS-zeroed object, so opaque fields
     * (the ones we haven't typed yet) must stay zero after init too.
     * Future chips will type these and depend on the zero contract. */
    unsigned char *bytes = (unsigned char *)&g_stage_palette_house;
    for (size_t i = 0; i < sizeof(g_stage_palette_house); i++) {
        bytes[i] = 0xaa;
    }

    stage_palette_init_house();

    for (size_t i = 0; i < sizeof(g_stage_palette_house); i++) {
        if (bytes[i] != 0) {
            T_FAIL("byte %zu = 0x%02x, want 0x00", i, (unsigned)bytes[i]);
        }
    }
    return 0;
}

int test_stage_palette_init_house_idempotent(void)
{
    stage_palette_init_house();
    stage_palette_t *p1 = g_stage_palette;
    stage_palette_init_house();
    T_ASSERT(g_stage_palette == p1);
    T_ASSERT(g_stage_palette == &g_stage_palette_house);
    return 0;
}

/* ─── FUN_0043244c + FUN_00474681 — probe-only no-op bodies ─────────── */

int test_stage_palette_clear_resource_caches_does_not_crash(void)
{
    /* Body is probe-only (caches not allocated in port). Just confirm
     * the call returns cleanly without touching unrelated state. */
    stage_palette_init_house();
    stage_palette_t *saved_ptr = g_stage_palette;
    int32_t saved_mode = g_stage_palette_house.mode;

    stage_palette_clear_resource_caches();

    T_ASSERT(g_stage_palette == saved_ptr);
    T_ASSERT_EQ_I(g_stage_palette_house.mode, saved_mode);
    return 0;
}

int test_stage_palette_load_for_stage_does_not_clobber_palette(void)
{
    /* FUN_00474681's pointer set is delegated to stage_palette_init_house;
     * this function should not zero or repoint the palette record. */
    stage_palette_init_house();
    g_stage_palette_house.mode = 7;  /* mutate to detect a clobber */

    stage_palette_load_for_stage();

    T_ASSERT(g_stage_palette == &g_stage_palette_house);
    T_ASSERT_EQ_I(g_stage_palette_house.mode, 7);

    /* Cleanup: re-init for downstream tests. */
    stage_palette_init_house();
    return 0;
}

int test_stage_palette_load_for_stage_invokes_cache_clear(void)
{
    /* The function's main observable side effect is dispatching to
     * stage_palette_clear_resource_caches.  Without a hook injection
     * we can only check that the call chain doesn't crash on
     * BSS-default state.  Probe firing is verified via the
     * call_trace log, not via host tests. */
    stage_palette_init_house();
    stage_palette_load_for_stage();
    /* Idempotency: call twice. */
    stage_palette_load_for_stage();
    T_ASSERT(g_stage_palette == &g_stage_palette_house);
    return 0;
}
