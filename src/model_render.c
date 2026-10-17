/*
 * model_render.c — see model_render.h.
 */
#include "model_render.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdlib.h>
#include <string.h>

#include "actor_table.h"
#include "call_trace.h"
#include "math3d.h"
#include "mesh.h"
#include "mesh_load.h"
#include "model_anchor.h"
#include "sprite.h"
#include "tables_enemy.h"
#include "xfile.h"

#define MODEL_GFX 32
#define MAX_INF   8

typedef struct {
    int32_t nv;
    uint8_t *n;                     /* influences per vertex */
    int16_t (*bone)[MAX_INF];       /* the skin index */
    float   (*w)[MAX_INF];
} infl_t;

typedef struct {
    int       tried;
    mesh_t   *mesh;                 /* unbaked: each Mesh in its Frame's space */
    xfile_t  *xf;                   /* the skins */
    infl_t   *infl;                 /* per .x Mesh */
    const xfile_hier *bound;        /* the tree the bone pointers below belong to */
    xfile_sframe ***bones;          /* per .x Mesh: its skins' bone frames */
    xfile_sframe **frame;           /* per .x Mesh: its own frame (rigid) */
} model_gfx_t;

static model_gfx_t s_gfx[MODEL_GFX];

static void gfx_free(model_gfx_t *g)
{
    if (g->xf) {
        for (int i = 0; i < g->xf->mesh_count; i++) {
            if (g->infl) { free(g->infl[i].n); free(g->infl[i].bone); free(g->infl[i].w); }
            if (g->bones) free(g->bones[i]);
        }
        xfile_free(g->xf);
    }
    free(g->infl);
    free(g->bones);
    free(g->frame);
    if (g->mesh) mesh_free(g->mesh);
    memset(g, 0, sizeof *g);
}

static void gfx_load(IDirect3DDevice8 *dev, int model)
{
    model_gfx_t *g = &s_gfx[model];
    gfx_free(g);
    g->tried = 1;
    const char *path = model_anchor_file(model);
    if (path == NULL) return;
    mesh_t *m = mesh_load_hier(path);
    if (m == NULL) return;
    if (m->error[0]) { mesh_free(m); return; }
    if (dev) (void)mesh_load_finalize_win32(m, (struct IDirect3DDevice8 *)dev);
    g->mesh = m;
    g->xf = mesh_load_parse_xfile(path);
    if (g->xf == NULL) return;
    const int nx = g->xf->mesh_count;
    g->infl = (infl_t *)calloc((size_t)nx, sizeof *g->infl);
    g->bones = (xfile_sframe ***)calloc((size_t)nx, sizeof *g->bones);
    g->frame = (xfile_sframe **)calloc((size_t)nx, sizeof *g->frame);
    if (!g->infl || !g->bones || !g->frame) return;
    for (int i = 0; i < nx; i++) {
        const xfile_mesh *xm = &g->xf->meshes[i];
        infl_t *f = &g->infl[i];
        f->nv = xm->vertex_count;
        if (xm->skin_count == 0 || f->nv <= 0) continue;
        f->n = (uint8_t *)calloc((size_t)f->nv, 1);
        f->bone = calloc((size_t)f->nv, sizeof *f->bone);
        f->w = calloc((size_t)f->nv, sizeof *f->w);
        if (!f->n || !f->bone || !f->w) continue;
        for (int k = 0; k < xm->skin_count; k++) {
            const xfile_skin *sk = &xm->skins[k];
            for (int j = 0; j < sk->n; j++) {
                const int v = sk->idx[j];
                if (v < 0 || v >= f->nv || f->n[v] >= MAX_INF) continue;
                f->bone[v][f->n[v]] = (int16_t)k;
                f->w[v][f->n[v]] = sk->w[j];
                f->n[v]++;
            }
        }
    }
}

void model_render_load(struct IDirect3DDevice8 *dev, const int32_t present[100])
{
    uint8_t want[MODEL_GFX] = {0};
    for (int t = 0; t < 100 && t < ENEMY_COUNT; t++)
        if (present[t] && g_enemy[t].flags == 1 && g_enemy[t].unknown_24 >= 0 &&
            g_enemy[t].unknown_24 < MODEL_GFX)
            want[g_enemy[t].unknown_24] = 1;
    for (int k = 0; k < MODEL_GFX; k++)
        if (want[k]) gfx_load((IDirect3DDevice8 *)dev, k);
}

/* the bone / frame pointers of the tree the model is posed in */
static void gfx_bind(model_gfx_t *g, const xfile_hier *h)
{
    if (g->bound == h || g->xf == NULL) return;
    g->bound = h;
    for (int i = 0; i < g->xf->mesh_count; i++) {
        const xfile_mesh *xm = &g->xf->meshes[i];
        free(g->bones[i]);
        g->bones[i] = NULL;
        const char *fp = xm->frame_path, *slash = strrchr(fp, '/');
        g->frame[i] = fp[0] ? xfile_find_frame(h->root, slash ? slash + 1 : fp) : h->root;
        if (g->frame[i] == NULL) g->frame[i] = h->root;
        if (xm->skin_count == 0) continue;
        g->bones[i] = (xfile_sframe **)calloc((size_t)xm->skin_count, sizeof *g->bones[i]);
        if (g->bones[i] == NULL) continue;
        for (int k = 0; k < xm->skin_count; k++)
            g->bones[i][k] = xfile_find_frame(h->root, xm->skins[k].bone);
    }
}

