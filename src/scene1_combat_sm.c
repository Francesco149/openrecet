/*
 * scene1_combat_sm.c — per-record state machine (combat tick).
 *
 * Engine source: FUN_0043865e @ 0x43865e.  Chip C8jb.3 extends C8jb.2's
 * Phase B head with the nested per-NPC sub-iter loop (1/7/2 iters by
 * NPC type), the position lookup (npc.combat_pose OR rdata-indexed
 * anchor), and the 2D-XZ distance + AABB Y-band collision check.  See
 * scene1_combat_sm.h and docs/findings/scene1-records-b-state-machine.md.
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
 *
 * Asm verification for C8jb.3 (re-runnable):
 *   nix develop --command i686-w64-mingw32-objdump -d -M intel \
 *       --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
 *       --start-address=0x438762 --stop-address=0x438880
 *
 * Confirms (against decomp all.c L35203-L35260):
 *   - sub-iter count: NPC type 0x44/0x45 → 7; 0x46/0x47 → 2; else → 1.
 *     (Engine asm 0x438762-0x43879f reads NPC type from `[esi+0x424]`
 *      and switches `[ebp+0x8]` accordingly.)
 *   - default pose: npc.combat_pose (record bytes 0x3e4/0x3e8/0x3ec,
 *     asm 0x4387b2-0x4387d7) and reach radius scalar at +0xabc
 *     (asm 0x4387da).
 *   - per-sub-iter override: if NPC type ∈ {0x46, 0x47}, anchor index
 *     read from DAT_005c530c[sub_iter]; else if sub_iter > 0, from
 *     DAT_005c5314[sub_iter] AND local_18 flag set to 1.
 *   - rdata tables (re-runnable: `objdump -s
 *     --start-address=0x5c530c --stop-address=0x5c5340`):
 *       DAT_005c530c = {1, 2, -1, 0}     (only [0..1] used)
 *       DAT_005c5314 = {-1, 0, 1, 2, 3, 6, 7}  (only [1..6] used)
 *   - anchor pose: record bytes 0x6fc + iVar8*12 (asm 0x438805-0x438821).
 *     Engine stride: `lea ecx,[eax+eax*2+0x1bf]` + `fld [esi+ecx*4]`.
 *   - reach halved when an anchor is used:
 *     `local_c = npc.attack_radius * 0.5` (asm 0x43882a-0x438836,
 *      multiplier .rdata 0x51935c = 0.5).
 *   - distance: `local_3c = sqrt(dx² + dz²)` where (dx, dy, dz) =
 *     pose - slot[0x17..0x19] (asm 0x438839-0x438851).
 *     `if (dx == 0 && dz == 0) dz = 0.01` jitter at L438862-L438876
 *     (.rdata 0x5193a4 = 0.01).
 *   - distance gate (asm L43887d+, decomp L35244):
 *     `local_3c - slot[0x2a] < local_c * dist_mul * radius_mul`
 *   - AABB Y-band gate (decomp L35246-L35249):
 *     `local_1c = slot[0x2a] * 0.8`
 *     `local_8 = local_c * y_band_mul * radius_mul`
 *     `|dy - local_1c| < local_8` (two-sided check).
 */
#include "scene1_combat_sm.h"

#include <math.h>
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
int32_t g_scene1_combat_phase_b_collision_count;

scene1_combat_npc_type_attrs_t
    g_scene1_combat_npc_type_attrs[SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT];

static scene1_combat_phase_b_visit_fn     g_phase_b_visit_hook;
static scene1_combat_phase_b_collision_fn g_phase_b_collision_hook;

/* ─── rdata tables (DAT_005c530c, DAT_005c5314) ──────────────────────── */
/*
 * Captured from vendor/unpacked/recettear.unpacked.exe bytes
 * 0x5c530c..0x5c532b.  These select which entry of the NPC's
 * `anchors[]` table to use for a given multi-hit sub-iter.
 *
 * The engine arrays overlap: DAT_005c5314 starts 8 bytes into
 * DAT_005c530c.  Port keeps them as two distinct read-only arrays for
 * clarity.
 */
static const int32_t k_anchor_index_46_47[2] = {1, 2};
static const int32_t k_anchor_index_44_45[7] = {-1, 0, 1, 2, 3, 6, 7};
/* k_anchor_index_44_45[0] = -1 is engine's "unused" sentinel.  Sub-iter
 * 0 never reads this slot (engine `if (local_50 != 0)` gate). */

