/*
 * scene1_postload.c — see scene1_postload.h for the Cf.1 MVP scope.
 *
 * Ports the tail of engine FUN_00436f97 (the scene-1 INGAME state-
 * entry init).  Only block-11 i=0 + block-23 are in scope; the other
 * 23 logical blocks are deferred (see docs/findings/scene1-postload-
 * init.md).
 *
 * PORT-DEBT(simplified, FUN_00436f97): of the 710-line state-entry init this
 * file ports block-11 i=0, block-23, and the block-21 stage-placement writer
 * (scene1_postload_walker_phase2_init — which now also feeds collision_house's
 * DAT_0438c058 object origins, retiring its hardcoded k_house_objects[]).
 * scene1_preload_house also runs, in retail order, the floor-layout generator
 * FUN_0047df4d (floor_layout.c, + the 0x438b484 clear), the actor reset
 * FUN_0041f252 (actor_spawn.c) and the bg-NPC stage reset FUN_0046f1e9 — every
 * rand() draw FUN_00436f97 makes in the house.  The remaining ~20 logical
 * blocks (the global-zeroing preamble, the cc08/event machine seed, the
 * alt-stage menu arm) are deferred. Retire = plan Step 3.4.
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

#include "actor_spawn.h"           /* actor_spawn_reset (FUN_0041f252) */
#include "actor_table.h"           /* g_actor_alloc_n (DAT_0438b1bc) */
#include "dungeon_flow.h"          /* the floor-flow scalars (b4b4, bea0, …) */
#include "dungeon_objects.h"       /* FUN_0044ad81 */
#include "dungeon_party.h"         /* the party resets */
#include "party.h"
#include "enemy_place.h"           /* FUN_00431d54 / FUN_00431b1c */
#include "floor_layout.h"          /* the marks DAT_0438b434, DAT_0438b4b0 */
#include <math.h>
#include "map_collision.h"         /* g_mapcol_objs (DAT_0438bfb0..) */
#include "scene1_combat_sm.h"      /* g_scene1_combat_stage_id (DAT_0438b4c8) */
#include "scene1_maplight.h"       /* scene1_current_stage_record (DAT_068dd2f0) */
#include "stage_gate.h"            /* DAT_0438b4cc, FUN_0043195d, FUN_004319d6 */
#include "chara_equip.h"      /* chara_equip_get_current_bank — active save slot */
#include "customer_roster.h"         /* roster_compute_centroid (FUN_0048439a) */
#include "news_daily.h"            /* g_news_ticker_timer (DAT_0438b92c) */
#include "save_bank.h"        /* SAVE_BANK_STRIDE_BYTES */
#include "save_work.h"        /* the active working record */
#include "scene1_actor_say.h"      /* actor_say_stage_reset (DAT_0438b764/b76c) */
#include "scene1_bg_npc.h"         /* scene1_bg_npc_stage_reset (FUN_0046f1e9) */
#include "scene1_camera.h"    /* g_scene1_camera_char_mode */
#include "scene1_chr_sprite.h"     /* CHR_ACTOR_FACING */
#include "scene1_companion_ctrl.h"/* scene1_companion_ctrl_reset (actor-2 bob) */
#include "scene1_conversation_pose.h"/* scene1_conversation_pose_reset */
#include "scene1_counter_menu.h"   /* counter_menu_stage_reset (DAT_0438b74c..b754) */
#include "scene1_day_close.h"      /* scene1_house_morning_reseat (FUN_004852fb) */
#include "scene1_particles_tick.h"
#include "scene1_player_ctrl.h"   /* player_ctrl_pose_house_standing */
#include "scene1_preload.h"        /* scene1_preload_floor_layout (FUN_0047df4d) */
#include "scene1_records.h"        /* scene1_records_reset (FUN_0040f64b) */
#include "scene1_records_b_spawn.h"
#include "scene1_records_c_spawn.h"
#include "scene1_shop_display.h"   /* shop_display_highlight_clear (DAT_0438cbfc) */
#include "scene1_shop_walker.h"    /* scene1_customer_npc_reset (FUN_0046f892) */
#include "scene1_spawn.h"
#include "scene1_top_hud.h"       /* scene1_top_hud_tooltip_house_entry (FUN_004060ff) */
#include "scene1_tutorial_dispatch.h"/* the iv2 beat: house entry, scene1_beat_flag (b928) */
#include "scene1_walker_pass_init.h"
#include "speech_bubble.h"         /* FUN_004060ff's bubble part */
#include "stage_palette.h"
#include "stage_post_load.h"      /* stage_post_load_get_dat_056da1cc (player char) */
#include "title_save_dialog.h"     /* FUN_00435612 cursor hide */
#include "x87.h"

