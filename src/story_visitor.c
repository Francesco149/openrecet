/*
 * story_visitor.c — see story_visitor.h.  Line refs are retail VAs.
 *
 * No recording reaches the scene yet (debt-gameover-restart's day 8 never sets
 * +0x2bcd8): ported from the disasm, unverified.  PORT-DEBT(render-coupled,
 * FUN_00470a46): the visitor's two sprites (chars 47 / 48 at DAT_073a8b38 /
 * b44, shown while [0x24]) are not drawn.
 */
#include "story_visitor.h"

#include <math.h>
#include <string.h>

#include "dungeon_party.h"        /* FUN_00482a51 */
#include "fade.h"                 /* FUN_004526f5 / FUN_004528b3 / FUN_0045281c */
#include "party.h"
#include "save_work.h"
#include "scene1_actor_say.h"     /* DAT_0438b764 */
#include "scene1_chr_sprite.h"    /* FUN_00482a71 */
#include "scene1_intro_dialogue.h"/* FUN_0044ba2c */
#include "scene1_particles_tick.h"/* DAT_073de39c */
#include "scene1_player_ctrl.h"   /* DAT_056db000 / 004 / 048 */
#include "scene1_postload.h"      /* FUN_004851e2 */
#include "scene1_shop_walker.h"   /* FUN_0046fbee, the walker layout */
#include "scene1_tutorial_dispatch.h" /* DAT_0438b924 */
#include "rng.h"
#include "x87.h"

int32_t g_story_visitor[0x26];
int32_t g_story_visitor_chr[2][11];
float   g_story_visitor_pos[2][3];
int32_t g_story_visitor_frame;
int32_t g_story_visitor_meet = 1200;

static const float k_pi2 = 1.5707963705062866f, k_mpi2 = -1.5707963705062866f;
static const float k_pi8 = 0.39269909262657166f, k_2pi = 6.2831854820251465f, k8 = 8.0f;

/* the walker record's float fields (the customer-walker layout) */
typedef union { int32_t i; float f; } vf_t;
#define VF(off) (((vf_t *)(void *)&g_story_visitor[(off)])->f)

static uint8_t *work(void) { return save_work_bank_at(save_work_active_slot()); }

int story_visitor_active(void)
{
    const uint8_t *wb = work();
    return wb != NULL && wb[STORY_SCENE_OFF] != 0;
}

/* the player faces Tear: −π/2 / octant 2 when Tear is on the left, else +π/2 / 6 */
static void face_tear(int32_t tear_octant_left, int32_t tear_octant_right)
{
    if (g_party.pos[2][0] <= g_party.pos[0][0]) {
        PW(0x56db05cu).f = k_mpi2;
        g_party.rec[0][6] = 2;                       /* DAT_056dab00 */
        if (tear_octant_left >= 0) g_party.rec[2][6] = tear_octant_left;   /* DAT_056dab58 */
    } else {
        PW(0x56db05cu).f = k_pi2;
        g_party.rec[0][6] = 6;
        if (tear_octant_right >= 0) g_party.rec[2][6] = tear_octant_right;
    }
}

