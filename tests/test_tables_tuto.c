/*
 * test_tables_tuto.c — unit tests for src/tables_tuto.c.
 *
 * Tests the FUN_00475270 tutorial-loop parser: opcode dispatch (16
 * keywords across ASCII + SJIS), parser stride (200 records / file),
 * sentinel writes, the engine's `id < -1` text-only branch, and the
 * 7-int reader that walks past line ends on short lines.
 *
 * Pure C, runnable under host gcc + ASan/UBSan via `make -C tests run`.
 * No vendor file required.
 */

#include "t.h"
#include "tables_tuto.h"

/* SJIS bytes for the Japanese opcode keywords. Source: pe.py str-dump
 * of the engine's opcode table at .data:0x005cb3e0..0x005cb434. */
#define KW_NEDAN    "\x92\x6c\x92\x69"          /* 値段, 4 bytes  */
#define KW_TAKAKU   "\x8d\x82\x82\xad"          /* 高く, 4 bytes  */
#define KW_NEBIKI   "\x92\x6c\x88\xf8"          /* 値引, 4 bytes  */
#define KW_NEAGE    "\x92\x6c\x8f\xe3"          /* 値上, 4 bytes  */
#define KW_SHOKI    "\x8f\x89\x8a\xfa\x8b\xe0\x8a\x7a\x8c\x88\x92\xe8"  /* 初期金額決定, 12 bytes */
#define KW_AITEMU   "\x83\x41\x83\x43\x83\x65\x83\x80"                  /* アイテム, 8 bytes */
#define KW_KENSEN   "\x8c\x95\x91\x49\x91\xf0"                          /* 剣選択, 6 bytes */

/* Local fixture array — sized large enough for any single test. */
#define FIX_SLOTS  600

static void clear_fixture(struct tuto_record *fix)
{
    memset(fix, 0, sizeof(struct tuto_record) * FIX_SLOTS);
}

/* ─── core: empty / sentinel / blanks ──────────────────────────────────── */

int test_tables_tuto_empty(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    int n = tables_parse_tuto(0, (const unsigned char *)"", 0, fix);
    T_ASSERT_EQ_I(n, 0);
    /* Sentinel at slot 0 (file 0 base). */
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_SENTINEL);
    return 0;
}

int test_tables_tuto_blanks_and_comments(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "\r\n"
        "//comment line\r\n"
        "//\xb0\xbd\xc4\xde  SJIS comment\r\n"
        "\n"
        "0,TAGD,\t//actual record\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].id, 0);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_TAGD);
    /* Sentinel at slot 1. */
    T_ASSERT_EQ_I(fix[1].opcode, TUTO_OP_SENTINEL);
    return 0;
}

/* ─── opcode dispatch — ASCII 4-byte tokens ────────────────────────────── */

int test_tables_tuto_chr0_basic(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "0,CHR0,4,O-kai-o!\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_CHR0);
    T_ASSERT_EQ_I(fix[0].chr_arg, 4);
    T_ASSERT_EQ_I(strcmp(fix[0].text, "O-kai-o!"), 0);
    return 0;
}

int test_tables_tuto_chr1_basic(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0,CHR1,0,Well then.<BR>Listen up.\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_CHR1);
    T_ASSERT_EQ_I(fix[0].chr_arg, 0);
    T_ASSERT_EQ_I(strcmp(fix[0].text, "Well then.<BR>Listen up."), 0);
    return 0;
}

int test_tables_tuto_no_arg_opcodes(void)
{
    /* TAGD/PRID/PRIA/TAGN/TOUT: opcode set, no further reads.
     * Each on its own line; expect the opcode in the corresponding slot. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0,TAGD\r\n"
        "0,PRID\r\n"
        "9,PRIA,\t//tab\r\n"
        "0,TAGN\r\n"
        "0,TOUT\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 5);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_TAGD);
    T_ASSERT_EQ_I(fix[1].opcode, TUTO_OP_PRID);
    T_ASSERT_EQ_I(fix[2].opcode, TUTO_OP_PRIA);
    T_ASSERT_EQ_I(fix[2].id,     9);  /* PRIA preserves jump-target id */
    T_ASSERT_EQ_I(fix[3].opcode, TUTO_OP_TAGN);
    T_ASSERT_EQ_I(fix[4].opcode, TUTO_OP_TOUT);
    return 0;
}

