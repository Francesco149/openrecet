/*
 * font_upload.c — Win32 D3D glyph texture upload.
 *
 * See font_upload.h for the high-level format + signature. The
 * pure-C `font_upload_expand_pixel` lives outside the _WIN32 guard
 * so tests can exercise the byte→ARGB mapping on Linux.
 */

#include "font_upload.h"

#include <stdio.h>

#include "font.h"
#include "font_atlas.h"

uint32_t font_upload_expand_pixel(uint8_t glyph_byte)
{
    /* Engine math at FUN_0047cf22 lines 81-90:
     *   alpha (high nibble) is replicated into R/G/B as (alpha << 4)
     *   edge  (low nibble)  becomes the alpha channel  as (edge  << 4)
     *
     * Both nibbles get left-shifted by 4 (multiplied by 16), so 15
     * → 240 not 255. Slightly dimmer than ideal but matches the
     * engine's exact bit pattern. */
    uint32_t alpha = (glyph_byte >> 4) & 0xf;
    uint32_t edge  =  glyph_byte       & 0xf;
    uint32_t rgb_byte = alpha << 4;       /* 0, 16, 32, ..., 240 */
    uint32_t a_byte   = edge  << 4;
    return (a_byte << 24) | (rgb_byte << 16) | (rgb_byte << 8) | rgb_byte;
}

#ifdef _WIN32

#define COBJMACROS
#include <d3d8.h>

void font_slot_release(int slot_id)
{
    if (slot_id < 0 || slot_id >= FONT_SLOT_COUNT) return;
    IDirect3DTexture8 *tex = (IDirect3DTexture8 *)g_font.textures[slot_id];
    if (tex) {
        IDirect3DTexture8_Release(tex);
        g_font.textures[slot_id] = NULL;
    }
}

int font_slot_upload(int slot_id, struct IDirect3DDevice8 *dev_)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_;
    if (!dev) return 0;
    if (slot_id < 0 || slot_id >= FONT_SLOT_COUNT) return 0;

    uint32_t record_id = g_font.slots[slot_id].record_id;
    if ((size_t)record_id >= g_font_atlas.fontidx_count) {
        fprintf(stderr,
            "font_upload: record_id %u out of range (%zu)\n",
            (unsigned)record_id, g_font_atlas.fontidx_count);
        return 0;
    }
    struct font_atlas_record *rec = &g_font_atlas.fontidx[record_id];

    /* Release any existing texture in the slot — defensive: the
     * allocator's release callback should have done this already
     * during eviction, but on a fresh allocation into a previously
     * freed slot the pointer might still be set (e.g. mid-frame
     * upload retry). */
    font_slot_release(slot_id);

    /* Empty glyph (size 0) — common for ASCII non-printables that
     * GDI rasterizes to nothing. Texture stays NULL; draw path skips. */
    if (rec->data_size == 0 || rec->tex_width <= 0 || rec->tex_height <= 0) {
        return 1;
    }

    /* Bounds check the glyph bytes against the loaded atlas. */
    size_t end = (size_t)rec->data_offset + (size_t)rec->data_size;
    if (end > g_font_atlas.fontdata_size) {
        fprintf(stderr,
            "font_upload: record %u glyph bytes overrun atlas "
            "(off=%u size=%u atlas=%zu)\n",
            (unsigned)record_id,
            (unsigned)rec->data_offset,
            (unsigned)rec->data_size,
            g_font_atlas.fontdata_size);
        return 0;
    }
    const uint8_t *glyph = g_font_atlas.fontdata + rec->data_offset;
    int tex_w = rec->tex_width;
    int tex_h = rec->tex_height;

    /* Caller passed glyph buffer that's exactly tex_w * tex_h bytes —
     * verify so a malformed atlas can't read past its data. */
    if ((uint32_t)(tex_w * tex_h) != rec->data_size) {
        fprintf(stderr,
            "font_upload: record %u tex_w*tex_h=%d but data_size=%u\n",
            (unsigned)record_id, tex_w * tex_h, (unsigned)rec->data_size);
        return 0;
    }

    IDirect3DTexture8 *tex = NULL;
    HRESULT hr = IDirect3DDevice8_CreateTexture(
        dev,
        (UINT)tex_w, (UINT)tex_h,
        /*Levels*/1,
        /*Usage*/0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &tex);
    if (FAILED(hr) || !tex) {
        fprintf(stderr,
            "font_upload: CreateTexture failed (slot=%d w=%d h=%d hr=0x%08lx)\n",
            slot_id, tex_w, tex_h, (unsigned long)hr);
        return 0;
    }

    D3DLOCKED_RECT lr;
    hr = IDirect3DTexture8_LockRect(tex, 0, &lr, NULL, 0);
    if (FAILED(hr)) {
        IDirect3DTexture8_Release(tex);
        fprintf(stderr,
            "font_upload: LockRect failed (slot=%d hr=0x%08lx)\n",
            slot_id, (unsigned long)hr);
        return 0;
    }

    uint8_t *dst_base = (uint8_t *)lr.pBits;
    for (int y = 0; y < tex_h; y++) {
        uint32_t *row = (uint32_t *)(dst_base + (size_t)y * lr.Pitch);
        const uint8_t *src_row = glyph + (size_t)y * tex_w;
        for (int x = 0; x < tex_w; x++) {
            row[x] = font_upload_expand_pixel(src_row[x]);
        }
    }
    IDirect3DTexture8_UnlockRect(tex, 0);

    g_font.textures[slot_id] = tex;
    return 1;
}

#endif /* _WIN32 */
