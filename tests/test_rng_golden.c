/* test_rng_golden.c — --rng-golden: per-tick split detection against retail's golden. */
#include "t.h"
#include "rng_golden.h"

#include <string.h>

static int load_str(const char *s)
{
    FILE *fp = tmpfile();
    if (!fp) return -1;
    fputs(s, fp);
    rewind(fp);
    int r = rng_golden_load_fp(fp);
    fclose(fp);
    return r;
}

static const char *GOLD =
    "# test golden\n"
    "frames 4\n"
    "0 1 00000010\n"
    "2 2 00000022\n"
    "4 1 00000040\n";

int test_rng_golden_identical_run(void)
{
    T_ASSERT(load_str(GOLD) == 0);
    T_ASSERT_EQ_U(rng_golden_frames(), 4);
    rng_golden_note(0, 0, 1, 0x10, 0x100);
    T_ASSERT(!rng_golden_end_tick(1));            /* ticks 0 and 1 (no draws) */
    rng_golden_note(1, 2, 0x10, 0x99, 0x104);     /* a reseed counts for nothing */
    rng_golden_note(0, 2, 0x99, 0x21, 0x108);
    rng_golden_note(0, 2, 0x21, 0x22, 0x10c);
    T_ASSERT(!rng_golden_end_tick(2));
    T_ASSERT(!rng_golden_end_tick(3));
    rng_golden_note(0, 4, 0x22, 0x40, 0x110);
    T_ASSERT(!rng_golden_end_tick(4));
    rng_golden_note(0, 5, 0x40, 0x50, 0x114);     /* past the golden: unchecked */
    T_ASSERT(!rng_golden_end_tick(5));
    uint32_t f = 99;
    T_ASSERT(!rng_golden_split(&f));
    rng_golden_reset();
    return 0;
}

