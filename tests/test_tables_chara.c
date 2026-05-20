/*
 * test_tables_chara.c — unit tests for `data/chara.txt` parsing.
 *
 * Coverage:
 *   1. Empty input                       (zero-init + defaults only)
 *   2. Defaults seeded                   (init values from L1035..L1047)
 *   3. Basic record                      (one "000:" line populates all 10 fields)
 *   4. Lv100 line alone                  ("100:" without "000:" overlays lv100 only)
 *   5. Both blocks combined              (000: + 100: into the same record)
 *   6. Comments + blank skipped          ('/', CRLF, LF-only, '//' which has '/' first)
 *   7. Out-of-range "008:" / "108:"      (engine bug guard: no OOB write)
 *   8. Field permutation lv100           (file AT,DF,MT,MF,HP,SP → struct lv100 fields)
 *   9. Vendor shape end-to-end           (8 adventurers, Louie + Arma spot checks)
 */
#include "t.h"
#include "tables_chara.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Float comparison: bit-equal is too strict for atof + (float) cast,
 * so allow a small epsilon. The vendor values like 0.175 and 0.2625
 * are exactly representable as float, but 0.05 and 0.06 are not — we
 * accept the nearest float. */
static int float_close(float a, float b)
{
    return fabsf(a - b) <= 1e-6f;
}

#define T_ASSERT_FLOAT_CLOSE(a, b) do {                                  \
    float _a = (a), _b = (b);                                            \
    if (!float_close(_a, _b))                                            \
        T_FAIL("expected %s ≈ %s (got %g, want %g)", #a, #b,             \
               (double)_a, (double)_b);                                  \
} while (0)

/* ------------------------------------------------------------------ */

int test_tables_chara_empty(void)
{
    chara_def_t out[CHARA_COUNT];
    memset(out, 0xCC, sizeof out);

    tables_parse_chara((const unsigned char *)"", 0, out);

    /* Every record must equal the engine defaults. */
    for (int i = 0; i < CHARA_COUNT; i++) {
        T_ASSERT_EQ_I(out[i].level_threshold, 1);
        T_ASSERT_EQ_I(out[i].hp_base, 50);
        T_ASSERT_EQ_I(out[i].sp_base, 30);
        T_ASSERT_EQ_I(out[i].at_base, 10);
        T_ASSERT_EQ_I(out[i].df_base, 13);
        T_ASSERT_EQ_I(out[i].mt_base, 5);
        T_ASSERT_EQ_I(out[i].mf_base, 10);
        T_ASSERT_FLOAT_CLOSE(out[i].move_speed, 0.15f);
        T_ASSERT_FLOAT_CLOSE(out[i].dash_speed, 0.20f);
        T_ASSERT_FLOAT_CLOSE(out[i].crit_rate, 0.0f);
        T_ASSERT_EQ_I(out[i].hp_lv100, 0);
        T_ASSERT_EQ_I(out[i].sp_lv100, 0);
        T_ASSERT_EQ_I(out[i].at_lv100, 0);
        T_ASSERT_EQ_I(out[i].df_lv100, 0);
        T_ASSERT_EQ_I(out[i].mt_lv100, 0);
        T_ASSERT_EQ_I(out[i].mf_lv100, 0);
    }
    return 0;
}

int test_tables_chara_defaults_bit_exact(void)
{
    /* The init writes 0x3e19999a and 0x3e4ccccd to the move/dash
     * slots. Verify these match `0.15f` and `0.20f` byte-for-byte,
     * since the C float literals must produce the engine's bytes. */
    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)"", 0, out);

    uint32_t move_bits, dash_bits;
    memcpy(&move_bits, &out[0].move_speed, 4);
    memcpy(&dash_bits, &out[0].dash_speed, 4);
    T_ASSERT_EQ_U(move_bits, 0x3e19999au);
    T_ASSERT_EQ_U(dash_bits, 0x3e4ccccdu);
    return 0;
}

int test_tables_chara_basic_record(void)
{
    /* Recreates Louie's vendor row exactly. */
    const char input[] =
        "000:1, 10,10, 4, 4, 20,10, 0.175, 0.2625, 0.05\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    /* file field1 = 1 → stored 0 */
    T_ASSERT_EQ_I(out[0].level_threshold, 0);
    /* file field6,7 = 20,10 → hp/sp base */
    T_ASSERT_EQ_I(out[0].hp_base, 20);
    T_ASSERT_EQ_I(out[0].sp_base, 10);
    /* file field2..5 = 10,10,4,4 → AT/DF/MT/MF base */
    T_ASSERT_EQ_I(out[0].at_base, 10);
    T_ASSERT_EQ_I(out[0].df_base, 10);
    T_ASSERT_EQ_I(out[0].mt_base, 4);
    T_ASSERT_EQ_I(out[0].mf_base, 4);
    /* file field8..10 = move/dash/crit */
    T_ASSERT_FLOAT_CLOSE(out[0].move_speed, 0.175f);
    T_ASSERT_FLOAT_CLOSE(out[0].dash_speed, 0.2625f);
    T_ASSERT_FLOAT_CLOSE(out[0].crit_rate, 0.05f);
    /* Record 0 has no lv100 line yet → lv100 stays zero. */
    T_ASSERT_EQ_I(out[0].hp_lv100, 0);
    T_ASSERT_EQ_I(out[0].sp_lv100, 0);
    T_ASSERT_EQ_I(out[0].at_lv100, 0);
    /* Other records untouched → still defaults. */
    T_ASSERT_EQ_I(out[1].level_threshold, 1);
    T_ASSERT_EQ_I(out[1].hp_base, 50);
    return 0;
}

