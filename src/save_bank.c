/*
 * save_bank.c — port of FUN_004901c2 + FUN_0049001c + their three
 * small helpers (FUN_0048ffd9, FUN_0048ff93, FUN_0047a8c0).
 *
 * See save_bank.h for the high-level model + arena layout.
 *
 * ── Engine fidelity notes ──
 *
 * (1) STARTER_ITEMS (DAT_005cf788) and STARTER_FLAG_PAIRS (DAT_005cf864)
 * are extracted byte-for-byte from vendor/unpacked/recettear.unpacked.exe
 * via tools/analyze/pe.py. The flag-pair table is undersized in the
 * engine: only 64 of 80 declared (8 chara × 10) pairs hold valid
 * data, and the trailing 16 pairs read from adjacent .data (which
 * happens to be the "wb"/"rb"/"_save.dat"/"save.dat" file-mode strings).
 * The engine quirk is preserved verbatim — same bytes either way —
 * and the table is dormant in vendor data because Recettear is the
 * only chara hireable at NEW GAME so only STARTER_FLAG_PAIRS[0] gets
 * read at init. Documented in docs/findings/engine-quirks.md.
 *
 * (2) FUN_0047a8c0 ("per-chara stat interpolation") reads chara base +
 * level-100 stats from g_chara[] and writes interpolated values into
 * the bank chara record. The formula is
 *   value = base + (lv100 - base) * level / 100
 * with `level` read from bank chara[N] field at relative offset
 * -0x30 (i.e., bank chara record's "current level" word). At first
 * call from FUN_0049001c, that field is zero (bank was just memset'd),
 * so the interpolation degenerates to `value = base` — exactly what we
 * want at NEW GAME (level 1 == base stats).
 *
 * (3) FUN_0049001c calls thunk_FUN_005041f6 (== rand_lcg) once per
 * chara record but writes nothing useful — `*puVar1 = uVar5 % 10;`
 * is immediately overwritten on the very next line. Faithfully
 * reproduced via rand_consume() so the post-init RNG state matches.
 */

#include "save_bank.h"

#include <string.h>

#include "audio_fade.h"
#include "rng.h"
#include "tables_chara.h"

/* ── Arena buffer ── */

static uint8_t g_arena[SAVE_BANK_ARENA_BYTES];
static save_header_init_hook_t g_header_init_hook = NULL;

uint8_t *save_arena_base(void)
{
    return g_arena;
}

uint8_t *save_bank_at(int bank_idx)
{
    if (bank_idx < 0 || bank_idx >= SAVE_BANK_COUNT) {
        return NULL;
    }
    return g_arena + SAVE_BANK_HEADER_BYTES
                   + (size_t)bank_idx * SAVE_BANK_STRIDE_BYTES;
}

uint32_t *save_bank_dwords_at(int bank_idx)
{
    uint8_t *p = save_bank_at(bank_idx);
    return p ? (uint32_t *)p : NULL;
}

static uint32_t *header_dwords(void)
{
    return (uint32_t *)g_arena;
}

void save_bank_set_header_init_hook(save_header_init_hook_t hook)
{
    g_header_init_hook = hook;
}

void save_bank_arena_clear(void)
{
    memset(g_arena, 0, sizeof g_arena);
}

/* ── Header accessors ── */

uint32_t save_header_magic(void)            { return header_dwords()[0]; }
int      save_header_get_se_slider(void)    { return (int)header_dwords()[1]; }
int      save_header_get_bgm_slider(void)   { return (int)header_dwords()[2]; }
int      save_header_get_se_b_slider(void)  { return (int)header_dwords()[3]; }
int      save_header_get_slider3(void)      { return (int)header_dwords()[5]; }

static int clamp_slider(int v)
{
    if (v < 0) return 0;
    if (v > 9) return 9;
    return v;
}

