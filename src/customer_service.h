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

/* Golden-replay harness (roster_golden_replay.c): set 1 to make
 * customer_service_session_init skip the rng-neutral scene/worker tails so
 * the roster scan runs headless at boot on a captured arena.  Never set in
 * the real game. */
void customer_service_set_roster_replay(int v);

/* ── per-frame master tick — FUN_00462403 ──────────────────────────────────
 * Run every frame while cc08==4 (dispatched from the player-controller's
 * non-free-roam arm).  Owns the arrival/leave anim, the speech-bubble screen
 * position, the patience timers, and the b534 state switch; for the scripted
 * tutorial sell (b51c==1) the b534==1 arm dispatches the scripted machine
 * (FUN_00461c00) every frame.  `cur`/`pressed`/`held` = the engine button masks
 * DAT_073dddd0 (raw) / DAT_073dddd4 (edge) / DAT_073dddd6 (held-with-repeat). */
void customer_service_master_tick(uint32_t cur, uint32_t pressed, uint32_t held);

/* ── FUN_0045e6a5 — the cc08==4 ESC "Cancelling tutorial?" skip gate ──────────
 * Called from the in-game ESC dispatch (esc_dispatch.c) when cc08==4.  During the
 * scripted haggle tutorial (b51c==1, not leaving/armed) it opens the "Cancelling
 * tutorial. Are you sure?" Yes/No choice box and arms b5e4 (the master tick polls
 * it: Yes → leave to free-roam, No → resume).  Returns 1 if it consumed the ESC
 * (armed the prompt), 0 to fall through to the in-game pause menu. */
int customer_service_esc_skip_arm(void);
int32_t customer_service_b5e4(void);   /* the skip-armed flag (flow-trace probe) */
int32_t customer_service_skip_modal_active(void); /* the cc08 ESC-skip b150 hold (→ PAUSE_OPEN/CLOSE) */

/* Load-worker completion (DAT_0438b1cc → 0) — the asset-load worker's callback;
 * the master tick is inert until it fires.  Host tests call it after session_init
 * to release the load gate. */
void customer_service_notify_loaded(void);

/* Trace-harness `{csloadpin:N}` — pin the cc08==4 d3e load bracket to N frames
 * (extend-only normalization, like {tutloadpin}); N <= 0 clears.  _elapsed
 * advances the bracket counter + reports readiness (always 1 when unset). */
void customer_service_set_load_pin(int n);
int  customer_service_load_pin_elapsed(void);
/* Whether a {csloadpin} is currently in effect (s_csload_pin > 0) — gates the
 * deterministic worker force-complete in the cc08==4 load-release bridge. */
int  customer_service_load_pin_active(void);

/* Read the active customer-service SELL sub-state (DAT_0730b534) — for the
 * render dispatch + the flow-trace state probe. */
int32_t customer_service_b534(void);

/* Read the player's current asking price (DAT_005c6bb8) and the customer's
 * current offer (DAT_0730b574) — read by the BARGAIN!! panel render. */
int32_t customer_service_player_ask(void);
int32_t customer_service_offer(void);
int32_t customer_service_variant(void);  /* DAT_0730b5e0 — last cs_pick_line variant (trace probe) */
int32_t customer_service_base_price(void);
int32_t customer_service_budget_level_day(int cand_idx);

/* Flow-trace / render state: the transaction-type selector (DAT_0730b5a8),
 * active customer record index (DAT_0730b56c), arrival-anim counter
 * (DAT_0730b5a0), and haggle round (DAT_0730b584). */
int32_t customer_service_b5a8(void);
int32_t customer_service_b56c(void);
/* DAT_0450f404[slot] — sell-active (1 = player-initiated counter sell; 0 = the
 * autonomous first customer).  Gates the companion's at-counter arm vs the
 * free-roam follow in scene1_companion_ctrl_tick (FUN_0048a833 local_c). */
int32_t customer_service_f404(void);
int32_t customer_service_arrival_anim(void);
int32_t customer_service_round(void);

