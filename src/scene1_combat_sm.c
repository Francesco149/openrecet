/*
 * scene1_combat_sm.c — per-record state machine (combat tick).
 *
 * Engine source: FUN_0043865e @ 0x43865e.  Chip C8jb.2 extends C8jb.1's
 * Phase A entry-gate skeleton with Phase B head: the attacker NPC scan
 * iteration shell.  See scene1_combat_sm.h and
 * docs/findings/scene1-records-b-state-machine.md.
 *
 * Asm verification for C8jb.2 (re-runnable):
 *   nix develop --command i686-w64-mingw32-objdump -d -M intel \
 *       --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
 *       --start-address=0x4386e0 --stop-address=0x438762
 *
 * Confirms (against decomp all.c L35186-L35226):
 *   - iter base   = DAT_0076c478 (people-table entry 0 byte +0x724)
 *   - iter stride = 0xba4 B (= 0x2e9 dw)
 *   - iter bound  = DAT_007c9678 (= 128 records)
 *   - record_base = local_30 - 0xb08 B (asm `lea esi, [ecx-0xb08]`)
 *   - gate 1: `[ecx-0x14]   > 0`        → record byte +0x710 (combat_cooldown_5)
 *   - gate 2: `[ecx]       != 0`        → record byte +0x724 (sister_724)
 *   - gate 3 (target-lock; only if slot[FLAG_A]==3):
 *             `[edi+0x14] != 0 && [edi+0x14] == ecx-0xb08`
 *                                          slot[OWNER_B] == record_base
 *   - gate 4: `[ecx-0x6e0] == 1`
 *             `|| ([ecx-0x6e0] == 2 && [ecx+0x90] != 0)`
 *                                          alive ∈ {1, (2 ∧ alias_24)}
 *   - hit-history: scan `[ecx+0x54..0x78]` (10 dwords) for slot[SEQ_ID];
 *             if NOT found (`9 < iVar8`), NPC is a hit candidate.
 */
#include "scene1_combat_sm.h"

#include <stddef.h>
#include <stdint.h>

#include "scene1_particles_tick.h"   /* g_scene1_people, SCENE1_PEOPLE_COUNT */
#include "scene1_records.h"          /* SCENE1_RECORDS_B_OFF_* */
#include "scene1_records_b_tick.h"   /* g_scene1_records_b_tick_flag */
#include "scene1_sim.h"              /* g_scene1_ingame_paused_flag */

int32_t g_scene1_combat_subphase;    /* DAT_0438be98 */
int32_t g_scene1_combat_world_pause; /* DAT_0438be9c */
int32_t g_scene1_combat_aux_pause;   /* DAT_0438bea0 */

float   g_scene1_combat_player_hp;   /* _DAT_056db0bc */
int32_t g_scene1_combat_phase_b_visit_count;

static scene1_combat_phase_b_visit_fn g_phase_b_visit_hook;

scene1_combat_phase_b_visit_fn
scene1_combat_set_phase_b_visit_hook(scene1_combat_phase_b_visit_fn fn)
{
    scene1_combat_phase_b_visit_fn prev = g_phase_b_visit_hook;
    g_phase_b_visit_hook = fn;
    return prev;
}

static int phase_b_npc_passes_skip_gates(const scene1_people_entry_t *npc,
                                         int32_t slot_state,
                                         int32_t slot_owner_b_int)
{
    /* Gate 1: per-NPC cooldown countdown — engine `[ecx-0x14] > 0` skip. */
    if (npc->combat_cooldown_5 > 0) return 0;

    /* Gate 2: NPC type-0 sentinel — engine `[ecx] != 0` skip.  In our
     * port `[ecx]` is sister_724 (the people-table "iter cursor"
     * field).  Engine treats nonzero as "this slot is interactable /
     * not a hit target this tick". */
    if (npc->sister_724 != 0) return 0;

    /* Gate 3: target-lock — when the slot is in hit-recovery (state 3)
     * AND its OWNER_B points to this exact NPC's record_base, skip.
     * Engine: `[edi+4]==3 && [edi+0x14]!=0 && [edi+0x14]==ecx-0xb08`.
     *
     * Port pointer convention: scene1_record_b_spawn_npc stores OWNER_B
     * as `(int32_t)(intptr_t)owner`.  Comparing the int form against
     * `(int32_t)(intptr_t)npc` is equivalent under our 32/64-bit host
     * (low 32 of the pointer).  Tests that exercise this gate cast a
     * `scene1_people_entry_t *` to int32_t and store it as OWNER_B. */
    if (slot_state == 3
        && slot_owner_b_int != 0
        && slot_owner_b_int == (int32_t)(intptr_t)npc) {
        return 0;
    }

    /* Gate 4: NPC state filter — engine `[ecx-0x6e0]` (= record +0x44 =
     * `alive`).  Pass if `alive == 1` OR (`alive == 2` AND
     * `alias_24 != 0`).  All other values skip. */
    if (npc->alive == 1) {
        return 1;
    }
    if (npc->alive == 2 && npc->alive_alias_24 != 0) {
        return 1;
    }
    return 0;
}

