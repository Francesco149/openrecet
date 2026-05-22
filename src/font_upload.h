/*
 * font_upload.h — Win32 D3D8 glyph texture upload (FUN_0047cf22).
 *
 * The engine's approach: build a 32-bpp TGA image in memory and load
 * it through D3DXCreateTextureFromFileInMemoryEx. We skip the TGA
 * dance and call IDirect3DDevice8::CreateTexture + LockRect directly
 * with D3DFMT_A8R8G8B8. Same on-GPU result, no D3DX dependency.
 *
 * Per-pixel format (matches what the engine writes into the TGA):
 *
 *   glyph_byte → { B = high_nibble << 4,
 *                  G = high_nibble << 4,
 *                  R = high_nibble << 4,
 *                  A = low_nibble  << 4 }
 *
 * High nibble (0..15) is the GGO_GRAY4 alpha or the dilation "edge"
 * marker bit; low nibble (0..15) is the edge halo intensity OR the
 * 0xf "body pixel" marker the blit installs.
 *
 * The downstream draw call alpha-blends this against the destination
 * — RGB carries the font's grayscale brightness, A carries the
 * coverage. White body + dark halo combine into the familiar
 * outlined-glyph look.
 *
 * Pure-C helper `font_upload_expand_pixel` is exposed so the pixel
 * encoding can be unit-tested without a D3D device.
 */

#ifndef OPENRECET_FONT_UPLOAD_H
#define OPENRECET_FONT_UPLOAD_H

#include <stdint.h>

/*
 * Convert one encoded glyph byte into an ARGB pixel.
 *
 * Mirrors the engine pixel writer at FUN_0047cf22 lines 80-91:
 *   alpha = (byte >> 4) & 0xf       → RGB = alpha << 4 (replicate)
 *   edge  = byte & 0xf              → A   = edge  << 4
 *
 * Returns a 32-bit value laid out as 0xAARRGGBB (D3DFMT_A8R8G8B8).
 */
uint32_t font_upload_expand_pixel(uint8_t glyph_byte);

#ifdef _WIN32
/* Forward — avoids dragging d3d8.h into headers that don't need it. */
struct IDirect3DDevice8;

/*
 * Allocate and upload a D3D texture for one glyph cache slot. Reads
 * the glyph's fontidx record + raw bytes out of g_font_atlas, expands
 * each byte into an A8R8G8B8 pixel, and stores the resulting
 * IDirect3DTexture8 pointer in g_font.textures[slot_id].
 *
 * If g_font.textures[slot_id] already holds a non-null pointer, it
 * is Release'd before being overwritten — this matches the engine's
 * texture lifecycle (allocator nulls the pointer on evict, upload
 * fills it on new-alloc, but we still defend the order in case the
 * Release callback got skipped).
 *
 * Returns 1 on success (texture installed), 0 on any failure (slot
 * left null, error logged). Returns 1 with texture=NULL for empty
 * glyphs (data_size == 0) — the draw path treats null as "skip".
 *
 * `dev` must be the same IDirect3DDevice8 the renderer uses; pass
 * g_dev from main.c.
 */
int font_slot_upload(int slot_id, struct IDirect3DDevice8 *dev);

/*
 * Release the D3D texture in g_font.textures[slot_id] (if non-null)
 * and null the pointer. Hook this into the allocator via
 * g_font_alloc_release_cb so eviction frees GPU memory.
 */
void font_slot_release(int slot_id);
#endif

#endif /* OPENRECET_FONT_UPLOAD_H */
