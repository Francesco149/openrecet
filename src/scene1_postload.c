/*
 * scene1_postload.c — see scene1_postload.h for the Cf.1 MVP scope.
 *
 * Ports the tail of engine FUN_00436f97 (the scene-1 INGAME state-
 * entry init).  Only block-11 i=0 + block-23 are in scope; the other
 * 23 logical blocks are deferred (see docs/findings/scene1-postload-
 * init.md).
 *
 * PORT-DEBT(simplified, FUN_00436f97): ports only block-11 i=0 + block-23 of
 * the 710-line state-entry init; the other 23 logical blocks (incl. the
 * DAT_0438c058 furniture stage_positions writer) are deferred. Retire = plan
 * Step 3.4 — also retires collision_house.c's hardcoded k_house_objects[].
 *
 * Asm reference for block-23 (verified via objdump -d at 0x438199):
 *
 *   fld    DWORD PTR ds:0x56db05c   ; copy of camera_yaw_alt → DAT_0438b4ac
 *   mov    eax,ds:0x68dd2f0         ; eax = g_stage_palette
 *   fstp   DWORD PTR ds:0x438b4ac
 *   cmp    DWORD PTR [eax+0x1b28],ebx     ; ebx=0; gate check
 *   je     0x43820d                       ; skip loop if gate==0
 *   ...load player.x/y+2/z into [ebp-4/-c/-8]...
 *   mov    esi,0xc8                       ; counter = 200
 *   ; loop body:
 *   push   edi                            ; edi=1 → param_7
 *   push   ecx ; fld1; fstp [esp]         ; arg6 = scale = 1.0f
 *   push   0x4f                            ; arg5 = type = 0x4f
 *   push   ecx ; fld [ebp-8]; fstp [esp]  ; arg4 = z
 *   push   ecx ; fld [ebp-c]; fstp [esp]  ; arg3 = y + 2.0
 *   push   ecx ; fld [ebp-4]; fstp [esp]  ; arg2 = x
 *   push   ebx                            ; arg1 = 0 (slot_hint)
 *   call   0x447f4f                       ; scene1_spawn(...)
 *   add    esp,0x1c                       ; 7 dword args cleanup
 *   call   0x40fb3a                       ; scene1_particles_tick()
 *   dec    esi ; jne loop_top
 *
 * Note: `_DAT_0438b4ac = _DAT_056db05c` (the single-field yaw_alt
 * snapshot at L689) is OUT of MVP scope — `DAT_0438b4ac` has no
 * consumer in our port.  When a future caller reads it, port it as
 * a separate one-liner.
 */

#include "scene1_postload.h"

#include <stdint.h>
#include <string.h>

#include "chara_equip.h"      /* chara_equip_get_current_bank — active save slot */
#include "save_bank.h"        /* save_arena_base + SAVE_BANK_STRIDE_BYTES */
#include "scene1_camera.h"    /* g_scene1_camera_char_mode */
#include "scene1_particles_tick.h"
#include "scene1_companion_ctrl.h"/* scene1_companion_ctrl_reset (actor-2 bob) */
#include "scene1_player_ctrl.h"   /* player_ctrl_pose_house_standing */
#include "scene1_records_b_spawn.h"
#include "scene1_records_c_spawn.h"
#include "scene1_spawn.h"
#include "scene1_walker_pass_init.h"
#include "stage_palette.h"
#include "stage_post_load.h"      /* stage_post_load_get_dat_056da1cc (player char) */

float g_scene1_stage_player_default_pos[3] = {-40.0f, 0.0f, -60.0f};

/* CLI overrides — see scene1_postload.h for the contract.  Module-
 * static so they survive a later stage_palette_init_house() reset
 * (which would otherwise wipe a palette-side flag). */
static int   g_force_ambient         = 0;
static int   g_ambient_type_override = -1;
static int   g_pose_override_set     = 0;
static float g_pose_override[3]      = {0.0f, 0.0f, 0.0f};

/* C8j.fin.c — table C smoke wiring.  Both type overrides default to
 * -1 (no-op).  Count + mag are used only when world_drop_type ≥ 0;
 * defaults match a sensible "lightly seeded ring of 8 drops at unit
 * magnitude" (consistent with the world_drop_default wrapper which
 * the engine uses for type 0x6e particle chains via scene1_mesh_emit). */