void save_header_set_se_slider(int v)   { header_dwords()[1] = (uint32_t)clamp_slider(v); }
void save_header_set_bgm_slider(int v)  { header_dwords()[2] = (uint32_t)clamp_slider(v); }
void save_header_set_se_b_slider(int v) { header_dwords()[3] = (uint32_t)clamp_slider(v); }
void save_header_set_slider3(int v)
{
    if (v < 0) v = 0;
    if (v > 2) v = 2;
    header_dwords()[5] = (uint32_t)v;
}

/* ── Checksum ── */

/* The engine's "checksum" is just the sum of every dword in the bank
 * from index 0 to (CHECKSUM_DWORD - 1) inclusive, stored at index
 * CHECKSUM_DWORD. See FUN_004901c2 L34-40:
 *
 *   iVar3 = 0xb7f0;   piVar2 = piVar4;
 *   do {
 *     iVar1 = iVar1 + *piVar2;
 *     piVar2 = piVar2 + 1;
 *     iVar3 = iVar3 + -1;
 *   } while (iVar3 != 0);
 *   if (iVar1 != piVar4[0xb7f1]) goto LAB_00490241;
 *
 * The 0xb7f0-count loop covers indices [0, 0xb7f0) — i.e., 0 through
 * 0xb7ef — and compares against [0xb7f1]. Index 0xb7f0 is skipped
 * entirely (probably reserved padding). */
static uint32_t bank_checksum(const uint32_t *bank)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < SAVE_BANK_FIELD_CHECKSUM - 1; i++) {
        sum += bank[i];
    }
    return sum;
}

void save_bank_stamp_checksum(int bank_idx)
{
    uint32_t *bank = save_bank_dwords_at(bank_idx);
    if (!bank) return;
    bank[SAVE_BANK_FIELD_CHECKSUM] = bank_checksum(bank);
}

int save_bank_checksum_ok(int bank_idx)
{
    uint32_t *bank = save_bank_dwords_at(bank_idx);
    if (!bank) return 0;
    return bank_checksum(bank) == bank[SAVE_BANK_FIELD_CHECKSUM];
}

/* ── Per-chara starter tables (extracted from .data via pe.py) ── */

/* DAT_005cf788 — starter-item IDs per chara. 5 slots each, -1 = empty.
 * Read by FUN_0048ff93 once per chara, written into the bank's
 * 100-record item grid. */
static const int32_t STARTER_ITEMS[SAVE_BANK_CHARA_COUNT][5] = {
    {           1,        1301,        1501,        2301,          -1 },  /* chara[0] */
    {         101,        1201,        1401,        2401,          -1 },  /* chara[1] */
    {         201,        1101,        1401,          -1,          -1 },  /* chara[2] */
    {         301,        1201,        1401,          -1,          -1 },  /* chara[3] */
    {         501,        1001,        1401,        2001,          -1 },  /* chara[4] */
    {         401,        1001,        1401,        2101,          -1 },  /* chara[5] */
    {         601,        1201,        2201,        2401,          -1 },  /* chara[6] */
    {         701,        1201,        1601,          -1,          -1 },  /* chara[7] */
};

/* DAT_005cf864 — starter flag-pairs per chara. Engine reads 10 pairs
 * per chara; only the first 8 pairs for chara[0..5] + 4 pairs for
 * chara[6] are valid data (64 pairs out of 80). The trailing 16
 * pairs read from adjacent .data strings — quirk preserved here
 * verbatim by extracting the raw bytes; dormant in vendor because
 * NEW GAME only triggers chara[0]'s row. */
