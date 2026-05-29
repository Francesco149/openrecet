/*
 * scene1_chr_sprite.{c,h} — Cchr.2b: the HOUSE character sprite leaf
 * renderer (engine FUN_0045a56f, 0x45a56f, 1223 B).
 *
 * This is the leaf that draws Recette / Tear / NPC billboards in the
 * shop scene.  Cchr.1 ground-truthed it (the player sprite at
 * g_player_pos is a 12-prim DrawPrimitiveUP from this function);
 * Cchr.2a landed the static data layer it reads (chr_sprite_meta:
 * the per-character descriptor + the chr/formdata.bin blob).  This chip
 * is the renderer itself.
 *
 * The engine builds N textured billboard quads — one per 32×32 sprite
 * cell that composes the current frame — into a stack vertex buffer
 * (FVF 0x142 = XYZ|DIFFUSE|TEX1, stride 0x18) and draws them with
 * DrawPrimitiveUP against the currently-bound sprite-sheet texture and
 * the billboard world matrix the caller has already SetTransform'd.
 *
 * Split, like the rest of the scene1 renderers:
 *   - chr_sprite_build_quads()  — PURE geometry (host-testable): given
 *     the actor sprite-state, the char id, the formdata blob and the
 *     sheet-texture dims, it fills the vertex buffer exactly as the
 *     engine's per-cell loop does.  No D3D.
 *   - scene1_chr_sprite_render() — Win32 wrapper: SetTransform +
 *     build + the COLOROP-bracketed DrawPrimitiveUP tail.
 *
 * Faithful to objdump @ 0x45a56f; constants verified 2026-05-29 (see
 * docs/findings/scene1-char-sprite-render.md "Cchr.2b").
 *
 * --------------------------------------------------------------------
 * Actor sprite-state struct (engine param_1, stride 0x44 = 0x11 dwords):
 *   [0]  animation set     (×0x100 into the frame LUT)
 *   [4]  current frame idx
 *   [6]  facing index      (→ the 8-entry bank/flip facing tables)
 *   [7]  flag — alpha/color gate ([7]>=1 → RGB forced to white)
 *   [8]  flag — color gate / draw-tail selector
 *   [9]  flag — color gate / draw-tail selector
 *   [10] spawn age         (>0 drives the 20-frame shimmer ease)
 *   [0xb..0xd] pos.x/y/z   (read by the walker, not the leaf)
 * --------------------------------------------------------------------
 */
#ifndef OPENRECET_SCENE1_CHR_SPRITE_H
#define OPENRECET_SCENE1_CHR_SPRITE_H

#include <stddef.h>
#include <stdint.h>

/* Actor-state dword indices the leaf reads from param_1. */
#define CHR_ACTOR_ANIM    0
#define CHR_ACTOR_FRAME   4
#define CHR_ACTOR_FACING  6
#define CHR_ACTOR_FLAG7   7
#define CHR_ACTOR_FLAG8   8
#define CHR_ACTOR_FLAG9   9
#define CHR_ACTOR_AGE     10

/* FVF 0x142 vertex (XYZ | DIFFUSE | TEX1), stride 0x18 = 24 bytes. */
typedef struct {
    float    x, y, z;
    uint32_t diffuse;
    float    u, v;
} chr_sprite_vertex;

/* A sprite never composes more than ~1023 cells (the engine's stack
 * buffer is 0x24054 bytes ≈ 1024 quads); cap the host buffer there. */
#define CHR_SPRITE_MAX_QUADS  1024
#define CHR_SPRITE_MAX_VERTS  (CHR_SPRITE_MAX_QUADS * 6)

/*
 * Build the billboard quads for one actor sprite (engine FUN_0045a56f
 * per-cell loop).  Writes 6 vertices per cell (two triangles, emitted
 * as a TRIANGLELIST in the engine's exact order V0,V1,V2,V3,V0,V2) into
 * `out`, up to `out_max` vertices.  Returns the number of cells
 * (= quads); the caller draws `ncells*2` primitives.  Bit-faithful
 * vertex layout — host-tested against hand-computed geometry.
 *
 *   actor       — the sprite-state struct (param_1), >= 0x11 dwords.
 *   char_id     — descriptor / formdata index (engine param_2==param_3).
 *   color       — base ARGB diffuse (param_5); gated per [7]/[8]/[9].
 *   formdata    — the chr/formdata.bin blob (g_chr_formdata).
 *   formdata_sz — its byte size (bounds-checks every blob read).
 *   tex_w/tex_h — the bound sheet-texture dims (engine DAT_073a9b1c /
 *                 DAT_073a9b20 [char_id*0x10]).
 *
 * Returns 0 (and writes nothing) on a NULL/degenerate input or when a
 * required blob read would fall outside formdata_sz.  Requires the
 * char's descriptor to have been parsed (chr_meta_*).
 */
int chr_sprite_build_quads(chr_sprite_vertex *out, int out_max,
                           const int32_t *actor, int char_id,
                           uint32_t color,
                           const uint8_t *formdata, size_t formdata_sz,
                           int tex_w, int tex_h);

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * Win32 render path (engine FUN_0045a56f, full).  SetTransform(WORLD,
 * world) → build the quads from g_chr_formdata → DrawPrimitiveUP.
 *
 * The draw tail mirrors the engine's flag-gated branch: when
 * `(actor[8] < 1 || actor[9] != 0) && actor[7] > 0` it brackets the
 * draw with COLOROP=7/8 (the engine's two SetTextureStageState calls);
 * otherwise it issues a single DrawPrimitiveUP.
 *
 * The engine leaf inherits FVF 0x142 + the bound sheet texture from its
 * caller (the actor walker); for the standalone MVP this wrapper sets
 * the FVF itself but leaves the texture binding to the caller.
 *
 * No-op when dev/actor is NULL or the sprite composes 0 cells.
 */
void scene1_chr_sprite_render(struct IDirect3DDevice8 *dev,
                              const int32_t *actor, int char_id,
                              const float world[16], uint32_t color,
                              int tex_w, int tex_h);
#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_CHR_SPRITE_H */
