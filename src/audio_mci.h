/*
 * audio_mci.h — debug-only MCI/audio command recorder.
 *
 * Faithful port of FUN_00451874 (47 bytes). The engine maintains a
 * 60×80 char buffer at &DAT_06a47aac (60 rows, 0x50 bytes per row;
 * total size 4800 bytes, confirmed via FUN_00451863's dword-zero loop
 * `for iVar1 = 0x4b0 ...` and FUN_00451ea7's `&DAT_06a48d6c -
 * &DAT_06a47aac == 0x12c0` row-iteration bound). The on-screen
 * debug overlay (FUN_00451ea7) renders the buffer as a grid of
 * 8×8 glyphs at 10-pixel column pitch — i.e. the rows are *text*
 * rows on a hidden debug display.
 *
 * The recorder is gated on `DAT_0438ccb4 != 0` (a debug flag that is
 * zero in normal play). All 77 call sites across the engine are
 * therefore dormant in shipped builds — but we still port the
 * function for completeness, future debug-overlay work, and so the
 * later SE/fade ports can mirror their original logging calls without
 * leaving phantom `// TODO: hook MCI record` comments.
 *
 * Naming: the task brief uses `channel`/`row_index`; in the engine's
 * indexing math (`base[channel + row_index*0x50 + i]`) `channel` is a
 * column offset within an 80-byte row and `row_index` is the row
 * number. Don't read too much into "channel" — at the call sites it's
 * always a small constant (0, 5, 7, 10, 0x14, 0x19, 0x1e, 0x26,
 * 0x28, 0x2a, 0x2c, 0x2d, 0x32, 0x37) that picks a sub-column within
 * the row.
 *
 * Engine source: docs/decompiled/by-address/451874.c
 */

#ifndef OPENRECET_AUDIO_MCI_H
#define OPENRECET_AUDIO_MCI_H

#include <stddef.h>

#define AUDIO_MCI_ROW_BYTES   0x50   /* 80 chars per row (engine constant). */
#define AUDIO_MCI_ROWS        60     /* 4800 / 80 — see FUN_00451863. */
#define AUDIO_MCI_BUFFER_SIZE (AUDIO_MCI_ROW_BYTES * AUDIO_MCI_ROWS)

/* The recorder's backing store. Mirrors &DAT_06a47aac. Zero-init at
 * BSS matches the engine's .bss layout; FUN_00451863 also wipes it
 * to zero on demand (audio_mci_clear). */
extern char g_audio_mci_buffer[AUDIO_MCI_BUFFER_SIZE];

/* Mirrors FUN_00451874. Copies up to AUDIO_MCI_ROW_BYTES bytes from
 * `cmd` into the buffer starting at index
 *     (channel + row_index * AUDIO_MCI_ROW_BYTES)
 * stopping early on the first NUL byte in `cmd`. The NUL itself is
 * NOT written. If `cmd` is 80+ bytes with no NUL, exactly 80 bytes
 * are copied and the loop exits.
 *
 * No bounds check on (channel, row_index): the engine has none either,
 * and at param_1=0x37 + row_index=59 with an 80-byte source the
 * write spills 0x37 bytes past the buffer's end. The recorder is
 * only ever called with DAT_0438ccb4 set in debug builds, so the
 * spill is dormant in practice. See engine-quirks.md.
 */
void audio_mci_record_command(int channel, int row_index, const char *cmd);

/* Mirrors FUN_00451863 — zeroes the whole 4800-byte buffer. The
 * engine calls this from inside the debug-overlay render path
 * (FUN_00451ea7, the DAT_06a4993c==1 branch) at the top of every
 * overlay frame. Useful for tests; reset state between cases. */
void audio_mci_clear(void);

#endif /* OPENRECET_AUDIO_MCI_H */