int test_tables_chara_lv100_alone(void)
{
    /* A "100:" line by itself populates lv100 fields, leaves base at
     * defaults (record 0's base stays at level_threshold=1, hp=50, ...). */
    const char input[] =
        "100: 93, 95, 50, 68, 460,100\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    /* file AT,DF,MT,MF,HP,SP = 93,95,50,68,460,100 */
    T_ASSERT_EQ_I(out[0].at_lv100, 93);
    T_ASSERT_EQ_I(out[0].df_lv100, 95);
    T_ASSERT_EQ_I(out[0].mt_lv100, 50);
    T_ASSERT_EQ_I(out[0].mf_lv100, 68);
    T_ASSERT_EQ_I(out[0].hp_lv100, 460);
    T_ASSERT_EQ_I(out[0].sp_lv100, 100);
    /* Base stats untouched → defaults. */
    T_ASSERT_EQ_I(out[0].level_threshold, 1);
    T_ASSERT_EQ_I(out[0].hp_base, 50);
    T_ASSERT_EQ_I(out[0].sp_base, 30);
    T_ASSERT_EQ_I(out[0].at_base, 10);
    return 0;
}

int test_tables_chara_both_blocks_combined(void)
{
    /* Both "000:" and "100:" target the same record — they accumulate. */
    const char input[] =
        "000:1, 10,10, 4, 4, 20,10, 0.175, 0.2625, 0.05\r\n"
        "100: 93, 95, 50, 68, 460,100\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    /* Base */
    T_ASSERT_EQ_I(out[0].at_base, 10);
    T_ASSERT_EQ_I(out[0].hp_base, 20);
    T_ASSERT_FLOAT_CLOSE(out[0].crit_rate, 0.05f);
    /* Lv100 */
    T_ASSERT_EQ_I(out[0].hp_lv100, 460);
    T_ASSERT_EQ_I(out[0].at_lv100, 93);
    T_ASSERT_EQ_I(out[0].mf_lv100, 68);
    return 0;
}

int test_tables_chara_comments_skipped(void)
{
    /* '/' anywhere as the first byte → skip. '//' starts with '/' so
     * skipped. Blank lines (CRLF, LF) → skipped. */
    const char input[] =
        "// human comment\r\n"
        "/000:99,99,99,99,99,99,99,9.9,9.9,9.9\r\n"      /* '/' prefix → skip */
        "\r\n"
        "\n"
        "001:8,  9, 8, 8, 9, 16,15, 0.195, 0.2925, 0.07\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    /* Record 0 must still be at defaults (the /000: line was skipped). */
    T_ASSERT_EQ_I(out[0].level_threshold, 1);
    T_ASSERT_EQ_I(out[0].at_base, 10);
    T_ASSERT_EQ_I(out[0].hp_base, 50);
    /* Record 1 must be Charme's stats (file_lv 8 → stored 7). */
    T_ASSERT_EQ_I(out[1].level_threshold, 7);
    T_ASSERT_EQ_I(out[1].at_base, 9);
    T_ASSERT_EQ_I(out[1].df_base, 8);
    T_ASSERT_EQ_I(out[1].mt_base, 8);
    T_ASSERT_EQ_I(out[1].mf_base, 9);
    T_ASSERT_EQ_I(out[1].hp_base, 16);
    T_ASSERT_EQ_I(out[1].sp_base, 15);
    return 0;
}

int test_tables_chara_out_of_range_index_guarded(void)
{
    /* "008:" and "108:" would write into g_models[0] in the engine.
     * The port caps the inner match loop at CHARA_COUNT (=8) so
     * these lines silently no-op. All 8 records stay at defaults
     * (no record is "the 9th"). */
    const char input[] =
        "008:1,99,99,99,99,99,99,9.9,9.9,9.9\r\n"
        "108: 999, 999, 999, 999, 9999, 999\r\n"
        "009:1,99,99,99,99,99,99,9.9,9.9,9.9\r\n"
        "109: 999, 999, 999, 999, 9999, 999\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    for (int i = 0; i < CHARA_COUNT; i++) {
        T_ASSERT_EQ_I(out[i].level_threshold, 1);
        T_ASSERT_EQ_I(out[i].at_base, 10);
        T_ASSERT_EQ_I(out[i].hp_lv100, 0);
        T_ASSERT_EQ_I(out[i].at_lv100, 0);
    }
    return 0;
}