/* g_scene1_stage_player_default_pos (DAT_0438b1ec) lives in floor_layout.c,
 * its retail writer FUN_0047df4d. */

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

/* FUN_004851e2 (0x4851e2): seat the player, actor 1 and Tear at the door-table
 * entry [shop tier (work record +0x2cde0) + 4·a] of rodata 0x5cd0c4 —
 * a = 0 the counter standing spot, a = 1 the front door (FUN_00436f97 picks a by
 * work-record dword +0x2cdf4).  Tear sits +0.9 to the side at y 3.0. */
static const float k_door_spawn[8][2] = {
    { -0.3f, 9.35f }, { -0.3f, 9.35f }, { -0.25f, 9.5f }, { 10.7f, 17.0f },
    {  2.0f, -3.24f }, { 5.0f, -5.67f }, {  5.0f, -5.67f }, {  5.0f, -5.67f },
};
static const float k_tear_side = 0.9f;   /* m32 0x519718 */

void scene1_postload_place_actors(int a)
{
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    int idx = (w ? (int)w[0x2cde0 / 4] : 0) + 4 * a;
    if (idx < 0 || idx >= 8)   /* retail reads past the 8 real entries */
        idx = 4 * a;
    const float x = k_door_spawn[idx][0], z = k_door_spawn[idx][1];
    g_scene1_actor_pos[0][0] = x;  g_scene1_actor_pos[0][1] = 0.0f;  g_scene1_actor_pos[0][2] = z;
    g_scene1_actor_pos[1][0] = x;  g_scene1_actor_pos[1][1] = 0.0f;  g_scene1_actor_pos[1][2] = z;
    g_scene1_actor_pos[2][0] = spill32((long double)x + k_tear_side);
    g_scene1_actor_pos[2][1] = 3.0f;
    g_scene1_actor_pos[2][2] = z;
}

static void postload_seat(void);

void scene1_postload_pose_house_standing(void)
{
    /* The HOUSE standing pose: FUN_00436f97's alt-stage arm seats the actors via
     * FUN_004851e2 (overriding the floor-layout spawn origin pose_player copied);
     * the actor state (char id, scale, sprite record) is seeded in
     * scene1_player_ctrl.  The counter spot here; the door seat (work +0x2cdf4,
     * the world-map return) is applied below, after the record seed. */
    scene1_postload_place_actors(0);

    /* FUN_00436f97 calls FUN_004060ff (top-HUD tooltip band/counter = -1) right
     * before FUN_004850ec (cc08 = 1, inside pose_house_standing). */
    scene1_top_hud_tooltip_house_entry();
    player_ctrl_pose_house_standing(stage_post_load_get_dat_056da1cc());

    /* Reset the companion hover-bob phase (engine DAT_056db054) on scene entry,
     * alongside the actor-2 seed inside pose_house_standing. */
    scene1_companion_ctrl_reset();

    /* Drop any held iv1_2 conversation pose latch on scene entry. */
    scene1_conversation_pose_reset();

    /* Clear the beat timer b924 on scene entry (b928 rides through). */
    scene1_tutorial_dispatch_house_entry();

    /* the world-map return seats the pair at the front door (rhost
     * multiday-tutorials-suggest f20576: Tear x 2.9, not the counter's 0.6). */
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    if (w != NULL && w[0x2cdf4 / 4] != 0)
        postload_seat();
}