static void xform(float out[3], const float p[3], const float m[16])
{
    out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
}

/* FUN_00456d48 → FUN_00404a20: the unlit skinning shaders (oD0 = the vertex
 * colour, oT0 = uv) — here the blend on the CPU into world space:
 * Σ w · (v · offset · combined) */
static void model_draw(IDirect3DDevice8 *dev, model_gfx_t *g, const xfile_hier *h)
{
    mesh_t *m = g->mesh;
    if (m == NULL || g->xf == NULL || g->infl == NULL) return;
    gfx_bind(g, h);
    static mesh_vertex *buf;
    static int32_t cap;
    if (cap < m->vertex_count) {
        mesh_vertex *nb = (mesh_vertex *)realloc(buf, (size_t)m->vertex_count * sizeof *nb);
        if (nb == NULL) return;
        buf = nb;
        cap = m->vertex_count;
    }
    /* the per-skin palettes offset · combined */
    static float pal[64][16];
    for (int32_t s = 0; s < m->submesh_count; s++) {
        const mesh_submesh *sm = &m->submeshes[s];
        if (sm->vertex_count < 3) continue;
        const int xi = sm->xmesh;
        if (xi < 0 || xi >= g->xf->mesh_count) continue;
        const xfile_mesh *xm = &g->xf->meshes[xi];
        const infl_t *f = &g->infl[xi];
        const int nsk = xm->skin_count < 64 ? xm->skin_count : 64;
        for (int k = 0; k < nsk; k++) {
            const xfile_sframe *b = g->bones[xi] ? g->bones[xi][k] : NULL;
            if (b) mat4_mul(pal[k], xm->skins[k].offset, b->combined);
            else mat4_identity(pal[k]);
        }
        for (int32_t v = sm->vertex_offset; v < sm->vertex_offset + sm->vertex_count; v++) {
            mesh_vertex out = m->vertices[v];
            const float p[3] = { out.x, out.y, out.z };
            const int32_t src = m->src_vertex ? m->src_vertex[v] : -1;
            float r[3] = { 0, 0, 0 };
            if (nsk > 0 && f->n && src >= 0 && src < f->nv && f->n[src] > 0) {
                float wsum = 0;
                for (int j = 0; j < f->n[src]; j++) wsum += f->w[src][j];
                if (wsum <= 0) wsum = 1;
                for (int j = 0; j < f->n[src]; j++) {
                    const int bi = f->bone[src][j];
                    if (bi >= nsk) continue;
                    float t[3];
                    xform(t, p, pal[bi]);
                    const float w = f->w[src][j] / wsum;
                    r[0] += w * t[0]; r[1] += w * t[1]; r[2] += w * t[2];
                }
            } else {
                xform(r, p, g->frame[xi] ? g->frame[xi]->combined : h->root->combined);
            }
            out.x = r[0]; out.y = r[1]; out.z = r[2];
            buf[v] = out;
        }
        IDirect3DBaseTexture8 *tex = NULL;
        const int mi = sm->material_index;
        if (m->texture_slots && mi >= 0 && mi < m->material_count) {
            const int slot = m->texture_slots[mi];
            if (slot >= 0 && slot < g_mesh_tex_cache.count && g_mesh_tex_cache.entries[slot].sprite)
                tex = (IDirect3DBaseTexture8 *)((sprite_t *)g_mesh_tex_cache.entries[slot].sprite)->tex;
        }
        IDirect3DDevice8_SetTexture(dev, 0, tex);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, (UINT)(sm->vertex_count / 3),
                                         &buf[sm->vertex_offset], sizeof(mesh_vertex));
    }
}

void model_render_pass(struct IDirect3DDevice8 *dev_in, int pass)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (dev == NULL) return;
    float id[16];
    mat4_identity(id);
    for (int i = 0; i < ACTOR_COUNT; i++) {
        actor_t *a = &g_actor[i];
        if (act_geti(a, ACT_INUSE) == 0) continue;
        const int32_t type = act_geti(a, ACT_TYPE);
        if (type < 0 || type >= ENEMY_COUNT || g_enemy[type].flags != 1) continue;
        const int opaque = act_geti(a, 0x3dc) == 255;
        if (pass == 0 ? !opaque : (opaque || act_geti(a, 0xaf4) > 0)) continue;
        int model = -1;
        const xfile_hier *h = model_anchor_pose(a, &model);
        if (h == NULL || model < 0 || model >= MODEL_GFX || s_gfx[model].mesh == NULL) continue;
        /* FUN_00454f03(1) + LIGHTING off, or (4) while +0x1c is set */
        if (act_geti(a, 0x1c) == 0) {
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        } else {
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
        }
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
        const int32_t alpha = act_geti(a, 0x3dc);
        if (alpha != 255) {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, 0xffffffu | ((uint32_t)alpha << 24));
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
        }
        IDirect3DDevice8_SetVertexShader(dev, MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)id);
        model_draw(dev, &s_gfx[model], h);
        if (alpha != 255)
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    }
}

#endif /* _WIN32 */