static const int32_t STARTER_FLAG_PAIRS[SAVE_BANK_CHARA_COUNT][10][2] = {
    { /* chara[0] */
        {           3,           3 }, {           1,           0 },
        {           0,           1 }, {           9,           1 },
        {          10,           3 }, {          11,           0 },
        {           3,           6 }, {           6,           6 },
        {           9,           6 }, {          12,           6 },
    },
    { /* chara[1] */
        {           4,           3 }, {           1,           0 },
        {           0,           1 }, {           9,           1 },
        {          10,           3 }, {          11,           0 },
        {           3,           6 }, {           6,           6 },
        {           9,           6 }, {          12,           6 },
    },
    { /* chara[2] */
        {           4,           3 }, {           1,           0 },
        {           0,           1 }, {          14,           1 },
        {          10,           3 }, {          11,           0 },
        {           3,           6 }, {           6,           6 },
        {           9,           6 }, {          12,           6 },
    },
    { /* chara[3] */
        {           4,           3 }, {           1,           0 },
        {           0,           1 }, {          14,           1 },
        {          10,           3 }, {          11,           0 },
        {           3,           6 }, {           6,           6 },
        {           9,           6 }, {          12,           6 },
    },
    { /* chara[4] */
        {          10,          13 }, {           5,          10 },
        {          20,          10 }, {          20,          18 },
        {          15,          18 }, {          20,          20 },
        {          12,           5 }, {          30,          20 },
        {          15,          70 }, {          15,           8 },
    },
    { /* chara[5] */
        {          15,          16 }, {          25,          35 },
        {          25,          22 }, {          15,          20 },
        {          45,          25 }, {          20,          15 },
        {          10,          16 }, {          30,          30 },
        {          23,          20 }, {          45,          30 },
    },
    { /* chara[6] — last 6 pairs overrun into adjacent .data strings;
       quirk preserved byte-for-byte. */
        {          40,          80 }, {          30,          50 },
        {          15,          20 }, {         100,          10 },
        {       25207,  1702257011 }, {  1952539694,           0 },
        {       25202,  1702257011 }, {  1952539694,           0 },
        {       25202,  1986098015 }, {  1633955429,         116 },
    },
    { /* chara[7] — all 10 pairs overrun, see above. */
        {       25207,  1702257011 }, {  1952539694,           0 },
        {       25207,  1986098015 }, {  1633955429,         116 },
        {           7,           1 }, {           0,           0 },
        {           3,          10 }, {          17,          22 },
        {       10000,       30000 }, {      100000,      500000 },
    },
};

/* ── Helpers (FUN_0048ff93 / FUN_0048ffd9 / FUN_0047a8c0) ── */

/* Engine bank-field offsets (in bytes from bank start) for the
 * per-helper write destinations. These are derived from the
 * decompiled output of FUN_0049001c — the helper functions all take
 * a bank pointer + write at fixed byte offsets within it. */

/* FUN_0048ff93 (puVar4 = param_1 + 0x2cec8) — writes 8 chara × 5
 * item-slot pairs into the per-chara starter-inventory window. Each
 * pair is two dwords 5 entries apart; outer stride is 0x1b dwords
 * (108 bytes). */
#define BANK_OFFSET_STARTER_ITEMS_BYTES   0x2cec8

/* FUN_0048ffd9 (puVar3 = param_1 + 0x2ce14) — writes 10 flag-pairs
 * starting 1 dword before that offset, contiguous 2-dword pairs.
 * Reads chara index from bank field at byte 0x2cde0. */
#define BANK_OFFSET_STARTER_FLAGS_BYTES   0x2ce14
#define BANK_OFFSET_CHARA_INDEX_BYTES     0x2cde0

/* FUN_0047a8c0 (puVar4 = param_1 + 0x2cee0) — writes interpolated
 * level-up stat fields per chara, stride 0x6c (= SAVE_BANK_CHARA_STRIDE_DWORDS
 * × 4). Reads chara level from edi-0x30 (=puVar4-0x30). */
#define BANK_OFFSET_CHARA_INTERP_BYTES    0x2cee0
#define BANK_INTERP_LEVEL_OFFSET_BYTES    (-0x30)

/* Port of FUN_0048ff93. Walks the 8-chara × 5-slot STARTER_ITEMS
 * table; for each entry writes two copies of the encoded slot
 * (id<<6 | 0x20) into the bank, 5 dwords apart. -1 entries stay as
 * 0xffffffff in both positions. */