void scene1_postload_day2_actor_replace(void)
{
    /* The day-2 in-scene actor RE-PLACE (retail FUN_0048526d → FUN_00436f97 —
     * the mini scene-entry re-place the HOUSE controller polls at the
     * day-advance, `if (DAT_0438b4e0==1) FUN_0048526d()` all.c:86750).
     *
     * The port models the iv2_1..iv2_6 chain as start_single DIALOGUE-loads, not
     * retail's scene-reload day-advance, so the scene-entry actor placement never
     * runs: the actors keep their stale first-customer COUNTER positions (player
     * px 0.796, companion cx -0.694 — mirror-SWAPPED vs retail) all the way into
     * the day-2 broom.  Re-seat them at the house-standing pose here.
     *
     * POSITIONS ONLY, and only the two live actors — the values are the same
     * door-placement standing seeds pose_house_standing uses (player -0.30/0/9.35,
     * companion 0.6/3.0/9.35; ground truth = retail day2 beat @15470).  Everything
     * else already matches retail across the beat: the record ANIM (player 6 /
     * companion 4) is held by the conversation-pose beat, and the FACING is
     * re-derived by scene1_conversation_pose_apply from the corrected positions
     * (player oct6 / companion oct2, since tear_x 0.6 > player_x -0.30) — so a full
     * pose_house_standing (which also resets the beat + pose + companion bob) is
     * both unnecessary and wrong here (it would zero the very beat driving this).
     * The companion then holds cx≈0.6 (free-roam law: dist 0.9 < CO_THRESHOLD 1.5);
     * retail's cx 0.6→1.0 ease is FUN_0048a833's b928/b924 intro branch, a
     * separate follow-up (finding cutscene-replay-anchor-drift §DAY2 #4). */
    scene1_postload_place_actors(0);
}

/* FUN_00436f97 L706-724, the house seat: work dword +0x2cdf4 == 0 → the
 * counter spot facing +π/2; else (the world-map return) the front door, facing
 * down (octant 4, angle 0, shop tier +0x2cde0 set) or left (octant 2, −π/2). */
static void postload_seat(void)
{
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    if (w == NULL || w[0x2cdf4 / 4] == 0) {
        player_ctrl_set_facing_angle(1.5707964f);
        scene1_postload_place_actors(0);
        return;
    }
    int32_t *p0 = player_ctrl_actor_record_mut(0);
    int32_t *p2 = player_ctrl_actor_record_mut(2);
    if (w[0x2cde0 / 4] != 0) {
        p0[CHR_ACTOR_FACING] = 4;
        p2[CHR_ACTOR_FACING] = 4;
        player_ctrl_set_facing_angle(0.0f);
    } else {
        player_ctrl_set_facing_angle(-1.5707964f);
        p0[CHR_ACTOR_FACING] = 2;
        p2[CHR_ACTOR_FACING] = 2;
    }
    scene1_postload_place_actors(1);
}

/* The placed collision objects of the house arm, in retail's aliased layout:
 * phase-1 i → object i, phase-2 j → object 5+j (DAT_0438bfb8.., c008/c058/c0a8/c0f8). */
void scene1_postload_mapcol_objs_from_walker(void)
{
    g_mapcol_objs.n1 = g_scene1_walker_phase1_count;
    g_mapcol_objs.n2 = g_scene1_walker_phase2_count;
    for (int i = 0; i < g_scene1_walker_phase1_count && i < 5; i++) {
        g_mapcol_objs.slot[i] = g_scene1_walker_phase1_mesh_index[i];
        g_mapcol_objs.rot[i]  = g_scene1_walker_phase1_rot_y[i];
        g_mapcol_objs.x[i]    = g_scene1_walker_phase1_pos_x[i];
        g_mapcol_objs.y[i]    = g_scene1_walker_phase1_pos_y[i];
        g_mapcol_objs.z[i]    = g_scene1_walker_phase1_pos_z[i];
    }
    for (int j = 0; j < g_scene1_walker_phase2_count && 5 + j < MAPCOL_OBJS; j++) {
        g_mapcol_objs.slot[5 + j] = g_scene1_walker_phase2_mesh_type[j];
        g_mapcol_objs.rot[5 + j]  = g_scene1_walker_phase2_rot_y[j];
        g_mapcol_objs.x[5 + j]    = g_scene1_walker_phase2_pos_x[j];
        g_mapcol_objs.y[5 + j]    = g_scene1_walker_phase2_pos_y[j];
        g_mapcol_objs.z[5 + j]    = g_scene1_walker_phase2_pos_z[j];
    }
}

