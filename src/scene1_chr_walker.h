/*
 * scene1_chr_walker.{c,h} — Cchr.2d: the HOUSE character-sprite walker
 * (engine FUN_00456f56 @ 0x456f56, 1982 B).
 *
 * This is the per-frame driver that BUILDS the world matrix + diffuse
 * color for every on-screen actor billboard and hands each to the
 * validated leaf renderer FUN_0045a56f (Cchr.2b, src/scene1_chr_sprite.c).
 * It is the render-side counterpart of the animation tick (Cchr.2c,
 * chr_anim_tick): the tick advances an actor's frame; this walker draws it.
 *
 * Dispatched from scene1_render_meshes (FUN_00459dfd L248-L251) in the
 * second WIDE-frustum slot (z_far = 2000), replacing the old
 * scene1_walk_wide_b_TODO stub.
 *
 * Four passes (engine line/asm verified @ 0x456f56):
 *   Preamble        — FVF 0x142, the additive-billboard TSS/RS envelope,
 *                     LightEnable(0,FALSE), LIGHTING=FALSE, FUN_0047047b.
 *   Pass 1 companion— DAT_056da1d4 != -1: one billboard (char id 2) at the
 *                     companion pos/scale, blend (ONE,ONE).
 *   Pass 2 player   — DAT_056da1cc != -1: a two-sweep loop over the actor
 *                     sprite-state array (stride 0x44).  Outer i=0 draws the
 *                     player array (slot-0x154); i=1 (gated on DAT_056daae0)
 *                     the party array.  Per actor: spawn-pop ease + draw-
 *                     order alpha, blend (SRCALPHA, INVSRCCOLOR).
 *   Mid + tail RS   — restore filters/lighting, palette-gated blend.
 *   Pass 3 NPC      — the people record table (DAT_0076c464 family, stride
 *                     0x2e9 dw — the SAME table scene1_shop_walker models):
 *                     off-screen fade ramp, char id 0x43, billboard quad.
 *   Pass 4 NPC sub  — same table, type==1: delegates to FUN_00456d48
 *                     (already ported in scene1_shop_walker).
 *
 * ── DORMANT IN HOUSE (today) ────────────────────────────────────────────
 * The actor sprite-state array (DAT_056dacc0 / companion DAT_056dab40) and
 * the people record table are populated by FUN_00436f97 (4788 B) — the
 * unported "Cf.* writer chunk" that STATUS.md lists as the top HOUSE-pixel
 * blocker.  Until it ports, those tables are empty, so the four pass bodies
 * iterate nothing and no character billboards appear — exactly the state of
 * the sibling walkers (scene1_shop_walker / scene1_alpha_walker).  The live
 * D3D state envelope IS correct; the dormant data is reached through the
 * chr_walker_*_slot accessors (return NULL / count 0 today).  When
 * FUN_00436f97 ports, swap those accessors to the real engine state and the
 * bodies fire verbatim.
 *
 * The pure scalar math each pass needs — fade-in, spawn-pop ease, draw-order
 * alpha, the NPC off-screen fade ramp — is split out as host-testable leaf
 * functions below.  Engine float constants decoded from .rdata 2026-05-29
 * (-75/-70/+70/50/255 fade ramp, 0.05 npc-scale, 0.02 z-bias, 10/20 ease,
 * 0.03 fade-scale, 30 fade divisor); FUN_00503954 == __ftol (truncate).
 */
#ifndef OPENRECET_SCENE1_CHR_WALKER_H
#define OPENRECET_SCENE1_CHR_WALKER_H

/* ── pure, host-testable per-actor math (engine FUN_00456f56) ───────────── */

/*
 * Fade-in factor for the whole companion+player block (engine L?? /
 * asm @ 0x457050).  counter = DAT_0438b4b4 (a scene-entry countdown);
 * the block only runs while counter <= 0x5a.  Returns (0x5a-counter)/30,
 * clamped to <= 1.0.  The caller multiplies this by 0.03 to get the
 * per-actor scale factor.
 */
float chr_walker_fadein(int counter);

