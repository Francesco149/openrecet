/*
 * party.h — the party block in retail layout: the three party actors (the
 * player = actor 0, the partner = actor 1, Tear = actor 2) and the player
 * controller's state, DAT_056da1b8 .. DAT_056dd7dc.
 *
 * Retail code addresses it both as globals and through a base pointer
 * (FUN_004897c6 / FUN_0044376a / FUN_004147d5 take &DAT_056da1b8), so the port
 * keeps the whole block as one union: named arrays for the structured parts,
 * and PW(va) for any dword by its retail address — `PW(0x56db048).i` is
 * DAT_056db048 (int), `PW(0x56db05c).f` is _DAT_056db05c (float).  rhost
 * shadows compare it byte for byte.
 *
 * Older modules kept private copies of these fields (player_ctrl's actor
 * records, the companion's db054, stage_post_load's char ids / HP); they now
 * alias this storage.
 */
#ifndef OPENRECET_PARTY_H
#define OPENRECET_PARTY_H

#include <stdint.h>

#include "map_collision.h"   /* mapcol_hit_t (FUN_00432e50 output) */

#ifdef __cplusplus
extern "C" {
#endif

#define PARTY_VA     0x56da1b8u
#define PARTY_SIZE   0x3624u          /* .. DAT_056dd7dc */
#define PARTY_RECS   10               /* DAT_056db11c: 10 × 248 dwords */

typedef union { int32_t i; float f; } pword_t;

typedef union party_u {
    pword_t w[PARTY_SIZE / 4];
    struct {
        pword_t      _h[5];           /* +0x000 DAT_056da1b8 .. 1c8 */
        int32_t      chr[3];          /* +0x014 DAT_056da1cc: char id per actor, -1 = none */
        float        pos[3][3];       /* +0x020 DAT_056da1d8 */
        float        pos_hist[40][3]; /* +0x044 DAT_056da1fc: actor 0's position, newest first */
        int32_t      rec_hist[40][11];/* +0x224 DAT_056da3dc: actor 0's record, newest first */
        float        vel[3][3];       /* +0x904 DAT_056daabc */
        pword_t      _aae0[2];        /* +0x928 */
        int32_t      rec[3][11];      /* +0x930 DAT_056daae8: the sprite-state records */
        int32_t      trail[5][17];    /* +0x9b4 DAT_056dab6c: after-image bank 0 */
        int32_t      burst[5][17];    /* +0xb08 DAT_056dacc0: after-image bank 1 */
        pword_t      _ae14;           /* +0xc5c */
        float        scale_xz[3];     /* +0xc60 DAT_056dae18 */
        float        scale_y[3];      /* +0xc6c DAT_056dae24 */
        pword_t      _ae30[(0xd04 - 0xc78) / 4];
        mapcol_hit_t hit[3];          /* +0xd04 DAT_056daebc: each actor's ground query */
        float        ground_y[3];     /* +0xdd0 DAT_056daf88 */
        float        ground_y2[3];    /* +0xddc DAT_056daf94 */
        pword_t      _afa0[(0xf64 - 0xde8) / 4];
        int32_t      recs[PARTY_RECS][248]; /* +0xf64 DAT_056db11c */
    };
} party_t;

_Static_assert(sizeof(party_t) == PARTY_SIZE, "DAT_056da1b8 .. DAT_056dd7dc");

extern party_t g_party;

/* any dword of the block by its retail VA */
#define PW(va) (g_party.w[((uint32_t)(va) - PARTY_VA) >> 2])

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_PARTY_H */