static int   g_force_c_pickup_type     = -1;
static int   g_force_c_world_drop_type = -1;
static int   g_force_c_world_drop_count = 8;
static float g_force_c_world_drop_mag  = 1.0f;

/* C8j.fin.b — table B smoke wiring.  Fake owner blobs back the two
 * allocators' owner pointers; allocators read pos + matrix from fixed
 * offsets inside the owner struct (NPC: pos@+0x3f0, matrix@+0x39c;
 * entity: pos@+0x20, matrix@+0xde8, owner_flag@+0xeac).  Blob size
 * covers the maximum offset the preamble + anchor-type body touches. */
#define SMOKE_B_NPC_BLOB_SIZE     1024
#define SMOKE_B_ENTITY_BLOB_SIZE  3760
static int   g_force_b_npc_type        = -1;
static int   g_force_b_entity_type     = -1;
static uint8_t g_smoke_b_npc_blob[SMOKE_B_NPC_BLOB_SIZE];
static uint8_t g_smoke_b_entity_blob[SMOKE_B_ENTITY_BLOB_SIZE];
static int   g_smoke_b_blobs_inited    = 0;

void scene1_postload_init_stage_defaults(void)
{
    g_scene1_stage_player_default_pos[0] = -40.0f;
    g_scene1_stage_player_default_pos[1] =   0.0f;
    g_scene1_stage_player_default_pos[2] = -60.0f;
}

void scene1_postload_pose_player(void)
{
    g_scene1_player_pos[0] = g_scene1_stage_player_default_pos[0];
    g_scene1_player_pos[1] = g_scene1_stage_player_default_pos[1];
    g_scene1_player_pos[2] = g_scene1_stage_player_default_pos[2];
}

void scene1_postload_pose_house_standing(void)
{
    /* Cchr.2h — the HOUSE standing player pose (de-MVP of the per-call
     * scene1_shop_walker_set_player_inject).
     *
     * Position: the engine derives the runtime standing position from
     * door/spawn placement (DAT_0438b1ec → DAT_056da1d8, all.c:34425); that
     * placement isn't ported, so until it lands we seed the leaf-validated
     * value directly — runs/cchr2b HOUSE frame 17544 / scene1-char-sprite-
     * trace.md both give (-0.30, 0, 9.35).  This OVERRIDES the (-40,0,-60)
     * stage default that pose_player set as the engine pre-gate mirror.
     *
     * The remaining actor state (char id, scale, sprite-state record) is
     * seeded in scene1_player_ctrl from the same capture. */
    g_scene1_player_pos[0] = -0.30f;
    g_scene1_player_pos[1] =  0.0f;
    g_scene1_player_pos[2] =  9.35f;

    player_ctrl_pose_house_standing(stage_post_load_get_dat_056da1cc());

    /* Reset the companion hover-bob phase (engine DAT_056db054) on scene entry,
     * alongside the actor-2 seed inside pose_house_standing. */
    scene1_companion_ctrl_reset();
}

void scene1_postload_ambient_spawn(void)
{
    if (g_stage_palette == 0) {
        return;
    }
    if (!g_force_ambient && g_stage_palette->ambient_spawn_flag == 0) {
        return;
    }

    int type = (g_ambient_type_override >= 0) ? g_ambient_type_override : 0x4f;

    float x, y, z;
    if (g_pose_override_set) {
        x = g_pose_override[0];
        y = g_pose_override[1];
        z = g_pose_override[2];
    } else {
        x = g_scene1_player_pos[0];
        y = g_scene1_player_pos[1] + 2.0f;
        z = g_scene1_player_pos[2];
    }

    for (int i = 200; i > 0; --i) {
        scene1_spawn(0, x, y, z, type, 1.0f, 1);
        scene1_particles_tick();
    }
}

void scene1_postload_force_ambient_flag(int value)
{
    if (g_stage_palette == 0) {
        return;
    }
    g_stage_palette->ambient_spawn_flag = value;
}

void scene1_postload_set_force_ambient(int force)
{
    g_force_ambient = force;
}

void scene1_postload_set_ambient_type_override(int type)
{
    g_ambient_type_override = type;
}

void scene1_postload_set_ambient_pose_override(int enable,
                                               float x, float y, float z)
{
    g_pose_override_set = enable ? 1 : 0;
    if (enable) {
        g_pose_override[0] = x;
        g_pose_override[1] = y;
        g_pose_override[2] = z;
    }
}

