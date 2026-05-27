/*
 * chara_equip.c — port of FUN_004844ef + FUN_0048093f.
 *
 * See chara_equip.h for the subsystem writeup.  Source-level
 * reference: docs/decompiled/by-address/4844ef.c (310 B aggregator)
 * and docs/decompiled/by-address/48093f.c (136 B slot distributor).
 */

#include "chara_equip.h"

#include <string.h>

#include "call_trace.h"
#include "scene1_combat_sm.h"  /* g_scene1_combat_damage_base_idle{,_2} */
#include "tables_item.h"

/* ─── Active bank/chara selectors ──────────────────────────────────── */

static int32_t g_dat_0438b1e0;  /* current bank/stage index */
static int32_t g_dat_0438b7d8;  /* active chara within bank */

void chara_equip_set_current_bank(int32_t bank_idx)
{
    g_dat_0438b1e0 = bank_idx;
}
int32_t chara_equip_get_current_bank(void)
{
    return g_dat_0438b1e0;
}
void chara_equip_set_current_chara(int32_t chara_idx)
{
    g_dat_0438b7d8 = chara_idx;
}
int32_t chara_equip_get_current_chara(void)
{
    return g_dat_0438b7d8;
}

/* ─── Per-(bank, chara) record storage ─────────────────────────────── */

/* Flat byte buffer matching engine layout — `bank * 0x2dfc8 + chara *
 * 0x6c + offset` indexing.  Engine allocates 0x2dfc8 per bank; the port
 * only models the per-chara 0x6c portion, since no caller reads outside
 * the 0..0x3b range we model.  Compact: 1 bank * 8 chara * 0x6c =
 * 0x360 bytes (864).  Expand BANK_COUNT when save-load lands. */
static uint8_t g_record_bytes[CHARA_EQUIP_BANK_COUNT]
                             [CHARA_EQUIP_CHARA_COUNT]
                             [CHARA_EQUIP_RECORD_BYTES];

static uint8_t *record_at(int32_t bank, int32_t chara)
{
    if (bank  < 0 || bank  >= CHARA_EQUIP_BANK_COUNT)  return NULL;
    if (chara < 0 || chara >= CHARA_EQUIP_CHARA_COUNT) return NULL;
    return &g_record_bytes[bank][chara][0];
}

uint32_t chara_equip_get_slot(int32_t bank, int32_t chara, int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= CHARA_EQUIP_SLOT_COUNT) return 0;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return 0;
    uint32_t out;
    memcpy(&out, rec + 0x04 + slot_idx * 4, sizeof(out));
    return out;
}

void chara_equip_set_slot(int32_t bank, int32_t chara, int slot_idx,
                          uint32_t slot_val)
{
    if (slot_idx < 0 || slot_idx >= CHARA_EQUIP_SLOT_COUNT) return;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return;
    memcpy(rec + 0x04 + slot_idx * 4, &slot_val, sizeof(slot_val));
}

int32_t chara_equip_get_base_stat(int32_t bank, int32_t chara, int stat_idx)
{
    if (stat_idx < 0 || stat_idx >= CHARA_EQUIP_STAT_COUNT) return 0;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return 0;
    int32_t out;
    memcpy(&out, rec + 0x2c + stat_idx * 4, sizeof(out));
    return out;
}

void chara_equip_set_base_stat(int32_t bank, int32_t chara, int stat_idx,
                               int32_t value)
{
    if (stat_idx < 0 || stat_idx >= CHARA_EQUIP_STAT_COUNT) return;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return;
    memcpy(rec + 0x2c + stat_idx * 4, &value, sizeof(value));
}

int32_t chara_equip_get_chara_level(int32_t bank, int32_t chara)
{
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return 0;
    int32_t out;
    memcpy(&out, rec, sizeof(out));
    return out;
}

void chara_equip_set_chara_level(int32_t bank, int32_t chara, int32_t level)
{
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return;
    memcpy(rec, &level, sizeof(level));
}

uint8_t chara_equip_get_record_byte(int32_t bank, int32_t chara,
                                    int byte_offset)
{
    if (byte_offset < 0 || byte_offset >= CHARA_EQUIP_RECORD_BYTES) return 0;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return 0;
    return rec[byte_offset];
}

void chara_equip_set_record_byte(int32_t bank, int32_t chara,
                                 int byte_offset, uint8_t value)
{
    if (byte_offset < 0 || byte_offset >= CHARA_EQUIP_RECORD_BYTES) return;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return;
    rec[byte_offset] = value;
}