/* Remaining once-per-frame flow-trace fields the retail 0x48670f probe
 * declares (tools/flow/retail_fields.json): leave/dissolve phase (DAT_0730b520),
 * idle frame counter (DAT_0730b524), per-state sub-frame timer (DAT_0730b544),
 * and patience (DAT_0730b590). */
int32_t customer_service_b520(void);
int32_t customer_service_b524(void);
int32_t customer_service_b544(void);
int32_t customer_service_b590(void);

/* Load-phase gate (DAT_0438b1cc): 2 = the cc08==4 asset-load worker (d3e) is
 * running (master tick inert, render off); 1 = loaded (master tick + render
 * active).  Read by the render gate (FUN_0046602e: `b1cc==1`) and the engine
 * wiring's load-release check. */
int32_t customer_service_b1cc(void);

/* The cc08==4 d3e asset-load overlay gate (engine gate2 DAT_06a49960): 1 while
 * the d3e customer-asset load is pending (b1cc==2).  Folded into
 * anchor_world.loading_active so LOADING_END/HOUSE_FREEROAM span the load like
 * retail (RE §21.9).  Anchor/capture only — does NOT gate the sim. */
int customer_service_d3e_loading(void);
/* frame-start snapshot of the d3e-load gate (set by the cc08==4 arm before
 * notify_loaded; read by a later-in-frame consumer like the companion ctrl). */
void customer_service_note_frame_load(int loading);
int  customer_service_load_at_frame_start(void);

/* Customer-service-active flag (DAT_0438b7b0) — set on session init, read by
 * both render functions (FUN_0046602e/FUN_00466b7b gate on `b7b0 != 0`). */
int32_t customer_service_active(void);

/* Scripted-machine state probes (RE §9): b51c (DAT_0730b51c, the scripted gate),
 * b608 (DAT_0730b608, the script sub-state; ==4 = the price-confirm choice),
 * fileidx (DAT_005c6bb0).  customer_service_bargain_active() == (b51c!=0 &&
 * b608==4) — the BARGAIN choice is open; the anchor ORs it into pause_active so
 * PAUSE_OPEN fires at the haggle like retail's DAT_0438b150 (RE §9.6). */
int32_t customer_service_b51c(void);
int32_t customer_service_b608(void);
int32_t customer_service_b604(void);
int32_t customer_service_fileidx(void);
int32_t customer_service_bargain_active(void);

/* The active dialogue/script-file index (DAT_005c6bb0) — set at the cc08==4
 * entry sites by FUN_00461bf6(idx) BEFORE session_init.  Selects the tuto
 * script block g_tuto[idx*200+pc] the scripted machine walks. */
void    customer_service_set_script_file(int32_t idx);

/* ── cc08==4 SCENE RENDER (FUN_0046602e + FUN_00466b7b) ─────────────────────
 * The retail customer-service stage is drawn by two 2D-overlay functions the
 * port wires into its HUD render path (customer_service_render.c):
 *   - FUN_0046602e (the 2D character art + letterbox bars + offer panel),
 *     called at the TOP of the merchant HUD aggregator FUN_00409925.
 *   - FUN_00466b7b (the haggle dialogue box + typewriter line + BARGAIN!!
 *     price layout), called from the 2D-UI overlay render FUN_0040a765.
 * Both read a once-per-frame snapshot of the DAT_0730bXXX / DAT_005c6bXX state
 * (the engine's render reads those globals directly).  Full spec:
 * docs/findings/customer-service-haggle-RE.md §8.6. */