void scene1_postload_set_force_c_pickup_type(int type)
{
    g_force_c_pickup_type = type;
}

void scene1_postload_set_force_c_world_drop_type(int type)
{
    g_force_c_world_drop_type = type;
}

void scene1_postload_set_force_c_world_drop_count(int count)
{
    g_force_c_world_drop_count = count;
}

void scene1_postload_set_force_c_world_drop_mag(float mag)
{
    g_force_c_world_drop_mag = mag;
}

static void smoke_b_init_blobs(void)
{
    /* Zero both blobs + populate the identity matrix at the
     * allocator-read matrix offset (NPC: +0x39c, entity: +0xde8).
     * Idempotent — every preload entry can call without re-zeroing. */
    static const float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    memset(g_smoke_b_npc_blob,    0, sizeof g_smoke_b_npc_blob);
    memset(g_smoke_b_entity_blob, 0, sizeof g_smoke_b_entity_blob);
    memcpy(g_smoke_b_npc_blob    + 0x39c, kIdentity, sizeof kIdentity);
    memcpy(g_smoke_b_entity_blob + 0xde8, kIdentity, sizeof kIdentity);
    g_smoke_b_blobs_inited = 1;
}

static void smoke_b_blob_set_pos(uint8_t *blob, int pos_off,
                                 float x, float y, float z)
{
    memcpy(blob + pos_off + 0, &x, sizeof x);
    memcpy(blob + pos_off + 4, &y, sizeof y);
    memcpy(blob + pos_off + 8, &z, sizeof z);
}

void scene1_postload_set_force_b_npc_type(int type)
{
    g_force_b_npc_type = type;
}

void scene1_postload_set_force_b_entity_type(int type)
{
    g_force_b_entity_type = type;
}

void scene1_postload_smoke_b_spawn(void)
{
    if (g_force_b_npc_type < 0 && g_force_b_entity_type < 0) {
        return;
    }

    if (!g_smoke_b_blobs_inited) {
        smoke_b_init_blobs();
    }

    float x, y, z;
    if (g_pose_override_set) {
        x = g_pose_override[0];
        y = g_pose_override[1];
        z = g_pose_override[2];
    } else {
        x = g_scene1_player_pos[0];
        y = g_scene1_player_pos[1] + 2.0f;
        z = g_scene1_player_pos[2];
    }

    if (g_force_b_npc_type >= 0) {
        /* NPC allocator reads pos at owner+0x3f0. */
        smoke_b_blob_set_pos(g_smoke_b_npc_blob, 0x3f0, x, y, z);
        scene1_record_b_spawn_npc(g_smoke_b_npc_blob,
                                  g_force_b_npc_type, 0);
    }

    if (g_force_b_entity_type >= 0) {
        /* Entity allocator reads pos at owner+0x20 when flag==-1. */
        smoke_b_blob_set_pos(g_smoke_b_entity_blob, 0x20, x, y, z);
        scene1_record_b_spawn_entity(g_smoke_b_entity_blob,
                                     g_force_b_entity_type, -1);
    }
}

void scene1_postload_smoke_c_spawn(void)
{
    if (g_force_c_pickup_type < 0 && g_force_c_world_drop_type < 0) {
        return;
    }

    float x, y, z;
    if (g_pose_override_set) {
        x = g_pose_override[0];
        y = g_pose_override[1];
        z = g_pose_override[2];
    } else {
        x = g_scene1_player_pos[0];
        y = g_scene1_player_pos[1] + 2.0f;
        z = g_scene1_player_pos[2];
    }

    if (g_force_c_pickup_type >= 0) {
        scene1_records_c_spawn_pickup(0, x, y, z, g_force_c_pickup_type);
    }

    if (g_force_c_world_drop_type >= 0 && g_force_c_world_drop_count > 0) {
        scene1_records_c_spawn_world_drop_default(
            0, x, y, z,
            g_force_c_world_drop_type,
            g_force_c_world_drop_count,
            g_force_c_world_drop_mag,
            /* e1 */       0,
            /* extra_aux */0);
    }
}

/* ─── Cf.minimal — FUN_00436f97 alt-stage arm writer chunk ──────────── */

