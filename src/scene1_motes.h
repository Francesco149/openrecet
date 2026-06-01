/*
 * scene1_motes.h — HOUSE free-roam ambient floor motes.
 *
 * Ports the 3-function ambient-mote subsystem:
 *
 *   FUN_0046f621 (0x46f621, 39 B)  — scene1_motes_tick()
 *         warmup pump: the FIRST call runs the sim 180× (DAT_073a8bb8
 *         latch), every later call runs it once.  Called once per frame
 *         on FUN_0048670f's main path (all.c:86722).
 *   FUN_0046f2a3 (0x46f2a3, 894 B) — scene1_motes_sim_once()
 *         spawn + integrate the 6 motes (DAT_005c7dd4).  THE RNG consumer:
 *         every mote drifts along X at floor level, re-rolling z/velocity/
 *         mode off the shared LCG (DAT_006023a0) on each bound-cross
 *         respawn — the sporadic free-roam consumer in
 *         findings/scene1-rng-stream-parity.md.
 *   FUN_0046f648 (0x46f648, 239 B) — scene1_motes_render()
 *         draw each mote as a soft dark contact-shadow blob, INSIDE the
 *         shadow pass's render envelope (called from FUN_00470385 @ the
 *         FUN_0045aa36 L122 slot; see scene1_chr_shadow.c).
 *
 * Field map (engine record: 100 B / 0x19 dw, base DAT_073a7f80, the SoA
 * the three functions share — see objdump @ 0x46f2a3).  We model only the
 * fields the sim/render touch; the leading 6-dword sprite-anim header
 * (fed to FUN_00482a51/71) is a deferred stub — it drives an unported
 * BRIGHT-sprite pass, consumes NO RNG, and is NOT read by the contact-
 * shadow render, so omitting it is behaviour-identical here.
 */
#ifndef OPENRECET_SCENE1_MOTES_H
#define OPENRECET_SCENE1_MOTES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_005c7dd4 — static .data, = 6 for the INGAME controller scene. */
#define SCENE1_MOTE_COUNT 6

/* Per-mote state (engine offsets in comments, base DAT_073a7f80). */
typedef struct {
    int   dir;       /* +0x44: drift direction +1 / -1 (0 == not yet spawned) */
    int   visible;   /* +0x48: render flag (0 draws, -1 hides) */
    int   type;      /* +0x4c: sprite-type index, DAT_005c7dd8[idx%6] */
    float x;         /* +0x2c: drift coordinate (the only animated axis) */
    float y;         /* +0x30: always 0 (render lifts +0.08) */
    float z;         /* +0x34: room depth (-11 .. -15) */
    float speed;     /* +0x50: per-tick drift magnitude factor, [0.5,1.0) */
    int   pause;     /* +0x54: pause-state counter (never set — see .c) */
    float vthresh;   /* +0x58: mode-switch X target (feeds the dead pause path) */
    int   mode;      /* +0x5c: 0/1/2 anim-mode selector (feeds the dead path) */
    int   prob;      /* +0x60: rng%100 probability base (mode rolls) */
    int   flip;      /* +0x18: sprite flip (1 spawn / 6 dir+ / 2 dir-) */
    int   anim_id;   /* +0x14: requested anim id (-1 init); fed to the
                      *        deferred sprite-anim stub */
} scene1_mote_t;

/* The live mote array (inspection / host tests). */
extern scene1_mote_t g_scene1_motes[SCENE1_MOTE_COUNT];

/* FUN_0046f621: warmup-then-per-frame pump.  Replaces the old
 * player_ctrl_prologue_churn no-op in the controller tick. */
void scene1_motes_tick(void);

/* FUN_0046f2a3: one spawn+integrate pass (exposed for the warmup + tests). */
void scene1_motes_sim_once(void);

/* Zero all mote state + cursors (scene (re)entry).  The engine's
 * equivalent reset of DAT_073a8bb0/bb4/bb8 lives in an unported scene-load
 * path; calling this at HOUSE entry keeps a fresh warmup. */
void scene1_motes_reset(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* FUN_0046f648: draw the mote contact-shadow blobs.  MUST run with the
 * shadow pass's render envelope active (shade.bmp texture + multiplicative
 * darken + ZWRITE off) — call it from inside scene1_chr_shadow_render. */
void scene1_motes_render(struct IDirect3DDevice8 *dev);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_MOTES_H */