struct cs_render_state {
    int32_t b1cc, cs_active;        /* DAT_0438b1cc / DAT_0438b7b0 — gates */
    int32_t b52c, b530, b53c;       /* sprite-slide / letterbox / flash */
    int32_t b540, b548, b55c, b558; /* Yes-No / reveal budget / line-done */
    int32_t b54c, b550, b56c;       /* per-speaker sprite-slot / record idx */
    int32_t b560, b564;             /* price-digit cursor / showcase enable */
    int32_t b58c, b590, b598, b59c; /* button delay / patience / BARGAIN anim */
    int32_t b5a0, b5a4, b5a8;       /* arrival anim / offered handle / mode */
    int32_t b5b4, b5bc, b5c0, b5c8; /* blink / arrival banner / list scroll */
    int32_t b5d0, b5d4, b5d8, b5dc; /* pose state / timer / want-idx / rows */
    int32_t b51c;                   /* scripted-sell flag */
    int32_t cust_name_index;        /* g_kyaku.records[b56c].name_index — slot-1 plate cell */
    int32_t cust_active[2];         /* DAT_06a5ea70/74 — on-screen speakers */
    int32_t pose_timer[2];          /* DAT_0730b278/b27c — pose-in counters */
    int32_t item_pick[18];          /* DAT_0730b274 — {id,col,row}×6 */
    int32_t price_ask, price_base;  /* DAT_005c6bb8 / DAT_005c6bc0 */
    int32_t price_count;            /* DAT_005c6bc4 — item count */
    int32_t price_fileidx;          /* DAT_005c6bb0 — script-file / prompt sel */
    int32_t price_bc8, price_cursor;/* DAT_005c6bc8 / DAT_005c6bcc */
    const char *line;               /* DAT_0730b270 — active visible line */
};
void customer_service_get_render_state(struct cs_render_state *out);

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_0046602e — 2D character art + letterbox + offer panel. */
void customer_service_render_chars(struct IDirect3DDevice8 *dev);
/* FUN_00466b7b — haggle dialogue box + typewriter line + BARGAIN!! price. */
void customer_service_render_overlay(struct IDirect3DDevice8 *dev);
#endif

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

/* ── host-test seams — the LIVE machine's b534==0xf haggle decision
 * (FUN_004658ab): closeness ±deltas, loyalty latch, pushback patience. ────── */
void customer_service_live_haggle_state_for_test(int32_t b534, int32_t b584,
        int32_t b570, int32_t b590, int32_t offer, int32_t ask, int32_t base,
        int32_t haggle_floor, int32_t fair);
void customer_service_live_machine_tick_for_test(uint32_t pressed);
void customer_service_cand_extra_set_for_test(int idx, int32_t v);  /* DAT_06a5d564 */
int32_t customer_service_b53c(void);   /* the loyalty rank-up flash timer */
int32_t customer_service_pushback_line_for_test(void);   /* FUN_00460f16 */

/* Sale-fanfare EXP popup queue (FUN_004606fc build; types at DAT_0730b194,
 * values at DAT_06a5ea78, len = b5bc, active = b5c0).  Consumed by the
 * TOTAL-EXP popup renderer (FUN_00485861 chain) + host tests.  Entry
 * types: 0 just-price bonus, 2 near-price bonus, 1 combo, 3 TOTAL. */
int32_t customer_service_popup_queue_len(void);
int32_t customer_service_popup_queue_active(void);
int32_t customer_service_popup_queue_type(int i);
int32_t customer_service_popup_queue_val(int i);
/* Entry i's display counter (DAT_0730b304[i]) — the render alpha/slide
 * timeline the master tick advances. */
int32_t customer_service_popup_disp(int i);
void    customer_service_cand_extra_set_for_test(int idx, int32_t v);
int32_t customer_service_b53c(void);
int32_t customer_service_pushback_line_for_test(void);
int     customer_service_kind_select_for_test(void);
void    customer_service_set_queue_for_test(int idx, int32_t kyaku, int32_t item_slot, int32_t kind);

/* FUN_004361b2 — daily-news price-trend classifier (reads the active
 * haggle price panel + the merchant-HUD item tooltip).  Neutral 0 while
 * the news list is empty (all pre-day-9 traces). */
int32_t cs_news_price_trend(int32_t item_handle);

#endif /* OPENRECET_CUSTOMER_SERVICE_H */
