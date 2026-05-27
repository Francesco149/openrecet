/*
 * d3d_pool.c — see d3d_pool.h for the chip writeup.
 *
 * Engine functions:
 *   FUN_00471905 @ 0x471905 → d3d_pool_release_type
 *   FUN_00473474 @ 0x473474 → d3d_pool_release_post_fade
 */

#include "d3d_pool.h"

#include "call_trace.h"

/* Engine DAT_073a97e8 — 200-slot pool array.  Stays NULL-initialized
 * until the wrapper allocator (FUN_0047183b) ports.  Each slot is a
 * pointer to a `d3d_pool_entry`. */
static d3d_pool_entry *g_pool[D3D_POOL_SLOT_COUNT];

/* COM-like release call.  The engine reads `entry->resource[0][8/4]` —
 * i.e. `(*(void ***)resource)[2]` — which is the third slot of the
 * resource's vtable.  For all D3D8 / IUnknown-derived types this is the
 * Release method (Release = vtable[2] per the IUnknown ABI).
 *
 * The release function takes the resource as `this` and returns its new
 * reference count (we discard).  No Win32 surface needed — this is a
 * standard COM call shape that works wherever the resource was created. */
static void d3d_pool_release_resource(void *resource)
{
    if (!resource) return;
    /* Layout: resource is a pointer to an object whose first dword is
     * a pointer to its vtable.  vtable[2] is the Release thunk. */
    void ***vptr = (void ***)resource;
    typedef unsigned long (*release_fn)(void *);
    release_fn release = (release_fn)((*vptr)[2]);
    release(resource);
}

void d3d_pool_release_type(int32_t type_tag)
{
    /* E.3 probe — FUN_00471905 @ 0x471905. */
    CALL_TRACE_ENTER(0x471905u);

    /* Engine walks 200 slots from DAT_073a97e8 to DAT_073a9b08.  For
     * each non-NULL wrapper matching the type tag: release the resource
     * pointer at wrapper offset 0, zero it, then zero the slot. */
    for (int i = 0; i < D3D_POOL_SLOT_COUNT; i++) {
        d3d_pool_entry *e = g_pool[i];
        if (e == NULL) {
            continue;
        }
        if (e->type_tag != type_tag) {
            continue;
        }
        d3d_pool_release_resource(e->resource);
        e->resource = NULL;
        g_pool[i] = NULL;
    }
}

void d3d_pool_release_post_fade(void)
{
    /* E.3 probe — FUN_00473474 @ 0x473474. */
    CALL_TRACE_ENTER(0x473474u);

    /* Engine: FUN_00471905(2).  Releases all wrappers whose type tag
     * is 2 — the "stage-scoped post-fade" resource category. */
    d3d_pool_release_type(2);
}

/* ─── test helpers ────────────────────────────────────────────────────── */

d3d_pool_entry *d3d_pool_slot_for_test(int idx)
{
    if (idx < 0 || idx >= D3D_POOL_SLOT_COUNT) return NULL;
    return g_pool[idx];
}

void d3d_pool_set_slot_for_test(int idx, d3d_pool_entry *entry)
{
    if (idx < 0 || idx >= D3D_POOL_SLOT_COUNT) return;
    g_pool[idx] = entry;
}

void d3d_pool_reset_for_test(void)
{
    for (int i = 0; i < D3D_POOL_SLOT_COUNT; i++) {
        g_pool[i] = NULL;
    }
}
