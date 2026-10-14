/*
 * state_va.h — `--state-va VA,..` + `--state-log FILE`: per-frame PORT values of
 * retail globals, logged right after the anchor evaluation as
 *   post N <va>=<int> ...
 * — the same line rhost's `--watch` prints after retail's tick N (tools/rhost),
 * so `rhost.py portstate` diffs the two frame by frame.  Each VA maps to the
 * port's mirror of that engine global; an unmapped VA is skipped (warned).
 */
#ifndef OPENRECET_STATE_VA_H
#define OPENRECET_STATE_VA_H

#include <stdint.h>
#include <stdio.h>

/* Parse a comma-separated hex VA list into the active set.  Returns the count of
 * mirrored VAs; one without a port mirror is skipped and reported in *bad (the
 * last such VA; *bad untouched when all are mirrored). */
int  state_va_select(const char *list, uint32_t *bad);
/* Write one `post <frame> va=val ...` line for the active set (no-op if empty). */
void state_va_log(FILE *fp, uint32_t frame);
/* The mapped VAs, for usage text: "6023a0 rng, 438b1c0 scene, ..." */
const char *state_va_known(void);

#endif /* OPENRECET_STATE_VA_H */