/* The ground probe FUN_00436f97 runs around a floor-object spot: the spot,
 * then up to 4 points 2 away ((k + 0.5)·2π/4); a point is taken when the
 * ground is within 0.5 of y 0.  Returns 0 when none is (p = the last try). */
static int postload_spot_probe(float p[3], float cx, float cz)
{
    static const float k_quad = 4.0f, k2pi = 6.2831854820251465f, k0p5 = 0.5f;
    for (int k = 0; k < 5; k++) {
        p[0] = cx; p[1] = 0.0f; p[2] = cz;
        if (k > 0) {
            float a = spill32((long double)k);
            a = spill32(((long double)a + k0p5) * k2pi / k_quad);
            const double ad = a;
            float t = spill32(x87_crt_sin(ad));
            p[0] = spill32((long double)t + t + p[0]);
            t = spill32(x87_crt_cos(ad));
            p[2] = spill32((long double)t + t + p[2]);
        }
        mapcol_hit_t hit;
        if (mapcol_ground(p[0], spill32((long double)p[1] + k0p5), p[2], &hit) == 1 &&
            spill32(fabsl((long double)hit.height)) < k0p5)
            return 1;
    }
    return 0;
}

/* a cell's centre: (-cell·40) */
static float postload_cell_xy(int32_t c)
{
    return spill32((long double)spill32((long double)-c) * 40.0f);
}

/* FUN_00436f97's `> 4` arm: the per-dungeon setup, the enemy placement, the
 * floor objects (FUN_0044ad81), the floor title. */
