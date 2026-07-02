/*
 * save_bank.h — port of FUN_004901c2 (top-level save-arena init) +
 * FUN_0049001c (per-bank fresh-state initializer).
 *
 * ── Arena layout ──
 *
 * The engine reserves a single ~18 MB BSS region at DAT_056e5770:
 *
 *   [ shared header     ]  0x0b10 bytes = 2832
 *   [ bank 0            ]  0x2dfc8 bytes = 188360  (47090 dwords)
 *   [ bank 1            ]
 *   ...
 *   [ bank 99           ]
 *
 * Total: 0x011f7530 = 18,838,832 bytes = 4,709,708 dwords.
 *
 * On disk the arena is dumped verbatim to save.dat (FUN_004902aa
 * "save_clear_all" memcpys the whole thing and FUN_004902fe loads it
 * back). The header carries global state shared across every save
 * slot (audio sliders, etc.); each bank is one complete save slot.
 *
 * ── Shared header (dword indices within g_save_arena[0..0xb10)) ──
 *
 *   [0]  u32 magic              — 0x341944da when initialized
 *   [1]  u32 se_slider          — engine default 9
 *   [2]  u32 bgm_slider         — engine default 5
 *   [3]  u32 se_b_slider        — engine default 9 (dormant per
 *                                 engine-quirks #46)
 *   [4]  packed: u16 lo +       — DAT_056e5780 (zero-init)
 *        u16 hi (DAT_056e5782)    overlay short (zero-init, dormant)
 *   [5]  u32 slider3            — engine default 1 (DAT_056e5784;
 *                                 mirrors settings.c's slider3)
 *   [6]  u32 last_slot_used     — DAT_056e578c (zero-init; bumped by
 *                                 the save-slot UI in FUN_0049a59e)
 *   ...
 *   The remaining ~700 dwords are zero-init scratch state (cleared
 *   each call to save_bank_init_all on magic mismatch). Specific
 *   consumers will surface field semantics as they port; for now only
 *   the four sliders + slot-cursor are exposed via named accessors.
 *
 * ── Bank layout (dword indices within a bank) ──
 *
 *   [0]              ??? (zero-init)
 *   [1]              u32 magic (0x341944da)
 *   [2]              u32 zero
 *   [3]              u32 initial_pix (1000 — Recettear's starting gold)
 *   [4]              u16 = 2 + u16 padding
 *   [6..6+19999]     0xFFFFFFFF × 20000 (item slot table, 80 KB)
 *   [0x4e26..+299]   SHOP DISPLAY GRID — 15×20 = 300 cells, one item ID
 *                    per cell (0xFFFFFFFF = empty). The free-roam shop
 *                    render (FUN_004161c7) loops these and draws each
 *                    occupied cell via FUN_00415fab (DAT_044f7030 base).
 *                    See findings/shop-item-display-RE-status.md.
 *   [0x9e76..0xa586] 100 records × 18 dwords — per-bank RANKING summary
 *                    (NOT shop-floor display items, despite the old
 *                    "item-grid scratch" label). Only reader/writer is
 *                    FUN_0049f012 (ranking screen, based DAT_0450b170).
 *                    See findings/shop-item-display-RE-status.md.
 *   [0xaec6]         0
 *   [0xafc7..+299]   0xFFFFFFFF × 300
 *   [0xb0f6]         u32 gold_objective (1000)
 *   [0xb0fa]         u32 = 7
 *   [0xb0fe]         u32 = 0
 *   [0xb0ff]         u32 = 100
 *   [0xb1e8..+399]   0xFFFFFFFF × 400
 *   [0xb388..0xb38d] mini-block of misc consts (3,3,1,0,0,1)
 *   [0xb3ac..]       8 chara records × 27 dwords each (stride 0x6c)
 *   [0xb7f1]         u32 checksum — XOR-sum gate for the bank
 *
 * (Full field semantics surface as consumers port. The constants are
 * preserved as named #defines so callers can reference them without
 * memorizing offsets.)
 *
 * ── Engine call sites ──
 *
 *   FUN_004902fe (save-load, boot path) — calls FUN_004901c2 to
 *     ensure the arena has live magic, then optionally overwrites
 *     it with parsed save.dat contents.
 *   FUN_004902aa (save_clear_all) — wipes the magic then calls
 *     FUN_004901c2 to force a fresh-init, then memcpys to disk.
 *   FUN_0049a59e L213 — on NEW GAME (DAT_0438bed4=1), calls
 *     FUN_0049001c(active_bank) to reset the chosen save slot.
 *
 * Our port wires save_bank_init_all() at boot (replacing the deferred
 * save-load), and save_bank_init_one(active_bank) in
 * scene_post_fade_init() for NEW GAME.
 *
 * Pure-C — no Win32, no D3D. Unit-testable under host gcc.
 */