static void apply_starter_items(uint8_t *bank_bytes)
{
    uint32_t *base = (uint32_t *)(bank_bytes + BANK_OFFSET_STARTER_ITEMS_BYTES);

    for (int chara = 0; chara < SAVE_BANK_CHARA_COUNT; chara++) {
        uint32_t *row = base + (size_t)chara * SAVE_BANK_CHARA_STRIDE_DWORDS;
        for (int slot = 0; slot < 5; slot++) {
            int32_t id = STARTER_ITEMS[chara][slot];
            uint32_t encoded = (id == -1)
                ? 0xffffffffu
                : (uint32_t)(((uint32_t)id << 6) | 0x20u);
            /* The engine writes `puVar1[-5]` and `*puVar1` — two
             * dwords, the second 5 slots after the first. */
            row[slot]     = encoded;
            row[slot + 5] = encoded;
        }
    }
}

/* Port of FUN_0048ffd9. Reads `bank[CHARA_INDEX]` (single byte/word
 * sized field at byte offset 0x2cde0), uses it as a row index into
 * STARTER_FLAG_PAIRS, and writes 10 pairs (id, count) contiguously
 * starting 1 dword before BANK_OFFSET_STARTER_FLAGS_BYTES.
 *
 * In the engine, this is called from FUN_0049001c's chara loop with
 * the bank's chara index field still at zero (memset'd) — so only
 * STARTER_FLAG_PAIRS[0] is read in practice. We preserve the
 * "read the field" indirection so future callers (recruit-NPC paths)
 * pick up the right row. */
static void apply_starter_flag_pairs(uint8_t *bank_bytes)
{
    uint32_t chara_idx = *(uint32_t *)(bank_bytes + BANK_OFFSET_CHARA_INDEX_BYTES);
    if (chara_idx >= (uint32_t)SAVE_BANK_CHARA_COUNT) {
        /* Engine doesn't guard; we do, to keep the array access in-bounds. */
        return;
    }
    uint32_t *dst = (uint32_t *)(bank_bytes + BANK_OFFSET_STARTER_FLAGS_BYTES);
    /* Engine writes [-1] then [0] then advances by 2; same as writing
     * two dwords starting one dword before the target. */
    for (int pair = 0; pair < 10; pair++) {
        dst[pair * 2 - 1] = (uint32_t)STARTER_FLAG_PAIRS[chara_idx][pair][0];
        dst[pair * 2]     = (uint32_t)STARTER_FLAG_PAIRS[chara_idx][pair][1];
    }
}

/* Port of FUN_0047a8c0. For each of 8 chara records, interpolate
 * (base, lv100) stats into the bank chara record at the current
 * level (stored at bank record byte offset -0x30 from the interp
 * window start).
 *
 * Formula: value = base + (lv100 - base) * level / 100  (truncated
 * via _ftol — equivalent to C int truncation for non-negative
 * results, which is the case for monotonic growth stats).
 *
 * Field mapping (from the disasm at 0x47a8c0):
 *   record[0]  ← chara.df_base    (esi+4,  32-bit copy)
 *   record[4]  ← chara.mt_base    (esi+8)
 *   record[8]  ← chara.mf_base    (esi+0xc)
 *   record[0xc] ← chara.hp_base   (esi-8, 16-bit copy)
 *
 *   record[-4] ← interp(at_base,  at_lv100)
 *   record[0]  ← interp(df_base,  df_lv100)  (overwrites df copy above)
 *   record[4]  ← interp(mt_base,  mt_lv100)  (overwrites mt copy)
 *   record[8]  ← interp(mf_base,  mf_lv100)  (overwrites mf copy)
 *   record[0xc] (short) ← interp(hp_base, hp_lv100)
 *   record[0x10] (short) ← interp(sp_base, sp_lv100)
 *
 * The four 32-bit copies of base stats are effectively dead writes
 * (the interp result overwrites the same memory). Preserved for
 * faithful behavior — observable only via the (zero-cycle) memory
 * traffic. */
