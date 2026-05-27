/*
 * test_chara_equip.c — tests for src/chara_equip.{c,h}.
 *
 * Covers:
 *   - tables_item_find_slot_by_id behavior on hit, miss, empty DB
 *   - distribute_slot_stats via the aggregator (private static fn)
 *   - chara_equip_recompute_aggregate scratch resets + counter
 *   - aggregate stat sums with synthetic item DB
 *   - sentinel (0xffffffff) slot skip path
 *   - max-stat enchantment bonus add
 *   - base-stat addition pass
 *   - bank/chara selector round-trip
 *
 * Synthetic g_item: each test builds a small in-memory item table by
 * directly writing into g_item.records[...] + g_item.count, then calls
 * the aggregator and inspects the side effects.
 */

#include "t.h"

#include <string.h>

#include "chara_equip.h"
#include "scene1_combat_sm.h"
#include "tables_item.h"

static void setup_clean(void)
{
    chara_equip_reset_for_test();
    memset(&g_item, 0, sizeof(g_item));
}

static void define_item(int slot, int32_t item_id,
                       int32_t atk, int32_t def, int32_t matk, int32_t mdef)
{
    g_item.records[slot].valid         = 1;
    g_item.records[slot].item_id       = item_id;
    g_item.records[slot].attack        = atk;
    g_item.records[slot].defense       = def;
    g_item.records[slot].magic_attack  = matk;
    g_item.records[slot].magic_defense = mdef;
    if (slot >= g_item.count) g_item.count = slot + 1;
}

/* ─── tables_item_find_slot_by_id ──────────────────────────────────── */

int test_items_find_slot_by_id_empty_db_returns_neg1(void)
{
    setup_clean();
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 0),    -1);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 1234), -1);
    return 0;
}

int test_items_find_slot_by_id_null_state_returns_neg1(void)
{
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(NULL, 0), -1);
    return 0;
}

int test_items_find_slot_by_id_hit_returns_slot_index(void)
{
    setup_clean();
    define_item(0, 0,    10, 0, 0, 0);
    define_item(1, 100,   0, 5, 0, 0);
    define_item(2, 9999,  0, 0, 0, 3);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 0),    0);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 100),  1);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 9999), 2);
    return 0;
}

int test_items_find_slot_by_id_miss_returns_neg1(void)
{
    setup_clean();
    define_item(0, 0,   10, 0, 0, 0);
    define_item(1, 100,  0, 5, 0, 0);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 99), -1);
    return 0;
}

int test_items_find_slot_by_id_returns_first_match_on_dup(void)
{
    /* Engine returns the FIRST match (loop short-circuits on first
     * exact item_id hit).  Document by setting two records with the
     * same id. */
    setup_clean();
    define_item(0, 42, 1, 0, 0, 0);
    define_item(1, 42, 0, 1, 0, 0);
    T_ASSERT_EQ_I(tables_item_find_slot_by_id(&g_item, 42), 0);
    return 0;
}

/* ─── bank/chara selectors ─────────────────────────────────────────── */

int test_chara_equip_selectors_roundtrip(void)
{
    setup_clean();
    /* BSS-zero defaults. */
    T_ASSERT_EQ_I(chara_equip_get_current_bank(),  0);
    T_ASSERT_EQ_I(chara_equip_get_current_chara(), 0);
    chara_equip_set_current_bank(7);
    chara_equip_set_current_chara(3);
    T_ASSERT_EQ_I(chara_equip_get_current_bank(),  7);
    T_ASSERT_EQ_I(chara_equip_get_current_chara(), 3);
    return 0;
}

/* ─── slot / base-stat get/set ────────────────────────────────────── */

int test_chara_equip_slot_roundtrip(void)
{
    setup_clean();
    chara_equip_set_slot(0, 0, 0, 0x1234abcdu);
    chara_equip_set_slot(0, 0, 4, 0xdeadbeefu);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 0), 0x1234abcdu);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 4), 0xdeadbeefu);
    /* untouched */
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 1), 0);
    /* out-of-range no-op */
    chara_equip_set_slot(0, 0, 5, 1);
    chara_equip_set_slot(0, 0, -1, 1);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 5), 0);
    return 0;
}

