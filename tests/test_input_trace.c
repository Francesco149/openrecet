/*
 * test_input_trace.c — sparse-JSONL record/replay for the Phase A harness.
 *
 * Covers:
 *   - parse_buf happy path: single line, sparse multi-line, hex+dec mix
 *   - lookup semantics: before first, at boundaries, sparse hold
 *   - parse rejects: out-of-order frames, unknown keys, missing keys,
 *     malformed JSON, mask > 0xffff
 *   - record path: open / change-detection / close round-trip via /tmp
 */
#define _GNU_SOURCE
#include "t.h"
#include "input_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int test_input_trace_parse_single_line(void)
{
    const char buf[] = "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 1);
    T_ASSERT_EQ_U(tr.entries[0].frame, 0);
    T_ASSERT_EQ_U(tr.entries[0].mask,  0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_sparse_three_lines(void)
{
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":30,\"buttons\":\"0x0010\"}\n"
        "{\"frame\":31,\"buttons\":\"0x0000\"}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 3);
    T_ASSERT_EQ_U(tr.entries[1].frame, 30);
    T_ASSERT_EQ_U(tr.entries[1].mask,  0x10);
    T_ASSERT_EQ_U(tr.entries[2].frame, 31);
    T_ASSERT_EQ_U(tr.entries[2].mask,  0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_decimal_buttons(void)
{
    /* JSON number form, no quotes, decimal — covers a hand-authored
     * scenario where the user typed `16` instead of `"0x0010"`. */
    const char buf[] = "{\"frame\":0,\"buttons\":0}\n"
                       "{\"frame\":5,\"buttons\":16}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 2);
    T_ASSERT_EQ_U(tr.entries[1].mask, 16);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_skips_comments_and_blank_lines(void)
{
    const char buf[] =
        "# z-press at frame 30\n"
        "\n"
        "{\"frame\":0,\"buttons\":0}\n"
        "  # indented comment\n"
        "{\"frame\":30,\"buttons\":\"0x10\"}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 2);
    T_ASSERT_EQ_U(tr.entries[1].frame, 30);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_buttons_key_first_also_works(void)
{
    /* The two keys may appear in either order. */
    const char buf[] = "{\"buttons\":\"0x10\",\"frame\":30}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 1);
    T_ASSERT_EQ_U(tr.entries[0].frame, 30);
    T_ASSERT_EQ_U(tr.entries[0].mask,  0x10);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_rejects_out_of_order_frames(void)
{
    const char buf[] =
        "{\"frame\":30,\"buttons\":0}\n"
        "{\"frame\":10,\"buttons\":1}\n";  /* goes backwards */
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 0);
    /* First entry was accepted before the second one failed. */
    T_ASSERT_EQ_U(tr.count, 1);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_rejects_duplicate_frames(void)
{
    /* Strictly-increasing required — equal frames aren't allowed
     * because then `lookup` would be ambiguous about which mask to
     * return. */
    const char buf[] =
        "{\"frame\":10,\"buttons\":0}\n"
        "{\"frame\":10,\"buttons\":1}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_parse_rejects_unknown_key(void)
{
    const char buf[] = "{\"frame\":0,\"derp\":1}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 0);
    return 0;
}

int test_input_trace_parse_rejects_missing_buttons(void)
{
    const char buf[] = "{\"frame\":0}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 0);
    return 0;
}

int test_input_trace_parse_rejects_mask_above_16_bit(void)
{
    const char buf[] = "{\"frame\":0,\"buttons\":\"0x10000\"}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 0);
    return 0;
}

int test_input_trace_parse_empty_buffer(void)
{
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf("", 0, &tr) == 1);
    T_ASSERT_EQ_U(tr.count, 0);
    return 0;
}

int test_input_trace_lookup_before_first_returns_zero(void)
{
    const char buf[] = "{\"frame\":10,\"buttons\":\"0x10\"}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 0), 0);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 9), 0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_lookup_holds_between_entries(void)
{
    /* Sparse semantics: mask set at frame 30 stays in effect at
     * frames 31..N until the next entry. */
    const char buf[] =
        "{\"frame\":0,\"buttons\":0}\n"
        "{\"frame\":30,\"buttons\":\"0x10\"}\n"
        "{\"frame\":31,\"buttons\":0}\n";
    struct input_trace tr = {0};
    T_ASSERT(input_trace_parse_buf(buf, sizeof(buf) - 1, &tr) == 1);

    T_ASSERT_EQ_U(input_trace_lookup(&tr, 29), 0);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 30), 0x10);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 31), 0);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 99999), 0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_lookup_empty_trace(void)
{
    struct input_trace tr = {0};
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 0),  0);
    T_ASSERT_EQ_U(input_trace_lookup(&tr, 30), 0);
    return 0;
}

int test_input_trace_load_round_trips_real_file(void)
{
    char path[80];
    snprintf(path, sizeof path, "/tmp/openrecet_input_trace_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) T_SKIP("mkstemp failed");

    const char body[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":42,\"buttons\":\"0x0010\"}\n";
    write(fd, body, sizeof(body) - 1);
    close(fd);

    struct input_trace tr = {0};
    int rc = input_trace_load(path, &tr);
    unlink(path);
    T_ASSERT(rc == 1);
    T_ASSERT_EQ_U(tr.count, 2);
    T_ASSERT_EQ_U(tr.entries[1].frame, 42);
    T_ASSERT_EQ_U(tr.entries[1].mask,  0x10);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_load_missing_file_returns_zero(void)
{
    struct input_trace tr = {0};
    T_ASSERT(input_trace_load("/tmp/openrecet_does_not_exist_xyzzy", &tr) == 0);
    return 0;
}

int test_input_trace_parse_grows_past_old_fixed_cap(void)
{
    /* Regression: the table used to be a fixed 4096-entry array, so any
     * longer trace silently failed the *whole* load (an 8256-entry
     * trace returned 0 → "replay disabled"). The heap table must now
     * grow to hold N ≫ 4096 entries. Build a dense N-line trace
     * (strictly-increasing frames) and confirm every entry round-trips. */
    const uint32_t N = 9000;   /* > the old 4096 cap, ~ the 8256 that bit us */
    /* Each line ≤ 32 bytes; size generously. */
    size_t capb = (size_t)N * 40 + 16;
    char *buf = malloc(capb);
    T_ASSERT(buf != NULL);

    size_t off = 0;
    for (uint32_t i = 0; i < N; i++) {
        int w = snprintf(buf + off, capb - off,
                         "{\"frame\":%u,\"buttons\":\"0x%04x\"}\n",
                         (unsigned)i, (unsigned)(i & 0x3fff));
        T_ASSERT(w > 0 && (size_t)w < capb - off);
        off += (size_t)w;
    }

    struct input_trace tr = {0};
    int rc = input_trace_parse_buf(buf, off, &tr);
    free(buf);
    T_ASSERT(rc == 1);
    T_ASSERT_EQ_U(tr.count, N);
    /* Spot-check entries past the old cap. */
    T_ASSERT_EQ_U(tr.entries[4096].frame, 4096);
    T_ASSERT_EQ_U(tr.entries[N - 1].frame, N - 1);
    T_ASSERT_EQ_U(tr.entries[N - 1].mask,  (N - 1) & 0x3fff);
    /* lookup still binary-searches correctly at the tail. */
    T_ASSERT_EQ_U(input_trace_lookup(&tr, N - 1), (N - 1) & 0x3fff);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_record_emits_first_frame_and_changes(void)
{
    /* Open record → write three frames where mask transitions are
     * 0 → 0 (no emit) → 0x10 (emit) → 0x10 (no emit) → 0 (emit). The
     * first call ALSO emits since record needs a seed line. */
    T_ASSERT(!input_trace_record_is_open());
    char path[80];
    snprintf(path, sizeof path, "/tmp/openrecet_input_record_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) T_SKIP("mkstemp failed");
    close(fd);

    T_ASSERT(input_trace_record_open(path) == 1);
    T_ASSERT(input_trace_record_is_open());

    input_trace_record_frame(0,  0);
    input_trace_record_frame(1,  0);     /* no change → no emit */
    input_trace_record_frame(30, 0x10);
    input_trace_record_frame(31, 0x10);  /* no change → no emit */
    input_trace_record_frame(32, 0);
    input_trace_record_close();
    T_ASSERT(!input_trace_record_is_open());

    /* Re-parse the produced file with the load path — round-trip. */
    struct input_trace tr = {0};
    int rc = input_trace_load(path, &tr);
    unlink(path);
    T_ASSERT(rc == 1);
    T_ASSERT_EQ_U(tr.count, 3);
    T_ASSERT_EQ_U(tr.entries[0].frame, 0);
    T_ASSERT_EQ_U(tr.entries[0].mask,  0);
    T_ASSERT_EQ_U(tr.entries[1].frame, 30);
    T_ASSERT_EQ_U(tr.entries[1].mask,  0x10);
    T_ASSERT_EQ_U(tr.entries[2].frame, 32);
    T_ASSERT_EQ_U(tr.entries[2].mask,  0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_record_reopen_truncates(void)
{
    char path[80];
    snprintf(path, sizeof path, "/tmp/openrecet_input_record_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) T_SKIP("mkstemp failed");
    close(fd);

    T_ASSERT(input_trace_record_open(path) == 1);
    input_trace_record_frame(0, 0xabcd);
    input_trace_record_close();

    /* Reopen same path — must truncate, not append. The second
     * recording should not include the earlier `0xabcd` line. */
    T_ASSERT(input_trace_record_open(path) == 1);
    input_trace_record_frame(0, 0);
    input_trace_record_close();

    struct input_trace tr = {0};
    int rc = input_trace_load(path, &tr);
    unlink(path);
    T_ASSERT(rc == 1);
    T_ASSERT_EQ_U(tr.count, 1);
    T_ASSERT_EQ_U(tr.entries[0].mask, 0);
    input_trace_free(&tr);
    return 0;
}

int test_input_trace_record_when_closed_is_noop(void)
{
    /* Must not crash when called without an open record. */
    T_ASSERT(!input_trace_record_is_open());
    input_trace_record_frame(0, 0x10);
    input_trace_record_close();
    return 0;
}

int test_input_trace_record_open_rejects_null(void)
{
    T_ASSERT(input_trace_record_open(NULL) == 0);
    T_ASSERT(!input_trace_record_is_open());
    return 0;
}