/* FUN_004706ea */
void story_visitor_start(void)
{
    static const float k3 = 3.0f, km3p24 = -3.240000009536743f, k0p7 = 0.699999988079071f;
    static const float k9 = 9.0f, k7 = 7.0f;
    const uint8_t *wb = work();
    const float yaw = g_scene1_camera_yaw;
    PW(0x56db05cu).f = spill32(-(long double)yaw);
    actor_say_stage_reset_age();                     /* DAT_0438b764 = 0 */
    /* DAT_0438be80 = 1, DAT_0438be7c = 0: no port mirror. */
    player_ctrl_emote_reset();                       /* DAT_056db000 / 004 = 0 */
    g_party.rec[0][6] = x87_ftol(((((long double)PW(0x56db05cu).f + yaw) + k_pi8) / k_2pi) * k8 + k8) & 7;
    scene1_postload_place_actors(0);                 /* FUN_004851e2(0) */
    face_tear(-1, -1);
    party_anim_set(g_party.rec[0], 6);
    VF(CS_NPC_OFF_POS_X) = k3;
    player_ctrl_set_db048(0);                        /* DAT_056db048 = 0 */
    /* DAT_0438b4e8 = 0: no port mirror. */
    VF(CS_NPC_OFF_POS_Y) = 0.0f;
    g_story_visitor_meet = 1200;
    g_story_visitor[0x24] = 1;
    VF(CS_NPC_OFF_POS_Z) = km3p24;
    g_story_visitor[0x25] = 0;
    g_story_visitor_frame = 0;
    VF(CS_NPC_OFF_VEL_X) = 0.0f;
    VF(CS_NPC_OFF_VEL_X + 1) = 0.0f;
    VF(CS_NPC_OFF_VEL_Z) = 0.0f;
    party_anim_set(g_story_visitor_chr[0], 1);
    party_anim_set(g_story_visitor_chr[1], 0);
    g_story_visitor_chr[0][6] = 2;
    g_story_visitor_chr[1][6] = 2;
    int32_t tier = 0;
    if (wb != NULL) memcpy(&tier, wb + 0x2cde0, 4);
    g_story_visitor[CS_NPC_OFF_GRID_X] = tier == 0 ? 6 : 7;
    g_story_visitor[CS_NPC_OFF_GRID_Y] = tier == 0 ? 2 : 0;
    g_story_visitor[CS_NPC_OFF_TYPE_IDX] = -1;
    g_story_visitor[CS_NPC_OFF_DETOUR] = 1;
    g_story_visitor[CS_NPC_OFF_FACING] = 1;
    g_story_visitor[CS_NPC_OFF_FLAG7] = 0;
    g_story_visitor[CS_NPC_OFF_FLAG8] = 0;
    g_story_visitor[CS_NPC_OFF_FLAG9] = 0;
    VF(CS_NPC_OFF_POS_X) = spill32(2.0L * spill32((long double)g_story_visitor[CS_NPC_OFF_GRID_X]) - k9);
    VF(CS_NPC_OFF_POS_Y) = 0.0f;
    VF(CS_NPC_OFF_POS_Z) = spill32(2.0L * spill32((long double)g_story_visitor[CS_NPC_OFF_GRID_Y]) - k7);
    const uint32_t r = rng_next15();
    g_story_visitor[CS_NPC_OFF_PARAM21] = 0;
    g_story_visitor[CS_NPC_OFF_ANIMCYCLE] = 0;
    VF(CS_NPC_OFF_SPEED) = k0p7;
    g_story_visitor[CS_NPC_OFF_WTIMER] = 0;
    g_story_visitor[CS_NPC_OFF_WSTATE] = -1;
    g_story_visitor[CS_NPC_OFF_FLAGS] = (int32_t)(r & 1u);
}

