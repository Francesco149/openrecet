/*
 * scene1_day_close.h — the shop day close: the night sequence after the 4th
 * period of the day (DAT_0450fb88 shop-time reaching 4), and the house
 * re-layout it starts from.
 *
 *   arm sites   → shop-time = 4, DAT_0438b4e0 = 1, FUN_0048526d (re-layout:
 *                 FUN_00436f97 in-scene + actors at the door, Recette facing
 *                 Tear in the tired pose 6, cc08 = 1, db048 = 15).
 *                 Sites: the cs leave (FUN_00462403), the free-roam
 *                 period advance (FUN_0048670f, return from town), the
 *                 dungeon exit (FUN_00442cef — dungeons unported).
 *   b4e0 == 1   → FUN_0048670f re-runs FUN_0048526d once more.
 *   b4e0 2..160 → the closing exchange: variant DAT_074b2eec (cycles 0→1→2
 *                 per close) picks Recette's line at 10 and Tear's reply at
 *                 90 (variant 2: 80), with voices; the player is frozen
 *                 (FUN_0048670f takes the LABEL_132 exit every frame).
 *   b4e0 == 160 → FUN_004536b9 (= FUN_00453384(1): open the day-end
 *                 summary), b4e0 = 0, cc08 = 1.
 *
 * RE: docs/findings/day-close-RE.md.
 */
#ifndef OPENRECET_SCENE1_DAY_CLOSE_H
#define OPENRECET_SCENE1_DAY_CLOSE_H

#include <stdint.h>

/* DAT_0438b4e0 — 0 idle, 1 armed, 2..159 the exchange.  Also gates the
 * window-NPC spawns + renders (FUN_0046f2a3/f648/f737) and ESC pause. */
int  day_close_b4e0(void);
int  day_close_variant(void);          /* DAT_074b2eec */

/* The shared arm tail: shop-time = 4 is the caller's; b4e0 = 1 then
 * FUN_0048526d. */
void day_close_begin(void);
/* DAT_0438b4e0 = 1 alone (the next master tick re-lays the room). */
void day_close_arm(void);

/* FUN_0048526d — in-scene house re-layout (FUN_00436f97 + FUN_004851e2(1)
 * + the tired pose facing Tear + cc08 = 1, db048 = 15, b924 = 300). */
void scene1_house_relayout(void);

/* FUN_0048670f @0x486d04: `if (b4e0 == 1) FUN_0048526d()`. */
void day_close_relayout_tick(void);

/* FUN_0048670f @0x486e27 (after b924++): the exchange.  Returns 1 when
 * b4e0 > 0 — the caller then takes LABEL_132 (player anim + companion +
 * the LABEL_675 tail) and skips the cc08 dispatch. */
int  day_close_machine_tick(void);

/* FUN_0048670f @0x486c64 — the free-roam period advance: when the work
 * record's "a period passed" flag (dword +0x2ce08, DAT_045105a0; set by the
 * world-map trip) is up in free-roam (cc08 == 1, cc04 == 0): clear it,
 * shop-time++, 1-in-5 news (day >= 9; the rand is drawn every time),
 * FUN_0045e028, and the night arm at shop-time 4. */
void shop_period_return_tick(int cc08, int cc04);

/* FUN_0045e028 — clear the 4 per-period event-checked bytes (work record
 * +0x2bca8..ab) after a period passes. */
void shop_period_events_clear(uint8_t *bank);

/* The summary's next-day tail clears DAT_0438b4e0 (FUN_0048dbfb). */
void day_close_clear(void);

/* FUN_004852fb — the morning re-seat after the summary: actors at the
 * counter spot, Recette in pose 6 facing Tear, db048 = 0, the actor-say
 * bubble and emote off; arms the day-8 collector visit / day-15 event. */
void scene1_house_morning_reseat(void);

void day_close_reset(void);            /* tests / new process */

#endif /* OPENRECET_SCENE1_DAY_CLOSE_H */
