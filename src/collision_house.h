/*
 * collision_house.h — live HOUSE room collision (W4.3 wiring).
 *
 * Builds the runtime collision triangle mesh for the HOUSE shop ROOM from
 * the render `shop_1st.x` (the same `.x` draw loop A renders), and hands it
 * to the player controller so walls + counter actually block the walk.
 *
 * Scope: ROOM ONLY (map slot 0).  Furniture (round table / vending machines)
 * lives in separate meshes whose world placement is the still-unported
 * FUN_00436f97 `stage_positions` → `DAT_0438c058` chip (engine-quirks §65), so
 * furniture collision is deferred — the room walls are self-placed in
 * shop_1st.x and need none of that.  See collision_mesh.h / collision_resolve.h.
 */
#ifndef OPENRECET_COLLISION_HOUSE_H
#define OPENRECET_COLLISION_HOUSE_H

#include "collision_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse shop_1st.x and build the room collision mesh.  Idempotent (frees any
 * prior build first); call once per HOUSE preload.  No-op on non-Win32 hosts
 * (the storage byte-load isn't linkable there — host tests build directly). */
void collision_house_build(void);

/* The built room collision mesh, or NULL if the build hasn't run / failed. */
const collision_mesh *collision_house_get(void);

/* Free the built mesh (called on stage teardown). */
void collision_house_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_COLLISION_HOUSE_H */
