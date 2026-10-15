/*
 * test_save_bank.c — coverage for the save-arena port
 * (src/save_bank.{c,h} = FUN_004901c2 + FUN_0049001c + helpers).
 *
 * Pure-C module — no Win32 surface — so we exercise the full init
 * paths in-process under ASan + UBSan, then verify named field
 * constants, checksum verify, slider defaults, and the post-init RNG
 * state.
 */
#include "t.h"
#include "chara_equip.h"
#include <string.h>

#include "save_bank.h"
#include "save_work.h"
#include "tables_item.h"
#include "tables_chara.h"
#include "rng.h"

/* Shared chara test fixture — engine reads g_chara[] inside
 * save_bank_init_one(); blank it to known values so the assertions
 * are stable irrespective of vendor data state. */
static void seed_chara_zero(void)
{
    memset(g_chara, 0, sizeof g_chara);
}

static void seed_chara_minimal(void)
{
    /* Just enough to exercise the chara loop with non-zero values. */
    memset(g_chara, 0, sizeof g_chara);
    g_chara[0].level_threshold = 0;
    g_chara[0].hp_base = 50;
    g_chara[0].sp_base = 30;
    g_chara[0].at_base = 10;
    g_chara[0].df_base = 13;
    g_chara[0].mt_base = 5;
    g_chara[0].mf_base = 10;
    g_chara[0].hp_lv100 = 500;
    g_chara[0].sp_lv100 = 300;
    g_chara[1].level_threshold = 5;
    g_chara[1].hp_base = 60;
}

/* ── Arena geometry / sizing ── */

int test_save_bank_arena_sizing(void)
{
    /* Sanity: 100 banks × stride + header == 18,838,832 = 0x011f7530. */
    T_ASSERT_EQ_U(SAVE_BANK_ARENA_BYTES, 0x011f7530u);
    T_ASSERT_EQ_U(SAVE_BANK_STRIDE_BYTES, 0x2dfc8u);
    T_ASSERT_EQ_U(SAVE_BANK_HEADER_BYTES, 0x0b10u);
    T_ASSERT_EQ_U(SAVE_BANK_COUNT, 100u);
    return 0;
}

int test_save_bank_bank_pointer_arithmetic(void)
{
    uint8_t *base = save_arena_base();
    T_ASSERT(base != NULL);
    T_ASSERT(save_bank_at(0)  == base + SAVE_BANK_HEADER_BYTES);
    T_ASSERT(save_bank_at(1)  == base + SAVE_BANK_HEADER_BYTES + SAVE_BANK_STRIDE_BYTES);
    T_ASSERT(save_bank_at(99) == base + SAVE_BANK_HEADER_BYTES + 99 * SAVE_BANK_STRIDE_BYTES);
    T_ASSERT(save_bank_at(-1) == NULL);
    T_ASSERT(save_bank_at(100) == NULL);
    return 0;
}

/* ── init_all on a fresh arena ── */

int test_save_bank_init_all_seeds_header_magic(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);

    T_ASSERT_EQ_U(save_header_magic(), 0u);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_header_magic(), SAVE_BANK_MAGIC);
    return 0;
}

int test_save_bank_init_all_seeds_slider_defaults(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    T_ASSERT_EQ_I(save_header_get_se_slider(),   SAVE_HEADER_SE_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_bgm_slider(),  SAVE_HEADER_BGM_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_se_b_slider(), SAVE_HEADER_SE_B_DEFAULT);
    T_ASSERT_EQ_I(save_header_get_slider3(),     SAVE_HEADER_SLIDER3_DEFAULT);

    /* Hard-coded values matching engine: SE=9, BGM=5, SE_B=9, slider3=1. */
    T_ASSERT_EQ_I(save_header_get_se_slider(),   9);
    T_ASSERT_EQ_I(save_header_get_bgm_slider(),  5);
    T_ASSERT_EQ_I(save_header_get_se_b_slider(), 9);
    T_ASSERT_EQ_I(save_header_get_slider3(),     1);
    return 0;
}

int test_save_bank_init_all_stamps_all_banks(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* Every bank is reset to live magic with an UNSTAMPED checksum (0):
     * FUN_0049001c never stamps (retail seed banks: stored 0, content sum
     * 0x345e7bcf), so a fresh arena re-resets them on every boot sweep. */
    for (int idx = 0; idx < SAVE_BANK_COUNT; idx++) {
        uint32_t *bank = save_bank_dwords_at(idx);
        T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_MAGIC], SAVE_BANK_MAGIC);
        T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_CHECKSUM], 0u);
    }
    return 0;
}

