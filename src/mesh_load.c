/*
 * mesh_load.c — orchestrator + global texture cache for the .x mesh
 * pipeline. See mesh_load.h for the API + engine references.
 *
 * Pure-C parts (classifier, cache, mesh_load core) compile and unit-test
 * on the host under ASan + UBSan. Win32 D3D8 upload + sprite creation
 * sit behind `#ifdef _WIN32`.
 */

#include "mesh_load.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "call_trace.h"
#include "xfile.h"

#ifdef _WIN32
#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>
#include "sprite.h"
#include "storage.h"
#endif

/* Easydisp gate (engine: DAT_0438b19c, read from recet.ini [setup]
 * easydisp at boot). main.c sets this once after recet_ini_load. */
static int g_mesh_load_easydisp = 0;
void mesh_load_set_easydisp(int v) { g_mesh_load_easydisp = v ? 1 : 0; }

/* ───── Texture-name classifier ────────────────────────────────────────── */

/* Returns 1 if `s` starts with `prefix` (exact length match required —
 * caller passes the constant length, mirroring the engine's iVar7 != N
 * unrolled compare). Mismatched/short strings return 0. */
static int starts_with(const char *s, const char *prefix, int plen)
{
    for (int i = 0; i < plen; i++) {
        if (s[i] != prefix[i]) return 0;     /* also catches '\0' early */
    }
    return 1;
}

/* Returns 1 if `s` at offset 0 matches `tok` over `tlen` chars. */
static int match_at(const char *s, const char *tok, int tlen)
{
    for (int i = 0; i < tlen; i++) {
        if (s[i] != tok[i]) return 0;
    }
    return 1;
}

void mesh_classify_texture_name(const char *name, mesh_tex_flags *out)
{
    memset(out, 0, sizeof *out);
    if (!name || !name[0]) return;

    /* Five prefix checks at position 0 (FUN_00472836:138..168). */
    if (starts_with(name, "water",      5)) out->water      = 1;
    if (starts_with(name, "hikari",     6)) out->hikari     = 1;
    if (starts_with(name, "kabe_",      5)) out->kabe_      = 1;
    if (starts_with(name, "yuka_",      5)) out->yuka_      = 1;
    if (starts_with(name, "shop_jutan", 10)) out->shop_jutan = 1;

    /* Per-character sweep up to 256 chars (FUN_00472836:171..273).
     *
     * At each position the engine runs ten 2/3-char compares — one of
     * which (".t" at DAT_005c8450) has its result thrown away (a bare
     * `break` from the inner loop, no flag set). We preserve the dead
     * .t check for fidelity but it's a no-op. The other nine are:
     *   "n_" / "w_"   → boolean flags  (overwritten on repeat — but the
     *                                   final state is "any match found")
     *   "u0_".."u3_"  → u_index (last match wins; default 0)
     *   "v0_".."v3_"  → v_index (last match wins; default 0)
     */
    for (int pos = 0; pos < 0x100; pos++) {
        if (name[pos] == '\0') break;
        const char *p = name + pos;

        /* dead .t check (engine has it; result unused) */
        (void)match_at(p, ".tga", 2);

        if (match_at(p, "n_", 2)) out->has_n_ = 1;
        if (match_at(p, "w_", 2)) out->has_w_ = 1;

        if (match_at(p, "u0_", 3)) out->u_index = 0;
        if (match_at(p, "u1_", 3)) out->u_index = 1;
        if (match_at(p, "u2_", 3)) out->u_index = 2;
        if (match_at(p, "u3_", 3)) out->u_index = 3;

        if (match_at(p, "v0_", 3)) out->v_index = 0;
        if (match_at(p, "v1_", 3)) out->v_index = 1;
        if (match_at(p, "v2_", 3)) out->v_index = 2;
        if (match_at(p, "v3_", 3)) out->v_index = 3;
    }

    /* `.tga` substring on the filename — engine sets this from the
     * dir+name buffer post sprite_load (DAT_005c848c = ".tga", 2-char
     * compare). For static meshes the dir prefix has no '.', so a
     * scan over just the filename yields the same answer. */
    for (int pos = 0; pos < 0x100; pos++) {
        if (name[pos] == '\0') break;
        if (match_at(name + pos, ".tga", 4)) { out->ext_tga = 1; break; }
    }
}

/* ───── Global cache ───────────────────────────────────────────────────── */

mesh_tex_cache_t g_mesh_tex_cache = { 0 };

