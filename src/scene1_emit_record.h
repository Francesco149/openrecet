/*
 * scene1_emit_record.h — C8e port of the per-record draw helpers
 * called from inside the four mesh walkers.
 *
 * Three engine functions land together because they're tightly
 * coupled:
 *
 *   FUN_00454f7c (104 B)  — mid-walker state preamble.  Zeros 4
 *                           scratch flags + sets CULLMODE=CW +
 *                           TSS MIPFILTER=POINT + TSS ADDRESS=WRAP.
 *                           Called once at the top of FUN_00455191
 *                           and twice more from FUN_00457714 +
 *                           FUN_00459847 (still TODO).
 *
 *   FUN_00454fe4 (429 B)  — per-material state-flip helper.  Takes
 *                           a material slot index, reads four
 *                           per-material flag tables at DAT_073cb684
 *                           / 5bc / 74c / 814, and applies the
 *                           corresponding D3D state diff (CULLMODE,
 *                           MIPFILTER, ADDRESSU, ADDRESSV).  Skips
 *                           writes if state already matches —
 *                           hence the cached scratch globals.
 *
 *   FUN_00455191 (217 B)  — the per-mesh draw entry.  Takes an
 *                           engine mesh-record pointer
 *                           (ID3DXMesh*, material-index array,
 *                           per-subset materials, subset count).
 *                           Outer-loops over material slots
 *                           [0, DAT_073cb108), inner-loops over
 *                           the mesh's subsets matching each slot,
 *                           binds material+texture per slot,
 *                           DrawSubset per matching subset.
 *
 * What this chip lands:
 *
 *   - Full port of FUN_00454f7c + FUN_00454fe4.  Pure state-write
 *     helpers — their bodies map 1:1 to D3D8 calls without needing
 *     any engine-side type imports.
 *
 *   - Scaffold port of FUN_00455191.  Outer loop is structured but
 *     the inner per-subset draw body is a TODO comment — it needs
 *     either:
 *       (a) the engine's ID3DXMesh + per-mesh material-index array
 *           shape (we'd have to build an alternate mesh wrapper),
 *       OR
 *       (b) a mapping from "engine mesh record" → our mesh_t, then
 *           a delegate call to scene1_render_emit_frame.
 *
 *     For HOUSE the outer count DAT_073cb108 is BSS-zero (no port
 *     has populated the per-material texture cache yet), so the
 *     loop short-circuits regardless.  When data populates, option
 *     (b) is the right wiring — see the TODO inline.
 *
 * Wiring: none of the four mesh walkers' per-record body actually
 * calls scene1_emit_record yet (each walker's per-record loop is
 * itself a TODO inside scene1_shop_walker.c / scene1_alpha_walker.c).
 * Landing the helpers now means the next walker-body port has a
 * stable target name.
 */

#ifndef OPENRECET_SCENE1_EMIT_RECORD_H
#define OPENRECET_SCENE1_EMIT_RECORD_H

#ifdef _WIN32

struct IDirect3DDevice8;

/* FUN_00454f7c — mid-walker state preamble.  Resets the four
 * cached per-material state slots so subsequent FUN_00454fe4 calls
 * always re-issue their first state diff.  Also issues 4
 * unconditional device writes.  No-op when dev is NULL. */
void scene1_emit_preamble(struct IDirect3DDevice8 *dev);

/* FUN_00454fe4 — apply per-material state diff for material slot
 * `material_slot`.  Reads four engine flag tables (cull / mipfilter
 * / address-u / address-v) and writes the device state only when
 * different from the cached value.  No-op when dev is NULL.
 *
 * For HOUSE: every flag table is BSS-zero → first call writes the
 * default state (CULLMODE=CCW, MIPFILTER=LINEAR, ADDRESSU=WRAP,
 * ADDRESSV=WRAP), subsequent calls short-circuit on cache match. */
void scene1_emit_apply_material_state(struct IDirect3DDevice8 *dev,
                                      int material_slot);

/* FUN_00455191 — per-mesh draw entry.  Takes the engine's mesh-
 * record pointer (an opaque void* in our header — the engine
 * struct layout is captured in the .c file).  Walks material
 * slots × subsets, binds material+texture, calls DrawSubset per
 * match.
 *
 * For HOUSE the outer-loop count (DAT_073cb108) is BSS-zero, so
 * passing any record results in no draws — the function still
 * issues the preamble + tail state writes though, matching engine.
 *
 * `override_table` corresponds to the engine's second-arg variant:
 *   FUN_00455191(0)              — no override
 *   FUN_00455191(&DAT_073a9680)  — pass-C/D override
 *   FUN_00455191(&DAT_073a9658)  — alpha-pass override
 *   FUN_00455191(&DAT_068dcf98)  — initial-asset override
 * Ignored today (passes opaque void* through to where engine
 * reads it; no use site ported).  May be NULL.
 *
 * No-op when dev is NULL.  Records passed as NULL also short-
 * circuit (engine UB; we guard explicitly). */
void scene1_emit_record(struct IDirect3DDevice8 *dev,
                        const void *mesh_record,
                        const void *override_table);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_EMIT_RECORD_H */
