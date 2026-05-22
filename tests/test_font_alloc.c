/*
 * test_font_alloc.c — unit tests for codepoint→record-id + slot LRU.
 */

#include "t.h"

#include "../src/font.h"
#include "../src/font_alloc.h"

/* ─── codepoint→record-id lookup ─────────────────────────────────────── */

int test_font_codepoint_ascii_is_byte_value(void)
{
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x00, 0), 0);
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x41, 0), 0x41);   /* 'A' */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x7f, 0), 0x7f);
    return 0;
}

int test_font_codepoint_high_byte_below_table_returns_none_if_missing(void)
{
    /* 0x8000 — invalid SJIS lead byte combo, below 0x883f, not in table. */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x80, 0x00), FONT_RECORD_NONE);
    return 0;
}

int test_font_codepoint_special_table_first_entry(void)
{
    /* 0x8140 = full-width space — entry 0 of the special table. */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x81, 0x40), 256 + 0);
    return 0;
}

int test_font_codepoint_special_table_known_punctuation(void)
{
    /* 0x8149 = "！" — entry 1. */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x81, 0x49), 256 + 1);
    return 0;
}

int test_font_codepoint_sjis_double_byte_at_boundary(void)
{
    /* 0x8840: first valid SJIS kanji. id = 0x8840 - 0x861f = 0x221 = 545 */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x88, 0x40), 0x221);
    /* 0x883f: phantom first-iter glyph; id = 0x220 = 544 */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0x88, 0x3f), 0x220);
    return 0;
}

int test_font_codepoint_sjis_double_byte_high_kanji(void)
{
    /* 0xe040: first SJIS kanji after the second gap range. */
    T_ASSERT_EQ_I(font_codepoint_to_record_id(0xe0, 0x40), 0xe040 - 0x861f);
    return 0;
}

/* ─── slot allocator ─────────────────────────────────────────────────── */

int test_font_slot_alloc_first_call_gets_slot_zero(void)
{
    font_init();
    int rec_id, is_new;
    int slot = font_slot_alloc('A', 0, &rec_id, &is_new);
    T_ASSERT_EQ_I(slot, 0);
    T_ASSERT_EQ_I(rec_id, 0x41);
    T_ASSERT_EQ_I(is_new, 1);
    T_ASSERT_EQ_U(g_font.slots[0].in_use, 1);
    T_ASSERT_EQ_U(g_font.slots[0].cp_byte0, 'A');
    T_ASSERT_EQ_U(g_font.slots[0].cp_byte1, 0);
    T_ASSERT_EQ_U(g_font.slots[0].record_id, 0x41);
    T_ASSERT_EQ_U(g_font.slots[0].age, 0);
    return 0;
}

int test_font_slot_alloc_match_returns_existing_resets_age(void)
{
    font_init();
    int rec_id, is_new;
    int s1 = font_slot_alloc('A', 0, &rec_id, &is_new);
    T_ASSERT_EQ_I(is_new, 1);
    /* age the slot */
    g_font.slots[s1].age = 7;
    /* Second alloc on same codepoint */
    int s2 = font_slot_alloc('A', 0, &rec_id, &is_new);
    T_ASSERT_EQ_I(s2, s1);
    T_ASSERT_EQ_I(is_new, 0);
    T_ASSERT_EQ_U(g_font.slots[s1].age, 0);
    return 0;
}

int test_font_slot_alloc_distinct_codepoints_get_distinct_slots(void)
{
    font_init();
    int s_a = font_slot_alloc('A', 0, NULL, NULL);
    int s_b = font_slot_alloc('B', 0, NULL, NULL);
    int s_c = font_slot_alloc('C', 0, NULL, NULL);
    T_ASSERT(s_a != s_b);
    T_ASSERT(s_a != s_c);
    T_ASSERT(s_b != s_c);
    T_ASSERT_EQ_I(s_a, 0);
    T_ASSERT_EQ_I(s_b, 1);
    T_ASSERT_EQ_I(s_c, 2);
    return 0;
}

int test_font_slot_alloc_unknown_codepoint_returns_none(void)
{
    font_init();
    int rec_id, is_new;
    int slot = font_slot_alloc(0x80, 0x00, &rec_id, &is_new);
    T_ASSERT_EQ_I(slot, FONT_SLOT_NONE);
    T_ASSERT_EQ_I(rec_id, FONT_RECORD_NONE);
    T_ASSERT_EQ_I(is_new, 0);
    return 0;
}

