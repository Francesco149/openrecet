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