static int interp(int base, int lv100, int level)
{
    /* Engine: f = (lv100 - base) * (float)level / 100.0; int += f
     * via _ftol (truncate-towards-zero). For non-negative growth
     * this matches C integer truncation. */
    int delta = lv100 - base;
    /* Use 64-bit intermediate to avoid overflow on huge stat deltas
     * (engine values cap well under 2^16 so any width works in
     * practice). */
    int64_t scaled = (int64_t)delta * (int64_t)level;
    int rounded = (int)(scaled / 100);  /* truncate towards zero */
    return base + rounded;
}

static void apply_chara_interp(uint8_t *bank_bytes)
{
    uint32_t *interp_base = (uint32_t *)(bank_bytes + BANK_OFFSET_CHARA_INTERP_BYTES);

    for (int chara = 0; chara < SAVE_BANK_CHARA_COUNT; chara++) {
        const chara_def_t *src = &g_chara[chara];
        uint32_t *rec = interp_base + (size_t)chara * SAVE_BANK_CHARA_STRIDE_DWORDS;

        /* Engine reads level from rec[-12] (= byte offset -0x30 from
         * interp window start). At first call this is the bank chara
         * record's "current level" word, freshly zeroed; subsequent
         * calls (hire NPC) carry the chara's actual level. */
        int level = (int)rec[-12];

        /* Dead 32-bit copies — overwritten by interp below but
         * preserved for fidelity. */
        rec[0] = (uint32_t)src->df_base;   /* edi[0] */
        rec[1] = (uint32_t)src->mt_base;   /* edi[4] */
        rec[2] = (uint32_t)src->mf_base;   /* edi[8] */
        /* edi[0xc] short: high-word of hp_base. Engine writes a 16-bit
         * value here, but the interp call below overwrites the same
         * field as a short — net effect is the same as the interp
         * write. Skipped: writing a 16-bit dead value into a dword
         * field is needlessly fiddly. */

        /* Interpolated writes (overwrites the dead copies). */
        rec[-1] = (uint32_t)interp(src->at_base, src->at_lv100, level);
        rec[0]  = (uint32_t)interp(src->df_base, src->df_lv100, level);
        rec[1]  = (uint32_t)interp(src->mt_base, src->mt_lv100, level);
        rec[2]  = (uint32_t)interp(src->mf_base, src->mf_lv100, level);

        /* HP/SP are stored as 16-bit in the bank record. */
        uint16_t *shorts = (uint16_t *)(rec + 3);  /* &rec[0xc] in bytes */
        shorts[0] = (uint16_t)interp(src->hp_base, src->hp_lv100, level);
        shorts[2] = (uint16_t)interp(src->sp_base, src->sp_lv100, level);
        /* shorts[1] is the high half of rec[3] (= byte offset 0xe);
         * left at zero (memset by bank init, untouched by engine). */
    }
}

/* ── FUN_0049001c body ── */