#ifndef OPENRECET_SAVE_BANK_H
#define OPENRECET_SAVE_BANK_H

#include <stddef.h>
#include <stdint.h>

/* ── Arena geometry ── */

#define SAVE_BANK_MAGIC            0x341944dau
#define SAVE_BANK_HEADER_BYTES     0x0b10
#define SAVE_BANK_STRIDE_BYTES     0x2dfc8         /* 188360 */
#define SAVE_BANK_STRIDE_DWORDS    0xb7f2          /* 47090  */
#define SAVE_BANK_COUNT            100
#define SAVE_BANK_ARENA_BYTES \
    (SAVE_BANK_HEADER_BYTES + SAVE_BANK_STRIDE_BYTES * SAVE_BANK_COUNT)
#define SAVE_BANK_ARENA_DWORDS     (SAVE_BANK_ARENA_BYTES / 4)

/* Dword offset (within a bank) of the trailing checksum word. */
#define SAVE_BANK_FIELD_CHECKSUM   0xb7f1

/* ── Bank field constants (dword indices within a bank) ── */

#define SAVE_BANK_FIELD_MAGIC          1
#define SAVE_BANK_FIELD_GOLD           3       /* 1000 */
#define SAVE_BANK_FIELD_OBJECTIVE_GOLD 0xb0f6  /* 1000 */
#define SAVE_BANK_FIELD_WEEK_COUNTER   0xb0fa  /* 7 */
#define SAVE_BANK_FIELD_DAY_INDEX      0xb0fe  /* 0 */
#define SAVE_BANK_FIELD_RANK_THRESHOLD 0xb0ff  /* 100 */

/* Per-chara record array within bank. 8 records × 27 dwords (0x6c
 * bytes) starting at dword index 0xb3ac. */
#define SAVE_BANK_CHARA_BASE_DWORD     0xb3ac
#define SAVE_BANK_CHARA_STRIDE_DWORDS  0x1b    /* 27 dwords = 108 bytes */
#define SAVE_BANK_CHARA_COUNT          8

/* ── Inventory / item-slot table (within a bank) ── */

/* Dword index of the first item-slot entry. The table is 20000 dwords
 * of item IDs; an empty slot reads 0xFFFFFFFF (-1). Engine base
 * `DAT_044e37b0` = working-bank + 0x18. */
#define SAVE_BANK_ITEM_TABLE_DWORD   6
#define SAVE_BANK_ITEM_TABLE_COUNT   20000

/* Dword index of the live "inventory count" field — the index of the
 * first empty item slot, recomputed on load. Engine `DAT_0450f2b0`. */
#define SAVE_BANK_FIELD_ITEM_COUNT   0xaec6

/* Dword index of the "bank occupied" marker the title slot-picker tests
 * for emptiness (== 0 ⇒ never-played slot). Engine `DAT_056e6288`
 * (= save-bank + 0x8). The same field doubles as the total PLAY-TIME in
 * frames (60 fps) the picker renders as TIME H:MM:SS. */
#define SAVE_BANK_FIELD_OCCUPIED     2
#define SAVE_BANK_FIELD_PLAYTIME     2       /* DAT_056e6288 — frames @60fps */

/* ── Save-slot picker summary fields (FUN_0049b556 per-card reads) ──
 * Engine bases indexed by `slot * 0x2dfc8` bytes; here as dword indices
 * within a bank (bank-0 base = arena + header 0x0b10 = abs 0x056e6280).
 * SCORE/LOOP/DAY/PORTRAIT/CHAR cluster sits just past OBJECTIVE_GOLD; the
 * game-mode tag lives near the bank tail. */
#define SAVE_BANK_FIELD_SCORE        0xb0f7  /* DAT_0571265c — "SCORE %8d"   */
#define SAVE_BANK_FIELD_LOOP         0xb0f9  /* DAT_05712664 — "LOOP %3d" +1 */
#define SAVE_BANK_FIELD_CARD_DAY     0xb0fb  /* DAT_0571266c — big day# (+1) */
#define SAVE_BANK_FIELD_PORTRAIT_ROT 0xb0fc  /* DAT_05712670 — portrait rot  */
/* Same dword 0xb0fc, read in the WORKING arena as the in-game clock-hand
 * TARGET (engine DAT_0450fb88, FUN_0049a59e L100638 snaps clock_phase to it
 * on a CONTINUE load; the day-hand eases toward it). The picker-card code
 * aliases the disk-bank copy as PORTRAIT_ROT above; in-game it is the clock
 * target. Verified: retail live DAT_0450fb88 == this save's dword == 1. */
