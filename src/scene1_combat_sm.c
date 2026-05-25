/*
 * scene1_combat_sm.c — per-record state machine (combat tick).
 *
 * Engine source: FUN_0043865e @ 0x43865e.  Chip C8jb.5a extends C8jb.4
 * with the damage-roll prologue: velocity-derived knockback factor +
 * hit-history ring bump + slot TYPE==0x53 heavy-attack short-circuit.
 * See scene1_combat_sm.h and
 * docs/findings/scene1-records-b-state-machine.md.
 *
 * Asm verification for C8jb.5a (re-runnable):
 *   nix develop --command i686-w64-mingw32-objdump -d -M intel \
 *       --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
 *       --start-address=0x438b47 --stop-address=0x438c1c
 *
 * Confirms (against decomp all.c L35276-L35307):
 *   - velocity-derived KB factor (engine 0x438b47-0x438b8b):
 *       `local_8 = sqrt(slot[+0x68]² + slot[+0x70]²)`  (VEL_X² + VEL_Z²)
 *       `if (local_8 > 0) local_8 = 0.7 / local_8`
 *     Engine .rdata 0x519748 = 0.7 (verified via tools/analyze/pe.py).
 *     Slot offsets: edi+0x68 = slot[0x1a dw] = VEL_X (off 26);
 *                   edi+0x70 = slot[0x1c dw] = VEL_Z (off 28).
 *   - hit-history ring bump (engine 0x438b8e-0x438bbe):
 *       `npc.hit_history[npc.hit_cursor] = slot[+0x11c]`  (slot SEQ_ID)
 *       `npc.hit_cursor = (npc.hit_cursor + 1) % 10`
 *     Engine field addresses: esi+0xb5c = hit_history start (10 dw);
 *                             esi+0xb84 = hit_cursor int.
 *     Slot offset: edi+0x11c = slot[0x47 dw] = SEQ_ID (off 71).
 *     Engine writes to npc BEFORE bumping cursor (post-increment).
 *   - 0x53 heavy-attack short-circuit (engine 0x438bb8-0x438c1a):
 *       `if (slot[TYPE] == 0x53):`
 *       `  if (per_type_attrs[npc.npc_type].heavy_atk_mode == 0`
 *       `      AND npc.npc_type != 0x22):`
 *       `    kill_age = (FUN_004319d6() == 1) ? 0x78 : 600`
 *       `    npc[+0xb18] = MAX(0, kill_age - slot[+0x98])`  (slot.AGE)
 *       `    DAT_0438bed8 = 4`
 *       `    local_8 = 0.0`
 *       `    goto LAB_004392a7`   (skip damage-roll, fall to hit emit)
 *     Engine reads npc.npc_type from `[esi+0x424]` (per-people-entry
 *     byte offset, identical to C8jb.3 source for sub-iter selection).
 *
 * Note on the LAB_004392a7 path: when the 0x53 short-circuit fires, the
 * engine jumps PAST the entire damage roll into the hit-emit section.
 * In C8jb.5a (no emit yet) we simply set damage_out=0 + kb_strength=0
 * and let the iteration continue.  C8jb.6 will model the LAB_004392a7
 * + return 1 contract.
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
 *
 * Engine decomp L35250-L35275 (C8jb.4 — per-collision arming):
 *
 *   armed = (sub-iter 0 of NPC type ∉ {0x48})        // engine local_18 = 0
 *   if NPC type == 0x48 → armed = false              // L35253
 *   if NPC type ∈ {0x44, 0x45}:
 *     if phase==6 ∧ subphase==1 → armed = true       // L35257 force-arm
 *     else:
 *       angle = atan2(dx, dz) - npc_yaw + π          // L35260-L35267
 *       normalize into [-π, π] via wrap loops
 *       if |angle| ≥ 0.9424779 (≈0.3π) → armed = false  // L35271
 *
 * Note: anchor-path sub-iter > 0 (engine L35232) already sets
 * `local_18 = 1` for non-0x46/0x47 NPCs.  In our port, the
 * `armed_from_anchor_path` flag in phase_b_resolve_pose propagates that
 * to the arming decision.
 *
 * Engine debug-text overlay (L35268-L35270, FUN_005038ff + FUN_00451874
 * with format "ANG: %f") is QA leftover — port skips it.  Adding a
 * stand-in hook would surface "did the angle filter evaluate" but
 * provides no gameplay value.
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
int32_t g_scene1_combat_phase_b_armed_collision_count;

/* C8jb.5a damage-roll prologue surfaces. */
float   g_scene1_combat_phase_b_kb_strength;
int32_t g_scene1_combat_phase_b_damage_out;
int32_t g_scene1_combat_phase_b_heavy_atk_count;
int32_t g_scene1_combat_dat_0438bed8;

/* C8jb.5b general damage formula globals. */
int32_t g_scene1_combat_damage_base_idle;     /* DAT_056db0b4 */
int32_t g_scene1_combat_damage_base_idle2;    /* DAT_056db0ac */
int32_t g_scene1_combat_scene_mul_014;        /* DAT_056db014 */
int32_t g_scene1_combat_scene_mul_01c;        /* DAT_056db01c */
int32_t g_scene1_combat_dat_056da1b8;         /* DAT_056da1b8 */
int32_t g_scene1_combat_owner_b_npc_type;     /* stand-in for *(int*)(OWNER_B+0x424) */

/* C8jb.5c post-damage clamp globals. */
int32_t g_scene1_combat_phase_b_local_1c_bits;
int32_t g_scene1_combat_owner_a_ce4;          /* stand-in for *(int*)(OWNER_A+0xce4) */
int32_t g_scene1_combat_owner_a_cec;          /* stand-in for *(int*)(OWNER_A+0xcec) */

/* C8jb.6 hit-effect emit globals. */
int32_t g_scene1_combat_phase_b_emit_count;
float   g_scene1_combat_phase_b_emit_pose[3];
int32_t g_scene1_combat_phase_b_emit_templates[2];
int32_t g_scene1_combat_phase_b_emit_se_id;
float   g_scene1_combat_dat_0438b904;
int32_t g_scene1_combat_dat_0438b908;
int32_t g_scene1_combat_dat_06a46f94;
int32_t g_scene1_combat_emit_aux_42e791_call_count;

/* C8jb.7 Phase C surfaces. */
int32_t g_scene1_combat_phase_c_visit_count;
int32_t g_scene1_combat_phase_c_hit_count;

/* C8jb.8a Phase C TYPE-dispatched sound + spawn observables. */
int32_t g_scene1_combat_phase_c_emit_spawn_count;
int32_t g_scene1_combat_phase_c_emit_template;
float   g_scene1_combat_phase_c_emit_scale;
float   g_scene1_combat_phase_c_emit_pose[3];
int32_t g_scene1_combat_phase_c_emit_param7;
int32_t g_scene1_combat_phase_c_emit_se_id;

int32_t g_scene1_projectiles[SCENE1_PROJ_COUNT * SCENE1_PROJ_STRIDE];

scene1_combat_npc_type_attrs_t
    g_scene1_combat_npc_type_attrs[SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT];

scene1_combat_proj_type_attrs_t
    g_scene1_combat_proj_type_attrs[SCENE1_COMBAT_PROJ_TYPE_ATTRS_COUNT];

static scene1_combat_phase_b_visit_fn      g_phase_b_visit_hook;
static scene1_combat_phase_b_collision_fn  g_phase_b_collision_hook;
static scene1_combat_phase_b_armed_fn      g_phase_b_armed_hook;
static scene1_combat_combo_held_fn         g_combo_held_hook;
static scene1_combat_rng_damage_scale_fn   g_rng_damage_scale_hook;
static scene1_combat_rng_unsigned_fn       g_rng_unsigned_hook;
static scene1_combat_emit_spawn_fn         g_emit_spawn_hook;
static scene1_combat_emit_overlay_spawn_fn g_emit_overlay_spawn_hook;
static scene1_combat_emit_se_fn            g_emit_se_hook;
static scene1_combat_emit_aux_42e791_fn    g_emit_aux_42e791_hook;
static scene1_combat_phase_c_visit_fn      g_phase_c_visit_hook;
static scene1_combat_phase_c_hit_fn        g_phase_c_hit_hook;

/* Engine angle-filter threshold = 0.9424779 (≈ 0.3π).  .rdata literal
 * at 0x51940c per asm scan (verified via objdump-s).  Stored as a named
 * constant for clarity. */
#define COMBAT_ANGLE_THRESHOLD  0.9424779f

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

scene1_combat_phase_b_armed_fn
scene1_combat_set_phase_b_armed_hook(scene1_combat_phase_b_armed_fn fn)
{
    scene1_combat_phase_b_armed_fn prev = g_phase_b_armed_hook;
    g_phase_b_armed_hook = fn;
    return prev;
}

scene1_combat_combo_held_fn
scene1_combat_set_combo_held_hook(scene1_combat_combo_held_fn fn)
{
    scene1_combat_combo_held_fn prev = g_combo_held_hook;
    g_combo_held_hook = fn;
    return prev;
}

scene1_combat_rng_damage_scale_fn
scene1_combat_set_rng_damage_scale_hook(scene1_combat_rng_damage_scale_fn fn)
{
    scene1_combat_rng_damage_scale_fn prev = g_rng_damage_scale_hook;
    g_rng_damage_scale_hook = fn;
    return prev;
}

static int combo_held_call(int button_id)
{
    return (g_combo_held_hook != NULL) ? g_combo_held_hook(button_id) : 0;
}

static float rng_damage_scale_call(int arg)
{
    return (g_rng_damage_scale_hook != NULL)
         ? g_rng_damage_scale_hook(arg)
         : 1.0f;
}

