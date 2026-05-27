/*
 * test_d3d_pool.c — tests for src/d3d_pool.{c,h}.
 *
 * Covers:
 *   - Empty-pool walks (default state) — no crash, no release calls.
 *   - Type-match release: entry matching the requested type tag gets
 *     released; slot zeroed.
 *   - Type-mismatch preservation: entries with different tags are
 *     left in place.
 *   - Multi-entry mixed: same walk releases only matching tags.
 *   - d3d_pool_release_post_fade dispatches to type 2.
 *   - 200-slot bounded walk: entries at the last slot (index 199) are
 *     reached; entries past 199 (would be index 200) aren't touched.
 *   - Test helper round-trip: install / read / reset.
 */

#include "t.h"
#include "d3d_pool.h"

/* ─── stub COM resource — minimal IUnknown-shaped vtable ──────────────── */

static int g_release_call_count;

typedef unsigned long (*release_fn)(void *self);

static unsigned long stub_release(void *self)
{
    (void)self;
    g_release_call_count++;
    return 0;
}

/* The pool walker reads `(*(void ***)resource)[2]` — i.e. the resource
 * is a pointer whose first dword is a vtable, and vtable[2] is Release.
 * We stage the vtable as a 3-slot pointer array; only slot 2 is read. */
static void *g_stub_vtable[3] = { NULL, NULL, (void *)stub_release };

typedef struct stub_resource {
    void **vtable;
} stub_resource;

static stub_resource g_stub_resource = { .vtable = g_stub_vtable };

static d3d_pool_entry make_entry(int type)
{
    d3d_pool_entry e = { 0 };
    e.resource = &g_stub_resource;
    e.type_tag = type;
    return e;
}

/* ─── empty-pool walks ────────────────────────────────────────────────── */

int test_d3d_pool_release_type_on_empty_pool_no_crash(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_release_type(2);
    T_ASSERT_EQ_I(g_release_call_count, 0);
    return 0;
}

int test_d3d_pool_release_post_fade_on_empty_pool_no_crash(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_release_post_fade();
    T_ASSERT_EQ_I(g_release_call_count, 0);
    return 0;
}

/* ─── type-match release ──────────────────────────────────────────────── */

int test_d3d_pool_release_type_matching_releases_and_zeros_slot(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_entry e = make_entry(2);
    d3d_pool_set_slot_for_test(7, &e);

    d3d_pool_release_type(2);

    T_ASSERT_EQ_I(g_release_call_count, 1);
    T_ASSERT(d3d_pool_slot_for_test(7) == NULL);
    T_ASSERT(e.resource == NULL);
    return 0;
}

int test_d3d_pool_release_type_mismatch_preserves_slot(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_entry e = make_entry(3);
    d3d_pool_set_slot_for_test(7, &e);

    d3d_pool_release_type(2);

    T_ASSERT_EQ_I(g_release_call_count, 0);
    T_ASSERT(d3d_pool_slot_for_test(7) == &e);
    T_ASSERT(e.resource == &g_stub_resource);
    return 0;
}

int test_d3d_pool_release_type_mixed_releases_only_matching(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_entry a = make_entry(2);
    d3d_pool_entry b = make_entry(3);
    d3d_pool_entry c = make_entry(2);
    d3d_pool_entry d = make_entry(7);
    d3d_pool_set_slot_for_test(0,  &a);
    d3d_pool_set_slot_for_test(5,  &b);
    d3d_pool_set_slot_for_test(50, &c);
    d3d_pool_set_slot_for_test(99, &d);

    d3d_pool_release_type(2);

    /* a and c (type 2) released; b and d (type 3, 7) preserved. */
    T_ASSERT_EQ_I(g_release_call_count, 2);
    T_ASSERT(d3d_pool_slot_for_test(0) == NULL);
    T_ASSERT(d3d_pool_slot_for_test(5) == &b);
    T_ASSERT(d3d_pool_slot_for_test(50) == NULL);
    T_ASSERT(d3d_pool_slot_for_test(99) == &d);
    return 0;
}

/* ─── post_fade wrapper dispatches to type 2 ──────────────────────────── */

int test_d3d_pool_release_post_fade_releases_type_2(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_entry t2 = make_entry(2);
    d3d_pool_entry t3 = make_entry(3);
    d3d_pool_set_slot_for_test(10, &t2);
    d3d_pool_set_slot_for_test(11, &t3);

    d3d_pool_release_post_fade();

    T_ASSERT_EQ_I(g_release_call_count, 1);
    T_ASSERT(d3d_pool_slot_for_test(10) == NULL);
    T_ASSERT(d3d_pool_slot_for_test(11) == &t3);
    return 0;
}

/* ─── 200-slot walk bound ─────────────────────────────────────────────── */

int test_d3d_pool_release_type_reaches_last_slot(void)
{
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_entry last = make_entry(4);
    d3d_pool_set_slot_for_test(199, &last);  /* last in-range slot */

    d3d_pool_release_type(4);

    T_ASSERT_EQ_I(g_release_call_count, 1);
    T_ASSERT(d3d_pool_slot_for_test(199) == NULL);
    return 0;
}

int test_d3d_pool_release_type_does_not_touch_out_of_bounds(void)
{
    /* The slot setter rejects out-of-range indices; this test just
     * confirms a normal walk doesn't accidentally walk past slot 199.
     * If it did, prior tests' dangling references would surface as
     * stale `g_release_call_count` here. */
    d3d_pool_reset_for_test();
    g_release_call_count = 0;
    d3d_pool_release_type(0);
    d3d_pool_release_type(0xff);  /* arbitrary tag */
    T_ASSERT_EQ_I(g_release_call_count, 0);
    return 0;
}

/* ─── test helpers ────────────────────────────────────────────────────── */

int test_d3d_pool_slot_setter_rejects_out_of_range_index(void)
{
    d3d_pool_reset_for_test();
    d3d_pool_entry e = make_entry(1);
    d3d_pool_set_slot_for_test(-1,  &e);
    d3d_pool_set_slot_for_test(200, &e);
    /* No slot in [0..199] populated. */
    for (int i = 0; i < 200; i++) {
        T_ASSERT(d3d_pool_slot_for_test(i) == NULL);
    }
    T_ASSERT(d3d_pool_slot_for_test(-1)  == NULL);
    T_ASSERT(d3d_pool_slot_for_test(200) == NULL);
    return 0;
}

int test_d3d_pool_reset_clears_populated_slots(void)
{
    d3d_pool_reset_for_test();
    d3d_pool_entry e = make_entry(1);
    d3d_pool_set_slot_for_test(0,   &e);
    d3d_pool_set_slot_for_test(100, &e);
    d3d_pool_set_slot_for_test(199, &e);

    d3d_pool_reset_for_test();

    for (int i = 0; i < 200; i++) {
        T_ASSERT(d3d_pool_slot_for_test(i) == NULL);
    }
    return 0;
}