#define SAVE_BANK_FIELD_CLOCK_TARGET 0xb0fc  /* DAT_0450fb88 (working arena) */
#define SAVE_BANK_FIELD_CHAR_LEVEL   0xb100  /* DAT_05712680 — char/level    */
#define SAVE_BANK_FIELD_GAME_MODE    0xb759  /* DAT_05713fe4 — 0 empty,1/2/3 */

/* Shop "items on display" grid — engine `&DAT_044f7030` (working-bank
 * dword 0x4e26): 15 rows × 20 cols = 300 cells, row-major, one item ID
 * per cell (0xFFFFFFFF = empty). Walked by the free-roam shop render
 * (FUN_004161c7 mid block 2) drawing each occupied cell via
 * FUN_00415fab. See findings/shop-item-display-RE-status.md. */
#define SAVE_BANK_FIELD_DISPLAY_GRID   0x4e26
#define SAVE_BANK_DISPLAY_GRID_ROWS    15
#define SAVE_BANK_DISPLAY_GRID_COLS    20
#define SAVE_BANK_DISPLAY_GRID_CELLS   300

/* ── Sale-commit bank fields (the FUN_004658ab accept-block helpers) ──
 *
 * Per-item best/worst sale price records — FUN_00460b3a.  Engine
 * `&DAT_044e3798 + item_slot·0x50 + 0x13d48` (max) / `+0x13d4c` (min);
 * item_slot = FUN_004681f6(id).  0x13d48/4 = 0x4f52, stride 0x50/4 = 0x14. */
#define SAVE_BANK_FIELD_SALE_MAX_BASE  0x4f52
#define SAVE_BANK_FIELD_SALE_MIN_BASE  0x4f53
#define SAVE_BANK_SALE_REC_STRIDE      0x14

/* Sold-item lists — FUN_00460083.  Engine `0x450f6b4 + (type·100 + i)·4`
 * (100 int32 entries per type, free = -1) with counts at `0x450fb64 +
 * type·4`.  (0x450f6b4-0x44e3798)/4 = 0xafc7; counts at 0xb0f3 + type.
 * The LIST region spans exactly 3 types (0x450f6b4→0x450fb64 = 300
 * dwords) — the COUNTS array extends further (the type-8 count at
 * 0x450fb84 gates the news-pair block; 0x450fb88 = CLOCK_TARGET). */
#define SAVE_BANK_FIELD_SOLD_LIST      0xafc7
#define SAVE_BANK_FIELD_SOLD_COUNT     0xb0f3
#define SAVE_BANK_SOLD_LIST_ENTRIES    100

/* News short-pairs — FUN_00460083's count[8]>8 block.  Engine
 * `0x451157c`: 0x14 pairs of (int16 item_id, int16 3), free = first==0.
 * BYTE offset from the bank base (int16 access). */
#define SAVE_BANK_NEWS_PAIRS_BYTE_OFF  0x2dde4
#define SAVE_BANK_NEWS_PAIRS_COUNT     0x14

/* Merchant EXP accumulator — engine `DAT_0450fb8c` ((0x450fb8c-
 * 0x44e3798)/4 = 0xb0fd).  The sale popup timeline (master tick,
 * all.c:60274-60277) adds the queue's type-3 TOTAL here when the
 * popup window closes; the merchant-level bar fill animates toward
 * it. */
#define SAVE_BANK_FIELD_MERCHANT_EXP   0xb0fd

/* Encyclopedia "sold" marks — FUN_00460f59.  Engine `0x450ae50`:
 * 100 pairs of (int32 catalog_slot, int32 3), free = first==0.
 * (0x450ae50-0x44e3798)/4 = 0x9dae. */
#define SAVE_BANK_FIELD_ENCYC_SOLD     0x9dae
#define SAVE_BANK_ENCYC_SOLD_PAIRS     100

/* ── Shared header default slider values (engine-init constants) ── */

#define SAVE_HEADER_SE_DEFAULT       9
#define SAVE_HEADER_BGM_DEFAULT      5
#define SAVE_HEADER_SE_B_DEFAULT     9
#define SAVE_HEADER_SLIDER3_DEFAULT  1

/* Shared-header dword index of the "last-used save slot" the continue
 * picker seeds its cursor from. Engine `DAT_056e578c` (= header + 0x1c
 * = dword 7; header dword 6 / DAT_056e5788 is the hidden-char unlock). */
#define SAVE_HEADER_FIELD_LAST_SLOT  7

/* ── Arena access ── */

/* Returns a pointer to the start of the shared header (== base of the
 * full arena). Aliased to a stable static buffer; same pointer
 * returned every call. */
uint8_t *save_arena_base(void);

/* Returns a pointer to bank `bank_idx`. Returns NULL if out of range. */
uint8_t *save_bank_at(int bank_idx);