int test_save_bank_init_all_is_idempotent(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* Snapshot one bank's checksum, run init_all again, and confirm
     * the snapshot is unchanged — init_all is a no-op when the
     * arena is already live and checksums verify. */
    uint32_t before = save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM];
    save_bank_init_all();
    uint32_t after = save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM];
    T_ASSERT_EQ_U(before, after);
    return 0;
}

/* ── init_one named field constants ── */

int test_save_bank_init_one_sets_money_and_objective(void)
{
    save_bank_arena_clear();
    seed_chara_minimal();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_GOLD],           1000u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_OBJECTIVE_GOLD], 1000u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_WEEK_COUNTER],   7u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_DAY_INDEX],      0u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_RANK_THRESHOLD], 100u);
    T_ASSERT_EQ_U(bank[SAVE_BANK_FIELD_MAGIC],          SAVE_BANK_MAGIC);

    /* The short at byte offset 0x10 should be 2. */
    uint16_t s = *(uint16_t *)((uint8_t *)bank + 0x10);
    T_ASSERT_EQ_U(s, 2u);
    return 0;
}

int test_save_bank_init_one_fills_item_slot_spans(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);

    /* First span: [6, 6+20000) all 0xFFFFFFFF. Spot-check head/mid/tail.
     * Note 6+20000 = 20006 = 0x4e26 — exact start of the second span,
     * so the two spans abut with no gap (a fact worth pinning). */
    T_ASSERT_EQ_U(bank[6],         0xffffffffu);
    T_ASSERT_EQ_U(bank[6 + 9999],  0xffffffffu);
    T_ASSERT_EQ_U(bank[6 + 19999], 0xffffffffu);
    T_ASSERT_EQ_U(bank[0x4e26],    0xffffffffu);  /* abuts first span */

    /* Just past the second span's end (0x4e26 + 300 = 0x4f52): in the
     * 100-record item-grid scratch, zero from memset. */
    T_ASSERT_EQ_U(bank[0x4f52],    0u);

    /* Third + fourth spans: spot-check first element of each. */
    T_ASSERT_EQ_U(bank[44999],    0xffffffffu);
    T_ASSERT_EQ_U(bank[0xb1e8],   0xffffffffu);
    return 0;
}

int test_save_bank_init_one_mini_block(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);

    /* ENGINE QUIRK: the named mini-block writes at bank[0xb388..0xb38d]
     * (constants 3,3,1,0,0,1) are DEAD WRITES — they get fully
     * overwritten by FUN_0048ffd9 (apply_starter_flag_pairs), which
     * spans bank[0xb384..0xb397] for chara index 0. So the final
     * values at 0xb388..0xb38d are the chara-0 flag-pairs[2..4]
     * (0,1, 9,1, 10,3), NOT the literal constants. Faithfully
     * reproduced.
     *
     * The lone surviving "constant" write is bank[0xaec6]=0 — well
     * outside the apply_starter_flag_pairs span. */
    T_ASSERT_EQ_U(bank[0xb388], 0u);   /* pair[2][0] */
    T_ASSERT_EQ_U(bank[0xb389], 1u);   /* pair[2][1] */
    T_ASSERT_EQ_U(bank[0xb38a], 9u);   /* pair[3][0] */
    T_ASSERT_EQ_U(bank[0xb38b], 1u);   /* pair[3][1] */
    T_ASSERT_EQ_U(bank[0xb38c], 10u);  /* pair[4][0] */
    T_ASSERT_EQ_U(bank[0xb38d], 3u);   /* pair[4][1] */
    T_ASSERT_EQ_U(bank[0xaec6], 0u);   /* mini-block constant survives */
    return 0;
}

/* ── Chara record init ── */