int test_rng_golden_count_split_keeps_the_ticks_draws(void)
{
    T_ASSERT(load_str(GOLD) == 0);
    rng_golden_note(0, 0, 1, 0x10, 0x100);
    T_ASSERT(!rng_golden_end_tick(1));
    rng_golden_note(0, 2, 0x10, 0x21, 0x108);     /* one draw short */
    T_ASSERT(rng_golden_end_tick(2));
    uint32_t f = 0;
    T_ASSERT(rng_golden_split(&f));
    T_ASSERT_EQ_U(f, 2);
    rng_golden_note(0, 3, 0x21, 0x33, 0x200);     /* later draws don't replace the split's */
    T_ASSERT(rng_golden_end_tick(3));
    FILE *fp = tmpfile();
    T_ASSERT(fp != NULL);
    T_ASSERT(rng_golden_write_report(fp) == 0);
    rewind(fp);
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = 0;
    T_ASSERT(strstr(buf, "split 2 retail 2 00000022 port 1 00000021\n") != NULL);
    T_ASSERT(strstr(buf, "rand 2 00000010 -> 00000021 @000108\n") != NULL);
    T_ASSERT(strstr(buf, "@000200") == NULL);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_same_count_other_state_splits(void)
{
    T_ASSERT(load_str(GOLD) == 0);
    rng_golden_note(0, 0, 1, 0x99, 0x100);        /* boot (frame 0) is never compared */
    T_ASSERT(!rng_golden_end_tick(1));
    rng_golden_note(0, 2, 0x10, 0x21, 0x108);
    rng_golden_note(0, 2, 0x21, 0x23, 0x10c);     /* two draws, another LCG state */
    T_ASSERT(rng_golden_end_tick(2));
    uint32_t f = 9;
    T_ASSERT(rng_golden_split(&f));
    T_ASSERT_EQ_U(f, 2);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_missing_tick_draws_split(void)
{
    T_ASSERT(load_str(GOLD) == 0);
    rng_golden_note(0, 0, 1, 0x10, 0x100);
    T_ASSERT(rng_golden_end_tick(3));             /* tick 2 drew nothing: split at 2 */
    uint32_t f = 0;
    T_ASSERT(rng_golden_split(&f));
    T_ASSERT_EQ_U(f, 2);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_rejects_malformed(void)
{
    T_ASSERT(load_str("0 1 10\n") == -1);                 /* no frames header */
    T_ASSERT(load_str("frames 2\n5 1 10\n") == -1);       /* tick past the header */
    T_ASSERT(!rng_golden_active());
    return 0;
}

/* the per-tick state check: retail's block hashes vs the port's hasher */
static uint32_t s_hash[2];
static uint32_t fake_hash(uint32_t va, uint32_t len)
{
    (void)len;
    return va == 0x100u ? s_hash[0] : s_hash[1];
}

static const char *GOLD_TH =
    "frames 4\n"
    "2 1 00000022\n"
    "blocks 100:10,200:8\n"
    "th 1 0:000000aa 1:000000bb\n"
    "th 2\n"
    "th 3 1:000000cc\n";   /* tick 4: no line, past the coverage */

int test_rng_golden_state_identical_and_coverage(void)
{
    T_ASSERT(load_str(GOLD_TH) == 0);
    rng_golden_set_block_hash(fake_hash);
    s_hash[0] = 0xaa; s_hash[1] = 0xbb;
    T_ASSERT(!rng_golden_end_tick(1));
    rng_golden_note(0, 2, 0x10, 0x22, 0x108);
    T_ASSERT(!rng_golden_end_tick(2));            /* tick 2: unchanged since 1 */
    s_hash[1] = 0xcc;
    T_ASSERT(!rng_golden_end_tick(3));
    s_hash[0] = 0x1234;                           /* tick 4 is not covered */
    T_ASSERT(!rng_golden_end_tick(4));
    rng_golden_set_block_hash(NULL);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_state_split_names_the_block(void)
{
    T_ASSERT(load_str(GOLD_TH) == 0);
    rng_golden_set_block_hash(fake_hash);
    s_hash[0] = 0xaa; s_hash[1] = 0xbb;
    T_ASSERT(!rng_golden_end_tick(1));
    rng_golden_note(0, 2, 0x10, 0x22, 0x108);
    T_ASSERT(!rng_golden_end_tick(2));
    T_ASSERT(rng_golden_end_tick(3));             /* block 1 should be cc */
    uint32_t f = 0;
    T_ASSERT(rng_golden_split(&f));
    T_ASSERT_EQ_U(f, 3);
    FILE *fp = tmpfile();
    T_ASSERT(fp != NULL);
    T_ASSERT(rng_golden_write_report(fp) == 0);
    rewind(fp);
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = 0;
    T_ASSERT(strcmp(buf, "state 3 block 200:8 retail 000000cc port 000000bb\n") == 0);
    rng_golden_set_block_hash(NULL);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_rng_split_wins_over_state(void)
{
    T_ASSERT(load_str(GOLD_TH) == 0);
    rng_golden_set_block_hash(fake_hash);
    s_hash[0] = 0; s_hash[1] = 0;                 /* state wrong too */
    T_ASSERT(rng_golden_end_tick(1));             /* tick 1: RNG identical (no draws), state splits */
    rng_golden_reset();
    T_ASSERT(load_str(GOLD_TH) == 0);
    s_hash[0] = 0xaa; s_hash[1] = 0xbb;
    T_ASSERT(!rng_golden_end_tick(1));
    T_ASSERT(rng_golden_end_tick(2));             /* tick 2: one draw missing → an RNG split */
    FILE *fp = tmpfile();
    T_ASSERT(rng_golden_write_report(fp) == 0);
    rewind(fp);
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = 0;
    T_ASSERT(strncmp(buf, "split 2 ", 8) == 0);
    rng_golden_set_block_hash(NULL);
    rng_golden_reset();
    return 0;
}

int test_rng_golden_th_rejects_malformed(void)
{
    T_ASSERT(load_str("frames 2\nth 1 0:aa\n") == -1);                  /* th before blocks */
    T_ASSERT(load_str("frames 2\nblocks 100:4\nth 1 1:aa\n") == -1);    /* block index out of range */
    T_ASSERT(load_str("frames 2\nblocks 100:4\nth 2\nth 1\n") == -1);   /* not increasing */
    T_ASSERT(!rng_golden_active());
    return 0;
}

/* the scalars block: `blocks ..,0:LEN` + `vas`, hashed over the value reader in list order */
static int32_t s_val[2];
static int32_t fake_val(uint32_t va) { return va == 0x10 ? s_val[0] : va == 0x20 ? s_val[1] : 0; }

int test_rng_golden_scalars_block(void)
{
    /* FNV-1a over {1, 2} = ed74208a; {1, 3} = ee74221d */
    T_ASSERT(load_str("frames 3\nblocks 0:8\nvas 10,20\nth 1 0:ed74208a\nth 3 0:ee74221d\n") == 0);
    rng_golden_set_scalar_value(fake_val);
    s_val[0] = 1; s_val[1] = 2;
    T_ASSERT(!rng_golden_end_tick(1));
    T_ASSERT(!rng_golden_end_tick(2));            /* unchanged tick: forward-filled */
    s_val[1] = 5;
    T_ASSERT(rng_golden_end_tick(3));             /* retail went to 3 */
    FILE *fp = tmpfile();
    T_ASSERT(fp != NULL);
    T_ASSERT(rng_golden_write_report(fp) == 0);
    rewind(fp);
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = 0;
    T_ASSERT(strncmp(buf, "state 3 block 0:8 retail ee74221d port ", 39) == 0);
    T_ASSERT(strstr(buf, "\nvals 10=1 20=5\n") != NULL);
    rng_golden_set_scalar_value(NULL);
    rng_golden_reset();
    return 0;
}
