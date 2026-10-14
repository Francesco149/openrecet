/*
 * event_probe.h — FUN_0045de68 (0x45de68, 433 B): "does a town event fire here
 * now?" over the event.txt table (tables_event.c, DAT_06a49b80).
 *
 * `loc` is the caller's location code: 2 → 広場 (cat 0), 1 → 市場 (cat 1),
 * 5 → 教会 (cat 2), 4 → 酒場 (cat 3); anything else (3 = the Merchant's Guild)
 * has no events → 0.  Callers: the world-map init FUN_0049de20 (dest 1..5,
 * commit 0), the market tick FUN_004922c0 (1 / 3, commit 1), the town scenes
 * FUN_0045e053 / 1a5 / 3dc.
 *
 * A record fires when (in table order, at most 100 records):
 *   - the save is not game mode 2 and the location's "event today" byte
 *     (work +0x2bca8 + cat) is clear;
 *   - LOOP (work dword 0xb0f9) >= loop_min and gold >= decay_or_max;
 *   - its own flag (work +0x2bc48 + prereq[0]) is clear and prereq[1..3] are
 *     each -1 or set;
 *   - a day pair holds start-1 <= day <= end-1 (end 999 = open) and
 *     time_first <= period <= time_max.
 * Then *grp = id, *idx = flag_on_trigger (the iv{grp}_{idx} script), and with
 * `commit` the location byte + the record's flag are set and today's score
 * (work dword 0xb0f8) += 100.  No rng.
 */
#ifndef OPENRECET_EVENT_PROBE_H
#define OPENRECET_EVENT_PROBE_H

#include <stdint.h>

int event_probe(uint32_t *bank, int loc, int *grp, int *idx, int commit);

#endif
