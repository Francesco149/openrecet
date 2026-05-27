/*
 * stage_gate.c — see stage_gate.h for the chip writeup.
 *
 * Three pure functions, port-faithful to the engine bodies:
 *
 *   FUN_00431990 @ 0x431990  → stage_gate_boss_id_allowed
 *   FUN_0043195d @ 0x43195d  → stage_gate_floor_is_checkpoint
 *   FUN_004319d6 @ 0x4319d6  → stage_gate_query
 */

#include "stage_gate.h"

#include "call_trace.h"
#include "scene1_combat_sm.h"   /* g_scene1_combat_stage_id (DAT_0438b4c8) */
#include "tables_enemylist.h"   /* g_enemylist.sections (DAT_0053f8e8) */

/* DAT_0438b4cc — next-floor id stand-in.  Engine writes this during
 * scene transitions; no port writer yet.  BSS-zero default. */
static int32_t g_stage_gate_next = 0;

int32_t stage_gate_get_next(void)             { return g_stage_gate_next; }
void    stage_gate_set_next(int32_t next)     { g_stage_gate_next = next; }

/* ─── FUN_00431990 — pure boss-id range predicate ───────────────────────
 *
 * Engine decomp:
 *
 *   if (p < 0x2c) {
 *     if ((p != 0x2b) &&
 *        ((p < 0x17 || ((0x19 < p && ((p < 0x1b ||
 *         ((0x1c < p && (p != 0x29))))))))))
 *       return 0;
 *   } else if (p != 0x31) {
 *     if (p < 0x36) return 0;
 *     if (0x37 < p) {
 *       if (p < 0x3b) return 0;
 *       if (0x49 < p) return 0;
 *     }
 *   }
 *   return 1;
 *
 * Distilled set of "true" values: 0x17,0x18,0x19, 0x1b,0x1c, 0x29,
 * 0x2b, 0x31, 0x36,0x37, 0x3b..0x49.  All other values → 0.
 *
 * Body kept in the same shape as decomp to ease 1:1 review.
 */
int stage_gate_boss_id_allowed(int32_t enemy_id)
{
    /* E.2 probe — FUN_00431990 @ 0x431990. */
    CALL_TRACE_ENTER(0x431990u);

    if (enemy_id < 0x2c) {
        if ((enemy_id != 0x2b) &&
            ((enemy_id < 0x17) ||
             ((0x19 < enemy_id) &&
              ((enemy_id < 0x1b) ||
               ((0x1c < enemy_id) && (enemy_id != 0x29)))))) {
            return 0;
        }
    } else if (enemy_id != 0x31) {
        if (enemy_id < 0x36) {
            return 0;
        }
        if (0x37 < enemy_id) {
            if (enemy_id < 0x3b) {
                return 0;
            }
            if (0x49 < enemy_id) {
                return 0;
            }
        }
    }
    return 1;
}

/* ─── FUN_0043195d — checkpoint-floor predicate ─────────────────────────
 *
 * Engine decomp:
 *
 *   if (DAT_0438b4c8 != 5)  return DAT_0438b4cc % 5 == 4;
 *   if (DAT_0438b4cc == 0x1d) return true;
 *   return 0x1d < DAT_0438b4cc;
 *
 * Reads two globals, no args, returns 0/1.
 */
int stage_gate_floor_is_checkpoint(void)
{
    /* E.2 probe — FUN_0043195d @ 0x43195d. */
    CALL_TRACE_ENTER(0x43195du);

    if (g_scene1_combat_stage_id != 5) {
        return (g_stage_gate_next % 5) == 4;
    }
    if (g_stage_gate_next == 0x1d) {
        return 1;
    }
    return 0x1d < g_stage_gate_next;
}

/* ─── FUN_004319d6 — outer gate ─────────────────────────────────────────
 *
 * Engine decomp (paraphrased):
 *
 *   (1) Hard-coded special transitions:
 *         (0 → 4):     return 1
 *         (4 → 0x1d):  return 1
 *         (4 → 99):    return 1
 *   (2) if (!FUN_0043195d()) return 0
 *   (3) Walk g_enemylist.sections[cur][0..59] for a section whose
 *       [floor_lo, floor_hi] contains next_floor.
 *       Termination: floor_lo == -1 sentinel → return 0; or 60 walked.
 *   (4) Walk that section's enemies[0..30] for the first enemy_id
 *       passing stage_gate_boss_id_allowed.  Termination: enemy_id ==
 *       -1 sentinel → return 0; or 31 walked.
 *   (5) Return 1 if found.
 *
 * The decomp has a dead `if (iVar1 * 0x2f0 == -0x53f8e8) return 0;`
 * branch between (3) and (4); see header note — skipped in the port
 * since it can never trigger with bounded indices.
 *
 * Defensive: if the engine ever sets cur dungeon id out of [0, 9]
 * range, we clamp to a safe "no boss" return (the engine would walk
 * off the table; we just return 0 — there's no observable engine
 * behaviour for an out-of-range dungeon id to preserve).
 */
int stage_gate_query(void)
{
    /* E.2 probe — FUN_004319d6 @ 0x4319d6. */
    CALL_TRACE_ENTER(0x4319d6u);

    const int32_t cur  = g_scene1_combat_stage_id;
    const int32_t next = g_stage_gate_next;

    /* (1) Hard-coded special transitions. */
    if (cur == 0) {
        if (next == 4) {
            return 1;
        }
    } else if (cur == 4) {
        if (next == 0x1d) {
            return 1;
        }
        if (next == 99) {
            return 1;
        }
    }

    /* (2) Checkpoint predicate. */
    if (!stage_gate_floor_is_checkpoint()) {
        return 0;
    }

    /* Defensive bounds check — engine indexes 10 dungeon slots and
     * never validates; we mirror the engine's "out-of-range = no
     * boss" effect explicitly to keep the access safe. */
    if (cur < 0 || cur >= ENEMYLIST_DUNGEON_SLOTS) {
        return 0;
    }

    /* (3) Outer walker: find section containing `next` in cur dungeon. */
    int section_idx = 0;
    for (; section_idx < ENEMYLIST_SECTIONS_PER_DUNGEON; ++section_idx) {
        const enemylist_section_t *s =
            &g_enemylist.sections[cur][section_idx];
        if (s->floor_lo == -1) {
            return 0;
        }
        if ((s->floor_lo <= next) && (next <= s->floor_hi)) {
            break;
        }
    }
    if (section_idx == ENEMYLIST_SECTIONS_PER_DUNGEON) {
        return 0;
    }

    /* (4) Inner walker: look for a boss-eligible enemy_id in the
     *     section's enemies[] table.  Engine cap is 0x1f = 31 — exactly
     *     ENEMYLIST_ENEMY_SLOTS_PER_SECTION. */
    const enemylist_section_t *sec = &g_enemylist.sections[cur][section_idx];
    for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; ++k) {
        const int32_t id = sec->enemies[k].enemy_id;
        if (id == -1) {
            return 0;
        }
        if (stage_gate_boss_id_allowed(id)) {
            return 1;
        }
    }
    return 0;
}
