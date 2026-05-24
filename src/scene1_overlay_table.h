/*
 * scene1_overlay_table.h — port of FUN_00474f4f (0x474f4f, 801 B), the
 * text-table parser for the 2D-overlay dispatcher's per-layer texture
 * filenames and per-shape UV/animation table.
 *
 * Engine surface (`docs/decompiled/by-address/474f4f.c` and all.c
 * L73243..L73419): one call per `ef/grpN.idx` file (N=1..4), executed
 * after `DAT_0076b948 = 0; DAT_0076b94c = 0;` (engine all.c L76530-31).
 * Caller is the tables-loader case 3 dispatch inside FUN_00475270 (the
 * massive 19645-B "init indexfile" function) — landed via `src/tables.c`
 * in a prior chip.
 *
 * File format (Shift-JIS comment lines, but all parseable tokens are
 * 7-bit ASCII):
 *
 *   /comment                              ← skipped (first char '/')
 *   <blank line>                          ← skipped (first char \r/\n)
 *   GRPNN:bmp/effect00.bmp                ← layer texture registration
 *   NNN:T:(ox,oy,sx,sy)(frames,stride,loop)  ← shape entry
 *   NNN:T:(ox,oy,sx,sy)                   ← shape entry, default anim
 *   NNN:T:(ox,oy,sx,sy)(frames,stride)    ← shape entry, default loop
 *
 *   NN  — 2 digits, [0..99]; layer index in the GRP-prefix table.  The
 *         engine doesn't actually use NN as an index — it just walks all
 *         100 prefixes per line and increments DAT_0076b948 on the
 *         first match (per-line write).  So duplicate NN values across
 *         lines are valid and each gets its own slot.
 *   NNN — 3 digits, [0..998]; shape index, indexes the 8-dw shape table
 *         (g_scene1_overlay_shapes) directly.  Engine caps at 999.
 *   T   — texture/layer index, 1-2 digits, [0..99]; indexes the layer
 *         texture table populated by GRP entries above (and by the
 *         engine's per-layer sprite_load loop at all.c L71673-71683).
 *   ox/oy/sx/sy — integer pixel coords inside the texture (4 args).
 *         Stored as float in the shape table (engine does an `fild`
 *         cast at read time; we cast at write time).
 *   frames/stride/loop — animation parameters; defaults 1/1/0.
 *
 * Engine quirks preserved:
 *   1. Both loops (GRP 0..99, NNN 0..998) iterate to completion on
 *      every line — no break on match.  Observable identical when each
 *      line matches at most one of either.
 *   2. The GRP prefix compares 6 chars ("GRPNN:"); the NNN prefix
 *      compares 4 chars ("NNN:" — the trailing colon).  Both checks
 *      are byte-exact; non-matching lines (including all blank / `/`
 *      comments) fall through.
 *   3. Optional inner `(frames,stride,loop)` group: the engine reads
 *      up to 3 values comma-separated.  Missing values keep defaults
 *      (1/1/0).
 *
 * Storage / state owned:
 *   g_scene1_overlay_layer_count           (extern in scene1_overlay.h)
 *   g_scene1_overlay_layer_filenames[][]   (extern in scene1_overlay.h)
 *   g_scene1_overlay_shapes[]              (extern in scene1_overlay.h)
 *   g_scene1_overlay_shapes_max_index      (extern in scene1_overlay.h)
 *
 * The parser does NOT touch g_scene1_overlay_layer_textures — those
 * are populated by the sysassets per-layer loader after parsing.
 */
#ifndef SCENE1_OVERLAY_TABLE_H
#define SCENE1_OVERLAY_TABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse one already-loaded buffer.  `buf_len` does NOT include any
 * trailing NUL; the parser stops at the first NUL OR after buf_len
 * bytes, whichever comes first (matches engine's `cVar6 == '\0'`
 * sentinel after the trailing NUL the engine writes at end-of-buffer).
 *
 * Appends to the running totals — call `scene1_overlay_layers_reset()`
 * before the first call of a parse session if you want a fresh slate
 * (matches engine's L76530-31 reset before the four-file sweep). */
void scene1_overlay_table_parse_buf(const char *buf, size_t buf_len);

#ifdef _WIN32
/* Load `name` through the engine's "disk first, storage overlay
 * second" path (matches FUN_005038b0 fopen + FUN_00434585/FUN_004346bf
 * fallback) and parse it.  Returns 1 on success, 0 if the file
 * couldn't be located in either source.
 *
 * Allocates + frees the read buffer.  Safe to call before
 * scene1_overlay_layers_reset; the parser is purely additive.  Gated
 * to _WIN32 because the storage subsystem (storage.c) requires
 * <windows.h> and isn't linked into host tests — exercise the
 * parse_buf path instead. */
int  scene1_overlay_table_load(const char *name);

/* Engine-default load: reset state then load the four ef/grpN.idx
 * files in N=1..4 order.  Mirrors all.c L76530-35 verbatim.  Returns
 * the resulting `g_scene1_overlay_layer_count` (0 if all four files
 * were missing).  Gated to _WIN32 same as scene1_overlay_table_load. */
int  scene1_overlay_table_load_all(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_OVERLAY_TABLE_H */