int test_chara_equip_base_stat_roundtrip(void)
{
    setup_clean();
    chara_equip_set_base_stat(0, 0, 0, 10);
    chara_equip_set_base_stat(0, 0, 3, -5);
    T_ASSERT_EQ_I(chara_equip_get_base_stat(0, 0, 0), 10);
    T_ASSERT_EQ_I(chara_equip_get_base_stat(0, 0, 3), -5);
    /* untouched */
    T_ASSERT_EQ_I(chara_equip_get_base_stat(0, 0, 1), 0);
    return 0;
}

int test_chara_equip_separate_chara_slots(void)
{
    setup_clean();
    chara_equip_set_slot(0, 0, 0, 100);
    chara_equip_set_slot(0, 1, 0, 200);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 0), 100);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 1, 0), 200);
    return 0;
}

/* ─── aggregator: scratch resets ──────────────────────────────────── */

int test_aggregate_sets_counter_to_5(void)
{
    setup_clean();
    /* Pre-pollute the counter to confirm the aggregator overwrites. */
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_dat_056db0a8(), 5);
    return 0;
}

int test_aggregate_empty_db_yields_zero_sums(void)
{
    /* g_item.count == 0 → every slot bottoms out at items_find_by_id
     * = -1 → distribute_slot_stats skips.  All sums and stats remain 0. */
    setup_clean();
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 0);
    return 0;
}

int test_aggregate_writes_to_existing_combat_globals(void)
{
    /* Confirm idx 0 ↔ idle2 and idx 2 ↔ idle. */
    setup_clean();
    define_item(0, 0, 7, 3, 11, 13);
    /* 5 slots all = 0 → resolved to item id 0 → item 0 → 5*atk = 35. */
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(g_scene1_combat_damage_base_idle2, 35);
    T_ASSERT_EQ_I(g_scene1_combat_damage_base_idle,  55);  /* 5*11 magic_attack */
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 35);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 55);
    return 0;
}

/* ─── aggregator: real sum behavior ───────────────────────────────── */

int test_aggregate_sums_five_slots_with_single_item(void)
{
    /* Item 0 has (atk=7, def=3, matk=11, mdef=13).  All 5 BSS-zero
     * slots decode to item_id=0 → 5 contributions per stat. */
    setup_clean();
    define_item(0, 0, 7, 3, 11, 13);
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 5 * 7);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 5 * 3);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 5 * 11);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 5 * 13);
    return 0;
}

int test_aggregate_sentinel_slot_skipped(void)
{
    /* Set slot 0 to 0xffffffff (empty sentinel).  Other 4 slots are
     * BSS-zero → resolve to item 0 → 4*item[0].stat each. */
    setup_clean();
    define_item(0, 0, 7, 3, 11, 13);
    chara_equip_set_slot(0, 0, 0, CHARA_EQUIP_SLOT_EMPTY);
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 4 * 7);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 4 * 3);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 4 * 11);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 4 * 13);
    return 0;
}

int test_aggregate_all_slots_sentinel_yields_zero(void)
{
    setup_clean();
    define_item(0, 0, 7, 3, 11, 13);
    for (int i = 0; i < CHARA_EQUIP_SLOT_COUNT; i++) {
        chara_equip_set_slot(0, 0, i, CHARA_EQUIP_SLOT_EMPTY);
    }
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 0);
    return 0;
}

int test_aggregate_encoded_slot_picks_correct_item(void)
{
    /* Slot encoding: (item_id << 6) | (meta_bits & 0x3f).
     * Place slot 0 = encoded item 100 with no enchant.  Items
     * 1..4 stay zero so the rest of the slots also resolve to item 0
     * but with zero stats; we add a distinct item 100 separately. */
    setup_clean();
    define_item(0, 0,   0, 0, 0, 0);   /* item 0: zero stats */
    define_item(1, 100, 9, 5, 0, 0);
    chara_equip_set_slot(0, 0, 0, (uint32_t)(100u << 6));  /* item 100, enchant 0 */
    chara_equip_recompute_aggregate();
    /* slot 0 contributes 9/5/0/0; slots 1..4 contribute item 0 = 0. */
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 9);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 5);
    return 0;
}