scene1_combat_rng_unsigned_fn
scene1_combat_set_rng_unsigned_hook(scene1_combat_rng_unsigned_fn fn)
{
    scene1_combat_rng_unsigned_fn prev = g_rng_unsigned_hook;
    g_rng_unsigned_hook = fn;
    return prev;
}

static uint32_t rng_unsigned_call(void)
{
    return (g_rng_unsigned_hook != NULL) ? g_rng_unsigned_hook() : 0u;
}

/* ─── C8jb.6 emit hook setters ───────────────────────────────────────── */

scene1_combat_emit_spawn_fn
scene1_combat_set_emit_spawn_hook(scene1_combat_emit_spawn_fn fn)
{
    scene1_combat_emit_spawn_fn prev = g_emit_spawn_hook;
    g_emit_spawn_hook = fn;
    return prev;
}

scene1_combat_emit_overlay_spawn_fn
scene1_combat_set_emit_overlay_spawn_hook(scene1_combat_emit_overlay_spawn_fn fn)
{
    scene1_combat_emit_overlay_spawn_fn prev = g_emit_overlay_spawn_hook;
    g_emit_overlay_spawn_hook = fn;
    return prev;
}

scene1_combat_emit_se_fn
scene1_combat_set_emit_se_hook(scene1_combat_emit_se_fn fn)
{
    scene1_combat_emit_se_fn prev = g_emit_se_hook;
    g_emit_se_hook = fn;
    return prev;
}

scene1_combat_emit_aux_42e791_fn
scene1_combat_set_emit_aux_42e791_hook(scene1_combat_emit_aux_42e791_fn fn)
{
    scene1_combat_emit_aux_42e791_fn prev = g_emit_aux_42e791_hook;
    g_emit_aux_42e791_hook = fn;
    return prev;
}

/* ─── C8jb.7 Phase C hook setters ────────────────────────────────────── */

scene1_combat_phase_c_visit_fn
scene1_combat_set_phase_c_visit_hook(scene1_combat_phase_c_visit_fn fn)
{
    scene1_combat_phase_c_visit_fn prev = g_phase_c_visit_hook;
    g_phase_c_visit_hook = fn;
    return prev;
}

scene1_combat_phase_c_hit_fn
scene1_combat_set_phase_c_hit_hook(scene1_combat_phase_c_hit_fn fn)
{
    scene1_combat_phase_c_hit_fn prev = g_phase_c_hit_hook;
    g_phase_c_hit_hook = fn;
    return prev;
}

static void emit_spawn_call(int call_index, int32_t template,
                            float x, float y, float z,
                            float scale, int32_t param7)
{
    if (g_emit_spawn_hook != NULL) {
        g_emit_spawn_hook(call_index, template, x, y, z, scale, param7);
    }
}

static void emit_overlay_spawn_call(int32_t template,
                                    float x, float y, float z,
                                    float scale,
                                    int32_t override_dur,
                                    float override_rot_y,
                                    int32_t mode)
{
    if (g_emit_overlay_spawn_hook != NULL) {
        g_emit_overlay_spawn_hook(template, x, y, z, scale,
                                  override_dur, override_rot_y, mode);
    }
}

static void emit_se_call(int32_t se_id)
{
    g_scene1_combat_phase_b_emit_se_id = se_id;
    if (g_emit_se_hook != NULL) g_emit_se_hook(se_id);
}

