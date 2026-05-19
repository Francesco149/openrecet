#include "sprite.h"

#include <string.h>

#define FVF_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct {
    float    x, y, z, rhw;
    uint32_t color;
    float    u, v;
} vert2d;

int sprite_create(IDirect3DDevice8 *dev,
                  const uint8_t *bgra, uint32_t w, uint32_t h,
                  sprite_t *out)
{
    out->tex = NULL;
    out->width = out->height = 0;

    IDirect3DTexture8 *tex = NULL;
    HRESULT hr = IDirect3DDevice8_CreateTexture(
        dev, w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
    if (FAILED(hr) || !tex) return 0;

    D3DLOCKED_RECT lr = {0};
    hr = IDirect3DTexture8_LockRect(tex, 0, &lr, NULL, 0);
    if (FAILED(hr)) { IDirect3DTexture8_Release(tex); return 0; }

    const uint8_t *src = bgra;
    uint8_t       *dst = lr.pBits;
    size_t row = (size_t)w * 4;
    for (uint32_t y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * lr.Pitch, src + (size_t)y * row, row);
    }
    IDirect3DTexture8_UnlockRect(tex, 0);

    out->tex    = tex;
    out->width  = w;
    out->height = h;
    return 1;
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
