/*
 * scene1_bg_npc.h — HOUSE background-window NPCs (the townsfolk seen drifting
 * past the shop's back window).
 *
 * HISTORY / NAMING: this subsystem was originally ported under the name
 * "ambient floor motes" — a MISIDENTIFICATION.  It is actually the engine's
 * background-NPC system: up to 6 characters that drift horizontally behind the
 * shop and are seen through the back window.  Each draws (a) a dark contact
 * shadow on the street and (b) a bright character sprite.  There is no separate
 * genuine ambient-mote/particle effect in the engine (a minor true ambient
 * particle effect may re-emerge later — see docs/findings/scene1-bg-npc.md).
 *
 * Ports the engine functions:
 *
 *   FUN_0046f621 (0x46f621, 39 B)  — scene1_bg_npc_tick()
 *         warmup pump: the FIRST call runs the sim 180× (DAT_073a8bb8
 *         latch), every later call runs it once.  Called once per frame
 *         on FUN_0048670f's main path (all.c:86722).
 *   FUN_0046f2a3 (0x46f2a3, 894 B) — scene1_bg_npc_sim_once()
 *         spawn + integrate the 6 NPCs (DAT_005c7dd4).  THE RNG consumer:
 *         every NPC drifts along X at floor level, re-rolling z/velocity/
 *         mode off the shared LCG (DAT_006023a0) on each bound-cross
 *         respawn — the sporadic free-roam consumer in
 *         findings/scene1-rng-stream-parity.md.  Per tick it also steps each
 *         NPC's sprite-anim header via FUN_00482a51 (set-anim) +
 *         FUN_00482a71 (advance-frame).
 *   FUN_0046f648 (0x46f648, 239 B) — scene1_bg_npc_shadow_render()
 *         draw each NPC as a soft dark contact-shadow blob, INSIDE the
 *         shadow pass's render envelope (called from FUN_00470385 @ the
 *         FUN_0045aa36 L122 slot; see scene1_chr_shadow.c).  This is the
 *         "shadows" the back-window NPCs cast on the street.
 *   FUN_0046f737 (0x46f737, 347 B) — scene1_bg_npc_sprite_render()
 *         draw each NPC's bright character billboard via the shared chr-sprite
 *         leaf (FUN_0045a56f / scene1_chr_sprite_render), sheet DAT_073a9b18
 *         [DAT_005c7ce0[type*2]], colour 0xff7f7f7f, scale 0.03.  Called from
 *         the shop-walker (FUN_004552d0 L457) between FUN_004705a3 and
 *         FUN_00470d44.
 *
 * Engine record: 100 B / 0x19 dw, SoA base DAT_073a7f80.  The leading 11
 * dwords are a chr-actor sprite-state header (the CHR_ACTOR_* layout the
 * sprite leaf consumes); the drift/respawn fields follow.  See objdump @
 * 0x46f2a3 / 0x46f737.
 */
#ifndef OPENRECET_SCENE1_BG_NPC_H
#define OPENRECET_SCENE1_BG_NPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_005c7dd4 — static .data, = 6 for the INGAME controller scene. */
#define SCENE1_BG_NPC_COUNT 6

/* Length of the chr-actor sprite-state header (engine record +0x00..+0x28),
 * in dwords — the same leading record scene1_chr_sprite_render reads
 * (CHR_ACTOR_ANIM=0, TIMER=2, COUNTER=3, FRAME=4, STATE=5, FACING=6,
 * FLAG7=7, FLAG8=8, FLAG9=9, AGE=10). */
#define BG_NPC_REC_DWORDS 11

/* Per-NPC state (engine offsets in comments, base DAT_073a7f80). */
typedef struct {
    /* +0x00..+0x28: chr-actor sprite-state header.  Stepped by the sim
     * (bg_npc_anim_set / chr_anim_tick) and passed verbatim to the bright
     * sprite render's chr-sprite leaf.  FACING (dword 6) doubles as the drift
     * "flip" the engine writes (1 spawn / 6 dir+ / 2 dir-). */
    int32_t arec[BG_NPC_REC_DWORDS];

    int   dir;       /* +0x44: drift direction +1 / -1 (0 == not yet spawned) */
    int   visible;   /* +0x48: render flag (0 draws, -1 hides) */
    int   type;      /* +0x4c: sprite-type index, DAT_005c7dd8[idx%6] */
    float x;         /* +0x2c: drift coordinate (the only animated axis) */
    float y;         /* +0x30: always 0 (render lifts +0.08) */
    float z;         /* +0x34: room depth (-11 .. -15) */
    float speed;     /* +0x50: per-tick drift magnitude factor, [0.5,1.0) */
    int   pause;     /* +0x54: pause-state counter (never set — see .c) */
    float vthresh;   /* +0x58: mode-switch X target (feeds the dead pause path) */
    int   mode;      /* +0x5c: 0/1/2 anim-mode selector */
    int   prob;      /* +0x60: rng%100 probability base (mode rolls) */
} scene1_bg_npc_t;

/* The live NPC array (inspection / host tests). */
extern scene1_bg_npc_t g_scene1_bg_npc[SCENE1_BG_NPC_COUNT];

/* type → sprite-sheet char id (engine DAT_005c7ce0[type*2], static .data).
 * Returns -1 for an unmapped type. */
int scene1_bg_npc_type_to_char(int type);

/* FUN_0046f621: warmup-then-per-frame pump. */
void scene1_bg_npc_tick(void);

/* FUN_0046f2a3: one spawn+integrate(+anim-step) pass (warmup + tests). */
void scene1_bg_npc_sim_once(void);

/* Zero all NPC state + cursors (scene (re)entry). */
void scene1_bg_npc_reset(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_0046f648: draw the NPC contact-shadow blobs.  MUST run with the shadow
 * pass's render envelope active (shade.bmp texture + multiplicative darken +
 * ZWRITE off) — call it from inside scene1_chr_shadow_render. */
void scene1_bg_npc_shadow_render(struct IDirect3DDevice8 *dev);

/* FUN_0046f737: draw the NPC bright character billboards.  Self-contained
 * render-state envelope; call from the shop-walker render path. */
void scene1_bg_npc_sprite_render(struct IDirect3DDevice8 *dev);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_BG_NPC_H */