static void emit_aux_42e791_call(int32_t npc_int, int32_t damage,
                                 int32_t armed, int32_t flag)
{
    g_scene1_combat_emit_aux_42e791_call_count++;
    if (g_emit_aux_42e791_hook != NULL) {
        g_emit_aux_42e791_hook(npc_int, damage, armed, flag);
    }
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
 *   *out_disarmed_by_anchor ← 0
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
 *     *out_disarmed_by_anchor ← 1     // engine `local_18 = 1`
 *
 * `out_pose` is filled with 3 floats.  `out_reach` with the scalar.
 * `out_disarmed_by_anchor` is 1 only when this sub-iter selected an
 * anchor pose for a non-{0x46, 0x47} NPC — the "secondary multi-hit"
 * sub-iters of 0x44/0x45.  C8jb.4 propagates this into the arming
 * decision.
 */
static void phase_b_resolve_pose(const scene1_people_entry_t *npc,
                                 int sub_iter,
                                 float out_pose[3],
                                 float *out_reach,
                                 int *out_disarmed_by_anchor)
{
    int32_t anchor_idx = -1;
    *out_disarmed_by_anchor = 0;

    if (npc->npc_type == 0x46 || npc->npc_type == 0x47) {
        anchor_idx = k_anchor_index_46_47[sub_iter];
    } else if (sub_iter != 0) {
        anchor_idx = k_anchor_index_44_45[sub_iter];
        *out_disarmed_by_anchor = 1;
    }

    if (anchor_idx < 0) {
        /* Default path: npc.combat_pose + full attack_radius. */
        out_pose[0] = npc->combat_pose[0];
        out_pose[1] = npc->combat_pose[1];
        out_pose[2] = npc->combat_pose[2];
        *out_reach  = npc->attack_radius;
        return;
    }

    /* Anchor path: bounds-check the anchor index against the port's
     * anchor table (8 entries).  Engine has no explicit bounds check —
     * .rdata data ensures valid indices.  Defensive fallback for tests
     * that inject bad anchor data. */
    if (anchor_idx >= 8) {
        out_pose[0] = npc->combat_pose[0];
        out_pose[1] = npc->combat_pose[1];
        out_pose[2] = npc->combat_pose[2];
        *out_reach  = npc->attack_radius;
        return;
    }

    out_pose[0] = npc->anchors[anchor_idx][0];
    out_pose[1] = npc->anchors[anchor_idx][1];
    out_pose[2] = npc->anchors[anchor_idx][2];
    *out_reach  = npc->attack_radius * 0.5f;
}

/*
 * Normalize an angle into [-π, π] via the engine's wrap loops at
 * L35262-L35267.  Engine uses 6.2831855f (= 2π) as the wrap step.
 * Iterative rather than fmod-based to preserve bit-exact behavior.
 */
static float combat_normalize_angle(float a)
{
    while (a < -3.1415927f) {
        a += 6.2831855f;
    }
    while (a > 3.1415927f) {
        a -= 6.2831855f;
    }
    return a;
}

/*
 * Engine decomp L35250-L35275 — per-collision arming.
 *
 * `dx`, `dz` are pose - slot.pos (used by the 0x44/0x45 angle filter).
 * Returns 1 (armed) or 0 (disarmed).
 */
static int phase_b_compute_armed(const scene1_people_entry_t *npc,
                                 int disarmed_by_anchor,
                                 float dx, float dz)
{
    /* Anchor-path disarming applies to non-0x46/0x47 sub-iter > 0. */
    int armed = !disarmed_by_anchor;

    int32_t type = npc->npc_type;

    /* L35252-L35254 — 0x48 always disarms in range. */
    if (type == 0x48) {
        return 0;
    }

    /* L35255-L35275 — 0x44/0x45 angle filter.  Note: even with
     * anchor-path disarming already set, the engine RE-EVALUATES the
     * angle filter for 0x44/0x45; if phase==6∧subphase==1, the engine
     * EXPLICITLY clears local_18 to 0 (force-arm).  We mirror that. */
    if (type == 0x44 || type == 0x45) {
        if (npc->npc_phase == 6 && npc->npc_subphase == 1) {
            /* Force-arm, overriding any anchor-path disarming. */
            return 1;
        }
        /* Engine: `atan2(local_48, local_44) - npc_yaw + π`
         * where local_48 = dx, local_44 = dz.  Normalize to [-π, π]. */
        float ang = combat_normalize_angle(
            atan2f(dx, dz) - npc->npc_yaw + 3.1415927f);
        if (ang >= COMBAT_ANGLE_THRESHOLD
            || ang <= -COMBAT_ANGLE_THRESHOLD) {
            return 0;
        }
    }

    return armed;
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

/*
 * C8jb.5a — damage-roll prologue.  Runs per in-range collision (armed or
 * not) BEFORE the per-collision arming check — engine evaluates the
 * vel-derived factor + hit-history bump + 0x53 short-circuit
 * unconditionally once the collision passes both distance + Y-band gates
 * (engine 0x438b47-0x438c1a).
 *
 * The 0x53 short-circuit jumps PAST the rest of the damage roll into
 * LAB_004392a7 (hit-emit + return 1).  In C8jb.5a we model this by
 * writing kb_strength=0 + damage_out=0 + heavy_atk_count++ + the engine
 * side effects (npc.npc_b18_kill_age_out + DAT_0438bed8=4).
 *
 * `slot_vel_x` / `slot_vel_z` are pre-loaded by the caller (read once
 * per Phase B entry; bit-equivalent under our slot model).
 */
static void phase_b_damage_roll_prologue(scene1_people_entry_t *npc,
                                         int32_t *slot,
                                         float slot_vel_x,
                                         float slot_vel_z)
{
    /* Velocity-derived KB factor.  Engine .rdata 0x519748 = 0.7. */
    float vel_mag = sqrtf(slot_vel_x * slot_vel_x
                          + slot_vel_z * slot_vel_z);
    float kb_strength;
    if (vel_mag > 0.0f) {
        kb_strength = 0.7f / vel_mag;
    } else {
        /* Engine: skips the divide when fcomp <= 0.  Local stays at the
         * sqrt result (= 0). */
        kb_strength = vel_mag;
    }

    /* Hit-history ring bump.  Engine post-increments cursor after write
     * (asm 0x438ba2 writes, then 0x438bae+ bumps).  `% 10` is performed
     * by `cdq + idiv` of the +1 value. */
    int32_t seq_id = slot[SCENE1_RECORDS_B_OFF_SEQ_ID];
    int32_t cursor = npc->hit_cursor;
    if (cursor < 0 || cursor >= 10) {
        /* Defensive guard: engine has no bound-check (the cursor is
         * always in [0, 9] via the `% 10` invariant).  Tests that inject
         * a fresh NPC start at cursor=0 anyway.  Clamp to safe range to
         * avoid OOB writes if tests inject corruption. */
        cursor = 0;
    }
    npc->hit_history[cursor] = seq_id;
    npc->hit_cursor = (cursor + 1) % 10;

    int32_t slot_type = slot[SCENE1_RECORDS_B_OFF_TYPE];

    /* Initialize damage-roll outputs.  C8jb.5b/c will refine. */
    g_scene1_combat_phase_b_damage_out  = 0;
    g_scene1_combat_phase_b_kb_strength = kb_strength;

    /* 0x53 heavy-attack short-circuit.  Engine 0x438bb8-0x438c1a. */
    if (slot_type == 0x53) {
        const scene1_combat_npc_type_attrs_t *attrs =
            &g_scene1_combat_npc_type_attrs[(unsigned)npc->npc_type & 0xff];

        if (attrs->heavy_atk_mode == 0 && npc->npc_type != 0x22) {
            /* Engine reads FUN_004319d6 result and picks 0x78 or 600. */
            int32_t kill_age =
                (scene1_records_b_invoke_aux_4319d6() == 1) ? 0x78 : 600;
            int32_t slot_age = slot[SCENE1_RECORDS_B_OFF_AGE];
            int32_t latch    = kill_age - slot_age;
            if (latch < 0) {
                latch = 0;
            }
            npc->npc_b18_kill_age_out      = latch;
            g_scene1_combat_dat_0438bed8   = 4;
            g_scene1_combat_phase_b_heavy_atk_count++;
            g_scene1_combat_phase_b_kb_strength = 0.0f;
            /* damage_out already 0; engine jumps to LAB_004392a7
             * (hit-emit + return 1).  C8jb.6 will model the early
             * exit; for now the iteration continues. */
        }
    }
}

/*
 * Signed `x / 2` matching the engine's `cdq; sub eax, edx; sar eax, 1`
 * round-toward-zero idiom.  Pure right-shift differs from C division for
 * negative values (rounds toward -inf instead of zero).
 */
static int32_t signed_div2(int32_t x)
{
    if (x < 0) {
        uint32_t neg = (uint32_t)(-(x + 1)) + 1u;  /* abs without UB at INT_MIN */
        return -(int32_t)(neg >> 1);
    }
    return x >> 1;
}

/*
 * C8jb.5b — Phase B general damage formula.  Runs per in-range collision
 * when slot.TYPE != 0x53 (engine asm at 0x438bc0 falls through to 0x438c1c
 * for the general formula; the 0x53 short-circuit jumps to LAB_004392a7
 * regardless of inner conditions, bypassing this entire chunk).
 *
 * Asm verification (re-runnable):
 *   nix develop --command i686-w64-mingw32-objdump -d -M intel \
 *       --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
 *       --start-address=0x438c1c --stop-address=0x438eab
 *
 * Confirms (against decomp all.c L35313-L35371):
 *
 *   Pass 1 — "first damage" (sub eax at 0x438ce6 + neg eax):
 *     IDLE (FLAG_A == 0):  damage_base = (int)DAT_056db0b4
 *                          npc_quirk1  = (int)((float)npc_attrs[+0x40]
 *                                            * npc.damage_quirk_mul_ab8²)
 *                          (if npc.damage_quirk_disable_b28 != 0 → 0)
 *                          first_damage = (int)(npc_quirk1 / 4.0
 *                                             - damage_base / 2.0)
 *     ATTACKER (FLAG_A != 0): per_attacker = (OWNER_B == 0)
 *                                          ? &attrs[0x1a]
 *                                          : &attrs[g_..._owner_b_npc_type]
 *                             rng = rng_damage_scale_call(attacker_npc_type)
 *                             damage_base = per_attacker[+0x3c] * rng
 *                             npc_quirk1  = per_attacker[+0x40]  (raw)
 *                             (if disable_b28 != 0 → 0)
 *                             first_damage = (int)(npc_quirk1 / 4.0
 *                                                - damage_base / 2.0)
 *
 *   Side effect: g_scene1_combat_dat_056da1b8 |= 2 (engine 0x438cdf).
 *
 *   Pass 2 preamble (engine 0x438ce8-0x438d11):
 *     npc_quirk2 = (int)((float)npc_attrs[+0x38] * damage_quirk_mul_ab8²)
 *     (uses the NPC's OWN per-type entry, NOT attacker's)
 *     (if disable_b28 != 0 → 0)
 *
 *   Pass 2 — "second damage":
 *     IDLE:     second_damage = (int)((float)DAT_056db0ac / 2.0
 *                                   - (float)npc_quirk2 / 4.0)
 *               (if slot[OWNER_FLAG] != 0 → local_18 = 1 for C8jb.5c clamp)
 *     ATTACKER: rng2 = rng_damage_scale_call(attacker_npc_type)
 *               second_damage = (int)((float)per_attacker[+0x34] * rng2 / 2.0
 *                                   - (float)npc_quirk2 / 4.0)
 *
 *   Per-slot.TYPE damage selection (engine 0x438dac-0x438e02):
 *     TYPE in {0x12, 0x52, 0x60, 0x61, 0x62, 0x6a, 0x83}:
 *         damage = -first_damage
 *     TYPE in {0x3e, 0x51, 0x5f, 0x63, 0x64, 0x69, 0x82}:
 *         damage = (second_damage + -first_damage) / 2 (signed)
 *     default:
 *         damage = second_damage
 *
 *   Scale by slot.SCALE_X (engine 0x438e10 `fmul [edi+0xb4]`):
 *     damage = (int)((float)damage * slot.SCALE_X)
 *
 *   Combo + scene-state modifiers (engine 0x438e1b-0x438ea8):
 *     IDLE:     if combo_held(5) || combo_held(3) → damage *= 2
 *               if scene_mul_014 > 0 || scene_mul_01c > 0 → damage *= 2
 *     ATTACKER: if combo_held(4) || combo_held(3) → damage *= 2
 *                                                   (LAB_00438e6d path)
 *
 *   Common tail (engine 0x438e75-0x438ea8):
 *     if combo_held(7) || combo_held(6) → damage /= 2 (signed)
 *     if npc.block_dodge_b38 > 0       → damage /= 2 (signed)
 *
 * .rdata constants used (verified via objdump-s):
 *     0x519314 = 2.0  (denominator for damage_base / 2.0)
 *     0x51939c = 4.0  (denominator for npc_quirk / 4.0)
 *
 * Side effect on g_scene1_combat_phase_b_damage_out: holds the LAST
 * collision's value when multiple collisions land in one tick.
 */
static int32_t phase_b_damage_roll_general(scene1_people_entry_t *npc,
                                           int32_t *slot,
                                           uint32_t *local_1c_out)
{
    int32_t flag_a       = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    int32_t owner_b_int  = slot[SCENE1_RECORDS_B_OFF_OWNER_B];
    int32_t slot_type    = slot[SCENE1_RECORDS_B_OFF_TYPE];
    int32_t owner_flag   = slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG];
    float   slot_scale_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_SCALE_X];

    const scene1_combat_npc_type_attrs_t *npc_attrs =
        &g_scene1_combat_npc_type_attrs[(unsigned)npc->npc_type & 0xff];

    /* Attacker per-type entry + RNG arg.  Used by FLAG_A != 0 paths only.
     * When OWNER_B is non-NULL, the engine derefs *(int *)(OWNER_B + 0x424)
     * for the npc_type.  Our stand-in reads g_scene1_combat_owner_b_npc_type
     * (host-settable). */
    const scene1_combat_npc_type_attrs_t *attacker_attrs;
    int                                   attacker_rng_arg;
    if (owner_b_int != 0) {
        unsigned t = (unsigned)g_scene1_combat_owner_b_npc_type & 0xff;
        attacker_attrs   = &g_scene1_combat_npc_type_attrs[t];
        attacker_rng_arg = (int)t;
    } else {
        attacker_attrs   = &g_scene1_combat_npc_type_attrs[0x1a];
        attacker_rng_arg = 0x1a;
    }

    float npc_quirk_mul_sq = npc->damage_quirk_mul_ab8
                           * npc->damage_quirk_mul_ab8;

    /* ─── Pass 1 ─────────────────────────────────────────────────────── */
    float damage_base1_f;
    float npc_quirk1_f;
    if (flag_a == 0) {
        damage_base1_f = (float)g_scene1_combat_damage_base_idle;
        npc_quirk1_f   = (float)npc_attrs->attrs_int_40 * npc_quirk_mul_sq;
    } else {
        float rng = rng_damage_scale_call(attacker_rng_arg);
        damage_base1_f = (float)attacker_attrs->attrs_int_3c * rng;
        npc_quirk1_f   = (float)attacker_attrs->attrs_int_40;
    }
    int32_t npc_quirk1_int = (int32_t)npc_quirk1_f;
    if (npc->damage_quirk_disable_b28 != 0) {
        npc_quirk1_int = 0;
    }
    int32_t first_damage = (int32_t)((float)npc_quirk1_int / 4.0f
                                   - damage_base1_f / 2.0f);

    /* Engine side effect (asm 0x438cdf — between pass 1 __ftol and pass 2
     * setup). */
    g_scene1_combat_dat_056da1b8 |= 2;

    /* ─── Pass 2 preamble — npc_quirk2 from NPC's own attrs (both branches) */
    int32_t npc_quirk2_int =
        (int32_t)((float)npc_attrs->attrs_int_38 * npc_quirk_mul_sq);
    if (npc->damage_quirk_disable_b28 != 0) {
        npc_quirk2_int = 0;
    }

    /* ─── Pass 2 per-branch base ─────────────────────────────────────── */
    int32_t second_damage;
    if (flag_a == 0) {
        float base2_f = (float)g_scene1_combat_damage_base_idle2;
        second_damage = (int32_t)(base2_f / 2.0f
                                - (float)npc_quirk2_int / 4.0f);
        /* Engine 0x438d40-0x438d48: idle + slot.OWNER_FLAG != 0 → seed
         * local_1c bit 0 (IS_PLAYER marker for downstream hit-effect
         * pick).  Engine literally writes `mov [ebp-0x18], 1` (not OR);
         * at this point local_1c is BSS-zero so the write is equivalent
         * to setting bit 0. */
        if (owner_flag != 0) {
            *local_1c_out |= 1u;
        }
    } else {
        float rng2 = rng_damage_scale_call(attacker_rng_arg);
        float base2_f = (float)attacker_attrs->attrs_int_34 * rng2;
        second_damage = (int32_t)(base2_f / 2.0f
                                - (float)npc_quirk2_int / 4.0f);
    }

    /* ─── Per-slot.TYPE damage selection ─────────────────────────────── */
    int32_t damage;
    switch (slot_type) {
    case 0x12: case 0x52: case 0x60: case 0x61: case 0x62: case 0x6a: case 0x83:
        damage = -first_damage;
        break;
    case 0x3e: case 0x51: case 0x5f: case 0x63: case 0x64: case 0x69: case 0x82:
        damage = signed_div2(second_damage + (-first_damage));
        break;
    default:
        damage = second_damage;
        break;
    }

    /* ─── Scale by slot.SCALE_X ──────────────────────────────────────── */
    damage = (int32_t)((float)damage * slot_scale_x);

    /* ─── Combo + scene-state modifiers ──────────────────────────────── */
    if (flag_a == 0) {
        if (combo_held_call(5) || combo_held_call(3)) {
            damage *= 2;
        }
        if (g_scene1_combat_scene_mul_014 > 0
            || g_scene1_combat_scene_mul_01c > 0) {
            damage *= 2;
        }
    } else {
        if (combo_held_call(4) || combo_held_call(3)) {
            damage *= 2;
        }
    }
    if (combo_held_call(7) || combo_held_call(6)) {
        damage = signed_div2(damage);
    }
    if (npc->block_dodge_b38 > 0) {
        damage = signed_div2(damage);
    }

    return damage;
}

