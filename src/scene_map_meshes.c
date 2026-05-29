/*
 * scene_map_meshes.c — see scene_map_meshes.h.
 *
 * Engine source: FUN_00474681 @ 0x474681 (123 bytes).  The mesh-load
 * loop only — `*(int*)(&DAT_068ded24+stage*0x1b3c)` meshes from the
 * stage record's map[] field (`&DAT_068dd60c+stage*0x1b3c`, stride
 * 0x100) into DAT_068dcca0[] (stride 0x28), via FUN_00472836
 * (= our mesh_load).
 */

#include "scene_map_meshes.h"

#include <stdio.h>

#include "tables_stage.h"

/* ─── pure-C body ─────────────────────────────────────────────────────── */

static int clamp_stage(int stage)
{
    if (stage < 0 || stage >= g_stage.count) return -1;
    return stage;
}

int scene_map_meshes_count(int stage)
{
    stage = clamp_stage(stage);
    if (stage < 0) return 0;
    int n = g_stage.records[stage].map_count;
    if (n < 0) n = 0;
    if (n > SCENE_MAP_MESH_SLOTS) n = SCENE_MAP_MESH_SLOTS;
    return n;
}

int scene_map_meshes_load_with(int stage, scene_map_load_fn load_fn,
                               void *userdata)
{
    int s = clamp_stage(stage);
    if (s < 0) return 0;
    /* Engine bumps map_count past the slot cap on overflow (no check),
     * but never loads past it because the parser stops writing strings
     * — so iterate the capped count. */
    int n = scene_map_meshes_count(s);
    int calls = 0;
    for (int i = 0; i < n; i++) {
        const char *path = g_stage.records[s].map[i];
        if (!path[0]) continue;
        if (load_fn) load_fn(path, i, userdata);
        calls++;
    }
    return calls;
}

/* ─── Win32 mesh storage + loader ─────────────────────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

#include "mesh.h"
#include "mesh_load.h"

mesh_t *g_scene_map_meshes[SCENE_MAP_MESH_SLOTS];

static int win32_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    if (slot < 0 || slot >= SCENE_MAP_MESH_SLOTS) return 0;
    if (g_scene_map_meshes[slot]) {
        mesh_free(g_scene_map_meshes[slot]);
        g_scene_map_meshes[slot] = 0;
    }
    /* Engine passes the loop index as param_3 (FUN_00472836's iVar4);
     * our mesh_load ignores it (dynamic-bone scratch deferred — see
     * mesh_load.h), but we pass it for engine parity. */
    mesh_t *m = mesh_load(path, slot);
    if (!m) {
        fprintf(stderr, "scene_map_meshes: mesh_load failed for %s\n", path);
        return 0;
    }
    if (m->error[0]) {
        fprintf(stderr, "scene_map_meshes: %s: %s\n", path, m->error);
        mesh_free(m);
        return 0;
    }
    if (dev) {
        long hr = mesh_load_finalize_win32(m, (struct IDirect3DDevice8 *)dev);
        if (hr) fprintf(stderr,
            "scene_map_meshes: finalize_win32 failed (hr=0x%08lx) for %s\n",
            (unsigned long)hr, path);
    }
    g_scene_map_meshes[slot] = m;
    return 1;
}

int scene_map_meshes_load_house(struct IDirect3DDevice8 *dev)
{
    /* Mirror FUN_00474681: load every map[] mesh of the HOUSE stage.
     * load_with does the iteration; win32_load_fn does the per-slot
     * mesh_load + finalize.  Count successes (NULL slots = failures). */
    scene_map_meshes_load_with(SCENE_MAP_STAGE_HOUSE, win32_load_fn, dev);
    int loaded = 0;
    for (int i = 0; i < SCENE_MAP_MESH_SLOTS; i++)
        if (g_scene_map_meshes[i]) loaded++;
    return loaded;
}

void scene_map_meshes_reset(void)
{
    for (int i = 0; i < SCENE_MAP_MESH_SLOTS; i++) {
        if (g_scene_map_meshes[i]) {
            mesh_free(g_scene_map_meshes[i]);
            g_scene_map_meshes[i] = 0;
        }
    }
}

mesh_t *scene_map_meshes_get(int idx)
{
    if (idx < 0 || idx >= SCENE_MAP_MESH_SLOTS) return 0;
    return g_scene_map_meshes[idx];
}

#else /* !_WIN32 */

void scene_map_meshes_reset(void) { }

#endif /* _WIN32 */