uint32_t chara_equip_get_record_dword(int32_t bank, int32_t chara,
                                      int byte_offset)
{
    if (byte_offset < 0 || byte_offset + 4 > CHARA_EQUIP_RECORD_BYTES) return 0;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return 0;
    uint32_t out;
    memcpy(&out, rec + byte_offset, sizeof(out));
    return out;
}

void chara_equip_set_record_dword(int32_t bank, int32_t chara,
                                  int byte_offset, uint32_t value)
{
    if (byte_offset < 0 || byte_offset + 4 > CHARA_EQUIP_RECORD_BYTES) return;
    uint8_t *rec = record_at(bank, chara);
    if (rec == NULL) return;
    memcpy(rec + byte_offset, &value, sizeof(value));
}

/* ─── Scratch DATs zeroed/written by the aggregator ─────────────────
 *
 * Engine zeros 056db074..056db08c (7 dwords with mixed _DAT_/DAT_
 * notation, suggesting some are 64-bit aliases of smaller named
 * symbols) + a 24-byte byte field at 056db090..056db0a7 + the
 * 4-byte _DAT_074b2ec0, then writes _DAT_056db0a8 = 5.  The four
 * dwords at 056db0ac..056db0b8 hold the aggregated sum.
 *
 * Of these, only 056db0ac (idle2) and 056db0b4 (idle) are read by
 * already-ported code (scene1_combat_sm).  The rest are write-only on
 * the NEW-GAME path.  Allocate the read-only-by-engine ones as
 * module-locals; access via the public getter for tests. */
static int32_t g_dat_056db074;
static int32_t g_dat_056db078;
static int32_t g_dat_056db07c;
static int32_t g_dat_056db080;
static int32_t g_dat_056db084;
static int32_t g_dat_056db088;
static int32_t g_dat_056db08c;
static uint8_t g_dat_056db090[24];   /* 056db090..056db0a7 */
static int32_t g_dat_056db0a8;       /* counter, set to 5 */
static int32_t g_dat_074b2ec0;
static int32_t g_dat_056db0b0;       /* sum[1] */
static int32_t g_dat_056db0b8;       /* sum[3] */

int32_t chara_equip_get_dat_056db0a8(void)
{
    return g_dat_056db0a8;
}

int32_t chara_equip_get_aggregate_stat(int idx)
{
    switch (idx) {
        case 0: return g_scene1_combat_damage_base_idle2;
        case 1: return g_dat_056db0b0;
        case 2: return g_scene1_combat_damage_base_idle;
        case 3: return g_dat_056db0b8;
        default: return 0;
    }
}

static void store_aggregate_stat(int idx, int32_t value)
{
    switch (idx) {
        case 0: g_scene1_combat_damage_base_idle2 = value; break;
        case 1: g_dat_056db0b0                    = value; break;
        case 2: g_scene1_combat_damage_base_idle  = value; break;
        case 3: g_dat_056db0b8                    = value; break;
        default: break;
    }
}

/* ─── FUN_0048093f — per-slot stat distributor ───────────────────────
 *
 * Engine body (docs/decompiled/by-address/48093f.c):
 *   - Bail on sentinel slot (0xffffffff = "empty").
 *   - Lookup encoded item_id (`(int)slot_val >> 6` — engine uses arithmetic
 *     shift, but only the low 26 bits of slot_val carry the id, so the
 *     sign bit is set only when the slot is decoded as a huge negative
 *     id; in practice slots are positive small ids → unsigned-equivalent
 *     shift).  Result `record_idx`:
 *       hit  → 0..g_item.count-1
 *       miss → -1 (engine UB: reads `&records[0].attack + (-1)*0x2cc`
 *                  = OOB into prior .data; port skips the sum).
 *   - For 4 consecutive dwords of the matched record (atk/def/matk/mdef
 *     at item record offsets +0x8..+0x14):
 *       sum[i]      += item.stat[i];
 *       local_14[i]  = item.stat[i];
 *   - Walk local_14[0..3] tracking the index of the maximum (-1 if all
 *     are <= 0 — engine init iVar1 = -1, iVar5 = 0; only replace on
 *     strict `>`).
 *   - If a positive-max stat was found, add (slot_val & 0xf) to
 *     sum[max_idx] — the equipment's enchantment-level bonus piled onto
 *     the dominant stat.
 */