/* FUN_00470a46 */
void story_visitor_tick(void)
{
    static const float k2p5 = 2.5f, k0p3 = 0.30000001192092896f, k0p1 = 0.10000000149011612f;
    uint8_t *wb = work();
    if (wb == NULL) return;
    const int32_t meet = g_story_visitor_meet;
    if (scene1_beat_counter() < meet - 30) {
        const float dx = spill32((long double)g_party.pos[0][0] - VF(CS_NPC_OFF_POS_X));
        const float dz = spill32((long double)g_party.pos[0][2] - VF(CS_NPC_OFF_POS_Z));
        const float d = spill32((long double)spill32((long double)sqrt(spill64((long double)dz * dz + (long double)dx * dx))));
        if (d < k2p5) g_story_visitor_meet = scene1_beat_counter() + 2;
    }
    const int32_t b924 = scene1_beat_counter();
    if (b924 >= 180 && b924 < g_story_visitor_meet) {
        int32_t tier = 0;
        memcpy(&tier, wb + 0x2cde0, 4);
        scene1_customer_npc_walk(g_story_visitor, tier);       /* FUN_0046fbee */
        const int32_t ws = g_story_visitor[CS_NPC_OFF_WSTATE];
        party_anim_set(g_story_visitor_chr[0], ws == 2 || ws == -1 ? 1 : 0);
    }
    const int32_t m = g_story_visitor_meet;
    if (b924 == m && !scene1_intro_dialogue_busy()) {           /* FUN_0044ba2c(10, 1, 0) */
        scene1_intro_dialogue_start_single(10, 1);
        wb[STORY_MET_OFF] = 1;
    }
    if (b924 == m + 50) {
        g_story_visitor[0x25] = 1;
        party_anim_set(g_story_visitor_chr[0], 2);
        party_anim_set(g_story_visitor_chr[1], 3);
    }
    if (b924 == m + 65) g_story_visitor[0x24] = 0;
    if (b924 == m + 80) party_anim_set(g_story_visitor_chr[1], 0);
    if (b924 == m + 110) party_anim_set(g_story_visitor_chr[1], 2);
    if (b924 == m + 230) party_anim_set(g_story_visitor_chr[1], 0);
    if (b924 == m + 260) {
        g_story_visitor_frame = 0;
        if (!scene1_intro_dialogue_busy())                      /* FUN_0044ba2c(10, 2, 0) */
            scene1_intro_dialogue_start_single(10, 2);
    }
    if (b924 == m + 290) {
        fade_phase1_start(0, 30);                               /* FUN_004526f5 */
    } else if (b924 > m + 290 && fade_is_done()) {
        fade_phase_out_start(0, 30);                            /* FUN_0045281c */
        wb[STORY_SCENE_OFF] = 0;
        wb[STORY_MET_OFF] = 0;
        scene1_beat_set_counter(192);
        face_tear(6, 2);
        party_anim_set(g_party.rec[0], 6);
        party_anim_set(g_party.rec[2], 4);
    }
    story_visitor_aux_tick();                                   /* FUN_004708f7 */
    for (int k = 0; k < 2; k++) {
        g_story_visitor_pos[k][0] = VF(CS_NPC_OFF_POS_X);
        g_story_visitor_pos[k][1] = VF(CS_NPC_OFF_POS_Y);
        g_story_visitor_pos[k][2] = VF(CS_NPC_OFF_POS_Z);
    }
    if (scene1_beat_counter() >= g_story_visitor_meet + 50) {
        const float t = spill32((long double)(scene1_beat_counter() - g_story_visitor_meet - 50));
        g_story_visitor_pos[0][1] = spill32((long double)t * k0p3 + VF(CS_NPC_OFF_POS_Y));
        g_story_visitor_pos[0][0] = spill32((long double)t * k0p1 + VF(CS_NPC_OFF_POS_X));
    }
}

/* FUN_00470970: the player and Tear face the visitor (the longer axis); from
 * meet + 50 the second sprite faces the player's side */
void story_visitor_face(void)
{
    const float dx = spill32((long double)g_party.pos[0][0] - VF(CS_NPC_OFF_POS_X));
    const float dz = spill32((long double)g_party.pos[0][2] - VF(CS_NPC_OFF_POS_Z));
    const float adx = spill32(fabsl((long double)dx)), adz = spill32(fabsl((long double)dz));
    if (adx <= adz) g_party.rec[0][6] = dz >= 0.0f ? 0 : 4;
    else g_party.rec[0][6] = dx >= 0.0f ? 2 : 6;
    g_party.rec[2][6] = g_party.rec[0][6];
    if (scene1_beat_counter() > g_story_visitor_meet + 50)
        g_story_visitor_chr[1][6] = dx >= 0.0f ? 6 : 2;
}

/* FUN_004708f7 */
void story_visitor_aux_tick(void)
{
    if (!story_visitor_active()) return;
    if (scene1_beat_counter() >= g_story_visitor_meet + 260)
        party_anim_set(g_story_visitor_chr[1], g_story_visitor_frame % 180 >= 30 ? 0 : 2);
    chr_anim_tick(g_story_visitor_chr[0], 47, 1.0f);
    chr_anim_tick(g_story_visitor_chr[1], 48, 1.0f);
    ++g_story_visitor_frame;
}