int test_tables_chara_lv100_field_permutation(void)
{
    /* Verify the explicit AT,DF,MT,MF,HP,SP → at,df,mt,mf,hp,sp
     * field-order permutation with distinct sentinels. */
    const char input[] =
        "100:11,22,33,44,55,66\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara((const unsigned char *)input, sizeof input - 1, out);

    T_ASSERT_EQ_I(out[0].at_lv100, 11);
    T_ASSERT_EQ_I(out[0].df_lv100, 22);
    T_ASSERT_EQ_I(out[0].mt_lv100, 33);
    T_ASSERT_EQ_I(out[0].mf_lv100, 44);
    T_ASSERT_EQ_I(out[0].hp_lv100, 55);
    T_ASSERT_EQ_I(out[0].sp_lv100, 66);
    return 0;
}

int test_tables_chara_vendor_shape(void)
{
    /* Reproduces the vendor data/chara.txt shape using ASCII only
     * (SJIS comment lines start with '/' and are skipped, so we omit
     * them from the fixture). 8 adventurers, both base and lv100. */
    static const unsigned char input[] =
        "000:1, 10,10, 4, 4, 20,10, 0.175, 0.2625, 0.05\r\n"
        "001:8,  9, 8, 8, 9, 16,15, 0.195, 0.2925, 0.07\r\n"
        "002:10, 6, 6,12,14, 10,50, 0.155, 0.2025, 0.04\r\n"
        "003:20, 7, 7, 8,10, 13,18, 0.175, 0.2625, 0.05\r\n"
        "004:15,13,11, 8, 9, 22,16, 0.185, 0.2775, 0.06\r\n"
        "005:15, 8, 9, 5, 7, 16,12, 0.155, 0.2325, 0.05\r\n"
        "006:30,12, 7,11,13, 18,20, 0.175, 0.2625, 0.06\r\n"
        "007:1, 11,13, 9,11, 25,30, 0.175, 0.2625, 0.00\r\n"
        "100: 93, 95, 50, 68, 460,100\r\n"
        "101: 88, 82, 68, 79, 340,250\r\n"
        "102: 52, 68,100, 95, 190,700\r\n"
        "103: 81, 78, 70, 86, 280,350\r\n"
        "104: 99,104, 52, 65, 600,150\r\n"
        "105: 87, 86, 65, 77, 380,300\r\n"
        "106: 90, 76, 88,106, 300,500\r\n"
        "107: 94, 96, 80, 90, 500,990\r\n";

    chara_def_t out[CHARA_COUNT];
    tables_parse_chara(input, sizeof input - 1, out);

    /* Record 0 — Louie */
    T_ASSERT_EQ_I(out[0].level_threshold, 0);
    T_ASSERT_EQ_I(out[0].hp_base, 20);
    T_ASSERT_EQ_I(out[0].sp_base, 10);
    T_ASSERT_EQ_I(out[0].at_base, 10);
    T_ASSERT_EQ_I(out[0].df_base, 10);
    T_ASSERT_FLOAT_CLOSE(out[0].move_speed, 0.175f);
    T_ASSERT_FLOAT_CLOSE(out[0].dash_speed, 0.2625f);
    T_ASSERT_FLOAT_CLOSE(out[0].crit_rate, 0.05f);
    T_ASSERT_EQ_I(out[0].hp_lv100, 460);
    T_ASSERT_EQ_I(out[0].at_lv100, 93);

    /* Record 6 — Griff (highest unlock level: 30 → stored 29) */
    T_ASSERT_EQ_I(out[6].level_threshold, 29);
    T_ASSERT_EQ_I(out[6].at_base, 12);
    T_ASSERT_EQ_I(out[6].hp_base, 18);
    T_ASSERT_EQ_I(out[6].mf_lv100, 106);

    /* Record 7 — Arma (level_threshold = 0, crit_rate = 0.0) */
    T_ASSERT_EQ_I(out[7].level_threshold, 0);
    T_ASSERT_EQ_I(out[7].at_base, 11);
    T_ASSERT_EQ_I(out[7].df_base, 13);
    T_ASSERT_EQ_I(out[7].hp_base, 25);
    T_ASSERT_EQ_I(out[7].sp_base, 30);
    T_ASSERT_FLOAT_CLOSE(out[7].crit_rate, 0.0f);
    T_ASSERT_EQ_I(out[7].sp_lv100, 990);
    T_ASSERT_EQ_I(out[7].hp_lv100, 500);

    /* All 8 records have non-default level_threshold (they were
     * parsed) and non-zero hp_lv100 (lv100 block present). */
    for (int i = 0; i < CHARA_COUNT; i++) {
        T_ASSERT(out[i].level_threshold != 1);  /* parsed */
        T_ASSERT(out[i].hp_lv100 != 0);
    }

    return 0;
}