/*
 * Wrap an angle into [-π, π] via the engine's two while-loops:
 *   while (a < -π) a += 2π;
 *   while (a >  π) a -= 2π;
 * Used by the C8jb.5c charge-attack body and the quadrant atan2.
 */
static float wrap_angle_pi(float a)
{
    while (a < -3.1415927f) a += 6.2831855f;
    while (a >  3.1415927f) a -= 6.2831855f;
    return a;
}

/*
 * C8jb.5c — Phase B post-damage clamp body.  Engine asm 0x438eab..0x4390d3
 * / decomp L35372-L35442.  Runs immediately after the C8jb.5b general
 * damage formula (when slot.TYPE != 0x53).
 *
 * Asm verification (re-runnable):
 *   nix develop --command i686-w64-mingw32-objdump -d -M intel \
 *       --no-show-raw-insn vendor/unpacked/recettear.unpacked.exe \
 *       --start-address=0x438eab --stop-address=0x439120
 *
 * Confirms:
 *
 *   NPC reset (engine 0x438eab-0x438ee4):
 *     if npc.npc_type in {0x44, 0x45} AND slot.TYPE == 0x12 AND
 *        sub_iter == 0 AND npc.npc_phase != 6:
 *       npc.npc_phase_counter1 = 0
 *       npc.npc_subphase       = 0
 *       npc.npc_phase_counter2 = 0
 *       npc.npc_phase          = 6
 *
 *   Charge-attack disarm (engine 0x438ee6-0x438f9a):
 *     if npc.charge_flag != 0 AND npc.npc_b18_kill_age_out == 0:
 *       angle = wrap_angle_pi(atan2(dx, dz) - npc.npc_yaw + π)
 *       if -1.0995574 < angle < 1.0995574  (i.e. |angle| < ~0.35π):
 *         disarmed = true
 *         aux_482a51_invoke(npc_ptr_int, 4)
 *         npc.npc_phase_counter1 = 0
 *         npc.npc_phase          = 4
 *
 *   npc_phase damage scale + bit 3 (engine 0x438f9a-0x438fc2):
 *     if 1 <= npc.npc_phase <= 6:
 *       local_1c |= 8
 *       damage = (int)((float)damage * 1.2)
 *
 *   Quadrant atan2 (engine 0x438fc5-0x439089):
 *     angle = wrap_angle_pi(atan2(dx, dz) - npc.npc_yaw + π)
 *     if angle >= π/4 OR angle <= -π/4:
 *       if angle >= 3π/4 OR angle <= -3π/4:
 *         local_1c |= 2  (rear hit)
 *         damage = (int)((float)damage * 1.5)
 *       else:
 *         local_1c |= 4  (side hit)
 *         damage = (int)((float)damage * 1.2)
 *     // else: front hit, no scaling, no bit set
 *
 *   Idle OWNER_A flag + IS_PLAYER (engine 0x43908c-0x4390c8):
 *     if slot.FLAG_A == 0:
 *       if owner_a_ce4 != 0 OR owner_a_cec != 0:
 *         damage = (int)((float)damage * 1.5)
 *       if slot.OWNER_FLAG != 0:
 *         damage *= 2
 *
 *   Final clamp (engine 0x4390cb-0x43911d):
 *     if disarmed:                damage = 0
 *     else if npc.npc_type == 5:  if damage < 0: damage = 0
 *     else:
 *       if damage < 1: damage = 1
 *       if damage >= 5:
 *         damage += rng_unsigned_call() % (uint32_t)(damage / 5)
 *       else:
 *         damage += rng_unsigned_call() & 1
 *
 * .rdata constants (verified via objdump-s):
 *   0x519394 = 0.7853982 (π/4)
 *   0x519bdc = 2.3561945 (3π/4)
 *   0x519be0 = -π/4
 *   0x519bd8 = -3π/4
 *   0x519be4 = -1.0995574 (charge-cone lower)
 *   0x519be8 =  1.0995574 (charge-cone upper)
 *   0x519924 = 1.2 (damage multiplier)
 *   0x5198e0 = 1.5 (damage multiplier)
 *   0x51943c = π (= 3.1415927)
 *   0x519398 = 2π (= 6.2831855)
 */
static void phase_b_damage_roll_clamps(scene1_people_entry_t *npc,
                                       int32_t *slot,
                                       int sub_iter,
                                       int armed_in,
                                       float dx,
                                       float dz,
                                       int32_t damage,
                                       uint32_t local_1c)
{
    int32_t flag_a    = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    int32_t slot_type = slot[SCENE1_RECORDS_B_OFF_TYPE];

    /* `armed_in == 0` is engine local_18 = 1 (disarmed-for-clamp). */
    int disarmed = (armed_in == 0);

    /* ─── NPC 0x44/0x45 + slot.TYPE 0x12 + sub_iter==0 reset ─────────── */
    if ((npc->npc_type == 0x44 || npc->npc_type == 0x45)
        && slot_type == 0x12
        && sub_iter == 0
        && npc->npc_phase != 6) {
        npc->npc_phase_counter1 = 0;
        npc->npc_subphase       = 0;
        npc->npc_phase_counter2 = 0;
        npc->npc_phase          = 6;
    }

    /* ─── Charge-attack disarm body ──────────────────────────────────── */
    if (npc->charge_flag != 0 && npc->npc_b18_kill_age_out == 0) {
        float ang = wrap_angle_pi(atan2f(dx, dz)
                                - npc->npc_yaw + 3.1415927f);
        if (ang > -1.0995574f && ang < 1.0995574f) {
            disarmed = 1;
            /* Engine pushes esi (= entry origin int) as the npc ptr arg.
             * Our hook receives that as an opaque int32 (host injects
             * whatever it wants; production has no hook = no-op). */
            scene1_records_b_invoke_aux_482a51((int32_t)(intptr_t)npc, 4);
            npc->npc_phase_counter1 = 0;
            npc->npc_phase          = 4;
        }
    }

    /* ─── npc_phase 1..6 → *1.2 + bit 3 ──────────────────────────────── */
    if (npc->npc_phase >= 1 && npc->npc_phase <= 6) {
        local_1c |= 8u;
        damage = (int32_t)((float)damage * 1.2f);
    }

    /* ─── Quadrant atan2 → *1.2 (side) / *1.5 (rear) ─────────────────── */
    {
        float ang = wrap_angle_pi(atan2f(dx, dz)
                                - npc->npc_yaw + 3.1415927f);
        if (ang >= 0.7853982f || ang <= -0.7853982f) {
            if (ang >= 2.3561945f || ang <= -2.3561945f) {
                local_1c |= 2u;
                damage = (int32_t)((float)damage * 1.5f);
            } else {
                local_1c |= 4u;
                damage = (int32_t)((float)damage * 1.2f);
            }
        }
    }

    /* ─── Idle: OWNER_A flag + IS_PLAYER ─────────────────────────────── */
    if (flag_a == 0) {
        if (g_scene1_combat_owner_a_ce4 != 0
            || g_scene1_combat_owner_a_cec != 0) {
            damage = (int32_t)((float)damage * 1.5f);
        }
        if (slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] != 0) {
            damage *= 2;
        }
    }

    /* ─── Final clamp ────────────────────────────────────────────────── */
    if (disarmed) {
        damage = 0;
    } else if (npc->npc_type == 5) {
        if (damage < 0) damage = 0;
    } else {
        if (damage < 1) damage = 1;
        if (damage >= 5) {
            uint32_t denom = (uint32_t)(damage / 5);
            if (denom > 0) {
                damage += (int32_t)(rng_unsigned_call() % denom);
            }
        } else {
            damage += (int32_t)(rng_unsigned_call() & 1u);
        }
    }

    g_scene1_combat_phase_b_damage_out      = damage;
    g_scene1_combat_phase_b_local_1c_bits   = (int32_t)local_1c;
}