void save_bank_init_one(int bank_idx)
{
    uint8_t *bank_bytes = save_bank_at(bank_idx);
    uint32_t *bank = save_bank_dwords_at(bank_idx);
    if (!bank) return;

    /* (1) zero the entire bank. */
    memset(bank, 0, SAVE_BANK_STRIDE_BYTES);

    /* (2) named field constants. */
    bank[2]                              = 0;
    bank[SAVE_BANK_FIELD_GOLD]           = 1000;
    bank[SAVE_BANK_FIELD_OBJECTIVE_GOLD] = 1000;
    bank[SAVE_BANK_FIELD_MAGIC]          = SAVE_BANK_MAGIC;

    /* `*(undefined2 *)(param_1 + 4) = 2` — short at byte offset 0x10. */
    *(uint16_t *)(bank_bytes + 0x10)     = 2;

    bank[SAVE_BANK_FIELD_WEEK_COUNTER]   = 7;

    /* (3) large 0xFFFFFFFF spans. */
    {
        /* param_1 + 6 ..  + 6 + 20000 dwords */
        uint32_t *p = bank + 6;
        for (int i = 0; i < 20000; i++) p[i] = 0xffffffffu;
    }
    {
        /* param_1 + 0x4e26 .. + 300 dwords */
        uint32_t *p = bank + 0x4e26;
        for (int i = 0; i < 300; i++) p[i] = 0xffffffffu;
    }
    {
        /* param_1 + 44999 .. + 300 dwords */
        uint32_t *p = bank + 44999;
        for (int i = 0; i < 300; i++) p[i] = 0xffffffffu;
    }
    {
        /* param_1 + 0xb1e8 .. + 400 dwords */
        uint32_t *p = bank + 0xb1e8;
        for (int i = 0; i < 400; i++) p[i] = 0xffffffffu;
    }

    /* (4) misc constants mini-block at 0xb388..0xb38d. */
    bank[0xb388] = 3;
    bank[0xb389] = 3;
    bank[0xb38a] = 1;
    bank[0xb38d] = 1;
    bank[0xb38b] = 0;
    bank[0xb38c] = 0;
    bank[0xaec6] = 0;
    bank[SAVE_BANK_FIELD_DAY_INDEX]      = 0;
    bank[SAVE_BANK_FIELD_RANK_THRESHOLD] = 100;

    /* (5) 100-record item-grid scratch loop at offset 0x9e78 with
     * stride 0x12 dwords. Each iter writes 18 dwords of zero. The
     * loop's [-2] / [-1] writes touch positions 0x9e76 + iter*0x12
     * and 0x9e77 + iter*0x12 — already zero from memset; preserved
     * for fidelity but no-op. */
    /* (No work needed — memset zeroed the whole region already.) */

    /* (6) Conditional chara-name carry-over: only fires if
     * DAT_005c80ac (chara count) is non-zero, indexing into
     * DAT_095d3808 (chara descriptor table). Both are upstream-
     * untracked in our port; until the recruit/NPC system lands we
     * skip this entire block. The fields it writes (bank
     * positions [0x9e64 + N*0x12]) stay zero from the memset, which
     * matches the engine on a first-boot DAT_005c80ac=0 path. */

    /* (7) FUN_0048ffd9 — starter flag-pairs for the bank's current
     * chara index (zero from memset → STARTER_FLAG_PAIRS[0]). */
    apply_starter_flag_pairs(bank_bytes);

    /* (8) 8-iteration per-chara loop. Mirrors FUN_0049001c L92-116:
     * each iter populates a chara record's level-1 stats from the
     * parsed g_chara[N] table. */
    for (int chara = 0; chara < SAVE_BANK_CHARA_COUNT; chara++) {
        uint32_t *rec = bank
                      + SAVE_BANK_CHARA_BASE_DWORD
                      + (size_t)chara * SAVE_BANK_CHARA_STRIDE_DWORDS;
        const chara_def_t *src = &g_chara[chara];

        rec[0x11] = 0;  /* + 0x44 — scratch */
        rec[0xb]  = (uint32_t)src->at_base;
        rec[0xc]  = (uint32_t)src->df_base;
        rec[0xd]  = (uint32_t)src->mt_base;
        rec[0xe]  = (uint32_t)src->mf_base;
        rec[0x12] = 0;  /* + 0x48 — scratch */
        rec[0x13] = 300;  /* + 0x4c — engine writes 300 unconditionally */

        /* HP/SP go in as shorts (i16 fields at byte +0x3c, +0x40). */
        uint16_t *shorts = (uint16_t *)(rec + 0xf);
        shorts[0] = (uint16_t)src->hp_base;
        shorts[2] = (uint16_t)src->sp_base;
        /* shorts[1] (high word of rec[0xf]) left zero — engine writes
         * nothing there; subsequent reads expect zero. */

        /* `uVar5 = thunk_FUN_005041f6(); *puVar1 = uVar5 % 10;`  —
         * the LCG result lands at rec[0] then is immediately
         * overwritten by `*puVar1 = (&DAT_073ae058)[chara*0x10];`.
         * Faithful: consume one RNG number (so post-init RNG state
         * matches engine), then write level_threshold. */
        (void)rng_next15();
        rec[0] = (uint32_t)src->level_threshold;
        if (chara == 0) {
            /* Engine: `if (param_1 == 0) *puVar1 = 0;` — Recette's
             * record always starts at level 0 regardless of chara.txt's
             * level_threshold field. */
            rec[0] = 0;
        }

        /* Per-chara FUN_0047a8c0 call. The engine calls it ONCE per
         * outer iter, but FUN_0047a8c0 itself walks all 8 records
         * internally — so this is effectively called 8 times with
         * redundant work. Faithful, but inefficient. We call it once
         * AFTER the chara loop instead (matches the final write per
         * chara; intermediate writes during iters 0..6 are dead). */

        /* Mark this chara as "present" — engine writes
         * `*(undefined1 *)(puVar1 + 0x18) = 1;` (byte at +0x60). */
        ((uint8_t *)(rec + 0x18))[0] = 1;
    }

    /* (8b) Single FUN_0047a8c0 call covers all 8 chara records at
     * once. The engine's per-iter call inside the loop is wasteful
     * (the same work overwritten 7 times); we collapse to one call. */
    apply_chara_interp(bank_bytes);

    /* (9) FUN_0048ff93 — starter items for all 8 charas. Reads the
     * 40-dword STARTER_ITEMS table and writes encoded slot dwords
     * into the bank's starter-inventory windows. */
    apply_starter_items(bank_bytes);

    /* (10) Stamp checksum so future verify passes accept this bank. */
    save_bank_stamp_checksum(bank_idx);
}