/* ─── 7-int reader ─────────────────────────────────────────────────────── */

int test_tables_tuto_goto_7_ints(void)
{
    /* GOTO with all 7 fields filled. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0,GOTO,9,10,11,12,13,14,15\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_GOTO);
    T_ASSERT_EQ_I(fix[0].args[0], 9);
    T_ASSERT_EQ_I(fix[0].args[1], 10);
    T_ASSERT_EQ_I(fix[0].args[2], 11);
    T_ASSERT_EQ_I(fix[0].args[3], 12);
    T_ASSERT_EQ_I(fix[0].args[4], 13);
    T_ASSERT_EQ_I(fix[0].args[5], 14);
    T_ASSERT_EQ_I(fix[0].args[6], 15);
    return 0;
}

int test_tables_tuto_goto_short_args_zero(void)
{
    /* `0,GOTO,9,//...` — only 1 int provided. Engine reads stack
     * garbage for the rest; our port reads zeros. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "0,GOTO,9,\t//9へ戻る\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_GOTO);
    T_ASSERT_EQ_I(fix[0].args[0], 9);
    /* Remaining args are 0 thanks to the zeroed line buffer. */
    for (int k = 1; k < 7; k++) {
        T_ASSERT_EQ_I(fix[0].args[k], 0);
    }
    return 0;
}

int test_tables_tuto_bun0_7_ints(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "0,BUN0,1,2,3,4,5,6,7\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_BUN0);
    for (int k = 0; k < 7; k++) {
        T_ASSERT_EQ_I(fix[0].args[k], k + 1);
    }
    return 0;
}

/* ─── SJIS opcode dispatch ─────────────────────────────────────────────── */

int test_tables_tuto_nedan_alias_takaku(void)
{
    /* Per the engine parser .data table (by-address 475270.c:2987-3046):
     * 値段 (0x5cb3e0) maps to op 5 (BUN0, the 7-tier fileidx-gated threshold)
     * — it is the BUY tutorial's branch — while 高く (0x5cb3e8) maps to op 12
     * (the 2-way PRICE compare) — tuto1's sell check.  They are NOT aliases. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0," KW_NEDAN  ",10,11,12,13,14,15,16\r\n"
        "0," KW_TAKAKU ",20,21,22,23,24,25,26\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_BUN0);    /* 値段 → op 5 (7-tier) */
    T_ASSERT_EQ_I(fix[0].args[0], 10);
    T_ASSERT_EQ_I(fix[0].args[6], 16);
    T_ASSERT_EQ_I(fix[1].opcode, TUTO_OP_PRICE);   /* 高く → op 12 (2-way) */
    T_ASSERT_EQ_I(fix[1].args[0], 20);
    T_ASSERT_EQ_I(fix[1].args[6], 26);
    return 0;
}

int test_tables_tuto_nebiki_neage(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0," KW_NEBIKI ",1,2,3,4,5,6,7\r\n"
        "0," KW_NEAGE  ",10,20,30,40,50,60,70\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_DISCOUNT);
    T_ASSERT_EQ_I(fix[1].opcode, TUTO_OP_MARKUP);
    T_ASSERT_EQ_I(fix[1].args[0], 10);
    return 0;
}

