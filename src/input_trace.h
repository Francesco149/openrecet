/*
 * input_trace.h — deterministic input record / replay for the Phase A
 * regression harness.
 *
 * Sparse JSONL trace format. One line per CHANGE in the 14-bit button
 * mask `g_input_state[0].buttons`:
 *
 *   {"frame":0,  "buttons":"0x0000"}
 *   {"frame":30, "buttons":"0x0010"}
 *   {"frame":31, "buttons":"0x0000"}
 *
 * Between change-points the mask holds. Frame indices are 0-based and
 * monotonically increasing across the file. Replay mode binds directly
 * to the engine's per-frame input slot (bypassing DirectInput), so the
 * replayed mask is byte-for-byte what the engine sees on a real
 * keypress.
 *
 * Two roles share this module:
 *
 *   - **Record** mode: `--input-trace-record <file>` opens the file in
 *     write mode; main.c calls `input_trace_record_frame(frame, mask)`
 *     once per ticked frame *after* `input_poll()` populates the
 *     button mask. Lines are only emitted when the mask differs from
 *     the previous frame's mask.
 *
 *   - **Replay** mode: `--input-trace-replay <file>` parses the file
 *     into an in-memory table at startup; main.c's replay loop calls
 *     `input_trace_replay_lookup(frame)` once per frame *before* sim_a
 *     to get the mask to write into `g_input_state[0].buttons`.
 *
 * Pure C — testable without Win32 / DirectInput. Replay parser is the
 * "important" surface (tests load goldens that drive the harness);
 * record path is a thin printf wrapper.
 */
#ifndef OPENRECET_INPUT_TRACE_H
#define OPENRECET_INPUT_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ─── Record ─────────────────────────────────────────────────────────── */

/* Open the trace file for writing. Returns 1 on success, 0 if path is
 * NULL or fopen fails. Idempotent: closes any previously-open record
 * before reopening. */
int  input_trace_record_open(const char *path);

/* Emit a line if `mask` differs from the previously-recorded mask (or
 * if this is the first call after open). The first call always emits
 * a `frame:0`-anchored line so replays start from a known mask. */
void input_trace_record_frame(uint32_t frame, uint16_t mask);

/* Flush + close. Safe to call when no record is open. */
void input_trace_record_close(void);

/* Test hook. */
int  input_trace_record_is_open(void);

/* ─── Replay table ───────────────────────────────────────────────────── */

struct input_trace_entry {
    uint32_t frame;     /* sim-frame index at which this mask takes effect */
    uint16_t mask;      /* 14-bit button mask, OR of input_binding_mask[] bits */
};

#define INPUT_TRACE_MAX_ENTRIES 4096

struct input_trace {
    struct input_trace_entry entries[INPUT_TRACE_MAX_ENTRIES];
    size_t                   count;
};

/* Parse a sparse JSONL trace from `path`. Returns 1 on success and
 * fills `out`. Returns 0 if the file can't be opened or any line fails
 * to parse — out->count is the number of entries successfully parsed
 * before the failure.
 *
 * Tolerated: blank lines, leading whitespace, `# …` comment lines,
 * trailing whitespace. Numeric values may be decimal or `0x`-prefixed
 * hex; "buttons" may be a JSON string ("0x0010") or a JSON number
 * (16). Entries are required to be in strictly increasing frame
 * order; out-of-order frames fail the parse. */
int input_trace_load(const char *path, struct input_trace *out);

/* Same, but parsing from an in-memory buffer — convenient for unit
 * tests that don't want to spill fixtures to /tmp. `len` is the byte
 * length of `buf`; a NUL terminator inside the buffer is not required
 * but tolerated. */
int input_trace_parse_buf(const char *buf, size_t len,
                          struct input_trace *out);

/* Return the mask in effect at `frame`. Sparse semantics: the result
 * is the mask from the most recent entry with `entries[i].frame <=
 * frame`. If no entry has `frame <= 0` (typically `frame:0` is the
 * first line), returns 0. */
uint16_t input_trace_lookup(const struct input_trace *trace, uint32_t frame);

#endif /* OPENRECET_INPUT_TRACE_H */