/* .rdata 0x5c5120 — per-(scene_type, mesh_index) world-anchor table.
 * Dump verified at landing time via:
 *
 *   nix develop --command python3 tools/analyze/pe.py bytes 0x5c5120 400
 *
 * 5 scene_types × 10 entries × (int32_t x, int32_t z) = 400 B.  Engine
 * formula at asm 0x437a68-0x437aa0: `addr = 0x5c5120 + (i + scene_type
 * * 10) * 8`.  scene_types 0..3 share the same anchor row (compact
 * shop layout); scene_type 4 has a distinct row with larger values
 * (different sub-class; not validated in HOUSE smoke). */
static const int32_t k_walker_anchor_table[5][10][2] = {
    /* scene_type 0 */
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    /* scene_type 1 */
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    /* scene_type 2 */
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    /* scene_type 3 */
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    /* scene_type 4 — distinct sub-class */
    {{12,111},{211,311},{411,511},{611,711},{1011,1012},
     {1112,1211},{1311,1411},{1512,1611},{1711,2011},{2107,2207}},
};

/* Stand-in storage (defaults match BSS-zero / disabled). */
static int     g_walker_scene_type = -1;
static int     g_walker_ivar8      = 0;
static int32_t g_walker_stage_positions[10][2];

void scene1_postload_set_walker_phase2_scene_type(int scene_type)
{
    g_walker_scene_type = scene_type;
}

void scene1_postload_set_walker_phase2_stage_positions(
    const int32_t positions[10][2])
{
    if (positions) {
        memcpy(g_walker_stage_positions, positions,
               sizeof g_walker_stage_positions);
    } else {
        memset(g_walker_stage_positions, 0,
               sizeof g_walker_stage_positions);
    }
}

void scene1_postload_set_walker_phase2_ivar8(int ivar8)
{
    g_walker_ivar8 = ivar8;
}

void scene1_postload_apply_walker_phase2_house_groundtruth(void)
{
    /* Retail-captured new-game HOUSE inputs (see header).  scene_type 0,
     * ivar8 3 (= retail phase2_count), and the 10 stage-position source
     * pairs from the per-save-slot record at +0x2ce10.  Verified to
     * reproduce retail's 3 live furniture meshes by the
     * ..._retail_groundtruth_new_game_house host test. */
    static const int32_t k_new_game_house_stage_positions[10][2] = {
        {3, 3}, {1, 0}, {0, 1}, {9, 1}, {10, 3},
        {11, 0}, {3, 6}, {6, 6}, {9, 6}, {12, 6},
    };
    g_walker_scene_type = 0;
    g_walker_ivar8      = 3;
    memcpy(g_walker_stage_positions, k_new_game_house_stage_positions,
           sizeof g_walker_stage_positions);
}

/* ─── de-MVP: source the Cf inputs from real save state ──────────────────
 *
 * The furniture (x,z) pairs the Cf walker reads from save-record +0x2ce10
 * are NOT computed — engine FUN_0048ffd9 copies them verbatim from a
 * static .data template (DAT_005cf864) into the active save record, and
 * the new-game record seeder FUN_0049d36d calls it.  The template is
 * indexed by the shop-tier selector at record+0x2cde0 (0 on a fresh game).
 * Rows 0..3 are the four HOUSE tiers (dumped from vendor/unpacked @
 * file-offset 0x1ce064).  Ground truth: retail new-game HOUSE has tier 0 →
 * row 0, confirmed live at record+0x2ce10 by tools/dump_demvp_groundtruth.py
 * (which also confirmed char_mode +0x2ce0c = 0, scene_type = 0). */
#define SCENE1_HOUSE_FURNITURE_TIERS 4
static const int32_t
k_house_furniture_template[SCENE1_HOUSE_FURNITURE_TIERS][10][2] = {
    {{3,3},{1,0},{0,1}, {9,1},{10,3},{11,0},{3,6},{6,6},{9,6},{12,6}}, /* tier 0 */
    {{4,3},{1,0},{0,1}, {9,1},{10,3},{11,0},{3,6},{6,6},{9,6},{12,6}}, /* tier 1 */
    {{4,3},{1,0},{0,1},{14,1},{10,3},{11,0},{3,6},{6,6},{9,6},{12,6}}, /* tier 2 */
    {{4,3},{1,0},{0,1},{14,1},{10,3},{11,0},{3,6},{6,6},{9,6},{12,6}}, /* tier 3 */
};