int test_tables_tuto_shoki_kingaku_kettei(void)
{
    /* 初期金額決定 (12 bytes) — no args. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "0," KW_SHOKI "\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_SET_INITIAL);
    return 0;
}

int test_tables_tuto_aitemu(void)
{
    /* アイテム (8 bytes) — no args. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "1," KW_AITEMU ",\t//comment\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].id, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_ITEM);
    return 0;
}

int test_tables_tuto_kensen_7_ints(void)
{
    /* 剣選択 (6 bytes) reads 7 ints. Vendor uses only the first two
     * (`0,剣選択,10,11,`) so the rest read zero in our port. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0," KW_KENSEN ",10,11,\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_SWORD);
    T_ASSERT_EQ_I(fix[0].args[0], 10);
    T_ASSERT_EQ_I(fix[0].args[1], 11);
    for (int k = 2; k < 7; k++) T_ASSERT_EQ_I(fix[0].args[k], 0);
    return 0;
}

/* ─── negative-id branches ────────────────────────────────────────────── */

int test_tables_tuto_id_minus_one_sentinel(void)
{
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] = "-1,-1,\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(fix[0].id, -1);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_SENTINEL);
    /* The trailing end-of-file sentinel lands at the next slot. */
    T_ASSERT_EQ_I(fix[1].opcode, TUTO_OP_SENTINEL);
    return 0;
}

int test_tables_tuto_id_below_minus_one_text_only(void)
{
    /* `-N,text` for N>=2: text copied from offset 3, opcode untouched
     * (stays at the BSS-zero default of CHR0). */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "-2,Let us try again.<BR>Carefully.\r\n"
        "-3,Excellent.\r\n";
    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(fix[0].id, -2);
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_CHR0);  /* unset → BSS-zero */
    T_ASSERT_EQ_I(strcmp(fix[0].text, "Let us try again.<BR>Carefully."), 0);
    T_ASSERT_EQ_I(fix[1].id, -3);
    T_ASSERT_EQ_I(strcmp(fix[1].text, "Excellent."), 0);
    return 0;
}

/* ─── parser stride / overflow ────────────────────────────────────────── */

int test_tables_tuto_file_index_stride(void)
{
    /* file_index=1 → writes start at slot 200 (= file_idx * 200). The
     * parser stride matches the consumer (runtime-confirmed 2026-06-20),
     * so each file owns its own 200-slot region — no overlap. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);
    static const unsigned char input[] =
        "0,CHR1,1,first record of tuto2\r\n";
    int n = tables_parse_tuto(1, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 1);
    /* Slots 0..199 untouched (file-0's clean region). */
    T_ASSERT_EQ_I(fix[0].opcode, TUTO_OP_CHR0);  /* BSS-zero */
    T_ASSERT_EQ_I(fix[0].chr_arg, 0);
    T_ASSERT_EQ_I(fix[199].chr_arg, 0);
    /* Record written at slot 200. */
    T_ASSERT_EQ_I(fix[200].opcode, TUTO_OP_CHR1);
    T_ASSERT_EQ_I(fix[200].chr_arg, 1);
    T_ASSERT_EQ_I(strcmp(fix[200].text, "first record of tuto2"), 0);
    /* Sentinel at slot 201. */
    T_ASSERT_EQ_I(fix[201].opcode, TUTO_OP_SENTINEL);
    return 0;
}

int test_tables_tuto_no_overlap_into_next_file(void)
{
    /* Synthesize a 60-record file-0. With the stride==200 region, all 60
     * records sit cleanly in slots 0..59 and NOTHING lands in file-1's
     * region (slot 200) — proving the files no longer collide (the bug
     * the old stride-50 reading caused: tuto2/tuto3 overwrote tuto1). */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);

    /* Build "0,CHR0,N,t\r\n" repeated 60 times, where N is the record
     * index so we can spot which slot got which. */
    unsigned char buf[4096];
    size_t pos = 0;
    for (int i = 0; i < 60; i++) {
        int w = snprintf((char *)buf + pos, sizeof buf - pos,
                         "0,CHR0,%d,t\r\n", i);
        pos += (size_t)w;
    }

    int n = tables_parse_tuto(0, buf, pos, fix);
    T_ASSERT_EQ_I(n, 60);
    T_ASSERT_EQ_I(fix[ 0].chr_arg, 0);
    T_ASSERT_EQ_I(fix[59].chr_arg, 59);
    T_ASSERT_EQ_I(fix[60].opcode, TUTO_OP_SENTINEL);  /* end-sentinel */
    /* file-1's region (slot 200) is pristine — no collision. */
    T_ASSERT_EQ_I(fix[200].opcode, TUTO_OP_CHR0);     /* BSS-zero */
    T_ASSERT_EQ_I(fix[200].chr_arg, 0);
    return 0;
}