int test_aggregate_max_stat_gets_enchant_bonus(void)
{
    /* Encode slot with enchant = 0xf and an item where atk is the
     * unique max.  Bonus should land on the atk column. */
    setup_clean();
    define_item(0, 0,   0, 0, 0, 0);
    define_item(1, 100, 20, 5, 1, 0);
    chara_equip_set_slot(0, 0, 0, (uint32_t)((100u << 6) | 0xfu));
    chara_equip_recompute_aggregate();
    /* slot 0: atk = 20 + 0xf = 35, def = 5, matk = 1, mdef = 0. */
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 20 + 0xf);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 5);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 1);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 0);
    return 0;
}

int test_aggregate_enchant_lands_on_first_max_on_tie(void)
{
    /* Engine uses strict < to select max → ties stay on the first one
     * encountered (index 0 in the atk/def/matk/mdef walk). */
    setup_clean();
    define_item(0, 0,   0, 0, 0, 0);
    define_item(1, 100, 7, 7, 0, 0);
    chara_equip_set_slot(0, 0, 0, (uint32_t)((100u << 6) | 0x5u));
    chara_equip_recompute_aggregate();
    /* atk + def both = 7; enchant 5 lands on atk (idx 0). */
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 7 + 5);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 7);
    return 0;
}

int test_aggregate_enchant_skipped_when_all_stats_zero(void)
{
    /* Engine inits max_idx = -1; only replaced on strict >.  A
     * zero-stat item leaves max_idx = -1 → enchant bonus is NOT
     * added.  Verify with encoded slot to non-zero enchant on
     * zero-stat item. */
    setup_clean();
    define_item(0, 0,   0, 0, 0, 0);
    chara_equip_set_slot(0, 0, 0, (uint32_t)((0u << 6) | 0xfu));
    chara_equip_recompute_aggregate();
    /* All sums stay 0 — no max stat to bonus. */
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), 0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), 0);
    return 0;
}

int test_aggregate_adds_base_stats(void)
{
    /* Base stats add on top of the equipment sum.  Verify with
     * empty item DB → all distribute calls skip → sum=0 → output
     * equals base stat. */
    setup_clean();
    chara_equip_set_base_stat(0, 0, 0, 100);
    chara_equip_set_base_stat(0, 0, 1, 50);
    chara_equip_set_base_stat(0, 0, 2, 25);
    chara_equip_set_base_stat(0, 0, 3, 10);
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 100);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1),  50);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2),  25);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3),  10);
    return 0;
}

int test_aggregate_reads_active_chara(void)
{
    /* Set up chara 0 with one set of base stats and chara 1 with
     * another; flip the selector and confirm the aggregator picks
     * the right one. */
    setup_clean();
    chara_equip_set_base_stat(0, 0, 0, 100);
    chara_equip_set_base_stat(0, 1, 0, 999);
    chara_equip_set_current_chara(1);
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), 999);
    return 0;
}

int test_aggregate_idempotent(void)
{
    /* Two consecutive calls produce the same result.  The aggregator
     * is a pure function of (g_item, equip table, bank/chara). */
    setup_clean();
    define_item(0, 0, 7, 3, 11, 13);
    chara_equip_recompute_aggregate();
    int32_t s0 = chara_equip_get_aggregate_stat(0);
    int32_t s1 = chara_equip_get_aggregate_stat(1);
    int32_t s2 = chara_equip_get_aggregate_stat(2);
    int32_t s3 = chara_equip_get_aggregate_stat(3);
    chara_equip_recompute_aggregate();
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(0), s0);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(1), s1);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(2), s2);
    T_ASSERT_EQ_I(chara_equip_get_aggregate_stat(3), s3);
    return 0;
}
