/*
 * scene1_postload.c — see scene1_postload.h for the Cf.1 MVP scope.
 *
 * Ports the tail of engine FUN_00436f97 (the scene-1 INGAME state-
 * entry init).  Only block-11 i=0 + block-23 are in scope; the other
 * 23 logical blocks are deferred (see docs/findings/scene1-postload-
 * init.md).
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

#include "scene1_particles_tick.h"
#include "scene1_records_b_spawn.h"
#include "scene1_records_c_spawn.h"
#include "scene1_spawn.h"
#include "stage_palette.h"

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