static void postload_dungeon_arm(void)
{
    static const float k0p35 = 0.3499999940395355f, k0p5 = 0.5f;
    uint8_t *rec = save_work_bank_at(save_work_active_slot());
    if (rec == NULL) return;
    int32_t story; memcpy(&story, rec + 187748, 4);   /* work dword +0x2dd64 */
    if (g_scene1_combat_stage_id == 0) {
        /* FUN_0044c88f: dungeon 0 floor 4 (the tutorial boss) — the party
         * lined up 1.5 apart in x from the spawn (5 further in z once the
         * story is past 2), facing 0, and the exit portal (kind 6, spent) on
         * the spawn */
        static const float k1p5 = 1.5f, k5 = 5.0f;
        g_dat_09643128 = 0;
        g_event_tick = 0;
        if (stage_gate_get_next() == 4) {
            PW(0x56db05cu).f = 0.0f;
            for (int i = 0; i < 3; i++) {
                const float o = spill32((long double)spill32((long double)i) * k1p5);
                g_party.pos[i][0] = spill32((long double)o + g_scene1_stage_player_default_pos[0]);
                g_party.pos[i][1] = g_scene1_stage_player_default_pos[1];
                g_party.pos[i][2] = story == 2
                    ? spill32(((long double)g_scene1_stage_player_default_pos[2] - o) + k5)
                    : spill32((long double)g_scene1_stage_player_default_pos[2] - o);
            }
            const float sc[3] = { k0p35, k0p35, k0p35 };
            dobj_spawn_special(0, 6, g_scene1_stage_player_default_pos, sc, 0.0f);   /* FUN_0044b257 */
        }
    } else if (g_scene1_combat_stage_id == 1) {
        dungeon1_stage_setup();                  /* FUN_0044d1e4 */
    } else {
        /* PORT-DEBT(stub, FUN_00436f97): dungeons 2..5's setups FUN_0044d9d3 /
         * 44e08f / 450443 / 450ee0 */
    }
    if ((stage_post_load_get_mode_b4d0() >= 0 && g_dat_0438b4d4 == 0) ||
        (g_scene1_combat_stage_id == 5 && stage_gate_get_next() == 30))
        g_equip_prompt = 1;
    const enemy_place_in_t in = {
        { g_scene1_combat_stage_id, stage_gate_get_next(), stage_post_load_get_mode_b4d0(),
          g_ghost_count, rec[0x2bc57] },
        rec, stage_gate_floor_is_checkpoint, stage_gate_query,
    };
    if (stage_gate_floor_is_checkpoint()) enemy_place_boss(&in);   /* FUN_00431b1c */
    else enemy_place_floor(&in);                                   /* FUN_00431d54 */
    rec[0x2bc82] = 0;
    if (g_scene1_combat_stage_id == 0 && stage_gate_get_next() == 4) {
        const float *o = g_scene1_stage_player_default_pos;
        if (rec[0x2bc81] != 0 || story == 2) {
            const float p[3] = { o[0], spill32((long double)o[1] + 2.0f), spill32((long double)o[2] + 10.0f) };
            const float sc[3] = { k0p35, k0p35, k0p35 };
            dobj_spawn(p, sc, 0.0f, 6);
        } else {
            rec[0x2bc82] = 1;
            const float p[3] = { o[0], 2.0f, spill32((long double)o[2] + 10.0f) };
            const float sc[3] = { k0p5, k0p5, k0p5 };
            dobj_spawn(p, sc, 0.0f, 4);
        }
    } else if (rec[0x2bce2] == 0 && rec[0x2bcf8] == 0 && !stage_gate_floor_is_checkpoint()) {
        /* the next-floor portal at the exit cell */
        float ex[3];
        const float cx = postload_cell_xy(g_floor_marks[1][0]), cz = postload_cell_xy(g_floor_marks[1][1]);
        if (!postload_spot_probe(ex, cx, cz)) { ex[0] = cx; ex[1] = 0.0f; ex[2] = cz; }
        const float sc5[3] = { k0p5, k0p5, k0p5 };
        dobj_spawn(ex, sc5, 0.0f, 8);
        if (g_scene1_combat_stage_id == 5 && (stage_gate_get_next() == 9 || stage_gate_get_next() == 19)) {
            /* PORT-DEBT(stub, FUN_00436f97): dungeon 5's return portal 6 beside
             * the exit (floors 9 / 19) */
        }
        /* the marked cells' objects, 4 / 5 alternating */
        for (int32_t k = 0; k < g_floor_extra_n; k++) {
            float p[3];
            if (!postload_spot_probe(p, postload_cell_xy(g_floor_marks[2 + k][0]),
                                     postload_cell_xy(g_floor_marks[2 + k][1])))
                continue;
            dobj_spawn(p, sc5, 0.0f, (k & 1) ? 5 : 4);
        }
        if (stage_post_load_get_dat_0438b4c4() != 0) {
            dungeon_title_banner_open(180, 1);   /* FUN_00406551 */
            g_floor_intro = 200;
            stage_post_load_set_dat_0438b4c4(0);
        }
    }
}