int test_save_bank_init_one_chara_records(void)
{
    save_bank_arena_clear();
    seed_chara_minimal();
    rng_seed(1);
    save_bank_init_all();

    uint32_t *bank = save_bank_dwords_at(0);
    uint32_t *rec0 = bank + SAVE_BANK_CHARA_BASE_DWORD;
    uint32_t *rec1 = bank + SAVE_BANK_CHARA_BASE_DWORD + SAVE_BANK_CHARA_STRIDE_DWORDS;

    /* ENGINE QUIRK: chara record dwords [1..10] are overlaid by
     * apply_starter_items, which writes encoded slot IDs (id<<6|0x20)
     * at puVar4[-5..-1] AND puVar4[0..4] where puVar4 = bank + 0x2cec8 +
     * chara*0x6c — i.e. 5 dwords BEFORE and 5 dwords AT puVar4.  Mapped
     * to the chara record (which starts at bank dword 0xb3ac = byte
     * 0x2ceb0), the writes hit rec[1..10] (= bank bytes 0x2ceb4..0x2ced8).
     * Original per-chara stat writes (rec[0xb..0x13]) survive — they
     * sit AFTER the starter-items write window.
     *
     * Recette (chara 0) — `*puVar1 = 0` overrides level_threshold. */
    T_ASSERT_EQ_I(rec0[0], 0);
    /* Per-chara presence byte at +0x60 (rec[0x18]) — survives. */
    T_ASSERT_EQ_I(((uint8_t *)(rec0 + 0x18))[0], 1);

    /* Final values at rec0[1..0xa] reflect apply_starter_items'
     * symmetric L/R write pattern.  STARTER_ITEMS[0] = {1, 1301, 1501,
     * 2301, -1}.  Encoded: (id<<6)|0x20 = 0x60 / 0x14560 / 0x17560 /
     * 0x23860 / 0xffffffff.  Each STARTER_ITEMS[0][N] is written to
     * rec[1+N] AND rec[6+N], with N from 0 (first inner iter) to 4
     * (last inner iter — emits the -1 sentinel). */
    T_ASSERT_EQ_U(rec0[1], (1u    << 6) | 0x20u);  /* L-write, slot 0 */
    T_ASSERT_EQ_U(rec0[2], (1301u << 6) | 0x20u);  /* L-write, slot 1 */
    T_ASSERT_EQ_U(rec0[3], (1501u << 6) | 0x20u);  /* L-write, slot 2 */
    T_ASSERT_EQ_U(rec0[4], (2301u << 6) | 0x20u);  /* L-write, slot 3 */
    T_ASSERT_EQ_U(rec0[5], 0xffffffffu);           /* L-write, slot 4 = -1 */
    T_ASSERT_EQ_U(rec0[6], (1u    << 6) | 0x20u);  /* R-write, slot 0 */
    T_ASSERT_EQ_U(rec0[7], (1301u << 6) | 0x20u);  /* R-write, slot 1 */
    T_ASSERT_EQ_U(rec0[8], (1501u << 6) | 0x20u);  /* R-write, slot 2 */
    T_ASSERT_EQ_U(rec0[9], (2301u << 6) | 0x20u);  /* R-write, slot 3 */
    T_ASSERT_EQ_U(rec0[0xa], 0xffffffffu);         /* R-write, slot 4 = -1 */

    /* Chara 1 — level_threshold preserved (NOT overridden — only
     * chara 0 gets the `*puVar1 = 0;` zeroing). */
    T_ASSERT_EQ_I(rec1[0], 5);
    /* Chara 1 also gets the symmetric L/R writes; first entry =
     * STARTER_ITEMS[1][0] = 101. */
    T_ASSERT_EQ_U(rec1[1], (101u << 6) | 0x20u);
    T_ASSERT_EQ_U(rec1[6], (101u << 6) | 0x20u);
    T_ASSERT_EQ_I(((uint8_t *)(rec1 + 0x18))[0], 1);
    return 0;
}

int test_save_bank_init_one_mirrors_starter_items_into_chara_equip(void)
{
    /* FUN_0048ff93 (puVar4 = bank + 0x2cec8, 0x6c per chara) writes each
     * starter item twice: puVar4[slot - 5] = the chara record's equip
     * slots (bank + 0x2ceb0 + chara * 0x6c + 4 + slot * 4, the ones
     * FUN_004844ef sums) and puVar4[slot] = the copy at record + 0x18.
     * chara_equip reads the same layout from the work bank.
     *
     * For chara 0, STARTER_ITEMS[0] = {1, 1301, 1501, 2301, -1}. */
    save_bank_arena_clear();
    chara_equip_reset_for_test();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_one(0);

    const uint8_t *rec0 = save_bank_at(0) + 0x2ceb0;
    uint32_t v;
    static const uint32_t k_c0[5] = {
        (1u << 6) | 0x20u, (1301u << 6) | 0x20u, (1501u << 6) | 0x20u,
        (2301u << 6) | 0x20u, 0xffffffffu,
    };
    for (int k = 0; k < 5; k++) {
        memcpy(&v, rec0 + 4 + 4 * k, 4);
        T_ASSERT_EQ_U(v, k_c0[k]);
        memcpy(&v, rec0 + 0x18 + 4 * k, 4);
        T_ASSERT_EQ_U(v, k_c0[k]);
    }
    /* Chara 7: STARTER_ITEMS[7] = {701, 1201, 1601, -1, -1}. */
    const uint8_t *rec7 = save_bank_at(0) + 0x2ceb0 + 7 * 0x6c;
    memcpy(&v, rec7 + 4, 4);
    T_ASSERT_EQ_U(v, (701u << 6) | 0x20u);
    memcpy(&v, rec7 + 4 + 4 * 4, 4);
    T_ASSERT_EQ_U(v, 0xffffffffu);

    /* Copied into the work bank, chara_equip sees the same slots. */
    memcpy(save_work_bank_at(0) + 0x2ceb0, rec0, 8 * 0x6c);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 0), k_c0[0]);
    T_ASSERT_EQ_U(chara_equip_get_slot(0, 0, 4), k_c0[4]);
    return 0;
}

