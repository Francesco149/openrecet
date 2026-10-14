/*
 * party.c — see party.h.
 */
#include "party.h"

#include <stddef.h>

_Static_assert(offsetof(party_t, chr) == 0x014 && offsetof(party_t, pos) == 0x020 &&
               offsetof(party_t, pos_hist) == 0x044 && offsetof(party_t, rec_hist) == 0x224 &&
               offsetof(party_t, vel) == 0x904 && offsetof(party_t, rec) == 0x930 &&
               offsetof(party_t, trail) == 0x9b4 && offsetof(party_t, burst) == 0xb08 &&
               offsetof(party_t, scale_xz) == 0xc60 && offsetof(party_t, scale_y) == 0xc6c &&
               offsetof(party_t, hit) == 0xd04 && offsetof(party_t, ground_y) == 0xdd0 &&
               offsetof(party_t, ground_y2) == 0xddc && offsetof(party_t, recs) == 0xf64,
               "party block field offsets (DAT_056da1b8 + off)");

party_t g_party;