/*
 * Spawn pop-in ease for a player/party actor (asm @ 0x4572bb).  age =
 * actor[0xe] (the spawn timer, also the alive gate).  Mutates the actor's
 * x/z scale in place: while age < 20 the sprite eases up in width
 * (× age/20) and squashes down in height (× ((20-age)/10 + 1)).  No-op
 * once age >= 20 (fully spawned).
 */
void chr_walker_spawn_ease(int age, float *sx, float *sz);

/*
 * Diffuse-alpha byte for a player/party actor (asm @ 0x4572fe).
 *   age        — actor[0xe] spawn timer.
 *   is_party   — outer sweep i==1 (party members) vs 0 (the player array).
 *   prio_base  — the per-slot draw-order priority (local_10: 0x9b, then
 *                -0x14 per slot) used by the party sweep.
 *   daae0      — DAT_056daae0 (party fade-state).
 * Returns the alpha (0..0x9b), or -1 if the actor should be skipped (the
 * engine's two `js` skip branches).  The caller forms color =
 * (alpha << 24) | 0x7f7fff.
 */
int chr_walker_actor_alpha(int age, int is_party, int prio_base, int daae0);

/*
 * Off-screen fade alpha for an NPC billboard (asm @ 0x45746b, __ftol path).
 *   pos  — record[-0x6a4], the actor's world coordinate along the fade axis.
 *   mult — record[+4], a 0..1 per-record alpha multiplier.
 * Returns the alpha byte, or <= 0 to skip the draw.  pos < -75 is fully
 * off-screen (skip); between -75 and -70 the alpha ramps 5..255; >= -70 is
 * full 255 before the per-record multiply.  Both stages truncate via __ftol.
 */
int chr_walker_npc_alpha(float pos, float mult);

/* ── MVP render-slot inject (populator-survey 2026-05-29) ────────────────
 *
 * The faithful per-frame populator of the walker's actor array (DAT_056dacc0)
 * is the ~18 KB FUN_0048b850 → FUN_0044376a subsystem (see the findings-doc
 * "POPULATOR SURVEY" banner — FUN_00436f97 only CLEARS the array).  Until
 * that ports, this inject hand-builds ONE player render slot so the ported
 * walker draws a standing actor in HOUSE end-to-end (matrix + alpha + D3D
 * state + leaf), validating everything but the populator.
 *
 * `scene1_chr_walker_set_inject` activates the inject and fills the player
 * sweep-0 slot 0 from the standing-pose fields below.  Pass `enable == 0`
 * to deactivate (the accessors revert to the dormant NULL/empty defaults).
 * Diffuse-only by design (no chr sheet bound), like the --force-player-
 * sprite leaf validation — it proves the walker's matrix/alpha/state path.
 *
 *   player_char — descriptor / formdata index (0 = Recette).
 *   anim/frame/facing — the leaf sprite-state ([0],[4],[6]); a static
 *     standing pose (anim 0, frame 2, facing 6 = the Cchr.2b-validated
 *     Recette billboard).  The timer/counter ([2]/[3]) are left 0 — the
 *     leaf selects the cell from `frame` directly (chr_anim_tick is not
 *     run here).
 *   px/py/pz — world position ([0xb..0xd], float); the new-game HOUSE
 *     groundtruth is (-0.30, 0, 9.35).
 *   age — the spawn/alive timer ([0xe]); >= 20 is fully spawned (no ease),
 *     and < 0x254 keeps the draw-order alpha positive.
 */
void scene1_chr_walker_set_inject(int enable, int player_char,
                                  int anim, int frame, int facing,
                                  float px, float py, float pz, int age);

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * Win32 render path (engine FUN_00456f56, full).  Lays down the live
 * additive-billboard D3D state envelope and runs the four actor passes.
 * Dormant in HOUSE until the actor/people tables populate (see header
 * note) OR scene1_chr_walker_set_inject seeds the MVP slot.
 */
void scene1_chr_walker_render(struct IDirect3DDevice8 *dev);
#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_CHR_WALKER_H */
