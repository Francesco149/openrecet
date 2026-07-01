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

#include <stddef.h>
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

/* Full engine record stride in dwords (0x64 / 4 = 25): the {bgnpcpin} capture
 * carries SCENE1_BG_NPC_COUNT of these raw engine records (objdump-verified
 * field map @ 0x46f2a3 / all.c:68776 — x@dw11/+0x2c, y@12, z@13, dir@dw17/+0x44,
 * visible@18, type@19, speed@20, pause@21, vthresh@22, mode@23, prob@24; dwords
 * 14-16 = +0x38..+0x40 are unmodeled by the port and rng-inert). */
#define BG_NPC_ENGINE_DWORDS 25

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

/* Canonical RNG origin for the {phasepin} normalization.  The retail Frida
 * agent MUST seed DAT_006023a0 to the SAME value at the FUN_0046f621 warmup
 * entry, or the pinned layouts won't match.  Any fixed value works (it only
 * needs to be shared); chosen to give a good 6-NPC spread. */
#define SCENE1_BG_NPC_PHASEPIN_SEED 19937u

/* Trace-harness {phasepin}: re-arm the warmup so the next scene1_bg_npc_tick()
 * re-runs the 180x spawn pass from SCENE1_BG_NPC_PHASEPIN_SEED, giving a
 * load-phase-independent, port↔retail-reproducible window-NPC layout. */
void scene1_bg_npc_phasepin(void);

/* Trace-harness {bgnpcseed}: latch `seed`/`cursor` so the NEXT
 * scene1_bg_npc_tick() forces the shared LCG + g_bg_npc_spawn_cursor to them
 * right before consuming RNG (RE §21.21).  Unlike scene1_bg_npc_phasepin()
 * this does NOT call scene1_bg_npc_reset() — it is meant to fire BEFORE
 * bg_npc's NATURAL first-ever tick (the warmup), seeding it with retail's own
 * captured pre-warmup origin instead of the synthetic canonical 19937.
 *
 * `cursor` matters because the warmup's spawn cursor is NOT always 0 at the
 * true first FUN_0046f621 call: on the observed savefile it was already 1 —
 * some earlier activity (title-screen bg render?) had already spawned+frozen
 * slot 0 (STATE=-1, dir=0 — the "unspawned"/dead sentinel both bg_npc_tick and
 * the renderer skip) before scene1's own warmup ever runs, so retail's REAL
 * spawn sequence starts at slot 1.  Slot 0's exact leftover x/y/z/type don't
 * matter (dir==0 skips both the tick's position update and the render), only
 * which slot index the warmup starts spawning from — hence a cursor value,
 * not a full SoA snapshot.  `cursor` of 0 preserves the old (pre-RE-§21.21)
 * behaviour.  Using it after the warmup already ran just seeds the next
 * (non-warmup) tick and does not touch the cursor — it does not re-arm the
 * 180x pass; use scene1_bg_npc_phasepin() for that. */
void scene1_bg_npc_seed_pin(uint32_t seed, int cursor);

/* Trace-harness {bgnpcpin}: overwrite the live NPC array from `n_dwords` raw
 * engine records (SoA dwords, BG_NPC_ENGINE_DWORDS each) captured from retail's
 * NATURAL DAT_073a7f80 at the pin anchor — the rng-survey foundation pin.  Unlike
 * {phasepin}'s synthetic warmup re-seed (which fabricates a 19937-layout that
 * does NOT match the recording), this snapshots retail's real drifted positions
 * so both sides drift identically from the anchor (RE §21.1).  Translates each
 * engine record into the port struct field-by-field (the port layout is NOT
 * byte-compatible with the 0x64 engine record), reinterpreting float bits exactly.
 * Caps at SCENE1_BG_NPC_COUNT; marks the warmup done so the next tick neither
 * re-runs the 180x pass nor seeds new NPCs. */
void scene1_bg_npc_pin(const uint32_t *soa, size_t n_dwords);

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