/*
 * C8jb.6 — Per-slot.TYPE scale of the kb_strength (engine 0x439146..0x4392a5).
 *
 * Runs after C8jb.5c's clamps.  Mutates kb_strength in place and writes
 * several NPC bookkeeping fields (npc_kbcd_440 / npc_kb_type_ba0 /
 * npc_phase_lock_1c / npc_field_28).  All bit-exact .rdata constants
 * verified via the objdump scan documented above:
 *   0x5193a0 = 0.1, 0x5198d8 = 0.2, 0x5194ec = 0.3, 0x51935c = 0.5,
 *   0x5198e0 = 1.5 (engine multiplies; 0x82/0x85/0x86 zero local_8).
 *
 * Two arms: NPC blocking (`npc.npc_blocking_b98 == 1`) → kb_strength *= 0.3
 * + jump straight to the NPC.+0xba0 = 0 line; else NPC block_dodge_mode
 * scales (1 → 0, 2 → *0.5) then per-slot.TYPE multipliers.
 */
static float phase_b_emit_scale_kb_strength(scene1_people_entry_t *npc,
                                            int32_t slot_type,
                                            int armed,
                                            float kb_strength)
{
    /* Engine `mov [esi+0x440], 0x28; if (slot.TYPE == 0x60) [esi+0x440] = 0x3c`. */
    npc->npc_kbcd_440 = (slot_type == 0x60) ? 0x3c : 0x28;

    /* Engine `if (local_18 == 0) [esi+0x1c] = 6` — armed-collision phase lock. */
    if (armed) {
        npc->npc_phase_lock_1c = 6;
    }

    if (npc->npc_blocking_b98 == 1) {
        /* Engine 0x43914f-0x43916b — blocking branch: *= 0.3, jmp 0x439264. */
        kb_strength *= 0.3f;
    } else {
        /* Engine 0x43916e-0x43918b — block/dodge mode scaling. */
        if (npc->npc_block_dodge_b54 == 1) {
            kb_strength = 0.0f;
        } else if (npc->npc_block_dodge_b54 == 2) {
            kb_strength *= 0.5f;
        }

        /* Per-slot.TYPE multipliers.  Engine emits a flat cmp+jne chain
         * (not a switch); preserve that — each branch independent so an
         * attacker can in principle match multiple (none of the engine's
         * literal IDs overlap, but the order matters for any hypothetical
         * future TYPE alias). */
        if (slot_type == 0x82) {
            kb_strength = 0.0f;
            npc->npc_field_28 = 1;
            npc->npc_kbcd_440 = 0x3c;
        }
        if (slot_type == 0x8a) {
            kb_strength *= 1.5f;
            npc->npc_kbcd_440 = 0x3c;
        }
        if (slot_type == 0x66) kb_strength *= 0.3f;
        if (slot_type == 0x62) kb_strength *= 0.3f;
        if (slot_type == 0x72) kb_strength *= 0.1f;
        if (slot_type == 0x73) kb_strength *= 0.3f;
        if (slot_type == 0x7e) kb_strength *= 0.3f;
        if (slot_type == 0x76) kb_strength *= 0.3f;
        if (slot_type == 0x78) kb_strength *= 0.2f;
        if (slot_type == 0x7a) kb_strength *= 0.2f;
        if (slot_type == 0x5b) {
            /* Engine reads `*(byte *)(slot.OWNER_B+0x10) & 1` — when bit 0
             * set, kb=0; else *= 0.2.  We stand in with `g_scene1_combat
             * _owner_b_npc_type` bit 0 as the proxy (no per-byte OWNER_B
             * field exists yet; the stand-in is good enough for tests). */
            if ((g_scene1_combat_owner_b_npc_type & 1) != 0) {
                kb_strength = 0.0f;
            } else {
                kb_strength *= 0.2f;
            }
        }
    }

    /* Engine 0x43926d-0x43929b — kb-type byte clear + 0x85/0x86 overrides. */
    npc->npc_kb_type_ba0 = 0;
    if (slot_type == 0x86) {
        kb_strength = 0.0f;
        npc->npc_kb_type_ba0 = 0x1e;
    }
    if (slot_type == 0x85) {
        kb_strength = 0.0f;
        npc->npc_kb_type_ba0 = 0x1e;
    }
    g_scene1_combat_dat_0438bed8 = 4;

    return kb_strength;
}

/*
 * C8jb.6 — Engine signed-div by 2 helper (used in the NPC.+0x440 halving
 * when block_dodge_mode == 2).  Engine asm `cdq; sub eax, edx; sar eax, 1`.
 */
static int32_t signed_div2_emit(int32_t x)
{
    int32_t s = x >> 31;        /* cdq result */
    return (x - s) >> 1;        /* sub eax, edx; sar eax, 1 */
}

/*
 * C8jb.6 — KB vector write (engine 0x4392a7..0x4393bf).  Gate:
 *
 *   (npc.npc_stun_b20 == 0  AND  armed  AND  npc.npc_block_dodge_b54 != 1)
 *   OR (npc.npc_blocking_b98 == 1  AND  npc.npc_hp_curr_42c - damage <= 0)
 *
 * If gate passes:
 *   - block_dodge_b54 == 2 → npc_kbcd_440 = signed_div2(npc_kbcd_440)
 *   - npc_combat_phase_b40 == 0 → clear npc_phase / counter1 / counter2
 *   - npc_b18_kill_age_out > 0 → clear kb_vec + aux_482a51(npc, 0); else
 *     aux_482a51(npc, 2) + write kb_vec
 *     - blocking + npc_hp_curr_42c - damage > 0:
 *         kb_vec.x = (float)damage * slot.vel_x
 *         kb_vec.y = 0.5
 *         kb_vec.z = (float)damage * slot.vel_z
 *       else:
 *         kb_vec.x = kb_strength * slot.vel_x
 *         kb_vec.y = 0.3
 *         kb_vec.z = kb_strength * slot.vel_z
 * Else (gate failed): npc_kbcd_440 = 0.
 */
static void phase_b_emit_kb_vector_write(scene1_people_entry_t *npc,
                                         int armed,
                                         int32_t damage,
                                         float kb_strength,
                                         float slot_vel_x,
                                         float slot_vel_z)
{
    int gate_a = (npc->npc_stun_b20 == 0)
              && armed
              && (npc->npc_block_dodge_b54 != 1);
    int gate_b = (npc->npc_blocking_b98 == 1)
              && ((npc->npc_hp_curr_42c - (float)damage) <= 0.0f);

    if (!(gate_a || gate_b)) {
        npc->npc_kbcd_440 = 0;
        return;
    }

    if (npc->npc_block_dodge_b54 == 2) {
        npc->npc_kbcd_440 = signed_div2_emit(npc->npc_kbcd_440);
    }

    if (npc->npc_combat_phase_b40 == 0) {
        npc->npc_phase           = 0;
        npc->npc_phase_counter1  = 0;
        npc->npc_phase_counter2  = 0;
    }

    if (npc->npc_b18_kill_age_out > 0) {
        npc->npc_kb_vec_3fc[0] = 0.0f;
        npc->npc_kb_vec_3fc[1] = 0.0f;
        npc->npc_kb_vec_3fc[2] = 0.0f;
        scene1_records_b_invoke_aux_482a51((int32_t)(intptr_t)npc, 0);
        return;
    }

    scene1_records_b_invoke_aux_482a51((int32_t)(intptr_t)npc, 2);

    if ((npc->npc_blocking_b98 == 1)
        && ((npc->npc_hp_curr_42c - (float)damage) > 0.0f)) {
        npc->npc_kb_vec_3fc[0] = (float)damage * slot_vel_x;
        npc->npc_kb_vec_3fc[1] = 0.5f;
        npc->npc_kb_vec_3fc[2] = (float)damage * slot_vel_z;
    } else {
        npc->npc_kb_vec_3fc[0] = kb_strength * slot_vel_x;
        npc->npc_kb_vec_3fc[1] = 0.3f;
        npc->npc_kb_vec_3fc[2] = kb_strength * slot_vel_z;
    }
}

/*
 * C8jb.6 — Pick the SE id played at the emit tail (engine 0x439528..0x4395bd).
 * Branch table on slot.TYPE, armed flag, slot.FLAG_A (idle/!idle),
 * slot.OWNER_FLAG (IS_PLAYER), with one RNG bucket for {0x85, 0x86, 0x87}.
 */
static int32_t phase_b_emit_pick_se_id(int32_t slot_type,
                                       int armed,
                                       int idle,
                                       int is_player)
{
    if (slot_type == 0x08) return 0x179;
    if (slot_type == 0x53) return 0x2af;
    if (!armed)            return 0x167;
    /* armed branch */
    if (!idle)             return 0x13f;          /* default for !idle */
    if (is_player)         return 0x148;
    /* armed + idle + !is_player */
    if (slot_type == 0x5b)                  return 0x13f;
    if (slot_type == 0x5c || slot_type == 0x5f) return 0x2a7;
    if (slot_type == 0x85
     || slot_type == 0x86
     || slot_type == 0x87) {
        /* Engine: call rng; test al,1; jne → default (0x13f); else 0x2a7. */
        if ((rng_unsigned_call() & 1u) != 0u) return 0x13f;
        return 0x2a7;
    }
    if (slot_type == 0x02
     || slot_type == 0x03
     || slot_type == 0x6d
     || slot_type == 0x6f
     || slot_type == 0x70) {
        return 0x153;
    }
    return 0x13f;
}

