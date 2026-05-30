#include "sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmp.h"
#include "storage.h"
#include "tga.h"

#define FVF_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

/* Engine color key for .bmp assets — pure green (R=0, G=0xff, B=0).
 * Original engine passes 0xFF00FF00 (ARGB) to D3DXCreateTextureFrom-
 * FileInMemoryEx; our bmp_load_mem takes the 24-bit RGB form. */
#define BMP_COLOR_KEY 0x0000FF00u

typedef struct {
    float    x, y, z, rhw;
    uint32_t color;
    float    u, v;
} vert2d;

/* Upload one A8R8G8B8 level: copy `src` (tightly packed, `w*4` stride)
 * into level `level` of `tex`, honouring the locked pitch. */
static int upload_level(IDirect3DTexture8 *tex, UINT level,
                        const uint8_t *src, uint32_t w, uint32_t h)
{
    D3DLOCKED_RECT lr = {0};
    HRESULT hr = IDirect3DTexture8_LockRect(tex, level, &lr, NULL, 0);
    if (FAILED(hr)) return 0;
    uint8_t *dst = lr.pBits;
    size_t   row = (size_t)w * 4;
    for (uint32_t y = 0; y < h; y++)
        memcpy(dst + (size_t)y * lr.Pitch, src + (size_t)y * row, row);
    IDirect3DTexture8_UnlockRect(tex, level);
    return 1;
}

/* 2x2 box-downsample BGRA `src` (sw×sh) into `dst` (dw×dh, each ≈half,
 * floored, min 1). Mirrors D3DX's MipFilter=D3DX_DEFAULT (= D3DX_FILTER_BOX:
 * a straight, non-premultiplied 2x2 average) so the generated chain matches
 * retail's D3DXCreateTextureFromFileInMemoryEx(MipLevels=0). For odd source
 * dimensions the +1 sample index is clamped to the last row/column (exact
 * for power-of-two textures, which every audited mesh asset is). */
static void box_downsample(const uint8_t *src, uint32_t sw, uint32_t sh,
                           uint8_t *dst, uint32_t dw, uint32_t dh)
{
    for (uint32_t y = 0; y < dh; y++) {
        uint32_t y0 = y * 2, y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t x0 = x * 2, x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
            const uint8_t *a = src + ((size_t)y0 * sw + x0) * 4;
            const uint8_t *b = src + ((size_t)y0 * sw + x1) * 4;
            const uint8_t *c = src + ((size_t)y1 * sw + x0) * 4;
            const uint8_t *d = src + ((size_t)y1 * sw + x1) * 4;
            uint8_t *o = dst + ((size_t)y * dw + x) * 4;
            for (int ch = 0; ch < 4; ch++)
                o[ch] = (uint8_t)((a[ch] + b[ch] + c[ch] + d[ch] + 2) >> 2);
        }
    }
}

