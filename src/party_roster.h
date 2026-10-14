/*
 * party_roster.h — the recruited-adventurer list (engine FUN_0047f1ce).
 *
 * FUN_0047f1ce @ 0x47f1ce (296 B) rebuilds DAT_0741bed8 (count) and
 * DAT_07477e74[] (adventurer ids 0..7, in id order) from the active work
 * bank: adventurer k is listed when byte 0x2bc4c+k OR byte 0x2bd18+k is set.
 * DAT_07477e70 (the dword before the list, never written → 0) is read as the
 * list's 1-based view: the adventurers' guild row r (1..count) is
 * DAT_07477e70[r], row 0 its "Leave" row.
 *
 * Callers in retail: FUN_0045bdc2 (adventurers' guild load), FUN_0047f2f6
 * (pause-menu build: "Status" is listed iff count > 0), FUN_00468338 (item
 * window open), FUN_0045edaa, FUN_0048670f.
 */
#ifndef OPENRECET_PARTY_ROSTER_H
#define OPENRECET_PARTY_ROSTER_H

#define PARTY_ROSTER_MAX 8

/* FUN_0047f1ce — rebuild from the active work bank. */
void party_roster_rebuild(void);

/* DAT_0741bed8 — recruited-adventurer count. */
int party_roster_count(void);

/* DAT_07477e70[row]: row 0 → 0 (the never-written DAT_07477e70), row 1..count
 * → DAT_07477e74[row-1]. */
int party_roster_row_id(int row);

void party_roster_reset(void);

#endif /* OPENRECET_PARTY_ROSTER_H */