/*
 * C8jb.6 — Mid-point spawn pose (engine 0x438b0c..0x438b44).  Engine
 * factors fVar3 = npc.attack_radius * dist_mul * radius_mul, then walks
 * from npc_pose toward slot.pos by fVar3 / dist along (dx, dz).  Y is
 * a fixed 0.85 lerp toward slot.pos.y + offset slot.pos.y.
 */
static void phase_b_compute_emit_pose(const scene1_people_entry_t *npc,
                                      const float npc_pose[3],
                                      float slot_pos_x,
                                      float slot_pos_y,
                                      float slot_pos_z,
                                      float out_pose[3])
{
    const scene1_combat_npc_type_attrs_t *attrs =
        &g_scene1_combat_npc_type_attrs[(unsigned)npc->npc_type & 0xff];

    float dx = npc_pose[0] - slot_pos_x;
    float dy = npc_pose[1] - slot_pos_y;
    float dz = npc_pose[2] - slot_pos_z;
    /* Engine uses the same 0.01 jitter convention as collision_check; in
     * practice we only reach here when collision_check already passed,
     * so dist > 0.  Defensive 0-guard kept to match engine's `fdiv`
     * behavior on NaN inputs (engine would produce ±inf, we produce 0). */
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist == 0.0f) dist = 0.01f;
    float fvar3 = npc->attack_radius
                * attrs->dist_mul
                * attrs->radius_mul;

    out_pose[0] = npc_pose[0] - (fvar3 * dx) / dist;
    out_pose[1] = dy * 0.85f + slot_pos_y;
    out_pose[2] = npc_pose[2] - (fvar3 * dz) / dist;
}

/*
 * C8jb.6 — Hit-effect emit cluster (engine 0x4393bf..0x43964d).  Runs
 * once per in-range collision after the C8jb.5b/c damage roll.  Returns
 * the SM ret value (always 1 in C8jb.6 — return 2 lands with Phase D).
 *
 * Branches:
 *   (A) idle + OWNER_A != 0 + OWNER_A.+0xcec != 0
 *       → 1 overlay_spawn(0x19, scale 1.0, override_dur -1)
 *       → skip to tail (no scene1_spawn calls)
 *   (B) idle + armed + is_player
 *       → 2 scene1_spawn (template 3, then 0xf)
 *   (C) idle + disarmed
 *       → 2 scene1_spawn (template 0x29, then 0x2a)
 *   (D) !idle (any state)                  → LAB_0043949c
 *       → 2 scene1_spawn (template 1, then 0x19)
 *   (E) idle + armed + !is_player           → LAB_0043949c
 *       → 2 scene1_spawn (template 1, then 0x19)
 *
 * Then: TYPE 4/0x52 + damage > 0 → 1 extra scene1_spawn (template 0x98,
 * at NPC pose); optional RNG call for {0x85, 0x86, 0x87}; SE play
 * (branch-table id); DAT writes; FUN_0042e791 (gated); FUN_00482a51
 * (gated); npc.npc_postdmg_ab4 = 1.0.
 */
static int phase_b_emit_hit_cluster(scene1_people_entry_t *npc,
                                    int32_t *slot,
                                    int armed,
                                    int32_t damage,
                                    const float npc_pose[3],
                                    float dist_minus_reach,
                                    float slot_pos_x,
                                    float slot_pos_y,
                                    float slot_pos_z)
{
    int32_t slot_type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    int32_t flag_a    = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    int     idle      = (flag_a == 0);
    int     is_player = (slot[SCENE1_RECORDS_B_OFF_OWNER_FLAG] != 0);

    /* Spawn pose midpoint — engine local_28/24/20. */
    float pose[3];
    phase_b_compute_emit_pose(npc, npc_pose,
                              slot_pos_x, slot_pos_y, slot_pos_z, pose);
    g_scene1_combat_phase_b_emit_pose[0] = pose[0];
    g_scene1_combat_phase_b_emit_pose[1] = pose[1];
    g_scene1_combat_phase_b_emit_pose[2] = pose[2];

    int32_t template_a = 0;
    int32_t template_b = 0;
    int     skip_double_spawn = 0;

    if (idle) {
        /* Engine 0x4393cf — OWNER_A.+0xcec != 0 path.  We stand in with
         * g_scene1_combat_owner_a_cec (existing C8jb.5c global). */
        if (g_scene1_combat_owner_a_cec != 0) {
            emit_overlay_spawn_call(0x19, pose[0], pose[1], pose[2],
                                    1.0f, -1, 0.0f, 0);
            skip_double_spawn = 1;
        } else if (armed) {
            if (is_player) {
                template_a = 3;
                template_b = 0xf;
            } else {
                /* LAB_0043949c path. */
                template_a = 1;
                template_b = 0x19;
            }
        } else {
            template_a = 0x29;
            template_b = 0x2a;
        }
    } else {
        /* !idle → LAB_0043949c path. */
        template_a = 1;
        template_b = 0x19;
    }

    if (!skip_double_spawn) {
        emit_spawn_call(0, template_a, pose[0], pose[1], pose[2], 0.2f, 1);
        emit_spawn_call(1, template_b, pose[0], pose[1], pose[2], 0.2f, 1);
    }

    g_scene1_combat_phase_b_emit_templates[0] = template_a;
    g_scene1_combat_phase_b_emit_templates[1] = template_b;

    /* Engine 0x4394e8 — TYPE 4/0x52 + damage > 0 → extra spawn at NPC pose
     * (template 0x98).  Engine's pose locals are local_34/local_54/local_4c
     * (decomp); the asm reads [ebp-0x30]/[ebp-0x50]/[ebp-0x48] which are
     * the npc.combat_pose / anchor pose components written during sub-iter
     * pose resolution (see phase_b_resolve_pose). */
    if ((slot_type == 0x04 || slot_type == 0x52) && damage > 0) {
        emit_spawn_call(2, 0x98,
                        npc_pose[0], npc_pose[1], npc_pose[2],
                        0.2f, 1);
    }

    /* Engine 0x4394e8..0x4395bd — SE branch table. */
    int32_t se_id = phase_b_emit_pick_se_id(slot_type, armed, idle, is_player);
    emit_se_call(se_id);

    /* Engine 0x4395c3-0x4395d9 — DAT writes (distance latch + 180-frame
     * timer + post-hit pose lock for idle + is_player). */
    g_scene1_combat_dat_0438b904 = dist_minus_reach;
    g_scene1_combat_dat_0438b908 = 0xb4;
    if (idle && is_player) {
        g_scene1_combat_dat_0438bed8 = 8;
    }

    /* Engine 0x4395ed-0x439605 — DAT_06a46f94 = min(damage, ftol(npc HP)). */
    g_scene1_combat_dat_06a46f94 = damage;
    int32_t hp_int = (int32_t)npc->npc_hp_curr_42c;
    if (hp_int < damage) {
        g_scene1_combat_dat_06a46f94 = hp_int;
    }

    /* Engine 0x43960a-0x439625 — slot.TYPE != 0x53 AND npc_extra_gate_428
     * == 1 → FUN_0042e791(npc, damage, armed_int, 0).  armed_int is the
     * engine local_18 (1 = disarmed, 0 = armed), so we pass !armed. */
    if (slot_type != 0x53 && npc->npc_extra_gate_428 == 1) {
        emit_aux_42e791_call((int32_t)(intptr_t)npc, damage,
                             armed ? 0 : 1, 0);
    }

    /* Engine 0x439628-0x43964a — block_dodge_b54 != 0 AND
     * npc_hp_curr_42c == 0 → aux_482a51(npc, 2).  The fcomp is against
     * 0.0 (.rdata 0x519320); we compare directly. */
    if (npc->npc_block_dodge_b54 != 0 && npc->npc_hp_curr_42c == 0.0f) {
        scene1_records_b_invoke_aux_482a51((int32_t)(intptr_t)npc, 2);
    }

    /* Engine 0x43964b-0x43964d — fld1 → NPC.+0xab4 = 1.0; ret 1. */
    npc->npc_postdmg_ab4 = 1.0f;
    g_scene1_combat_phase_b_emit_count++;
    return 1;
}

/*
 * C8jb.6 — Returns 1 when a hit was emitted (SM should return 1 from the
 * outer scan and break the iteration); 0 otherwise (continue scanning).
 */