/* Per-slot save-record field offsets (within the 0x2dfc8-byte record). */
#define SCENE1_REC_LAYOUT_SEL_OFF 0x2cde0   /* shop-tier selector (FUN_0048ffd9 index) */
#define SCENE1_REC_CHARMODE_OFF   0x2ce0c   /* camera char_mode (DAT_045105a4) */
#define SCENE1_REC_FURNITURE_OFF  0x2ce10   /* 10 × (int32 x, int32 z) furniture pairs */

static uint8_t *scene1_active_save_record(void)
{
    int32_t slot = chara_equip_get_current_bank();
    return save_arena_base() + (size_t)slot * SAVE_BANK_STRIDE_BYTES;
}

/* Test/debug scene_type override for the loader.  <0 (default) → use the
 * real HOUSE value (0).  >=0 → force that scene_type so `--force-walker-
 * phase2 N` can still exercise the synthetic tiers 1..4. */
static int s_house_scene_type_override = -1;

void scene1_postload_set_house_scene_type_override(int scene_type)
{
    s_house_scene_type_override = scene_type;
}

/* Port of engine FUN_0048ffd9 — seed the active record's furniture array
 * (+0x2ce10) from the template row selected by the record's shop-tier
 * field (+0x2cde0).  Engine reads the selector each iteration; we clamp to
 * the four ported HOUSE tiers. */
void scene1_postload_seed_house_furniture(void)
{
    uint8_t *rec = scene1_active_save_record();
    int32_t tier;
    memcpy(&tier, rec + SCENE1_REC_LAYOUT_SEL_OFF, sizeof tier);
    if (tier < 0 || tier >= SCENE1_HOUSE_FURNITURE_TIERS) {
        tier = 0;
    }
    int32_t *furn = (int32_t *)(rec + SCENE1_REC_FURNITURE_OFF);
    for (int i = 0; i < 10; i++) {
        furn[i * 2 + 0] = k_house_furniture_template[tier][i][0];
        furn[i * 2 + 1] = k_house_furniture_template[tier][i][1];
    }
}

/* Production HOUSE-entry loader.  Sources the Cf walker's three inputs +
 * the camera char_mode from real engine state, replacing the
 * --force-walker-phase2 MVP injection:
 *   - scene_type: DAT_068dd3fc[stage*0x6cf].  HOUSE (stage 0) = 0; the
 *     stage-table string loader that derives it for other stages is
 *     unported, so HOUSE's known-0 is used here (PHC).
 *   - ivar8: the engine constant 3 (FUN_00436f97 L178).
 *   - stage_positions: the save-record furniture array (+0x2ce10), seeded
 *     above from the template exactly as the Cf walker reads it.
 *   - char_mode: save-record +0x2ce0c (0 on a fresh game).
 * The bias_x/z_src camera inputs stay a HOUSE stand-in (the FUN_00432e50
 * placement search is unported — see scene1_camera_apply_house_groundtruth). */
void scene1_postload_load_house_phase2_inputs(void)
{
    uint8_t *rec = scene1_active_save_record();

    /* Establish the new-game HOUSE record fields that the port's save-bank
     * baseline does not (the full new-game record seeder FUN_0049d36d is
     * unported, and the port's save_bank_init_all leaves these at a -1
     * artifact rather than retail's 0): the shop-tier selector (+0x2cde0)
     * and char_mode (+0x2ce0c) are both 0 on a fresh HOUSE per the live
     * retail capture.  Writing them here makes the record faithful so the
     * reads below source real (new-game-correct) state.  NOTE: scoped to
     * the new-game HOUSE path — a loaded save would set these via save-load
     * (gameplay-state sync not yet ported). */
    const int32_t zero = 0;
    memcpy(rec + SCENE1_REC_LAYOUT_SEL_OFF, &zero, sizeof zero);
    memcpy(rec + SCENE1_REC_CHARMODE_OFF,   &zero, sizeof zero);

    scene1_postload_seed_house_furniture();

    g_walker_scene_type = (s_house_scene_type_override >= 0)
                              ? s_house_scene_type_override
                              : 0;   /* DAT_068dd3fc[0] for HOUSE (PHC) */
    g_walker_ivar8      = 3;         /* engine constant (FUN_00436f97 L178) */

    const int32_t *furn = (const int32_t *)(rec + SCENE1_REC_FURNITURE_OFF);
    for (int i = 0; i < 10; i++) {
        g_walker_stage_positions[i][0] = furn[i * 2 + 0];
        g_walker_stage_positions[i][1] = furn[i * 2 + 1];
    }

    int32_t char_mode;
    memcpy(&char_mode, rec + SCENE1_REC_CHARMODE_OFF, sizeof char_mode);
    g_scene1_camera_char_mode = (int)char_mode;
}