scene1_combat_phase_b_visit_fn
scene1_combat_set_phase_b_visit_hook(scene1_combat_phase_b_visit_fn fn)
{
    scene1_combat_phase_b_visit_fn prev = g_phase_b_visit_hook;
    g_phase_b_visit_hook = fn;
    return prev;
}

scene1_combat_phase_b_collision_fn
scene1_combat_set_phase_b_collision_hook(scene1_combat_phase_b_collision_fn fn)
{
    scene1_combat_phase_b_collision_fn prev = g_phase_b_collision_hook;
    g_phase_b_collision_hook = fn;
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

/*
 * Number of sub-iters for a given NPC type — engine asm 0x438768+:
 *   default 1; 0x44/0x45 → 7; 0x46/0x47 → 2.
 */
static int phase_b_sub_iter_count(int32_t npc_type)
{
    if (npc_type == 0x44 || npc_type == 0x45) return 7;
    if (npc_type == 0x46 || npc_type == 0x47) return 2;
    return 1;
}

/*
 * Per-sub-iter pose + reach selection — engine decomp L35217-L35234.
 *
 *   pose      ← npc.combat_pose                  (default)
 *   reach     ← npc.attack_radius                (default)
 *
 *   if npc_type ∈ {0x46, 0x47}:
 *     anchor_idx = DAT_005c530c[sub_iter]
 *     pose      ← npc.anchors[anchor_idx]
 *     reach     ← npc.attack_radius * 0.5
 *
 *   else if sub_iter != 0:
 *     anchor_idx = DAT_005c5314[sub_iter]
 *     pose      ← npc.anchors[anchor_idx]
 *     reach     ← npc.attack_radius * 0.5
 *     (sets local_18 flag in engine; this is the "subtype anchor used"
 *      flag, consumed by Phase B's later angle filter / damage flow.
 *      C8jb.3 does not yet read this flag.)
 *
 * `out_pose` is filled with 3 floats.  `out_reach` with the scalar.
 * Returns 1 on success, 0 if the anchor index is out of range (defensive
 * — engine bounds DAT_005c5314[1..6] to known-valid anchor indices, but
 * tests may inject bad data).
 */
static int phase_b_resolve_pose(const scene1_people_entry_t *npc,
                                int sub_iter,
                                float out_pose[3],
                                float *out_reach)
{
    int32_t anchor_idx = -1;

    if (npc->npc_type == 0x46 || npc->npc_type == 0x47) {
        anchor_idx = k_anchor_index_46_47[sub_iter];
    } else if (sub_iter != 0) {
        anchor_idx = k_anchor_index_44_45[sub_iter];
    }

    if (anchor_idx < 0) {
        /* Default path: npc.combat_pose + full attack_radius. */
        out_pose[0] = npc->combat_pose[0];
        out_pose[1] = npc->combat_pose[1];
        out_pose[2] = npc->combat_pose[2];
        *out_reach  = npc->attack_radius;
        return 1;
    }

    /* Anchor path: bounds-check the anchor index against the port's
     * anchor table (8 entries).  Engine has no explicit bounds check —
     * .rdata data ensures valid indices.  Defensive return for tests. */
    if (anchor_idx >= 8) {
        out_pose[0] = npc->combat_pose[0];
        out_pose[1] = npc->combat_pose[1];
        out_pose[2] = npc->combat_pose[2];
        *out_reach  = npc->attack_radius;
        return 0;
    }

    out_pose[0] = npc->anchors[anchor_idx][0];
    out_pose[1] = npc->anchors[anchor_idx][1];
    out_pose[2] = npc->anchors[anchor_idx][2];
    *out_reach  = npc->attack_radius * 0.5f;
    return 1;
}

/*
 * Distance + AABB Y-band check — engine decomp L35235-L35249.
 *
 *   dx = pose.x - slot.POS_X
 *   dy = pose.y - slot.POS_Y
 *   dz = pose.z - slot.POS_Z
 *   if (dx == 0 && dz == 0) dz = 0.01      ; jitter to avoid div-by-zero
 *   dist = sqrt(dx² + dz²)                  ; 2D-XZ distance
 *
 *   if (dist - slot.reach >= reach * dist_mul * radius_mul) → fail
 *   y_half = slot.reach * 0.8
 *   y_band = reach * y_band_mul * radius_mul
 *   if !(|dy - y_half| < y_band) → fail     ; engine writes the gate as
 *                                           ; `(dy - half < band) && (-band < half + dy)`
 *
 * Returns 1 on pass, 0 on fail.
 */
static int phase_b_check_collision(const scene1_people_entry_t *npc,
                                   const float pose[3],
                                   float reach,
                                   float slot_pos_x,
                                   float slot_pos_y,
                                   float slot_pos_z,
                                   float slot_reach)
{
    float dx = pose[0] - slot_pos_x;
    float dy = pose[1] - slot_pos_y;
    float dz = pose[2] - slot_pos_z;
    if (dx == 0.0f && dz == 0.0f) {
        dz = 0.01f;
    }
    float dist = sqrtf(dx * dx + dz * dz);

    const scene1_combat_npc_type_attrs_t *attrs =
        &g_scene1_combat_npc_type_attrs[(unsigned)npc->npc_type & 0xff];

    /* Distance gate. */
    float dist_threshold = reach * attrs->dist_mul * attrs->radius_mul;
    if (!((dist - slot_reach) < dist_threshold)) {
        return 0;
    }

    /* AABB Y-band gate.  Engine:
     *   `(dy - local_1c < local_8) && (-local_8 < local_1c + dy)`
     * with `local_1c = slot.reach * 0.8`, `local_8 = reach * y_band_mul *
     * radius_mul`.  Both conditions simplify to a symmetric band
     * around 0:  `-(half + band) < dy < (half + band)` (equivalent
     * to `|dy| < half + band`).  Port keeps the engine-verbatim two
     * conditions to preserve bit-exact float comparison order. */
    float half  = slot_reach * 0.8f;
    float band  = reach * attrs->y_band_mul * attrs->radius_mul;
    if (!((dy - half) < band)) {
        return 0;
    }
    if (!(-band < (half + dy))) {
        return 0;
    }
    return 1;
}

static void phase_b_npc_collision_pass(const scene1_people_entry_t *npc,
                                       int npc_index,
                                       float slot_pos_x,
                                       float slot_pos_y,
                                       float slot_pos_z,
                                       float slot_reach)
{
    /* Engine decomp L35204-L35234: sub-iter count by NPC type. */
    int iter_count = phase_b_sub_iter_count(npc->npc_type);

    for (int sub = 0; sub < iter_count; sub++) {
        float pose[3];
        float reach;
        if (!phase_b_resolve_pose(npc, sub, pose, &reach)) {
            /* Out-of-range anchor — engine has no bounds check, but our
             * port falls back to combat_pose.  C8jb.3 still runs the
             * collision check on the fallback for fidelity. */
        }

        if (phase_b_check_collision(npc, pose, reach,
                                    slot_pos_x, slot_pos_y, slot_pos_z,
                                    slot_reach)) {
            g_scene1_combat_phase_b_collision_count++;
            if (g_phase_b_collision_hook != NULL) {
                g_phase_b_collision_hook(npc_index, sub);
            }
        }
    }
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

    /* Slot pose + reach are read once per Phase B entry; they don't
     * change as the iteration walks NPCs.  Engine reads them inside the
     * inner loop (per sub-iter), but they're identical each time, so
     * hoisting is bit-equivalent. */
    float slot_pos_x  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float slot_pos_y  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float slot_pos_z  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float slot_reach  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_DRAG];

    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) {
        const scene1_people_entry_t *npc = &g_scene1_people[i];

        if (!phase_b_npc_passes_skip_gates(npc, state, owner_b_int)) {
            continue;
        }
        if (phase_b_npc_in_hit_history(npc, seq_id)) {
            continue;
        }

        /* NPC is a "would-collide" hit candidate. */
        g_scene1_combat_phase_b_visit_count++;
        if (g_phase_b_visit_hook != NULL) {
            g_phase_b_visit_hook(i);
        }

        /* C8jb.3 — run the nested sub-iter loop + collision math. */
        phase_b_npc_collision_pass(npc, i,
                                   slot_pos_x, slot_pos_y, slot_pos_z,
                                   slot_reach);
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

    /* Reset Phase B observable counters at every fall-through entry. */
    g_scene1_combat_phase_b_visit_count     = 0;
    g_scene1_combat_phase_b_collision_count = 0;

    /* Phase B — attacker NPC scan + collision math.  Skipped if slot is
     * NULL (Phase A tests use NULL to probe gates without prepping
     * a slot). */
    if (slot != NULL) {
        phase_b_scan(slot);
    }

    /* Phases C/D stub — return 0 unconditionally for C8jb.3. */
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
