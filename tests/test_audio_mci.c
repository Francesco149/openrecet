/*
 * test_audio_mci.c — covers the debug MCI command-recorder.
 *
 * The recorder is gated on a debug flag in the engine and never fires in
 * shipped data, but the math has a few sharp edges (write-then-NUL,
 * the 0x50 cap, no bounds on channel+row) we want pinned. Each test
 * starts from a fresh zeroed buffer via audio_mci_clear so any byte
 * still at 0 means "the function chose not to write".
 */
#include "t.h"
#include "audio_mci.h"

static char *row_base(int row)
{
    return g_audio_mci_buffer + row * AUDIO_MCI_ROW_BYTES;
}

int test_audio_mci_buffer_size_is_4800_bytes(void)
{
    /* Engine: FUN_00451863 zeroes 0x4b0 dwords = 4800 bytes, and
     * FUN_00451ea7's render loop walks 60 rows. */
    T_ASSERT_EQ_I(AUDIO_MCI_ROW_BYTES, 0x50);
    T_ASSERT_EQ_I(AUDIO_MCI_ROWS, 60);
    T_ASSERT_EQ_I(AUDIO_MCI_BUFFER_SIZE, 4800);
    return 0;
}

int test_audio_mci_clear_zeroes_buffer(void)
{
    /* Stamp the buffer, clear it, expect every byte zero. */
    for (size_t i = 0; i < AUDIO_MCI_BUFFER_SIZE; i++) {
        g_audio_mci_buffer[i] = (char)0xa5;
    }
    audio_mci_clear();
    for (size_t i = 0; i < AUDIO_MCI_BUFFER_SIZE; i++) {
        if (g_audio_mci_buffer[i] != 0) {
            T_FAIL("byte %zu not zeroed: 0x%02x",
                   i, (unsigned)(unsigned char)g_audio_mci_buffer[i]);
        }
    }
    return 0;
}

int test_audio_mci_basic_copy_at_row_zero_col_zero(void)
{
    audio_mci_clear();
    audio_mci_record_command(0, 0, "play");
    T_ASSERT(g_audio_mci_buffer[0] == 'p');
    T_ASSERT(g_audio_mci_buffer[1] == 'l');
    T_ASSERT(g_audio_mci_buffer[2] == 'a');
    T_ASSERT(g_audio_mci_buffer[3] == 'y');
    /* NUL not written — byte 4 stays zero from clear. */
    T_ASSERT(g_audio_mci_buffer[4] == 0);
    return 0;
}

int test_audio_mci_nul_early_exit_writes_nothing(void)
{
    /* Empty string: src[0]=='\0' so we return without writing.
     * Buffer must remain entirely zeroed. */
    audio_mci_clear();
    /* Plant a sentinel right under the would-be write location, then
     * verify the function didn't touch it. */
    g_audio_mci_buffer[0] = 0x5a;
    audio_mci_record_command(0, 0, "");
    T_ASSERT(g_audio_mci_buffer[0] == 0x5a);
    return 0;
}

int test_audio_mci_row_and_column_indexing(void)
{
    /* Engine: index = channel + row * 0x50 + i. Verify the bytes land
     * at the row's offset, with `channel` as the column offset. */
    audio_mci_clear();
    audio_mci_record_command(/*channel=*/5, /*row=*/3, "ok");
    char *row3 = row_base(3);
    T_ASSERT(row3[5] == 'o');
    T_ASSERT(row3[6] == 'k');
    /* Surrounding bytes stay zero. */
    T_ASSERT(row3[4] == 0);
    T_ASSERT(row3[7] == 0);
    /* Row 0 and row 2 untouched. */
    T_ASSERT(row_base(0)[5] == 0);
    T_ASSERT(row_base(2)[5] == 0);
    return 0;
}

int test_audio_mci_stops_at_first_nul_in_source(void)
{
    /* The literal "ab\0cd" stops at the embedded NUL — `c`/`d` are not
     * copied even though they sit in the source buffer. */
    audio_mci_clear();
    const char src[] = "ab\0cd";
    audio_mci_record_command(0, 0, src);
    T_ASSERT(g_audio_mci_buffer[0] == 'a');
    T_ASSERT(g_audio_mci_buffer[1] == 'b');
    T_ASSERT(g_audio_mci_buffer[2] == 0);
    T_ASSERT(g_audio_mci_buffer[3] == 0);
    return 0;
}