void scene1_postload_walker_phase2_init(void)
{
    int st = g_walker_scene_type;
    /* Alt-stage gate (asm 0x4378c5-0x4378d0): scene_type must be in
     * [0..4] for the writer to fire.  Negative or > 4 = disabled. */
    if (st < 0 || st > 4) {
        return;
    }

    /* Camera yaw = π (engine FUN_00436f97 L589: `_DAT_073de39c = π`).
     * FUN_00436f97 zeroes yaw at its top (L40) then writes π *only* in
     * this alt-stage else-branch — i.e. exactly when this writer fires.
     * Porting it here (rather than the flag-gated camera-groundtruth MVP)
     * makes the 180° HOUSE camera flip faithful.  The sibling spawn-angle
     * `_DAT_056db060 = π` (L590) is not modelled — it feeds the particle
     * spawn-camera, not the render pose. */
    g_scene1_camera_yaw = 3.1415927f;

    /* iVar8 is the engine constant 3 (FUN_00436f97 L178 `iVar8 = 3`); it
     * is NOT a per-game runtime input.  On the alt-stage path nothing
     * reassigns it, so the scene_type-0 phase-2 count is always 3.  The
     * settable `g_walker_ivar8` exists only so the host tests can exercise
     * the count-dispatch arithmetic with synthetic values. */
    int ivar8 = g_walker_ivar8;

    /* Count dispatch (asm 0x4378f0-0x437946).  Only phase 2 count
     * matters for PII.3b's draw loop; phase 1 (DAT_0438bfb0) is
     * deferred to PII.3c. */
    int32_t phase2_count;
    switch (st) {
    case 0:  phase2_count = ivar8; break;
    case 1:  phase2_count = 4;     break;
    case 2:  phase2_count = 6;     break;
    case 3:
    case 4:  phase2_count = 10;    break;
    default: return;
    }
    if (phase2_count < 0) phase2_count = 0;
    if (phase2_count > SCENE1_WALKER_PHASE2_MAX) {
        phase2_count = SCENE1_WALKER_PHASE2_MAX;
    }
    g_scene1_walker_phase2_count = phase2_count;

    /* Mesh-type scalar setup (asm 0x43795a-0x4379c5).  Engine writes:
     *   slots {0, 4, 6, 7, 8, 9} = esi (= iVar8)
     *   slots {1, 2, 3, 5}       = 4 (pop eax constant)
     *
     * The shop_table mesh-index formula (PII.3b
     * `scene1_walker_draw_b_mesh_index`) is `mesh_type - 3 + selector*2`;
     * with `mesh_type==4` and default selector 0 this resolves to 1
     * (= shop_table02.x in scene_table.c). */
    g_scene1_walker_phase2_mesh_type[0] = ivar8;
    g_scene1_walker_phase2_mesh_type[1] = 4;
    g_scene1_walker_phase2_mesh_type[2] = 4;
    g_scene1_walker_phase2_mesh_type[3] = 4;
    g_scene1_walker_phase2_mesh_type[4] = ivar8;
    g_scene1_walker_phase2_mesh_type[5] = 4;
    g_scene1_walker_phase2_mesh_type[6] = ivar8;
    g_scene1_walker_phase2_mesh_type[7] = ivar8;
    g_scene1_walker_phase2_mesh_type[8] = ivar8;
    g_scene1_walker_phase2_mesh_type[9] = ivar8;

    /* Rot_y scalar setup (asm 0x437a29-0x437a47).  Only slots 1, 2, 3
     * have explicit angles; the other slots stay at their prior values
     * (BSS-zero on first call). */
    g_scene1_walker_phase2_rot_y[1] =  0.0f;
    g_scene1_walker_phase2_rot_y[2] =  1.5707964f;   /* π/2  @ 0x519434 */
    g_scene1_walker_phase2_rot_y[3] = -1.5707964f;   /* -π/2 @ 0x519a18 */

    /* 10-iter position loop (asm 0x437a4b-0x437ac5).
     *   pos_x[i] = 2.0 × (stage_pos[i].x - anchor[scene_type][i].x)
     *   pos_y[i] = 0.0
     *   pos_z[i] = 2.0 × (stage_pos[i].z - anchor[scene_type][i].z)
     */
    for (int i = 0; i < 10; i++) {
        int32_t sx = g_walker_stage_positions[i][0];
        int32_t sz = g_walker_stage_positions[i][1];
        int32_t ax = k_walker_anchor_table[st][i][0];
        int32_t az = k_walker_anchor_table[st][i][1];
        g_scene1_walker_phase2_pos_x[i] = 2.0f * (float)(sx - ax);
        g_scene1_walker_phase2_pos_y[i] = 0.0f;
        g_scene1_walker_phase2_pos_z[i] = 2.0f * (float)(sz - az);
    }

    /* ── PII.3c — phase-1 (wall/floor/jutan) writer ──────────────────
     * Same engine block-21 else-branch (asm 0x437918+), the phase-1
     * half.  Draw loop A renders these out of the map-mesh pool
     * (g_scene_map_meshes / engine DAT_068dcca0).
     *
     * Count dispatch (asm 0x437918-0x437946, DAT_0438bfb0):
     *   st 0 → 2,  st 1 → 2,  st 2 → ivar8,  st 3/4 → 5.
     * HOUSE (st 0) → 2 instances: the room + the carpet. */
    int32_t phase1_count;
    switch (st) {
    case 0:
    case 1:  phase1_count = 2;     break;
    case 2:  phase1_count = ivar8; break;
    default: phase1_count = 5;     break;   /* st 3, 4 */
    }
    if (phase1_count < 0) phase1_count = 0;
    if (phase1_count > SCENE1_WALKER_PHASE1_MAX)
        phase1_count = SCENE1_WALKER_PHASE1_MAX;
    g_scene1_walker_phase1_count = phase1_count;

    /* Mesh-index array (engine DAT_0438bfb8[]): [0]=0, [1..4]=uVar13=1
     * (literal at FUN_00436f97 L395).  → map-mesh slot 0 = shop_1st.x
     * (room) for instance 0, slot 1 = shop_jutan.x (carpet) for the
     * rest.  Capture: phase1_mesh_index = [0,1,1,1,1,...]. */
    g_scene1_walker_phase1_mesh_index[0] = 0;
    for (int i = 1; i < SCENE1_WALKER_PHASE1_MAX; i++)
        g_scene1_walker_phase1_mesh_index[i] = 1;

    /* Transform block.  The engine builder reads the shared columns at
     * index (15 + j); index 15 is never written (→ instance 0 at the
     * origin), and the fixed constant block fills engine indices 16-19
     * (→ port instances 1-4).  Axis remap is asm-verified (see
     * scene1_walker_pass_init.h): X←rot_y-col, Y←pos_x-col, Z←pos_y-col,
     * rot←mesh_type-col (the latter is 0 in this region).
     *
     * Engine constants (FUN_00436f97 L607-622):
     *   X (col1[16..19]): -2.0, 13.0, -2.5, 13.0
     *   Y (col2[16..19]):  0,    0,    0,    0
     *   Z (col3[16..19]): -1.0, -1.0,  8.0,  8.0   */
    static const float k_phase1_x[4] = { -2.0f, 13.0f, -2.5f, 13.0f };
    static const float k_phase1_z[4] = { -1.0f, -1.0f,  8.0f,  8.0f };
    g_scene1_walker_phase1_pos_x[0] = 0.0f;   /* engine idx 15 — unwritten */
    g_scene1_walker_phase1_pos_y[0] = 0.0f;
    g_scene1_walker_phase1_pos_z[0] = 0.0f;
    g_scene1_walker_phase1_rot_y[0] = 0.0f;
    for (int j = 0; j < 4 && j + 1 < SCENE1_WALKER_PHASE1_MAX; j++) {
        g_scene1_walker_phase1_pos_x[j + 1] = k_phase1_x[j];
        g_scene1_walker_phase1_pos_y[j + 1] = 0.0f;
        g_scene1_walker_phase1_pos_z[j + 1] = k_phase1_z[j];
        g_scene1_walker_phase1_rot_y[j + 1] = 0.0f;
    }
}