void mesh_tex_cache_reset(void)
{
    /* E.2 probe — FUN_0047281e @ 0x47281e. */
    CALL_TRACE_ENTER(0x47281eu);

#ifdef _WIN32
    for (int i = 0; i < g_mesh_tex_cache.count; i++) {
        sprite_t *s = (sprite_t *)g_mesh_tex_cache.entries[i].sprite;
        if (s) { sprite_destroy(s); free(s); }
    }
#endif
    memset(&g_mesh_tex_cache, 0, sizeof g_mesh_tex_cache);
}

int mesh_tex_cache_find(const char *name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < g_mesh_tex_cache.count; i++) {
        if (strcmp(g_mesh_tex_cache.entries[i].name, name) == 0) return i;
    }
    return -1;
}

int mesh_tex_cache_insert(const char *name, const mesh_tex_flags *flags)
{
    if (!name || !name[0]) return -1;
    if (g_mesh_tex_cache.count >= MESH_TEX_CACHE_CAP) return -1;
    int i = g_mesh_tex_cache.count++;
    mesh_tex_entry *e = &g_mesh_tex_cache.entries[i];
    memset(e, 0, sizeof *e);
    strncpy(e->name, name, sizeof e->name - 1);
    if (flags) e->flags = *flags;
    return i;
}

int mesh_tex_cache_lookup_or_reserve(const char *name,
                                     const mesh_tex_flags *flags,
                                     int *was_new)
{
    int idx = mesh_tex_cache_find(name);
    if (idx >= 0) { if (was_new) *was_new = 0; return idx; }
    idx = mesh_tex_cache_insert(name, flags);
    if (was_new) *was_new = (idx >= 0) ? 1 : 0;
    return idx;
}

/* ───── mesh_load orchestrator ─────────────────────────────────────────── */

#ifdef _WIN32
/* Insert "_s" before the trailing ".x" extension (or any extension).
 * Engine FUN_00472836:55..62: scans for '.' and rewrites in-place. We
 * write into `out` (sized OUT_SZ bytes). No-op if no '.' found within
 * 256 chars; result is always nul-terminated. */
static void make_easydisp_variant(const char *src, char *out, size_t out_sz)
{
    if (out_sz == 0) return;
    out[0] = '\0';
    size_t slen = strlen(src);
    if (slen + 3 >= out_sz) slen = out_sz - 4;   /* trim if it'd overflow with "_s.x" */
    size_t i = 0;
    for (; i < slen && i < 0x100; i++) {
        char c = src[i];
        if (c == '.') {
            const char *suffix = "_s.x";
            size_t room = out_sz - i;
            size_t n = strlen(suffix) + 1;
            if (n > room) n = room;
            memcpy(out + i, suffix, n);
            out[out_sz - 1] = '\0';
            return;
        }
        out[i] = c;
    }
    out[i] = '\0';
}
#endif /* _WIN32 (make_easydisp_variant) */