/* ── FUN_004901c2 body ── */

void save_bank_init_all(void)
{
    uint32_t *header = header_dwords();

    /* (1) Shared-header init on magic mismatch. */
    if (header[0] != SAVE_BANK_MAGIC) {
        /* Zero the entire arena (header + all 100 banks). Engine
         * loops 0x47dd4c dwords starting at DAT_056e5770 == base. */
        memset(g_arena, 0, sizeof g_arena);

        /* Engine writes `_DAT_056e5780 = 0` explicitly (already zero
         * from memset; preserved for fidelity). */
        header[4] = 0;

        header[0] = SAVE_BANK_MAGIC;
        header[1] = SAVE_HEADER_SE_DEFAULT;
        header[3] = SAVE_HEADER_SE_B_DEFAULT;
        header[2] = SAVE_HEADER_BGM_DEFAULT;
        header[5] = SAVE_HEADER_SLIDER3_DEFAULT;

        /* Engine calls FUN_00499583() here — that's the BGM
         * SetVolume re-apply, since the BGM slider just changed.
         * audio.c installs a hook so save_bank doesn't link against
         * the audio backend directly. */
        if (g_header_init_hook) {
            g_header_init_hook();
        }
    }

    /* (2) Per-bank checksum verify; reset any bank that fails. The
     * engine gates this on `DAT_095d3728 == 0` — a "skip the verify
     * sweep" flag set elsewhere. We default to running the sweep
     * always; it's idempotent (live banks stay live) and the gate
     * isn't load-bearing for any current consumer. */
    for (int idx = 0; idx < SAVE_BANK_COUNT; idx++) {
        uint32_t *bank = save_bank_dwords_at(idx);
        int ok = (bank[SAVE_BANK_FIELD_MAGIC] == SAVE_BANK_MAGIC)
              && save_bank_checksum_ok(idx);
        if (!ok) {
            save_bank_init_one(idx);
        }
    }
}
