/*
 * chara_skills.c — port of FUN_004360b6.
 *
 * See chara_skills.h for the subsystem writeup.  Source-level
 * reference: docs/decompiled/by-address/4360b6.c (202 B).
 */

#include "chara_skills.h"

#include <string.h>

#include "call_trace.h"
#include "chara_equip.h"

/* ─── Per-chara skill state ─────────────────────────────────────────
 *
 * Engine VAs preserved in the comments for cross-reference.
 */

/* DAT_0438b7dc[8] — passed-threshold count per chara. */
static int32_t g_dat_0438b7dc[CHARA_SKILLS_CHARA_COUNT];

/* DAT_0438b7fc[8][5] — skill-slot index list per chara. */
static int32_t g_dat_0438b7fc[CHARA_SKILLS_CHARA_COUNT]
                             [CHARA_SKILLS_SLOTS_PER];

/* Three tail-write scratch DATs. */
static int32_t g_dat_0438b874;
static int32_t g_dat_0438b878;
static int32_t g_dat_0438b87c;

/* ─── RDATA: 8-row × 6-dword level threshold table at 0x5c5060 ──────
 *
 * Per-row layout: [count, threshold[0], ..., threshold[4]].
 *
 * Verified via objdump @ vendor/unpacked/recettear.unpacked.exe:
 *
 *   5c5060  02 00 00 00 00 00 00 00 09 00 00 00 00 00 00 00
 *   5c5070  00 00 00 00 00 00 00 00 03 00 00 00 00 00 00 00
 *   5c5080  09 00 00 00 13 00 00 00 00 00 00 00 00 00 00 00
 *   5c5090  05 00 00 00 00 00 00 00 04 00 00 00 09 00 00 00
 *   5c50a0  18 00 00 00 1d 00 00 00 04 00 00 00 00 00 00 00
 *   5c50b0  04 00 00 00 13 00 00 00 18 00 00 00 00 00 00 00
 *   5c50c0  03 00 00 00 00 00 00 00 09 00 00 00 13 00 00 00
 *   5c50d0  00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00
 *   5c50e0  09 00 00 00 13 00 00 00 1d 00 00 00 00 00 00 00
 *   5c50f0  03 00 00 00 00 00 00 00 09 00 00 00 13 00 00 00
 *   5c5100  00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
 *   5c5110  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * Engine reads `count` thresholds starting at row offset +4 (skipping
 * unused entries).  Per-chara level thresholds:
 *
 *   chara 0  count=2  thresholds [0, 9]
 *   chara 1  count=3  thresholds [0, 9, 19]
 *   chara 2  count=5  thresholds [0, 4, 9, 24, 29]
 *   chara 3  count=4  thresholds [0, 4, 19, 24]
 *   chara 4  count=3  thresholds [0, 9, 19]
 *   chara 5  count=4  thresholds [0, 9, 19, 29]
 *   chara 6  count=3  thresholds [0, 9, 19]
 *   chara 7  count=1  thresholds [0]
 *
 * Entries past `count` are unread by the engine (per the L33182-92
 * inner loop's `local_10` countdown), so 0-filled trailing slots
 * here are documentation only. */
static const int32_t kCharaSkillThresholds[CHARA_SKILLS_CHARA_COUNT][6] = {
    /* {count, t0, t1, t2,  t3,  t4 } */
    {  2,  0,  9,  0,  0,  0 },
    {  3,  0,  9, 19,  0,  0 },
    {  5,  0,  4,  9, 24, 29 },
    {  4,  0,  4, 19, 24,  0 },
    {  3,  0,  9, 19,  0,  0 },
    {  4,  0,  9, 19, 29,  0 },
    {  3,  0,  9, 19,  0,  0 },
    {  1,  0,  0,  0,  0,  0 },
};

/* ─── FUN_004360b6 body ────────────────────────────────────────────── */

void chara_skills_init_at_stage_load(void)
{
    CALL_TRACE_ENTER(0x4360b6u);

    const int32_t bank = chara_equip_get_current_bank();

    /* Pass 1: per-chara init + threshold count. */
    for (int chara = 0; chara < CHARA_SKILLS_CHARA_COUNT; chara++) {
        /* Engine: `piVar2[0x18] = (int)&DAT_01010101` + byte write at
         * +0x19's first byte → 5 byte writes to record bytes
         * +0x60..+0x64.  Equivalent byte-by-byte writes here, going
         * through chara_equip's record buffer. */
        for (int b = 0; b < 5; b++) {
            chara_equip_set_record_byte(bank, chara, 0x60 + b, 1);
        }

        /* Initialise skill-slot list to {0, 1, 2, 3, 4}. */
        for (int s = 0; s < CHARA_SKILLS_SLOTS_PER; s++) {
            g_dat_0438b7fc[chara][s] = s;
        }

        /* Count thresholds met by chara's current level. */
        int32_t count = kCharaSkillThresholds[chara][0];
        int32_t level = chara_equip_get_chara_level(bank, chara);
        g_dat_0438b7dc[chara] = 0;
        for (int i = 0; i < count; i++) {
            /* Engine reads `piVar4[1..count]` (the threshold entries).
             * RDATA row layout: [count, t0, t1, t2, t3, t4]. */
            int32_t threshold = kCharaSkillThresholds[chara][i + 1];
            if (threshold <= level) {
                g_dat_0438b7dc[chara] += 1;
            }
        }
    }

    /* Pass 2: rewrite the first `count` slots of each chara's slot
     * list with {0, 1, ..., count-1}.  For the engine's RDATA values
     * this is semantically a no-op (pass 1 already wrote those
     * exact values), but the engine emits the rewrite unconditionally
     * — preserved for layout fidelity. */
    for (int chara = 0; chara < CHARA_SKILLS_CHARA_COUNT; chara++) {
        int32_t cnt = g_dat_0438b7dc[chara];
        if (cnt > CHARA_SKILLS_SLOTS_PER) cnt = CHARA_SKILLS_SLOTS_PER;
        for (int i = 0; i < cnt; i++) {
            g_dat_0438b7fc[chara][i] = i;
        }
    }

    /* Tail scratch writes. */
    g_dat_0438b874 = 0;
    g_dat_0438b878 = 2;
    g_dat_0438b87c = 1;
}

/* ─── Accessors ────────────────────────────────────────────────────── */

int32_t chara_skills_get_count(int chara_idx)
{
    if (chara_idx < 0 || chara_idx >= CHARA_SKILLS_CHARA_COUNT) return 0;
    return g_dat_0438b7dc[chara_idx];
}

int32_t chara_skills_get_slot(int chara_idx, int slot_idx)
{
    if (chara_idx < 0 || chara_idx >= CHARA_SKILLS_CHARA_COUNT) return 0;
    if (slot_idx  < 0 || slot_idx  >= CHARA_SKILLS_SLOTS_PER)   return 0;
    return g_dat_0438b7fc[chara_idx][slot_idx];
}

int32_t chara_skills_get_dat_0438b874(void) { return g_dat_0438b874; }
int32_t chara_skills_get_dat_0438b878(void) { return g_dat_0438b878; }
int32_t chara_skills_get_dat_0438b87c(void) { return g_dat_0438b87c; }

void chara_skills_reset_for_test(void)
{
    memset(g_dat_0438b7dc, 0, sizeof(g_dat_0438b7dc));
    memset(g_dat_0438b7fc, 0, sizeof(g_dat_0438b7fc));
    g_dat_0438b874 = 0;
    g_dat_0438b878 = 0;
    g_dat_0438b87c = 0;
}