static void distribute_slot_stats(uint32_t slot_val, int32_t sum[4])
{
    CALL_TRACE_ENTER(0x48093fu);

    if (slot_val == CHARA_EQUIP_SLOT_EMPTY) {
        return;
    }

    /* Engine uses arithmetic right shift; positive slot values keep the
     * same numeric meaning either way. */
    int32_t item_id = (int32_t)slot_val >> 6;
    int32_t record_idx = tables_item_find_slot_by_id(&g_item, item_id);

    /* Miss: engine reads OOB at record_idx == -1 (an items.txt was
     * always present in the engine binary, so this branch was unreached
     * for vanilla data).  Port chooses "skip this slot" — same effect
     * as a zero-stats item, well-defined under all valid item DBs.
     * Documented as a known divergence on the rare miss path. */
    if (record_idx < 0) {
        return;
    }

    int32_t local_stats[4] = { 0, 0, 0, 0 };
    int32_t a = g_item.records[record_idx].attack;
    int32_t d = g_item.records[record_idx].defense;
    int32_t ma = g_item.records[record_idx].magic_attack;
    int32_t md = g_item.records[record_idx].magic_defense;
    sum[0] += a;  local_stats[0] = a;
    sum[1] += d;  local_stats[1] = d;
    sum[2] += ma; local_stats[2] = ma;
    sum[3] += md; local_stats[3] = md;

    /* Find max — engine uses `iVar5 < *piVar3` (strict less), so ties
     * keep the first-found maximum.  iVar1 init = -1 means "no positive
     * stat" stays -1 (and a sum of all zero stays unmodified). */
    int32_t max_val = 0;
    int     max_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (max_val < local_stats[i]) {
            max_idx = i;
            max_val = local_stats[i];
        }
    }
    if (max_idx >= 0) {
        sum[max_idx] += (int32_t)(slot_val & 0xfu);
    }
}

/* ─── FUN_004844ef — full chara stat aggregator ─────────────────────── */

void chara_equip_recompute_aggregate(void)
{
    CALL_TRACE_ENTER(0x4844efu);

    /* Zero the scratch DATs.  Layout matches the decomp's order exactly
     * — see chara_equip.h "Aggregator scratch" section for the read
     * audience of each. */
    g_dat_056db074 = 0;
    g_dat_056db078 = 0;
    g_dat_056db07c = 0;
    g_dat_056db080 = 0;
    g_dat_056db084 = 0;
    g_dat_056db088 = 0;
    g_dat_056db08c = 0;
    memset(g_dat_056db090, 0, sizeof(g_dat_056db090));
    g_dat_074b2ec0 = 0;
    g_dat_056db0a8 = 5;

    /* The sum buffer is a local — same as engine's local_18[0..3]. */
    int32_t sum[4] = { 0, 0, 0, 0 };

    /* Loop 5 slots: read each slot's encoded dword from the active
     * (bank, chara) record at offset +0x4 + i*4, call the distributor.
     * On a record-out-of-range bank/chara (shouldn't happen on NEW
     * GAME), the get/set helpers return 0 silently → all 5 calls fire
     * with slot_val = 0. */
    int32_t bank  = g_dat_0438b1e0;
    int32_t chara = g_dat_0438b7d8;
    for (int i = 0; i < CHARA_EQUIP_SLOT_COUNT; i++) {
        uint32_t slot_val = chara_equip_get_slot(bank, chara, i);
        distribute_slot_stats(slot_val, sum);
    }

    /* Add per-chara base stats (offset 0x2c..0x3b, 4 dwords) onto the
     * sum, then publish into the aggregated stat DATs. */
    for (int i = 0; i < CHARA_EQUIP_STAT_COUNT; i++) {
        int32_t base = chara_equip_get_base_stat(bank, chara, i);
        store_aggregate_stat(i, sum[i] + base);
    }
}

/* ─── Test helper ─────────────────────────────────────────────────── */

void chara_equip_reset_for_test(void)
{
    g_dat_0438b1e0 = 0;
    g_dat_0438b7d8 = 0;
    memset(g_record_bytes, 0, sizeof(g_record_bytes));
    g_dat_056db074 = 0;
    g_dat_056db078 = 0;
    g_dat_056db07c = 0;
    g_dat_056db080 = 0;
    g_dat_056db084 = 0;
    g_dat_056db088 = 0;
    g_dat_056db08c = 0;
    memset(g_dat_056db090, 0, sizeof(g_dat_056db090));
    g_dat_056db0a8 = 0;
    g_dat_074b2ec0 = 0;
    g_dat_056db0b0 = 0;
    g_dat_056db0b8 = 0;
    g_scene1_combat_damage_base_idle  = 0;
    g_scene1_combat_damage_base_idle2 = 0;
}