static int phase_b_npc_collision_pass(scene1_people_entry_t *npc,
                                      int32_t *slot,
                                      int npc_index,
                                      float slot_pos_x,
                                      float slot_pos_y,
                                      float slot_pos_z,
                                      float slot_reach,
                                      float slot_vel_x,
                                      float slot_vel_z)
{
    /* Engine decomp L35204-L35234: sub-iter count by NPC type. */
    int iter_count = phase_b_sub_iter_count(npc->npc_type);

    for (int sub = 0; sub < iter_count; sub++) {
        float pose[3];
        float reach;
        int   disarmed_by_anchor;
        phase_b_resolve_pose(npc, sub, pose, &reach, &disarmed_by_anchor);

        if (!phase_b_check_collision(npc, pose, reach,
                                     slot_pos_x, slot_pos_y, slot_pos_z,
                                     slot_reach)) {
            continue;
        }

        g_scene1_combat_phase_b_collision_count++;
        if (g_phase_b_collision_hook != NULL) {
            g_phase_b_collision_hook(npc_index, sub);
        }

        /* Per-collision arming — C8jb.4.  Uses pose - slot.pos for the
         * 0x44/0x45 angle filter. */
        float dx = pose[0] - slot_pos_x;
        float dz = pose[2] - slot_pos_z;
        int armed = phase_b_compute_armed(npc, disarmed_by_anchor, dx, dz);
        if (armed) {
            g_scene1_combat_phase_b_armed_collision_count++;
            if (g_phase_b_armed_hook != NULL) {
                g_phase_b_armed_hook(npc_index, sub);
            }
        }

        /* C8jb.5a — damage-roll prologue.  Engine runs this for every
         * in-range collision regardless of armed state (the
         * armed/disarmed distinction matters in the LAB_004390d3 final
         * clamp, ported in C8jb.5c).  */
        phase_b_damage_roll_prologue(npc, slot, slot_vel_x, slot_vel_z);

        /* C8jb.5b/5c — general damage formula + post-damage clamps.
         * Engine asm 0x438bc0 falls through to 0x438c1c when slot.TYPE
         * != 0x53; the 0x53 short-circuit (handled inside the prologue)
         * jumps to LAB_004392a7 and bypasses both chunks.  Mirror that
         * gate here. */
        if (slot[SCENE1_RECORDS_B_OFF_TYPE] != 0x53) {
            uint32_t local_1c = 0;
            int32_t  damage   = phase_b_damage_roll_general(npc, slot,
                                                            &local_1c);
            phase_b_damage_roll_clamps(npc, slot, sub, armed, dx, dz,
                                       damage, local_1c);
        }

        /* C8jb.6 — Per-slot.TYPE kb_strength scaling + KB vector write +
         * hit-effect emit cluster.  Always fires once per in-range
         * collision (regardless of TYPE 0x53 short-circuit).  Returns 1
         * to signal the outer scan to break + the SM to return 1. */
        float kb_strength = phase_b_emit_scale_kb_strength(
            npc,
            slot[SCENE1_RECORDS_B_OFF_TYPE],
            armed,
            g_scene1_combat_phase_b_kb_strength);
        g_scene1_combat_phase_b_kb_strength = kb_strength;

        phase_b_emit_kb_vector_write(npc,
                                     armed,
                                     g_scene1_combat_phase_b_damage_out,
                                     kb_strength,
                                     slot_vel_x,
                                     slot_vel_z);

        /* Compute (dist - slot.reach) for the DAT_0438b904 write inside
         * the emit cluster.  Reuses the same dx/dz already calculated
         * for arming. */
        float dy = pose[1] - slot_pos_y;
        (void)dy;  /* unused here; phase_b_check_collision already gated */
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist == 0.0f) dist = 0.01f;
        float dist_minus_reach = dist - slot_reach;

        return phase_b_emit_hit_cluster(npc, slot, armed,
                                        g_scene1_combat_phase_b_damage_out,
                                        pose, dist_minus_reach,
                                        slot_pos_x, slot_pos_y, slot_pos_z);
    }

    return 0;
}

/*
 * Returns the SM ret value (1 if a hit emitted, 0 if scan completed
 * with no hit).  C8jb.6 introduces the early-break: the first in-range
 * collision that fires the emit cluster aborts the per-NPC iteration.
 */
