/*
 * news_daily.h — the daily-news subsystem: generator FUN_00436623,
 * eligible-row picker FUN_004363c6, list reset FUN_00436180, attr-mask
 * display name FUN_0049e6b3, and the market price-trend classifier
 * FUN_004361b2 (previously PORT-DEBT(cs-price-trend), stubbed to 0).
 *
 * RE: docs/findings/news-daily-RE.md (objdump-verified 2026-07-10).
 *
 * The generator maintains the per-slot 20-entry featured-news list
 * (SAVE_BANK_FIELD_NEWS_* / SAVE_BANK_NEWS_ENTRY_BYTE_OFF) + the day's
 * newspaper headline buffers (SAVE_BANK_*NEWS_HL_*), consuming the
 * shared LCG in an exact, data-dependent draw order (documented per
 * step in news_daily.c — load-bearing for RNG parity).  All engine
 * call sites are gated `SHOP_DAY > 8`, so pre-day-9 traces draw zero
 * rng from this module.
 *
 * Call sites (engine): customer-leave restore all.c:60358 (rng%3),
 * master-tick clock advance 86738 (rng%5; mechanic unported —
 * PORT-DEBT(news-clock-advance)), morning beat 86711 (unconditional).
 * The newspaper/ticker RENDER of the headline buffers is
 * PORT-DEBT(news-ticker-render).
 *
 * Pure C (host-testable): the save-bank slot base is passed in; table
 * data comes from g_news / g_item; rng from rng_next15().
 */

#ifndef OPENRECET_NEWS_DAILY_H
#define OPENRECET_NEWS_DAILY_H

#include <stdint.h>

/* Boom-news row ids (1-based news.txt rows) the generator hard-codes
 * for the player-driven "sold N of the same item" news. */
#define NEWS_BOOM_ID       0x24   /* 36 */
#define NEWS_BOOM_ALT_ID   0x25   /* 37 */
/* Debug/external "normalized" sentinel id checked by expiry+classifier. */
#define NEWS_DEBUG_ID      500

/* The news-break ticker timer — engine DAT_0438b92c.  Set to 1 by the
 * call sites when the generator ran; the master tick increments it and
 * fires the news-jingle SE at 0x1e when headlines exist. */
extern int32_t g_news_ticker_timer;

/* FUN_004363c6 — pick a random eligible news-def row for `day`
 * (SHOP_DAY).  day==9 ⇒ 0 (scripted first news), no rng.  Else rows
 * with category != -100 and period_start<=day<=(period_end|999) are
 * counted; 1 rng draw iff any; returns the picked 0-BASED row index
 * (news id = index+1) or -1 if none (no draw). */
int news_pick_def(int day);

/* FUN_00436623 — run one daily-news update on the slot arena at
 * `bank` (byte base of the working save bank).  See the RE doc for
 * the full phase/rng map. */
void news_daily_update(uint8_t *bank);

/* FUN_00436180 — reset the featured-news list (id=-1, trend/dur=0;
 * target untouched) + headline count.  NB id=-1 is NOT the
 * generator's "free slot" (that is id==0) — a reset list never
 * regrows until entries are zeroed elsewhere. */
void news_list_reset(uint8_t *bank);

/* FUN_0049e6b3 — attribute-mask → English display name ("weapons",
 * "pieces of armor", ...); unknown mask ⇒ "" (engine DAT_005fd740). */
const char *news_attr_display_name(int mask);

/* FUN_004361b2 — market price-trend classifier for a live haggle.
 * `item_handle` = id<<6|quality; `tutorial_sell` mirrors the engine's
 * b1c0==1 && dungeon==0 && cc08==4 && f404!=0 gate (caller-evaluated;
 * true ⇒ 0).  Sums active matching entries' trend chars ('d' skipped):
 * any char <= -2 ⇒ -2, else clamped to [-1,1].  No rng. */
int news_price_trend(const uint8_t *bank, int item_handle, int tutorial_sell);

#endif /* OPENRECET_NEWS_DAILY_H */
