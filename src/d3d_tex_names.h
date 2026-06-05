/* Stable source-name registry for D3D textures, keyed by the texture
 * object pointer.
 *
 * The d3d-trace serialises a bound texture as its raw COM pointer
 * ("0xNN"), which is allocation-dependent — it differs between port and
 * retail (and between runs), so tools/render_diff.py can only compare
 * texture IDENTITY positionally (the "opaque-pointer" mode: map the Nth
 * distinct pointer to a synthetic #N).  That breaks the moment the two
 * sides bind textures in a different order, and it can never NAME the
 * texture for a human-readable diff.
 *
 * This registry maps each texture pointer to the LOAD-STABLE source name
 * it came from (e.g. "bmp/ivent/ive_window.tga").  The port populates it
 * at the texture-load chokepoint (sprite_load_impl); the retail side
 * mirrors it via a Frida hook on the engine's loaders
 * (FUN_0047193c UI / FUN_00471b24 mesh).  d3d_trace_SetTexture then emits
 * a "tex_name" field so render_diff can key texture identity on the name
 * — target-independent and human-readable — falling back to the opaque
 * pointer only when a name is unknown.
 *
 * Pure C (the key is `const void *`, no d3d8.h) so it links into the host
 * test suite.  Best-effort + lock-free: this is trace-build instrumentation,
 * not a correctness-bearing path.
 */
#ifndef OPENRECET_D3D_TEX_NAMES_H
#define OPENRECET_D3D_TEX_NAMES_H

/* Associate `name` with the texture object `tex`.  Copies the name
 * (truncated to the internal cap).  A repeat register for the same `tex`
 * overwrites the name.  NULL `tex`/`name` is a no-op. */
void d3d_tex_name_register(const void *tex, const char *name);

/* Return the registered name for `tex`, or NULL if unknown.  The returned
 * pointer is owned by the registry (valid until the slot is forgotten or
 * reset). */
const char *d3d_tex_name_lookup(const void *tex);

/* Drop any name registered for `tex` (call when the texture is released so
 * a recycled pointer doesn't carry a stale name). */
void d3d_tex_name_forget(const void *tex);

/* Clear the whole registry (device reload / test setup). */
void d3d_tex_name_reset(void);

#endif