/* Returns a uint32_t pointer to the same address (typed accessor for
 * cleaner field reads). Returns NULL if out of range. */
uint32_t *save_bank_dwords_at(int bank_idx);

/* ── Top-level lifecycle ── */

/* Port of FUN_004901c2.
 *
 *   1) If the shared-header magic doesn't match SAVE_BANK_MAGIC, zero
 *      the entire arena, then seed:
 *         - header[0]  = SAVE_BANK_MAGIC
 *         - header[1]  = SE_DEFAULT  (9)
 *         - header[2]  = BGM_DEFAULT (5)
 *         - header[3]  = SE_B_DEFAULT(9)
 *         - header[5]  = SLIDER3_DEFAULT (1)
 *      and fire the audio_fade BGM-apply callback so the new slider
 *      takes effect immediately (matches the engine's FUN_00499583
 *      call at FUN_004901c2 L27).
 *
 *   2) For each of the 100 banks, verify (magic == SAVE_BANK_MAGIC
 *      && checksum_matches). On any mismatch, call
 *      save_bank_init_one(idx) to reset that bank.
 *
 * Idempotent — once the arena is live, subsequent calls are no-ops
 * unless a bank's checksum has been tampered with. */
void save_bank_init_all(void);

/* Port of FUN_0049001c — reset one bank to "fresh new-game" state.
 *
 * Steps:
 *   - Zero the entire bank (0xb7f2 dwords).
 *   - Set named field constants (gold=1000, objective=1000, day=0,
 *     week=7, rank threshold=100, mini-block, etc.).
 *   - Fill 4 large 0xFFFFFFFF spans (item slot tables).
 *   - For each of 8 chara records (g_chara[N]), interpolate level-1
 *     base stats into the bank's chara record via FUN_0047a8c0
 *     (see save_bank_apply_chara_levels), then call
 *     save_bank_apply_starter_items + save_bank_apply_starter_flagpairs.
 *   - Stamp bank[CHECKSUM_DWORD] = xor-sum of preceding dwords.
 *
 * NOTE: the chara interpolation reads from g_chara, populated by
 * tables_parse_chara at tables_load_all time. If g_chara is all
 * zero (parser not run), the bank's stats fields end up zero — no
 * crash, just garbage values that later consumers would misread. */
void save_bank_init_one(int bank_idx);

/* Port of FUN_0047a8c0 — per-chara level-up stat interpolation.
 *
 * Walks all 8 chara records of bank_idx; for each, reads the current
 * level from rec[0] and writes interpolated atk/def/matk/mdef/hp/sp
 * fields based on g_chara[N]'s {base, lv100} stats.  Idempotent: the
 * same level produces the same writes every call.
 *
 * Engine call sites (9 per NEW GAME):
 *   - 8× from save_bank_init_one (one per outer iter of the chara loop)
 *   - 1× from stage_post_load_init (FUN_00435c98 L33118)
 *
 * The engine's per-outer-iter calls are wasteful (each call processes
 * all 8 records but only iter N has rec[N][0] freshly written), but
 * the final state is identical to a single post-loop call.  Mirroring
 * the engine's call pattern matches retail's per-frame call-count for
 * the function. */
void save_bank_apply_chara_interp(int bank_idx);

/* Recompute + stamp checksum at bank[SAVE_BANK_FIELD_CHECKSUM]. */
void save_bank_stamp_checksum(int bank_idx);

/* Returns 1 if bank's stored checksum matches a fresh re-computation,
 * 0 otherwise. */
int save_bank_checksum_ok(int bank_idx);

/* ── Shared-header accessors ── */

uint32_t save_header_magic(void);
int      save_header_get_se_slider(void);
int      save_header_get_bgm_slider(void);
int      save_header_get_se_b_slider(void);
int      save_header_get_slider3(void);
void     save_header_set_se_slider(int v);
void     save_header_set_bgm_slider(int v);
void     save_header_set_se_b_slider(int v);
void     save_header_set_slider3(int v);
int      save_header_get_last_slot(void);
void     save_header_set_last_slot(int v);

/* ── Test helpers ── */

/* Reset all state to "freshly mmap'd BSS" (zero magic, zero banks).
 * Used by tests to force a re-init path. Production code does NOT
 * need this — boot starts with a zero arena and save_bank_init_all
 * fills it. */
void save_bank_arena_clear(void);

/* Optional callback fired by save_bank_init_all on a fresh header
 * init — exists so audio.c can hook the engine's FUN_00499583 call
 * (re-apply BGM SetVolume) without making save_bank link against
 * audio. Pass NULL to clear. */
typedef void (*save_header_init_hook_t)(void);
void save_bank_set_header_init_hook(save_header_init_hook_t hook);

#endif /* OPENRECET_SAVE_BANK_H */