void scene1_postload_stage_entry_init(void)
{
    /* L1-2: DAT_068dd2f0 = the DAT_0438b4dc record (a guild Leave leaves the
     * dungeon stage there, quirk §141); DAT_0438b1bc = 0; DAT_0438cbe4 = 0
     * (no port mirror outside the placement input). */
    stage_palette_select(g_stage_b4dc);
    g_actor_alloc_n = 0;
    /* the floor-flow scalars of the head (b4b4, b8b0 = -1, be94..becc, b1e8 …)
     * and DAT_0438c150 (the mover count) = 0 */
    dungeon_flow_stage_reset();
    g_mapcol_movers.on[0] = 0;
    /* the camera yaw DAT_073de39c = 0 (the house arm sets π below);
     * PORT-DEBT(stub, FUN_0044b219): the camera shake reset, DAT_073de3a0 = 45 */
    g_scene1_camera_yaw = 0.0f;
    /* L8-L84: the scene BSS resets the port mirrors — the actor-say bubble
     * driver (b764/b76c), the counter-menu slides (b74c/b750/b754), the news
     * ticker (b92c), the camera snap flag (cc68).  b924 = 0 has no port mirror outside the iv2 beat. */
    actor_say_stage_reset();
    counter_menu_stage_reset();
    g_news_ticker_timer = 0;
    scene1_camera_snap_next();          /* DAT_0438cc68 = 1 */
    scene1_beat_set_counter(0);         /* DAT_0438b924 = 0 */

    /* L116-127: the companion wander state (DAT_0438be6c/70/74/78). */
    scene1_companion_ctrl_stage_reset();

    /* the 20-entry object columns DAT_0438bfb8 / c008 / c058 / c0a8 / c0f8 = 0
     * (the counts bfb0/bfb4 are left alone) */
    memset(g_mapcol_objs.slot, 0, sizeof g_mapcol_objs.slot);
    memset(g_mapcol_objs.rot, 0, sizeof g_mapcol_objs.rot);
    memset(g_mapcol_objs.x, 0, sizeof g_mapcol_objs.x);
    memset(g_mapcol_objs.y, 0, sizeof g_mapcol_objs.y);
    memset(g_mapcol_objs.z, 0, sizeof g_mapcol_objs.z);

    /* DAT_0438c85c..: 225 cells of 2400 (7200 in dungeon 0); cbe0 / cbe4 = 0
     * (dungeon_flow_stage_reset).  The party chars: a dungeon floor's party is
     * the adventurer (by DAT_0438b7d8), Recette, Tear; the house's Recette and
     * Tear (actor 1 off). */
    for (int k = 0; k < 225; k++) g_floor_timer[k] = g_scene1_combat_stage_id != 0 ? 2400 : 7200;
    g_ghost_timer = 0;
    g_ghost_count = 0;
    {
        const stage_record_t *st = scene1_current_stage_record();
        if (st != NULL && (uint32_t)st->dungeon_id > 4u) {
            static const int32_t k_chr[8] = { 3, 27, 28, 30, 40, 31, 41, 42 };
            const int32_t c = chara_equip_get_current_chara();
            g_party.chr[0] = (c >= 0 && c < 8) ? k_chr[c] : 42;
            g_party.chr[1] = 0;
        } else {
            g_party.chr[1] = -1;
            g_party.chr[0] = 0;
        }
        g_party.chr[2] = 1;
    }

    /* L21 + L180: DAT_0438b484 clear + the floor-layout generator. */
    scene1_preload_floor_layout();

    /* the hurry-up timer: rooms × 3600 / 2400 / 2100 / 1800 / 1200 / 900 by dungeon */
    {
        static const int32_t k_per_room[6] = { 3600, 2400, 2100, 1800, 1200, 900 };
        const int32_t d = g_scene1_combat_stage_id;
        if (d >= 0 && d < 6) g_floor_timer[0] = k_per_room[d] * g_floor_rooms_total;
    }

    /* the party: the three actors at the floor origin (actor 0 probed for ground),
     * their records / scales, the history rings, the block's scalars, db054 */
    party_stage_reset();
    /* the stage object table */
    dobj_reset();

    /* L384-399: FUN_0040d132 (PORT-DEBT(stub, FUN_0040d132)), FUN_0040f64b(1),
     * FUN_0041f252, FUN_004060ff, FUN_004850ec, FUN_0046f1e9. */
    g_dat_06a46fa0 = 0;
    scene1_records_reset(1);
    actor_spawn_reset();
    dobj_place_markers();                /* FUN_00432473 */
    scene1_top_hud_tooltip_house_entry();
    speech_bubble_reset();
    g_title_banner = g_title_banner_len = g_title_banner_kind = 0;   /* FUN_004060ff's part */
    player_ctrl_cc08_enter_freeroam();
    /* b8a0 = b8a4 = 0 (the per-dungeon event stage / counter; b8a0 was 1
     * through the init), DAT_0438ccb0 = DAT_0438b8fc = 0, FUN_0044bd03 */
    g_event_stage = 0;
    g_event_tick = 0;
    {
        const uint8_t *rec = save_work_bank_at(save_work_active_slot());
        scene1_bg_npc_stage_reset(rec ? rec[0x2bcfd] != 0 : 0);
    }

    const stage_record_t *sr = scene1_current_stage_record();
    if (sr != NULL && sr->dungeon_id > 4) {
        postload_dungeon_arm();
    } else {
        /* L607-725 the alt-stage (house) arm: camera yaw π + the furniture /
         * room placement (walker_phase2_init, inputs unchanged since the load),
         * then the seat: work dword +0x2cdf4 == 0 → the counter spot facing +π/2;
         * else the front door, facing down (octant 4, +0x2cde0 set) or left
         * (octant 2, −π/2). */
        scene1_postload_walker_phase2_init();
        scene1_postload_mapcol_objs_from_walker();
        /* L668-677: qword DAT_0438cbfc = -1 (no faced display cell), DAT_0438cbe8 = 0
         * (the display menu's open-once latch). */
        shop_display_highlight_clear();
        player_ctrl_display_latch_reset();
        postload_seat();
    }

    g_floor_spawn_yaw = PW(0x56db05cu).f;   /* DAT_0438b4ac */

    /* L727-740 the ambient spawn (gated off in the house). */
    scene1_postload_ambient_spawn();

    /* L741-746: FUN_0046f892, FUN_00435612, FUN_004852fb (in the shop while
     * b928 == 1 — a world-map exit on a morning-beat day: the actors re-seat
     * at the counter, Recette facing Tear, anim 6), FUN_0048439a (the shop
     * attribute centroid), FUN_00435fbb(0, -1) (the drop weights step). */
    scene1_customer_npc_reset();
    title_save_dialog_cursor_set_visible(0);
    if (g_stage_palette != NULL && g_stage_palette->mode == 0 && scene1_beat_flag() == 1)
        scene1_house_morning_reseat();
    {
        const uint8_t *rec = save_work_bank_at(save_work_active_slot());
        if (rec != NULL)
            roster_compute_centroid(rec);
    }
    stage_post_load_pulse_5fold(0, -1);
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

/* The ACTIVE WORKING record (DAT_044e3798 + DAT_0438b1e0*0x2dfc8) — the record
 * retail's FUN_0048ffd9 seeds and the Cf walker reads.  Was save_arena_base() +
 * slot*stride: the SAVE-FILE arena without its 0xb10-byte header, so the seed
 * landed in file bank 0 dwords 0xb0b4.. (found 2026-10-13 by rhost.py portsave
 * day2-save-commit: exactly the tier-0 furniture template, 88 bytes) and the
 * readers never saw the loaded save's own record. */
static uint8_t *scene1_active_save_record(void)
{
    return save_work_bank_at(chara_equip_get_current_bank());
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
    /* Only the -1 artifact is normalised: a loaded save's record already holds
     * its real selector/char_mode (copied save→work on load). */
    const int32_t zero = 0;
    int32_t sel, cm;
    memcpy(&sel, rec + SCENE1_REC_LAYOUT_SEL_OFF, sizeof sel);
    memcpy(&cm,  rec + SCENE1_REC_CHARMODE_OFF,   sizeof cm);
    if (sel == -1) memcpy(rec + SCENE1_REC_LAYOUT_SEL_OFF, &zero, sizeof zero);
    if (cm == -1)  memcpy(rec + SCENE1_REC_CHARMODE_OFF,   &zero, sizeof zero);

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
     * makes the 180° HOUSE camera flip faithful.  The sibling
     * `_DAT_056db060 = π` (L590) is the yaw FUN_0048b850 eases the camera to. */
    g_scene1_camera_yaw = 3.1415927f;
    PW(0x56db060u).f = 3.1415927f;

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