int test_save_bank_init_one_consumes_8_rng_steps(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    /* init_all calls init_one on 100 banks, each consuming 8 RNG
     * draws (one per chara). 800 draws total. Capture the RNG state
     * before and after. */
    uint16_t baseline = 0;
    {
        /* Pre-draw 800 from the same initial seed, to compute the
         * expected post-state. */
        rng_seed(1);
        for (int i = 0; i < 800; i++) baseline = rng_next15();
    }
    rng_seed(1);
    save_bank_init_all();
    uint16_t after_init = rng_next15();
    /* The 801st draw after seed 1 == one draw past 800-draw baseline. */
    rng_seed(1);
    uint16_t skip_baseline = 0;
    for (int i = 0; i < 800; i++) skip_baseline = rng_next15();
    (void)baseline;
    uint16_t expected = rng_next15();
    T_ASSERT_EQ_U(after_init, expected);
    (void)skip_baseline;
    return 0;
}

/* ── Checksum ── */

int test_save_bank_checksum_detects_tamper(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    /* A committed bank (checksum stamped, as the save commit does) verifies. */
    save_bank_stamp_checksum(3);
    T_ASSERT(save_bank_checksum_ok(3));

    /* Tamper with one byte and re-verify — should fail. */
    uint32_t *bank = save_bank_dwords_at(3);
    bank[SAVE_BANK_FIELD_GOLD] = 99999;
    T_ASSERT(!save_bank_checksum_ok(3));

    /* save_bank_init_all re-runs init_one for any failing bank (unstamped). */
    save_bank_init_all();
    T_ASSERT_EQ_U(save_bank_dwords_at(3)[SAVE_BANK_FIELD_CHECKSUM], 0u);
    T_ASSERT_EQ_U(save_bank_dwords_at(3)[SAVE_BANK_FIELD_GOLD], 1000u);
    return 0;
}

/* The skip-verify gate (engine DAT_095d3728): with it set, init_all must
 * NOT re-init a bank whose stored checksum is stale — retail preserves
 * LOADED banks even when their checksum is 0/mismatched. This is the
 * behaviour the save pillar's ranking_records FAIL came down to. */
int test_save_bank_skip_verify_preserves_stale_bank(void)
{
    save_bank_arena_clear();          /* resets the gate to 0 */
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();
    T_ASSERT_EQ_I(save_bank_get_skip_verify(), 0);

    /* Make bank 7 look like a loaded-but-never-committed slot: a real
     * value plus a deliberately STALE checksum (0), like the seed's
     * non-active banks. */
    uint32_t *bank = save_bank_dwords_at(7);
    bank[SAVE_BANK_FIELD_GOLD]     = 4242;
    bank[SAVE_BANK_FIELD_CHECKSUM] = 0;
    T_ASSERT(!save_bank_checksum_ok(7));

    /* Gate ON → the sweep is skipped → the stale bank is PRESERVED. */
    save_bank_set_skip_verify(1);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_GOLD], 4242u);
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM], 0u);

    /* Gate OFF → the sweep runs again → the stale bank is re-inited, and
     * (FUN_0049001c) left with an UNSTAMPED checksum. */
    save_bank_set_skip_verify(0);
    save_bank_init_all();
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_GOLD], 1000u);
    T_ASSERT_EQ_U(save_bank_dwords_at(7)[SAVE_BANK_FIELD_CHECKSUM], 0u);
    T_ASSERT(!save_bank_checksum_ok(7));
    return 0;
}