int test_audio_mci_full_80_byte_fill_no_terminator(void)
{
    /* Source is 80 bytes long with no NUL inside it. The loop must
     * write exactly 80 bytes and stop at iVar2 == 0x50. The NUL in
     * src[80] is never read because the loop exited first. */
    audio_mci_clear();
    char src[AUDIO_MCI_ROW_BYTES + 16];
    memset(src, 'A', sizeof src);
    /* Sentinel a NUL well past the 0x50 boundary so we can prove the
     * function never walked there. */
    src[AUDIO_MCI_ROW_BYTES + 1] = '\0';
    /* Also vary the first byte so we can tell the loop started correctly. */
    src[0] = 'Z';
    audio_mci_record_command(0, 1, src);
    char *row1 = row_base(1);
    T_ASSERT(row1[0] == 'Z');
    for (int i = 1; i < AUDIO_MCI_ROW_BYTES; i++) {
        if (row1[i] != 'A') {
            T_FAIL("byte %d not 'A': 0x%02x", i,
                   (unsigned)(unsigned char)row1[i]);
        }
    }
    /* The byte one past the cap (start of row 2 at column 0) stays zero
     * — the loop exited at i==0x50, no overflow. */
    T_ASSERT(row_base(2)[0] == 0);
    return 0;
}

int test_audio_mci_source_longer_than_cap_truncates(void)
{
    /* 200-byte source. We write exactly 80 bytes, then stop. The 81st
     * byte of source ('B') must NOT appear in the buffer. */
    audio_mci_clear();
    char src[200];
    memset(src, 'A', AUDIO_MCI_ROW_BYTES);
    memset(src + AUDIO_MCI_ROW_BYTES, 'B', sizeof(src) - AUDIO_MCI_ROW_BYTES - 1);
    src[sizeof src - 1] = '\0';
    audio_mci_record_command(0, 0, src);
    char *row0 = row_base(0);
    for (int i = 0; i < AUDIO_MCI_ROW_BYTES; i++) {
        T_ASSERT(row0[i] == 'A');
    }
    /* No 'B' anywhere in the buffer. */
    for (size_t i = 0; i < AUDIO_MCI_BUFFER_SIZE; i++) {
        if (g_audio_mci_buffer[i] == 'B') {
            T_FAIL("'B' leaked into buffer at offset %zu", i);
        }
    }
    return 0;
}

int test_audio_mci_repeated_record_at_same_slot_overwrites(void)
{
    /* First call writes "longerword" (10 bytes: l/o/n/g/e/r/w/o/r/d).
     * Second call writes "abc" (3 bytes) at the same slot. Bytes 0..2
     * become 'a'/'b'/'c'; bytes 3..9 keep the tail of the first write
     * (no NUL terminator is written by the function — engine quirk). */
    audio_mci_clear();
    audio_mci_record_command(0, 0, "longerword");
    audio_mci_record_command(0, 0, "abc");
    T_ASSERT(g_audio_mci_buffer[0] == 'a');
    T_ASSERT(g_audio_mci_buffer[1] == 'b');
    T_ASSERT(g_audio_mci_buffer[2] == 'c');
    T_ASSERT(g_audio_mci_buffer[3] == 'g');   /* tail of "longerword" */
    T_ASSERT(g_audio_mci_buffer[9] == 'd');
    /* Byte 10 was untouched by either call. */
    T_ASSERT(g_audio_mci_buffer[10] == 0);
    return 0;
}

int test_audio_mci_channel_offset_spans_into_next_row(void)
{
    /* Engine quirk: with a large channel offset, the function indexes
     * into the flat buffer with no row clamp, so writes spill from one
     * "row" into the next. Pin the quirk with a source long enough to
     * cross the boundary.
     *
     * channel=0x37 (55), row=0, source = 40 'X' + NUL → loop writes 40
     * bytes at flat offsets 0x37..0x37+39 = 0x37..0x5e. Row 0 covers
     * 0x00..0x4f; row 1 starts at 0x50. So the first 25 bytes (offsets
     * 0x37..0x4f) live in row 0, and bytes 26..39 (offsets 0x50..0x5e)
     * live in row 1. */
    audio_mci_clear();
    char src[41];
    memset(src, 'X', 40);
    src[40] = '\0';
    audio_mci_record_command(/*channel=*/0x37, /*row=*/0, src);

    /* Boundary bytes both ends of the row 0/row 1 split. */
    T_ASSERT(g_audio_mci_buffer[0x4f] == 'X');  /* last byte of row 0 */
    T_ASSERT(g_audio_mci_buffer[0x50] == 'X');  /* first byte of row 1 */
    /* Last byte written: offset 0x37+39 = 0x5e. */
    T_ASSERT(g_audio_mci_buffer[0x5e] == 'X');
    /* Loop stopped at NUL — offset 0x5f stays zero. */
    T_ASSERT(g_audio_mci_buffer[0x5f] == 0);
    /* Bytes before the write start are untouched. */
    T_ASSERT(g_audio_mci_buffer[0x36] == 0);
    return 0;
}
