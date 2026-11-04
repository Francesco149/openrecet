/* rng_golden.h — `--rng-golden FILE`: stop the port at its first RNG split from retail.
 *
 * Tooling only (docs/plans/free-play-capture.md FPC-3); no engine counterpart.
 * The golden is retail's per-tick LCG consumption from an rhost `--log-rand` run of
 * the same flat trace, written by tools/rhost/rhost.py:
 *
 *     # comment lines
 *     frames N                 retail ran ticks 1..N (frame 0 = boot)
 *     F COUNT END              tick F drew COUNT times, the LCG state after its
 *                              last draw was END (hex); ticks absent = 0 draws
 *
 * Ticks are labelled like `--rng-log` / rhost `--log-rand` (tick_label(); a deferred
 * loader worker's draws at the boundary before tick F count in F). After tick F the
 * main loop calls rng_golden_end_tick(F): every tick (from 1: boot's frame 0 differs by
 * construction, both sides enter tick 1 at seed 19937) not yet checked up to F is
 * compared (count, and END when COUNT > 0). The first differing tick is the split;
 * the draws of the tick being accumulated are kept (with callers) so the split
 * file carries the port side of the divergence without a second run. */
#ifndef OPENRECET_RNG_GOLDEN_H
#define OPENRECET_RNG_GOLDEN_H

#include <stdint.h>
#include <stdio.h>

/* 0 ok, -1 unreadable / malformed. Replaces any golden loaded before. */
int rng_golden_load(const char *path);
/* the same, from an open stream (host tests) */
int rng_golden_load_fp(FILE *fp);
int rng_golden_active(void);
uint32_t rng_golden_frames(void);

/* every LCG step (is_seed=0) / reseed (is_seed=1) with its tick label and the
 * caller's image-relative address */
void rng_golden_note(int is_seed, uint32_t frame, uint32_t before, uint32_t after, uint32_t caller_rva);

/* tick `frame` has run: check every unchecked tick <= frame (beyond the golden's
 * frames nothing is checked). Returns 1 once a split is found (also on later
 * calls), 0 while identical. */
int rng_golden_end_tick(uint32_t frame);
/* 1 + *frame = the split tick, 0 = none (yet) */
int rng_golden_split(uint32_t *frame);

/* the split report:  split F retail COUNT END port COUNT END
 *                    then the port's draws of tick F:  rand F BEFORE -> AFTER @RVA
 * 0 ok. Writes "identical CHECKED" when no split was found. */
int rng_golden_write_report(FILE *fp);

void rng_golden_reset(void);

#endif
