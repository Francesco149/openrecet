/*
 * customer_service.h — the in-shop CUSTOMER-SERVICE / selling mode (cc08==4).
 *
 * The engine's HOUSE interaction mode DAT_0438cc08==4 ("customer service"): a
 * customer walks up to the sell counter, the player picks an item + names a
 * price, and the customer haggles back (the BARGAIN!! panel).  The pure haggle
 * MATH is in customer_haggle.{c,h}; this module is the engine-facing driver that
 * binds it to the live DAT_0730bXXX / DAT_005c6bXX state + the kyaku record.
 *
 * Ported engine functions (the cc08==4 subsystem):
 *   - FUN_0045edaa (0x45edaa) — session init / customer-roster build.  Only the
 *     TUTORIAL forced-sale path (f406 → forced kyaku 13 "Woman") is ported here;
 *     the full eligible-roster scan is PORT-DEBT (the tutorial skips it).
 *   - FUN_00462403 (0x462403) — the per-frame master tick (arrival/leave anim,
 *     speech-bubble position, patience, the b534 state switch + b5a8 dispatch).
 *   - FUN_00463cfb (0x463cfb) — the SELL state machine (greeting → item-select →
 *     price-setup → offer → decision → accept/leave), wiring the haggle math.
 *
 * State model: the engine keeps this in a block of BSS globals (DAT_0730aca0 ..
 * DAT_0730b6xx + the DAT_005c6bXX price scalars).  The port mirrors the meaningful
 * fields as module statics (house style); the array regions (the customer queue,
 * the eligible list, the per-customer arrival timers) are arrays.  NB the engine
 * REUSES the item-pick [0].col/.row dwords (b278/b27c) as the 2 per-customer
 * arrival timers — temporally separate phases, modelled as the same storage.
 *
 * The customer index DAT_0730b56c is the SAME engine global the buy phase uses as
 * its page selector (port `g_scene_buy_current_page`, scene_buy.h) — reused here.
 *
 * Full RE + per-state map: docs/findings/customer-service-haggle-RE.md.
 */

#ifndef OPENRECET_CUSTOMER_SERVICE_H
#define OPENRECET_CUSTOMER_SERVICE_H

#include <stdint.h>

/* ── session init — FUN_0045edaa ───────────────────────────────────────────
 * Zeroes the per-session DAT_0730bXXX state, counts the items on display + draws
 * the customer-count RNG (1 LCG draw, BEFORE the path branch — load-bearing for
 * RNG parity), then builds the customer queue.  TUTORIAL path (DAT_0450f406[slot]
 * != 0): forces a single customer = kyaku 13 ("Woman"), item slot 0, kind 0.
 * Called the frame cc08 flips 1→…→4 (the auto-arrival site FUN_0048670f:86896). */
void customer_service_session_init(void);

/* ── per-frame master tick — FUN_00462403 ──────────────────────────────────
 * Run every frame while cc08==4 (dispatched from the player-controller's
 * non-free-roam arm).  Owns the arrival/leave anim, the speech-bubble screen
 * position (DAT_0438cc38/3c/40), the patience timers, and the b534 state switch;
 * dispatches the transaction states to the SELL machine (b5a8==4). */
void customer_service_master_tick(void);

/* Read the active customer-service SELL sub-state (DAT_0730b534) — for the
 * render dispatch + the flow-trace state probe. */
int32_t customer_service_b534(void);

/* Read the player's current asking price (DAT_005c6bb8) and the customer's
 * current offer (DAT_0730b574) — read by the BARGAIN!! panel render. */
int32_t customer_service_player_ask(void);
int32_t customer_service_offer(void);
int32_t customer_service_base_price(void);

/* ── test / debug hooks ────────────────────────────────────────────────────
 * Reset the whole state block (BSS-equivalent) — host tests call this between
 * cases so a prior session's leftovers don't bleed in. */
void customer_service_reset(void);

/* Inspect the built customer queue (entry 0's kyaku index / item slot / kind);
 * returns -1 for an out-of-range entry.  Used by the entry host test. */
int32_t customer_service_queue_kyaku(int entry);
int32_t customer_service_queue_item_slot(int entry);
int32_t customer_service_queue_kind(int entry);
int32_t customer_service_queue_count(void);

/* The eligible-list head (DAT_06a5d450[0]) — tutorial = 13.  Test hook. */
int32_t customer_service_eligible(int i);

#endif /* OPENRECET_CUSTOMER_SERVICE_H */