/* FUN_0049001c @0x4900ee: distinct-category store key+count from the item
 * catalog — one 18-dword record per run of equal consecutive categories. */
int test_save_bank_init_one_catalog_category_runs(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    item_state_t saved = g_item;
    memset(&g_item, 0, sizeof g_item);
    const int32_t cats[] = { 4, 4, 4, 9, 2, 2, 9 };   /* runs: 4x3, 9x1, 2x2, 9x1 */
    g_item.count = 7;
    for (int i = 0; i < 7; i++) { g_item.records[i].valid = 1; g_item.records[i].category = cats[i]; }

    save_bank_init_one(3);
    const uint32_t *b = save_bank_dwords_at(3);
    T_ASSERT_EQ_U(b[0x9e76 + 0 * 18], 4u);  T_ASSERT_EQ_U(b[0x9e77 + 0 * 18], 3u);
    T_ASSERT_EQ_U(b[0x9e76 + 1 * 18], 9u);  T_ASSERT_EQ_U(b[0x9e77 + 1 * 18], 1u);
    T_ASSERT_EQ_U(b[0x9e76 + 2 * 18], 2u);  T_ASSERT_EQ_U(b[0x9e77 + 2 * 18], 2u);
    T_ASSERT_EQ_U(b[0x9e76 + 3 * 18], 9u);  T_ASSERT_EQ_U(b[0x9e77 + 3 * 18], 1u);
    T_ASSERT_EQ_U(b[0x9e76 + 4 * 18], 0u);  T_ASSERT_EQ_U(b[0x9e77 + 4 * 18], 0u);
    T_ASSERT_EQ_U(b[SAVE_BANK_FIELD_CHECKSUM], 0u);     /* never stamped */

    /* a leading category of -1 opens no record (retail's run key starts at -1) */
    g_item.records[0].category = -1; g_item.count = 1;
    save_bank_init_one(4);
    T_ASSERT_EQ_U(save_bank_dwords_at(4)[0x9e76], 0u);
    T_ASSERT_EQ_U(save_bank_dwords_at(4)[0x9e77], 0u);
    g_item = saved;
    return 0;
}

/* ── Header init hook ── */

static int g_hook_call_count = 0;
static void test_hook(void) { g_hook_call_count++; }

int test_save_bank_header_init_hook_fires_once_per_reset(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    g_hook_call_count = 0;
    save_bank_set_header_init_hook(test_hook);

    save_bank_init_all();
    T_ASSERT_EQ_I(g_hook_call_count, 1);

    /* Second call: header magic is already live, so hook should NOT
     * fire again. */
    save_bank_init_all();
    T_ASSERT_EQ_I(g_hook_call_count, 1);

    save_bank_set_header_init_hook(NULL);
    return 0;
}

/* ── Slider setters ── */

int test_save_bank_header_slider_setters_clamp(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();

    save_header_set_se_slider(11);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 9);
    save_header_set_se_slider(-3);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 0);
    save_header_set_se_slider(5);
    T_ASSERT_EQ_I(save_header_get_se_slider(), 5);

    save_header_set_slider3(7);
    T_ASSERT_EQ_I(save_header_get_slider3(), 2);
    save_header_set_slider3(-1);
    T_ASSERT_EQ_I(save_header_get_slider3(), 0);
    return 0;
}

/* FUN_0046c320 END handler: script (group, sub) marked viewed in the active work
 * bank (+0x2d3a0) and the save header (+0x20) — the 7 bytes the day2-save-commit
 * port<->retail save compare found missing (iv1_7 .. iv2_6). */
int test_save_work_mark_script_viewed(void)
{
    save_bank_arena_clear();
    seed_chara_zero();
    rng_seed(1);
    save_bank_init_all();
    save_work_mark_script_viewed(2, 5);
    const uint8_t *bank = save_work_bank_at(save_work_active_slot());
    T_ASSERT(bank != NULL);
    T_ASSERT_EQ_U(bank[0x2d3a0 + 60 * 2 + 5], 1u);
    T_ASSERT_EQ_U(save_bank_header_bytes()[0x20 + 60 * 2 + 5], 1u);
    T_ASSERT_EQ_U(bank[0x2d3a0 + 60 * 2 + 4], 0u);           /* only that script */
    save_work_mark_script_viewed(-1, 0);                      /* ignored */
    save_work_mark_script_viewed(1, 60);                      /* ignored */
    return 0;
}