int test_font_slot_alloc_double_byte_codepoint_stores_both_bytes(void)
{
    font_init();
    int rec_id, is_new;
    int slot = font_slot_alloc(0x81, 0x40, &rec_id, &is_new);
    T_ASSERT(slot != FONT_SLOT_NONE);
    T_ASSERT_EQ_I(is_new, 1);
    T_ASSERT_EQ_U(g_font.slots[slot].cp_byte0, 0x81);
    T_ASSERT_EQ_U(g_font.slots[slot].cp_byte1, 0x40);
    return 0;
}

int test_font_slot_alloc_match_double_byte_requires_both(void)
{
    font_init();
    int slot1 = font_slot_alloc(0x81, 0x40, NULL, NULL);
    /* Hit: same codepoint */
    int is_new;
    int slot2 = font_slot_alloc(0x81, 0x40, NULL, &is_new);
    T_ASSERT_EQ_I(slot2, slot1);
    T_ASSERT_EQ_I(is_new, 0);
    /* Miss: different second byte */
    int slot3 = font_slot_alloc(0x81, 0x49, NULL, &is_new);
    T_ASSERT(slot3 != slot1);
    T_ASSERT_EQ_I(is_new, 1);
    return 0;
}

int test_font_slot_alloc_eviction_via_age_gate(void)
{
    font_init();
    /* Mark all 200 slots in_use with synthetic codepoint+record state.
     * We use cp_byte0=0xff (high bit set ⇒ double-byte path) so a
     * single-byte alloc like ('!', 0) won't accidentally match any
     * existing slot via the engine's single-byte gate. cp_byte1=i
     * makes each entry distinguishable. Age=1 keeps slots below the
     * eviction threshold (>3) until we deliberately bump one. */
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        g_font.slots[i].in_use   = 1;
        g_font.slots[i].cp_byte0 = 0xff;
        g_font.slots[i].cp_byte1 = (uint8_t)i;
        g_font.slots[i].age      = 1;
    }
    /* No slot should be age-evictable yet. New codepoint → NONE. */
    int is_new;
    int rc = font_slot_alloc(0x21, 0, NULL, &is_new);
    T_ASSERT_EQ_I(rc, FONT_SLOT_NONE);
    T_ASSERT_EQ_I(is_new, 0);

    /* Now age slot 7 enough to be evictable (age > 3). */
    g_font.slots[7].age = 100;
    rc = font_slot_alloc(0x21, 0, NULL, &is_new);
    T_ASSERT_EQ_I(rc, 7);
    T_ASSERT_EQ_I(is_new, 1);
    T_ASSERT_EQ_U(g_font.slots[7].cp_byte0, 0x21);
    T_ASSERT_EQ_U(g_font.slots[7].age, 0);
    return 0;
}

/* Release-callback hook capture for the eviction-callback test. */
static int g_release_capture = -1;
static void capture_release(int slot_id) { g_release_capture = slot_id; }

int test_font_slot_alloc_release_callback_fires_on_evict(void)
{
    font_init();
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        g_font.slots[i].in_use   = 1;
        g_font.slots[i].cp_byte0 = 0xff;
        g_font.slots[i].cp_byte1 = (uint8_t)i;
        g_font.slots[i].age      = 1;
    }
    g_font.slots[42].age = 10;
    g_font.textures[42] = (void *)0xdeadbeef;

    g_release_capture = -1;
    g_font_alloc_release_cb = capture_release;
    int rc = font_slot_alloc(0x21, 0, NULL, NULL);
    g_font_alloc_release_cb = NULL;

    T_ASSERT_EQ_I(rc, 42);
    T_ASSERT_EQ_I(g_release_capture, 42);
    T_ASSERT(g_font.textures[42] == NULL);
    return 0;
}

int test_font_slot_alloc_no_callback_still_nulls_texture(void)
{
    font_init();
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        g_font.slots[i].in_use   = 1;
        g_font.slots[i].cp_byte0 = 0xff;
        g_font.slots[i].cp_byte1 = (uint8_t)i;
        g_font.slots[i].age      = 1;
    }
    g_font.slots[99].age = 10;
    g_font.textures[99] = (void *)0xfacefeed;
    g_font_alloc_release_cb = NULL;

    int rc = font_slot_alloc(0x21, 0, NULL, NULL);
    T_ASSERT_EQ_I(rc, 99);
    T_ASSERT(g_font.textures[99] == NULL);
    return 0;
}