#ifdef _WIN32
static size_t storage_or_disk(const char *path, void **out_buf)
{
    /* mesh_load uses storage_read because the engine's FUN_004c8f74 goes
     * through DirectXFileCreate which reads from disk relative to cwd
     * (which is the game dir). storage_read covers the same files via
     * the lnkdata index. Falls back to disk via the storage layer's
     * existing precedence. */
    size_t sz = storage_get_size(path);
    if (sz == 0) {
        /* Try disk directly. */
        FILE *f = fopen(path, "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n <= 0) { fclose(f); return 0; }
        void *buf = malloc((size_t)n);
        if (!buf) { fclose(f); return 0; }
        size_t got = fread(buf, 1, (size_t)n, f);
        fclose(f);
        if (got != (size_t)n) { free(buf); return 0; }
        *out_buf = buf;
        return (size_t)n;
    }
    void *buf = malloc(sz);
    if (!buf) return 0;
    size_t got = storage_read(path, buf);
    if (got == 0) { free(buf); return 0; }
    *out_buf = buf;
    return got;
}
#endif

#ifdef _WIN32
/* Walk back from the end of `path` to the last '/' (or '\\'); return
 * the prefix length INCLUDING the slash. 0 if no slash found. */
static size_t dir_prefix_len(const char *path)
{
    size_t n = strlen(path);
    for (size_t i = n; i > 0; i--) {
        char c = path[i - 1];
        if (c == '/' || c == '\\') return i;
    }
    return 0;
}
#endif

mesh_t *mesh_load_from_buf(const void *data, size_t len,
                           const char *path_for_diagnostics, int param_3)
{
    (void)param_3;   /* dynamic-bone scratch deferred; see mesh_load.h */
    if (!data || len == 0) return NULL;

    xfile_t *xf = xfile_parse((const char *)data, len, path_for_diagnostics);
    if (!xf) return NULL;

    mesh_t *m = mesh_build_from_xfile(xf);
    xfile_free(xf);
    if (!m) return NULL;
    if (m->error[0]) return m;   /* caller checks m->error */

    mesh_compute_bounds(m);

    /* Allocate texture_slots parallel to materials, init -1. */
    if (m->material_count > 0) {
        m->texture_slots = (int32_t *)malloc((size_t)m->material_count * sizeof *m->texture_slots);
        if (!m->texture_slots) {
            snprintf(m->error, sizeof m->error, "oom texture_slots");
            return m;
        }
        for (int32_t i = 0; i < m->material_count; i++) m->texture_slots[i] = -1;
    }

    /* Per-material: classify + dedupe into the global cache.
     *
     * The engine zeroes a 12-byte slot at &DAT_073cc950 + (param_3*200+i)*0xc
     * when param_3 >= 0 (FUN_00472836:118..123) — dormant for all static
     * meshes (callers pass -1). Skipping that here intentionally. */
    for (int32_t i = 0; i < m->material_count; i++) {
        const char *tex = m->materials[i].texture;
        if (!tex || !tex[0]) {
            m->texture_slots[i] = -1;
            continue;
        }
        mesh_tex_flags flags;
        mesh_classify_texture_name(tex, &flags);
        int was_new = 0;
        int slot = mesh_tex_cache_lookup_or_reserve(tex, &flags, &was_new);
        m->texture_slots[i] = slot;
        /* slot == -1 means capacity overflow — engine would write
         * DAT_073cb108 (== cap, treated as the "not found" return) into
         * the texture_indices array even on overflow, then index past
         * the side-tables. We chose to fail closed (-1) instead.
         * In practice the engine ships fewer than 200 textures
         * (corpus survey: 165 unique) so this never trips. */
    }

    return m;
}

#ifdef _WIN32
mesh_t *mesh_load(const char *xfile_path, int param_3)
{
    if (!xfile_path || !xfile_path[0]) return NULL;

    void  *buf = NULL;
    size_t sz  = 0;

    /* Easydisp ("_s.x") variant first (FUN_00472836:55..72). */
    if (g_mesh_load_easydisp != 0) {
        char alt[512];
        make_easydisp_variant(xfile_path, alt, sizeof alt);
        if (alt[0]) sz = storage_or_disk(alt, &buf);
    }
    /* Normal path. */
    if (sz == 0) sz = storage_or_disk(xfile_path, &buf);
    if (sz == 0) return NULL;

    mesh_t *m = mesh_load_from_buf(buf, sz, xfile_path, param_3);
    free(buf);
    return m;
}
#else
mesh_t *mesh_load(const char *xfile_path, int param_3)
{
    (void)xfile_path; (void)param_3;
    /* Non-Win32 host build doesn't link storage_*; use mesh_load_from_buf. */
    return NULL;
}
#endif

/* ───── Win32 finalize: VB/IB + sprite uploads ─────────────────────────── */
#ifdef _WIN32

long mesh_load_finalize_win32(mesh_t *m, struct IDirect3DDevice8 *dev)
{
    if (!m || !dev) return E_INVALIDARG;

    /* VB/IB upload first. */
    HRESULT hr = mesh_upload_d3d8(m, dev);
    if (FAILED(hr)) return hr;

    /* For each cache entry with no sprite yet, attempt sprite_load. */
    char dir[512] = "";
    size_t dlen = dir_prefix_len(m->path);
    if (dlen > 0 && dlen < sizeof dir) {
        memcpy(dir, m->path, dlen);
        dir[dlen] = '\0';
    }

    for (int i = 0; i < g_mesh_tex_cache.count; i++) {
        mesh_tex_entry *e = &g_mesh_tex_cache.entries[i];
        if (e->sprite) continue;
        char full[768];
        snprintf(full, sizeof full, "%s%s", dir, e->name);
        sprite_t *s = (sprite_t *)calloc(1, sizeof *s);
        if (!s) return E_OUTOFMEMORY;
        if (!sprite_load_mipped((IDirect3DDevice8 *)dev, full, 0, 0, s)) {
            /* Texture missing on disk. Leave sprite NULL (renderer
             * will skip texturing this slot) and free the placeholder.
             * Engine's FUN_00471b24 shows a MessageBox on miss; we
             * just log and continue so headless captures don't pop. */
            fprintf(stderr, "mesh_load: sprite_load failed for '%s'\n", full);
            free(s);
            continue;
        }
        e->sprite = s;
    }

    return S_OK;
}

#endif /* _WIN32 */