static int phase_b_scan(int32_t *slot)
{
    /* Phase B outer gate (engine L35186):
     *   ((slot[FLAG_A] == 0 || slot[FLAG_A] == 3)
     *    && 0.0 < _DAT_056db0bc) */
    int32_t state = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    if (state != 0 && state != 3) return 0;
    if (!(g_scene1_combat_player_hp > 0.0f)) return 0;

    int32_t owner_b_int = slot[SCENE1_RECORDS_B_OFF_OWNER_B];
    int32_t seq_id      = slot[SCENE1_RECORDS_B_OFF_SEQ_ID];

    /* Slot pose + reach + velocity are read once per Phase B entry; they
     * don't change as the iteration walks NPCs.  Engine reads them
     * inside the inner loop (per sub-iter), but they're identical each
     * time, so hoisting is bit-equivalent. */
    float slot_pos_x  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float slot_pos_y  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float slot_pos_z  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float slot_reach  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_DRAG];
    float slot_vel_x  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X];
    float slot_vel_z  = *(const float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Z];

    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) {
        scene1_people_entry_t *npc = &g_scene1_people[i];

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

        /* C8jb.3/4/5a/5b/5c — collision math + arming + damage roll.
         * C8jb.6 — emit cluster + early-break on first hit. */
        int ret = phase_b_npc_collision_pass(npc, slot, i,
                                             slot_pos_x, slot_pos_y, slot_pos_z,
                                             slot_reach, slot_vel_x, slot_vel_z);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

/* ─── C8jb.8a — Phase C TYPE-dispatched emit helpers ───────────────── */
/*
 * Per-hit spawn helper.  Latches the (template, pose, scale, param7)
 * tuple into the Phase C observable globals + bumps the count, then
 * delegates to the shared emit_spawn hook (same hook as Phase B uses).
 * call_index is fixed at 0 because Phase C fires at most one spawn per
 * hit (proj.TYPE in {2,3} → 0x15 spawn; proj.TYPE == 0 → 0x16 spawn;
 * other TYPEs spawn nothing — the gates are disjoint).
 */
static void phase_c_emit_spawn(int32_t template,
                               float x, float y, float z,
                               float scale, int32_t param7)
{
    g_scene1_combat_phase_c_emit_template = template;
    g_scene1_combat_phase_c_emit_pose[0]  = x;
    g_scene1_combat_phase_c_emit_pose[1]  = y;
    g_scene1_combat_phase_c_emit_pose[2]  = z;
    g_scene1_combat_phase_c_emit_scale    = scale;
    g_scene1_combat_phase_c_emit_param7   = param7;
    g_scene1_combat_phase_c_emit_spawn_count++;
    if (g_emit_spawn_hook != NULL) {
        g_emit_spawn_hook(/*call_index=*/0, template, x, y, z, scale, param7);
    }
}

/*
 * Per-hit SE helper.  Latches the SE id into the Phase C observable;
 * delegates to the shared emit_se hook.  Note this writes ONLY the
 * Phase C observable — the Phase B SE observable
 * (g_scene1_combat_phase_b_emit_se_id) is left at its tick-top reset
 * value, so tests can distinguish which phase fired the SE.
 */
static void phase_c_emit_se(int32_t se_id)
{
    g_scene1_combat_phase_c_emit_se_id = se_id;
    if (g_emit_se_hook != NULL) g_emit_se_hook(se_id);
}

/* ─── C8jb.7 — Phase C projectile scan ──────────────────────────────── */
/*
 * Engine asm 0x439f28..0x43a10b / decomp L35613-L35660.  Iterates 210
 * projectile records at g_scene1_projectiles (stride 0xa8 dw), skipping
 * those whose TYPE or AUX field disqualifies them and those whose
 * recent-hit ring already contains slot.SEQ_ID.  Survivors run a 2D-XZ
 * AABB against per-projectile-type radii scaled by proj.SCALE; on hit,
 * the projectile's ring is bumped with slot.SEQ_ID (overwriting the
 * oldest entry — the ring IS the subtype-filter list), its STATE is
 * set to 5 ("active-hit"), and the mesh-emit sound bit (= 2) is OR'd
 * into g_scene1_combat_dat_056da1b8 when slot.TYPE is in the engine's
 * sound-eligible set.
 *
 * C8jb.7 stops at the on-hit side effects: it does NOT fire the
 * TYPE-dispatched effects (hit-particles / SE / RNG cascade — that's
 * C8jb.8) and it does NOT raise the SM return value to 1 (that's also
 * C8jb.8, when the per-TYPE bodies pick which projectile types early-
 * return).  The loop BREAKS after the first hit so the side effects
 * fire at most once per tick.
 *
 * Production: g_scene1_projectiles is BSS-zero (no writer ported) so
 * every record has TYPE=0 / AUX=0.  Skip cascade passes the BSS-zero
 * records (none of the disqualifying TYPE/AUX values match 0); the
 * subtype filter ALSO passes (slot.SEQ_ID=0 typically matches the
 * BSS-zero ring at index 0 — and slot.SEQ_ID > 0 for live slots, so
 * BSS-zero rings of all-0s don't match).  BUT the AABB gate fails
 * unconditionally because BSS-zero radii reduce the first condition
 * (`dist - reach < x_radius`) to `dist - reach < 0` — never true for
 * a non-overlapping projectile at the BSS-zero origin.
 *
 * Returns 1 if a hit fired (C8jb.7 reports the hit via the observable
 * but the SM caller doesn't see ret=1 — see scene1_combat_sm_tick).
 * The internal return is a debug aid only; the public tick collapses
 * Phase C's ret to 0 in C8jb.7.
 */
static int phase_c_scan(int32_t *slot)
{
    /* Phase C outer gate (engine 0x439f28):
     *   if (slot[FLAG_A] == 1 || slot[FLAG_A] == 3) skip Phase C
     */
    int32_t state = slot[SCENE1_RECORDS_B_OFF_FLAG_A];
    if (state == 1 || state == 3) return 0;

    int32_t slot_seq_id = slot[SCENE1_RECORDS_B_OFF_SEQ_ID];
    int32_t slot_type   = slot[SCENE1_RECORDS_B_OFF_TYPE];

    float slot_pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float slot_pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float slot_pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float slot_reach = *(const float *)&slot[SCENE1_RECORDS_B_OFF_DRAG];

    for (int i = 0; i < SCENE1_PROJ_COUNT; i++) {
        int32_t *proj = &g_scene1_projectiles[i * SCENE1_PROJ_STRIDE];

        int32_t proj_type = proj[SCENE1_PROJ_OFF_TYPE];
        int32_t proj_aux  = proj[SCENE1_PROJ_OFF_AUX];

        /* 10-entry skip cascade (engine 0x439f44..0x439fcb).  Order
         * matches engine. */
        if (proj_type == -1)   continue;
        if (proj_aux  == 3)    continue;
        if (proj_aux  == 7)    continue;
        if (proj_type == 0x16) continue;
        if (proj_type == 0x1e) continue;
        if (proj_type == 9)    continue;
        if (proj_type == 0xa)  continue;
        if (proj_type == 0x12) continue;
        if (proj_type == 0x13) continue;
        if (proj_type == 0xc)  continue;
        if (proj_type == 0xd)  continue;
        if (proj_type == 0xb)  continue;
        if (proj_type == 8)    continue;
        if (proj_aux  != 0)    continue;

        /* Shared subtype/hit-history filter (engine 0x439fd1..0x439fef).
         * The 10-dword window proj.RING[0..9] doubles as the "slot SEQ_IDs
         * that recently hit me" ring buffer.  If slot.SEQ_ID is already
         * in the ring, skip — engine prevents the same slot from re-hitting
         * the same projectile within 10 distinct seq_id windows. */
        int matched = 0;
        for (int j = 0; j < SCENE1_PROJ_OFF_RING_LEN; j++) {
            if (proj[SCENE1_PROJ_OFF_RING + j] == slot_seq_id) {
                matched = 1;
                break;
            }
        }
        if (matched) continue;

        g_scene1_combat_phase_c_visit_count++;
        if (g_phase_c_visit_hook != NULL) {
            g_phase_c_visit_hook(i);
        }

        /* AABB position + scale loads (engine 0x439ff5..0x43a079). */
        float proj_x     = *(const float *)&proj[SCENE1_PROJ_OFF_POS_X];
        float proj_y     = *(const float *)&proj[SCENE1_PROJ_OFF_POS_Y];
        float proj_z     = *(const float *)&proj[SCENE1_PROJ_OFF_POS_Z];
        float proj_scale = *(const float *)&proj[SCENE1_PROJ_OFF_SCALE];

        float dx = proj_x - slot_pos_x;
        float dy = proj_y - slot_pos_y;
        float dz = proj_z - slot_pos_z;
        /* Engine 0x43a019..0x43a03b — (dx==0 && dz==0) → dz = 0.01.
         * .rdata 0x5193a4 = 0.01f.  Note engine compares against 0x519320
         * (= 0.0f) via fcomp+sahf+jne; we mirror with `== 0.0f`. */
        if (dx == 0.0f && dz == 0.0f) dz = 0.01f;

        float dist            = sqrtf(dx * dx + dz * dz);
        float dist_minus_reach = dist - slot_reach;

        /* Per-projectile-type radii (engine 0x43a067..0x43a076). */
        const scene1_combat_proj_type_attrs_t *attrs =
            &g_scene1_combat_proj_type_attrs[proj_type & 0xff];
        float x_radius = attrs->x_radius * proj_scale;
        float z_radius = attrs->z_radius * proj_scale;

        /* AABB 3-condition gate (engine 0x43a079..0x43a0a3):
         *   dist - reach < x_radius
         *   dy           < slot_reach
         *   -(z_radius + slot_reach) < dy
         * Engine uses `jae 0x43a0a5` (skip) on first two, and `jb 0x43a0be`
         * (proceed) on third — note that fcomp's reversed-operand quirk
         * means the third is "if ST(0) < memory" = "-(z+reach) < dy". */
        if (!(dist_minus_reach < x_radius))                 continue;
        if (!(dy               < slot_reach))               continue;
        if (!(-(z_radius + slot_reach) < dy))               continue;

        /* HIT.  Engine 0x43a0be..0x43a10b — ring bump + state=5 +
         * conditional sound-flag OR. */
        g_scene1_combat_phase_c_hit_count++;
        if (g_phase_c_hit_hook != NULL) {
            g_phase_c_hit_hook(i);
        }

        int32_t cursor = proj[SCENE1_PROJ_OFF_CURSOR];
        proj[SCENE1_PROJ_OFF_RING + cursor] = slot_seq_id;
        proj[SCENE1_PROJ_OFF_CURSOR]        = (cursor + 1) % 10;
        proj[SCENE1_PROJ_OFF_STATE]         = 5;

        if (slot_type == 2    || slot_type == 0x54 ||
            slot_type == 0x6d || slot_type == 0x6f ||
            slot_type == 0x70) {
            g_scene1_combat_dat_056da1b8 |= 2;
        }

        /* C8jb.8a — Phase C TYPE-dispatched sound + spawn cluster.
         * Engine asm 0x43a10c..0x43a1df.  Two independent sub-blocks
         * (sound + 0x15 spawn for TYPE 2/3, then 0x16 spawn for TYPE 0),
         * separated by an unconditional fall-through at 0x43a18c.  Block 1
         * gates the SE id on TYPE; Block 2 gates the spawn on TYPE.  Both
         * blocks compute the spawn pose from the projectile + slot
         * midpoint (X/Z) and the projectile's OFFSET_Y field scaled by
         * a per-template constant (0.5 vs 20.5).  All other TYPEs play
         * SE 0x169 without spawning. */
        float proj_offset_y =
            *(const float *)&proj[SCENE1_PROJ_OFF_OFFSET_Y];
        float mid_x = proj_x - dx * 0.5f;   /* = (proj_x + slot_x) / 2 */
        float mid_z = proj_z - dz * 0.5f;   /* = (proj_z + slot_z) / 2 */

        /* Block 1 — sound + optional 0x15 spawn for TYPE 2/3. */
        if (proj_type == 2 || proj_type == 3) {
            /* Engine quirk (0x43a130): clears STATE back to 0 immediately
             * after the C8jb.7 STATE=5 write above.  Preserved for
             * fidelity — the STATE=5 is wasted for TYPE 2/3 but matches
             * the asm-emit order. */
            proj[SCENE1_PROJ_OFF_STATE] = 0;

            /* .rdata 0x51935c = 0.5f. */
            float emit_y = proj_y + proj_offset_y * 0.5f;
            phase_c_emit_spawn(/*template=*/0x15,
                               mid_x, emit_y, mid_z,
                               proj_scale, /*param7=*/6);
            phase_c_emit_se(0x159);
        } else if (proj_type == 0x15) {
            phase_c_emit_se(0x180);
        } else {
            phase_c_emit_se(0x169);
        }

        /* Block 2 — TYPE == 0 spawns hit-particle 0x16.  Independent of
         * Block 1 (Block 1 fires SE 0x169 for TYPE 0, which is the `else`
         * branch above).  .rdata 0x519bd0 = 20.5f. */
        if (proj_type == 0) {
            float emit_y = proj_y + proj_offset_y * 20.5f;
            phase_c_emit_spawn(/*template=*/0x16,
                               mid_x, emit_y, mid_z,
                               proj_scale, /*param7=*/6);
        }

        /* Engine flow continues into LIFETIME processing (C8jb.8b).  For
         * now, break out of the loop with the on-hit side effects + the
         * Block 1/2 emits applied; report "hit fired" to the caller
         * (which collapses Phase C's ret to 0 — C8jb.8b will rewrite the
         * contract once LIFETIME / TYPE 4/5/6/8/0x15 dispatch lands). */
        return 1;
    }

    return 0;
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
    g_scene1_combat_phase_b_visit_count            = 0;
    g_scene1_combat_phase_b_collision_count        = 0;
    g_scene1_combat_phase_b_armed_collision_count  = 0;
    g_scene1_combat_phase_b_heavy_atk_count        = 0;
    g_scene1_combat_phase_b_damage_out             = 0;
    g_scene1_combat_phase_b_kb_strength            = 0.0f;
    g_scene1_combat_phase_b_local_1c_bits          = 0;

    /* C8jb.6 emit observables — reset alongside the C8jb.5 ones. */
    g_scene1_combat_phase_b_emit_count             = 0;
    g_scene1_combat_phase_b_emit_pose[0]           = 0.0f;
    g_scene1_combat_phase_b_emit_pose[1]           = 0.0f;
    g_scene1_combat_phase_b_emit_pose[2]           = 0.0f;
    g_scene1_combat_phase_b_emit_templates[0]      = 0;
    g_scene1_combat_phase_b_emit_templates[1]      = 0;
    g_scene1_combat_phase_b_emit_se_id             = 0;
    g_scene1_combat_emit_aux_42e791_call_count     = 0;

    /* C8jb.7 Phase C observables — reset alongside the others. */
    g_scene1_combat_phase_c_visit_count            = 0;
    g_scene1_combat_phase_c_hit_count              = 0;

    /* C8jb.8a Phase C TYPE-dispatched emit observables. */
    g_scene1_combat_phase_c_emit_spawn_count       = 0;
    g_scene1_combat_phase_c_emit_template          = 0;
    g_scene1_combat_phase_c_emit_scale             = 0.0f;
    g_scene1_combat_phase_c_emit_pose[0]           = 0.0f;
    g_scene1_combat_phase_c_emit_pose[1]           = 0.0f;
    g_scene1_combat_phase_c_emit_pose[2]           = 0.0f;
    g_scene1_combat_phase_c_emit_param7            = 0;
    g_scene1_combat_phase_c_emit_se_id             = 0;

    /* Phase B — attacker NPC scan + collision math + emit.  Skipped if
     * slot is NULL (Phase A tests use NULL to probe gates without
     * prepping a slot). */
    int ret = 0;
    if (slot != NULL) {
        ret = phase_b_scan(slot);
    }

    /* Phase C — projectile/aura scan (C8jb.7).  Runs only when Phase B
     * returned 0 (no Phase B hit this tick); a Phase B ret=1 already
     * exits the SM via the engine's `return 1` paths at L35603+.
     *
     * C8jb.7 collapses Phase C's internal ret to 0 — the C8jb.8 chip
     * will lift the TYPE-dispatched return values into the public SM
     * contract.  The on-hit side effects (ring bump, state=5, sound
     * flag, observable counters) still apply. */
    if (ret == 0 && slot != NULL) {
        (void)phase_c_scan(slot);
    }

    return ret;
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
