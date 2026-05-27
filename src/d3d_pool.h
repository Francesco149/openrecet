/*
 * d3d_pool.h — port of the engine's stage-scoped D3D resource pool
 * walker.  Engine functions:
 *
 *   FUN_00471905 @ 0x471905 (54 B) → d3d_pool_release_type
 *   FUN_00473474 @ 0x473474 (9 B)  → d3d_pool_release_post_fade
 *
 * Engine model (from decomp at all.c L70318-70340 + sibling allocators):
 *
 *   DAT_073a97e8..DAT_073a9b08 holds 200 dword slots, each a pointer to
 *   a wrapper record allocated via FUN_0047183b.  Each wrapper has:
 *     +0  void *resource   — IUnknown-like with vtable[2] = Release
 *     +12 int   type_tag   — opaque integer (engine uses 1..0x11)
 *
 *   `d3d_pool_release_type(tag)` walks all 200 slots, and for each
 *   wrapper with matching type, calls resource->Release() then zeros
 *   both `wrapper.resource` and the slot.
 *
 *   `d3d_pool_release_post_fade()` is the thin wrapper at 0x473474:
 *   calls `d3d_pool_release_type(2)`.  Engine fires it from
 *   FUN_0049a59e (= NEW GAME post-fade commit) right between
 *   worker_load_close and fade_phase_out_start.
 *
 * Port-side model:
 *
 *   The engine's allocator (FUN_0047183b) and the wrapper-record type
 *   are unported — openrecet's D3D resource lifetimes are tied to
 *   C-level mesh_load / sprite_load / etc., not this pool.  So the
 *   pool stays empty (all 200 slots NULL) until a future allocator
 *   port populates it.  Walker functions still fire their probes for
 *   call_trace parity; the walk loop sees NULL slots and exits without
 *   releasing anything.
 *
 *   When the wrapper allocator lands, this module gains an
 *   `d3d_pool_alloc(resource, type_tag)` entry; the walker bodies
 *   already handle the populated case correctly (see d3d_pool.c).
 *
 * Pure C, no Win32 surface (the release call goes through the resource's
 * own COM vtable, which is platform-agnostic from this module's POV).
 * Unit-testable.
 */
#ifndef OPENRECET_D3D_POOL_H
#define OPENRECET_D3D_POOL_H

#include <stdint.h>

/* Engine: 200-slot dword array at DAT_073a97e8.  Each slot holds a
 * `d3d_pool_entry *` (or NULL for free slots). */
#define D3D_POOL_SLOT_COUNT 200

/* Wrapper record layout (matches engine layout + offsets used by
 * FUN_00471905 / FUN_004718d2): */
typedef struct d3d_pool_entry {
    void    *resource;       /* +0  IUnknown-like; resource->vtable[2] = Release */
    int32_t  _reserved_4;    /* +4  unused by walker — engine layout */
    int32_t  _reserved_8;    /* +8  unused by walker — engine layout */
    int32_t  type_tag;       /* +12 opaque type (engine: 1..0x11) */
} d3d_pool_entry;

/* FUN_00471905 @ 0x471905 — walk all 200 slots; for each non-NULL entry
 * whose type matches `type_tag`, call entry->resource->vtable[2]
 * (Release) and zero both `entry->resource` and the slot pointer. */
void d3d_pool_release_type(int32_t type_tag);

/* FUN_00473474 @ 0x473474 — thin wrapper: d3d_pool_release_type(2).
 * Engine fires this from FUN_0049a59e (NEW GAME post-fade commit). */
void d3d_pool_release_post_fade(void);

/* ─── test helpers ────────────────────────────────────────────────────── */

/* Read a slot for test inspection. */
d3d_pool_entry *d3d_pool_slot_for_test(int idx);

/* Install an entry into a slot (test-only — production-side allocator
 * isn't ported yet).  Pass NULL to clear the slot. */
void d3d_pool_set_slot_for_test(int idx, d3d_pool_entry *entry);

/* Reset all 200 slots to NULL.  Test-only. */
void d3d_pool_reset_for_test(void);

#endif /* OPENRECET_D3D_POOL_H */
