/*
 * news_golden_replay.h — headless golden-replay gate for the daily-news
 * generator (news_daily_update / FUN_00436623), the roster_golden_replay
 * pattern applied to the news chip.
 *
 * When OPENRECET_NEWS_GOLDEN names a captured working-arena snapshot
 * (tools/news_gen_capture.py's <out>.arena.bin — SAVE_BANK_STRIDE_BYTES
 * bytes = the engine DAT_044e3798 slot-0 body), this runs the ported
 * generator for a seed sweep on that exact input and writes a JSON
 * fixture identical in shape to the retail golden — the binary
 * bit-exact gate (list entries, headline bytes, scroll offsets, pairs,
 * rng draw count, final seed).
 *
 * Env:
 *   OPENRECET_NEWS_GOLDEN      path to the arena.bin (required to activate)
 *   OPENRECET_NEWS_SEEDS       comma list of u32 seeds (default "1")
 *   OPENRECET_NEWS_SEEDS_FILE  file with a comma/whitespace seed list
 *   OPENRECET_NEWS_OUT         output JSON path (default "news_port_out.json")
 *
 * Called once from WinMain after tables_load_all()+save_bank_init_all()
 * (right beside roster_golden_replay_maybe).  No effect when unset;
 * when active it runs the sweep, writes the JSON, and exits. */
#ifndef OPENRECET_NEWS_GOLDEN_REPLAY_H
#define OPENRECET_NEWS_GOLDEN_REPLAY_H

void news_golden_replay_maybe(void);

#endif /* OPENRECET_NEWS_GOLDEN_REPLAY_H */