static int phase_b_npc_in_hit_history(const scene1_people_entry_t *npc,
                                      int32_t slot_seq_id)
{
    /* Engine: linear scan [ecx+0x54..0x78] (10 dwords).  If slot's
     * SEQ_ID matches any entry, break and skip the NPC (already hit).
     * Decomp: `if (9 < iVar8) {<collision math>}` — i.e., only proceed
     * to collision when iVar8 reached 10 (no match found). */
    for (int k = 0; k < 10; k++) {
        if (npc->hit_history[k] == slot_seq_id) {
            return 1;
        }
    }
    return 0;
}

static void phase_b_scan(int32_t *slot)
{
    /* Phase B outer gate (engine L35186):
     *   ((slot[FLAG_A] == 0 || slot[FLAG_A] == 3)
     *    && 0.0 < _DAT_056db0bc) */
    int32_t state = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    if (state != 0 && state != 3) return;
    if (!(g_scene1_combat_player_hp > 0.0f)) return;

    int32_t owner_b_int = slot[SCENE1_RECORDS_B_OFF_OWNER_B];
    int32_t seq_id      = slot[SCENE1_RECORDS_B_OFF_SEQ_ID];

    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) {
        const scene1_people_entry_t *npc = &g_scene1_people[i];

        if (!phase_b_npc_passes_skip_gates(npc, state, owner_b_int)) {
            continue;
        }
        if (phase_b_npc_in_hit_history(npc, seq_id)) {
            continue;
        }

        /* NPC is a "would-collide" hit candidate.  C8jb.3 plugs in the
         * distance + AABB-Y collision math here.  C8jb.2 just records
         * the visit. */
        g_scene1_combat_phase_b_visit_count++;
        if (g_phase_b_visit_hook != NULL) {
            g_phase_b_visit_hook(i);
        }
    }
}

int scene1_combat_sm_tick(int32_t *slot)
{
    /* Engine L35173-L35181 — early-exit gates.  Order matches engine. */
    if (g_scene1_combat_subphase    > 0)  return 0;
    if (g_scene1_combat_world_pause > 0)  return 0;
    if (g_scene1_combat_aux_pause   > 0)  return 0;
    if (g_scene1_ingame_paused_flag != 0) return 0;

    /* Engine L35185 — `DAT_06a46f98 = 1`.  Resolves PHC #21. */
    g_scene1_records_b_tick_flag = 1;

    /* Reset Phase B observable counter at every fall-through entry. */
    g_scene1_combat_phase_b_visit_count = 0;

    /* Phase B head — attacker NPC scan.  Skipped if slot is NULL
     * (Phase A tests use NULL to probe gates without prepping a slot). */
    if (slot != NULL) {
        phase_b_scan(slot);
    }

    /* Phases C/D stub — return 0 unconditionally for C8jb.2. */
    return 0;
}

/* ─── void-hook adapter (test-only convenience) ──────────────────────── */
/*
 * The existing scene1_records_b_set_state_machine_hook accepts
 * `void (*)(int32_t *)`.  We adapt scene1_combat_sm_tick by discarding
 * the int return.  C8jb.fin replaces this adapter with an int-ret
 * variant.
 */
static void combat_sm_void_adapter(int32_t *slot)
{
    (void)scene1_combat_sm_tick(slot);
}

void scene1_combat_sm_install_as_void_hook(void)
{
    scene1_records_b_set_state_machine_hook(combat_sm_void_adapter);
}

void scene1_combat_sm_uninstall_void_hook(void)
{
    scene1_records_b_set_state_machine_hook(NULL);
}
