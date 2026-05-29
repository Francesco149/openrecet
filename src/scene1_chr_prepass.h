/*
 * scene1_chr_prepass.{c,h} — Cchr.2e: the records / people sprite pre-pass
 * (engine FUN_0045672a @ 0x45672a, 1317 B).
 *
 * Dispatched from scene1_render_meshes (FUN_00459dfd) at L244-L246, right
 * after the alpha-pre wrapper sets MIPFILTER=NONE — i.e. the slot the old
 * scene1_walk_alpha_pre_TODO() stub held.  It is the render-side sibling of
 * the character-sprite walker (Cchr.2d, scene1_chr_walker): where the walker
 * draws the player/companion/NPC actors, this pre-pass draws three OTHER
 * record families, in order:
 *
 *   Section A — records_b  (the 0x49-dword table g_scene1_records_b, count
 *               g_scene1_records_b_count = engine DAT_0076b964): every slot
 *               whose TYPE field == 0x61 ('a') is drawn as a 3D mesh via
 *               scene1_emit_record (engine FUN_00455191(&DAT_073a9658)).
 *               World = Scaling × Translation (NO billboard base matrix — these
 *               are world-space meshes).  Two scale modes, gated on the slot's
 *               AGE field (< 0x46 → a fixed (-0.14,0.04,0.14); else a
 *               size-field-driven (-0.5,1,0.5)×(field·0.2)).
 *
 *   Section B — records_a  (the 0x25-dword table g_scene1_records_a, count
 *               g_scene1_records_a_count = engine DAT_0076b960): every slot
 *               whose TYPE field == 0x97 (and != -1) is drawn as a 3D mesh via
 *               scene1_emit_record.  World = RotationY(ROT_X) × Scaling
 *               (-s,s,s) × Translation, s = SCALE·0.2.
 *
 *   Section C — the people billboard table (engine base DAT_0076b970, stride
 *               0xba4 B, 128 fixed entries — the NPC sprite table, UNPORTED).
 *               Depth-sorted (engine FUN_0045526a co-sort on the +0x450 key),
 *               then each active, non-0xff-alpha entry is drawn camera-facing
 *               via the validated leaf scene1_chr_sprite_render (Cchr.2b /
 *               engine FUN_0045a56f).  World = base(DAT_0438cdf8) × Scaling
 *               (desc[+0x44]·0.05) × Translation; per-entry diffuse alpha =
 *               alpha_byte · per-entry-mult, color | 0x7f7f7f.
 *
 * Sections A and B share a one-time D3D state envelope (engine FUN_00456c4f),
 * applied lazily on the first drawn slot of either section.  Section C has its
 * own one-time envelope (applied on its first drawn entry).
 *
 * ── DORMANT IN HOUSE (today) ────────────────────────────────────────────
 * On new-game HOUSE entry all three tables are empty: g_scene1_records_a/b
 * counts are 0 (their populators are unported), and the people table has no
 * port-side storage yet (chr_prepass_people_base() returns NULL — the same
 * "absent table" state as the walker's NPC pass).  So every section iterates
 * nothing and nothing draws.  Sections A/B are wired to the REAL record
 * globals, so they fire automatically once those tables populate; Section C
 * fires once the people table is ported and the accessor points at it.
 *
 * The only genuinely interesting standalone logic is the index co-sort, split
 * out as chr_prepass_sort() and host-tested.
 *
 * Texture-filtering note (for the 1:1-retail follow-up): the A/B envelope sets
 * MAG/MINFILTER = LINEAR (2); the C (people) envelope sets MAG/MINFILTER =
 * POINT (1).  Decoded from objdump @ 0x456a3c / 0x456c4f, 2026-05-29.
 */
#ifndef OPENRECET_SCENE1_CHR_PREPASS_H
#define OPENRECET_SCENE1_CHR_PREPASS_H

#include <stdint.h>

/*
 * Engine FUN_0045526a — ascending co-sort of keys[0..n) carrying idx[0..n)
 * in lockstep (a bounded bubble sort, smallest key toward index 0).  Section
 * C uses it to depth-order the people billboards before drawing back-to-front.
 * Pure / portable; host-tested.
 */
void chr_prepass_sort(int32_t *keys, int32_t *idx, int n);

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * The full pre-pass (engine FUN_0045672a).  `dev_in` is the engine D3D8
 * device (DAT_073dfcbc).  No-op when dev_in is NULL.
 */
void scene1_chr_prepass_render(struct IDirect3DDevice8 *dev_in);
#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_CHR_PREPASS_H */