/* ─── vendor-shape integration ────────────────────────────────────────── */

int test_tables_tuto_vendor_like_shape(void)
{
    /* Reproduces a slice of the real tuto1.txt structure: opening
     * dialogue, a TAGD cue, a PRID/PRIA pair with a GOTO. Validates
     * dispatch ordering and that the chr_arg/text fields are stored
     * even with CRLF / tab-comment trailers. */
    struct tuto_record fix[FIX_SLOTS];
    clear_fixture(fix);

    static const unsigned char input[] =
        "0,CHR1,0,Well then.<BR>Lecture begins.\r\n"
        "0,CHR0,4,O-kai-o!\r\n"
        "//Comedy extras added!\r\n"
        "0,TAGD,\t//comment\r\n"
        "\r\n"
        "0,PRID,\t//comment\r\n"
        "9,PRIA,\t//comment\r\n"
        "0," KW_TAKAKU ",11,10\r\n"
        "10,CHR1,2,Set the price higher.\r\n"
        "0,GOTO,9,\t//back to 9\r\n"
        "11,CHR1,1,Yes very good.\r\n"
        "0," KW_SHOKI "\r\n";

    int n = tables_parse_tuto(0, input, sizeof input - 1, fix);
    T_ASSERT_EQ_I(n, 10);

    T_ASSERT_EQ_I(fix[ 0].opcode, TUTO_OP_CHR1);
    T_ASSERT_EQ_I(fix[ 0].chr_arg, 0);

    T_ASSERT_EQ_I(fix[ 1].opcode, TUTO_OP_CHR0);
    T_ASSERT_EQ_I(fix[ 1].chr_arg, 4);
    T_ASSERT_EQ_I(strcmp(fix[1].text, "O-kai-o!"), 0);

    /* TAGD / PRID / PRIA — no further reads. */
    T_ASSERT_EQ_I(fix[ 2].opcode, TUTO_OP_TAGD);
    T_ASSERT_EQ_I(fix[ 3].opcode, TUTO_OP_PRID);
    T_ASSERT_EQ_I(fix[ 4].opcode, TUTO_OP_PRIA);
    T_ASSERT_EQ_I(fix[ 4].id,     9);

    /* 高く → opcode 12, args[0..1] = 11, 10. */
    T_ASSERT_EQ_I(fix[ 5].opcode, TUTO_OP_PRICE);
    T_ASSERT_EQ_I(fix[ 5].args[0], 11);
    T_ASSERT_EQ_I(fix[ 5].args[1], 10);

    /* CHR1 with jump-target id=10. */
    T_ASSERT_EQ_I(fix[ 6].id, 10);
    T_ASSERT_EQ_I(fix[ 6].opcode, TUTO_OP_CHR1);
    T_ASSERT_EQ_I(fix[ 6].chr_arg, 2);

    /* GOTO 9. */
    T_ASSERT_EQ_I(fix[ 7].opcode, TUTO_OP_GOTO);
    T_ASSERT_EQ_I(fix[ 7].args[0], 9);

    /* CHR1 with jump-target id=11. */
    T_ASSERT_EQ_I(fix[ 8].id, 11);
    T_ASSERT_EQ_I(fix[ 8].opcode, TUTO_OP_CHR1);

    /* 初期金額決定 (12-byte SJIS) — opcode 20, no args. */
    T_ASSERT_EQ_I(fix[ 9].opcode, TUTO_OP_SET_INITIAL);

    /* Sentinel after the last record (slot 10). */
    T_ASSERT_EQ_I(fix[10].opcode, TUTO_OP_SENTINEL);
    return 0;
}