static int sprite_create_impl(IDirect3DDevice8 *dev,
                              const uint8_t *bgra, uint32_t w, uint32_t h,
                              int gen_mips, sprite_t *out)
{
    out->tex = NULL;
    out->width = out->height = 0;

    /* Levels=0 lets D3D allocate the full chain (floor(log2(max))+1);
     * Levels=1 is the single-level path FUN_0047193c uses for 2D UI. */
    IDirect3DTexture8 *tex = NULL;
    HRESULT hr = IDirect3DDevice8_CreateTexture(
        dev, w, h, gen_mips ? 0 : 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
    if (FAILED(hr) || !tex) return 0;

    if (!upload_level(tex, 0, bgra, w, h)) {
        IDirect3DTexture8_Release(tex);
        return 0;
    }

    if (gen_mips) {
        UINT levels = IDirect3DTexture8_GetLevelCount(tex);
        /* Ping-pong CPU buffers: `prev` holds the level we just uploaded,
         * `cur` receives its box-downsample. Each level filters the one
         * above it (a true mip pyramid), matching D3DX. */
        uint8_t *prev = (uint8_t *)malloc((size_t)w * h * 4);
        uint8_t *cur  = (uint8_t *)malloc(((size_t)w * h * 4 + 3) / 4 + 4);
        if (prev && cur) {
            memcpy(prev, bgra, (size_t)w * h * 4);
            uint32_t pw = w, ph = h;
            for (UINT lvl = 1; lvl < levels; lvl++) {
                uint32_t cw = pw > 1 ? pw >> 1 : 1;
                uint32_t cl = ph > 1 ? ph >> 1 : 1;
                box_downsample(prev, pw, ph, cur, cw, cl);
                if (!upload_level(tex, lvl, cur, cw, cl)) break;
                memcpy(prev, cur, (size_t)cw * cl * 4);
                pw = cw; ph = cl;
            }
        }
        free(prev);
        free(cur);
    }

    out->tex    = tex;
    out->width  = w;
    out->height = h;
    return 1;
}

int sprite_create(IDirect3DDevice8 *dev,
                  const uint8_t *bgra, uint32_t w, uint32_t h,
                  sprite_t *out)
{
    return sprite_create_impl(dev, bgra, w, h, /*gen_mips*/0, out);
}

/* Slurp `name` into a freshly-malloc'd buffer: disk first, then the
 * storage overlay. Returns the buffer + sets *out_size on success;
 * returns NULL on failure. Caller frees with free(). Mirrors the
 * fopen-then-storage_read fallback in FUN_0047193c. */
static uint8_t *read_asset(const char *name, size_t *out_size)
{
    *out_size = 0;

    FILE *fp = fopen(name, "rb");
    if (fp) {
        if (fseek(fp, 0, SEEK_END) == 0) {
            long sz = ftell(fp);
            if (sz > 0) {
                rewind(fp);
                uint8_t *buf = (uint8_t *)malloc((size_t)sz);
                if (buf && fread(buf, 1, (size_t)sz, fp) == (size_t)sz) {
                    fclose(fp);
                    *out_size = (size_t)sz;
                    return buf;
                }
                free(buf);
            }
        }
        fclose(fp);
    }

    size_t need = storage_get_size(name);
    if (need == 0) return NULL;
    uint8_t *buf = (uint8_t *)malloc(need);
    if (!buf) return NULL;
    size_t got = storage_read(name, buf);
    if (got == 0) { free(buf); return NULL; }
    *out_size = got;
    return buf;
}

static int sprite_load_impl(IDirect3DDevice8 *dev, const char *name,
                            uint32_t expected_w, uint32_t expected_h,
                            int gen_mips, sprite_t *out)
{
    out->tex = NULL;
    out->width = out->height = 0;
    (void)expected_w;
    (void)expected_h;  /* TODO: resample to match engine's d3dx8 path. */

    size_t  size = 0;
    uint8_t *buf = read_asset(name, &size);
    if (!buf) return 0;

    int ok = 0;
    /* Sniff format from the first two bytes: BMP starts with 'BM'.
     * Otherwise treat as TGA (which has a zero-prefixed header with
     * no magic — we have to commit by elimination). */
    if (size >= 2 && buf[0] == 'B' && buf[1] == 'M') {
        bmp_image img = {0};
        if (bmp_load_mem(buf, size, BMP_COLOR_KEY, &img)) {
            ok = sprite_create_impl(dev, img.pixels, img.width, img.height,
                                    gen_mips, out);
            bmp_free(&img);
        }
    } else {
        tga_image img = {0};
        if (tga_load_mem(buf, size, &img)) {
            ok = sprite_create_impl(dev, img.pixels, img.width, img.height,
                                    gen_mips, out);
            tga_free(&img);
        }
    }

    free(buf);
    return ok;
}

int sprite_load(IDirect3DDevice8 *dev, const char *name,
                uint32_t expected_w, uint32_t expected_h,
                sprite_t *out)
{
    return sprite_load_impl(dev, name, expected_w, expected_h,
                            /*gen_mips*/0, out);
}

int sprite_load_mipped(IDirect3DDevice8 *dev, const char *name,
                       uint32_t expected_w, uint32_t expected_h,
                       sprite_t *out)
{
    return sprite_load_impl(dev, name, expected_w, expected_h,
                            /*gen_mips*/1, out);
}

void sprite_destroy(sprite_t *s)
{
    if (s->tex) { IDirect3DTexture8_Release(s->tex); s->tex = NULL; }
    s->width = s->height = 0;
}

void sprite_draw(IDirect3DDevice8 *dev, const sprite_t *s, float x, float y)
{
    if (!s->tex) return;

    /* Half-pixel offset (DX8 fixed-function texel-to-pixel correction). */
    float x0 = x - 0.5f, y0 = y - 0.5f;
    float x1 = x0 + (float)s->width;
    float y1 = y0 + (float)s->height;

    vert2d v[4] = {
        { x0, y0, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        { x0, y1, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
        { x1, y1, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
    };

    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,    FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,     D3DZB_FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE,    D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,    D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,   D3DBLEND_INVSRCALPHA);

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);

    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s->tex);
    IDirect3DDevice8_SetVertexShader(dev, FVF_2D);
    IDirect3DDevice8_DrawPrimitiveUP(
        dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(vert2d));
    IDirect3DDevice8_SetTexture(dev, 0, NULL);
}
