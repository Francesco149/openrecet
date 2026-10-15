/*
 * scene_town_event.h — engine modes 13..16, the town-visit scenes (world-map
 * dests 5 / 2 / 4): play the place's event script, fade, back to the world map.
 *
 * Engine sources ported here (one quartet per mode; DAT_06a5d400 / 404 shared):
 *   mode 13 教会 (dest 5): FUN_0045e3cd init, FUN_0045e3dc update, FUN_0045e48b
 *                         render, FUN_00473972 load (bmp/ivent/bg_kyoukai.bmp);
 *   mode 14 広場 (dest 2): FUN_0045e019, FUN_0045e053, FUN_0045e11c, FUN_00473991
 *                         (bg_hiro1/2/3.bmp);
 *   mode 15 酒場 (dest 4): FUN_0045e196, FUN_0045e1a5, FUN_0045e254, FUN_004739fb
 *                         (bg_sakaba.bmp);
 *   mode 16 (nothing enters it in retail): FUN_0045e2ce, FUN_0045e2dd (Z →
 *                         leave), FUN_0045e353, FUN_004739dc (bg_hiro1.bmp).
 *
 * The update runs from FUN_004536cb LABEL_106 only while no dialogue runs
 * (DAT_0438b1c8 == 0). Tick 0: FUN_0045de68(loc, …, 1) picks a pending event
 * (iv{grp}_{idx}), else the place's idle script iv16_{n} (the plaza's n by
 * period: 1 / 2 / 3). Tick 1 (the script over): the fade-out; once done the
 * period flag (work +0x2ce08) = 1, mode 8, FUN_00473969, the fade-in, the load.
 * No rng.
 */
#ifndef OPENRECET_SCENE_TOWN_EVENT_H
#define OPENRECET_SCENE_TOWN_EVENT_H

#include <stdint.h>

struct IDirect3DDevice8;

/* FUN_0045e019 / 196 / 2ce / 3cd: DAT_06a5d400 = DAT_06a5d404 = 0. */
void scene_town_event_reset(void);

/* The update for mode 13..16 (FUN_0045e3dc / 053 / 1a5 / 2dd). */
void scene_town_event_sim(int mode);

/* The render for mode 13..16 (FUN_0045e48b / 11c / 254 / 353). */
void scene_town_event_render(struct IDirect3DDevice8 *dev);

/* Load-worker cases 13..16 (FUN_00473972 / 991 / 9fb / 9dc). */
void scene_town_event_init(struct IDirect3DDevice8 *dev);

/* --state-va mirrors: DAT_06a5d400 / DAT_06a5d404. */
int scene_town_event_dbg(uint32_t va, int32_t *out);

#endif
