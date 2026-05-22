/*
 * font_alloc.h — codepoint→record-id lookup + 200-slot LRU allocator
 * (FUN_0047cbcb @ 0x47cbcb).
 *
 * The slot allocator's life-cycle for a single character:
 *
 *   1. Caller passes the byte(s) of the codepoint.
 *   2. We scan the 200 slots looking for an existing entry that matches
 *      (cp_byte0, cp_byte1). On hit: reset its age, return the slot
 *      index; the caller's texture pointer is still valid.
 *   3. On miss: find a free slot, or evict the oldest in-use one
 *      (Release its texture first via font_alloc_release_fn). Mark the
 *      slot in_use, save the codepoint bytes + record_id, return.
 *   4. The caller is responsible for uploading a new glyph texture to
 *      g_font.textures[slot] when `out_is_new` is non-zero. We don't
 *      do the D3D work in this layer — it stays pure-C.
 *
 * The codepoint→record_id mapping is the writer-iteration index of
 * the relevant fontidx slot:
 *
 *   - 0x00..0x7f (single-byte ASCII):          id = byte
 *   - SJIS double-byte from the special table: id = 256 + table_pos
 *   - SJIS double-byte not in special table:   id = sjis_value - 0x861f
 *
 * The table-lookup branch is engaged only when the double-byte value
 * is < 0x883f (anything below the kanji start range goes through the
 * special table first).
 */

#ifndef OPENRECET_FONT_ALLOC_H
#define OPENRECET_FONT_ALLOC_H

#include <stdint.h>

/*
 * Result code for the "no such glyph" case — returned by both the
 * codepoint lookup and the slot allocator. Caller should fall through
 * to a no-op render (engine's behavior: the slot pointer is checked
 * against NULL before SetTexture / DrawPrimitive).
 */
#define FONT_RECORD_NONE   (-1)
#define FONT_SLOT_NONE     (-1)

/*
 * Map a codepoint to its fontidx record index. cp_byte1 is ignored
 * when cp_byte0 < 0x80 (ASCII).
 *
 * Returns FONT_RECORD_NONE if the codepoint isn't representable in
 * the engine's atlas (a 2-byte value < 0x883f not present in the
 * special table).
 *
 * Pure function — no global state read.
 */
int font_codepoint_to_record_id(uint8_t cp_byte0, uint8_t cp_byte1);

/*
 * Find or allocate a cache slot for the given codepoint.
 *
 *   - If an existing slot already holds this codepoint, reset its
 *     age, set *out_is_new = 0, return the slot index.
 *   - Otherwise: allocate a free slot (or evict the oldest in-use
 *     one that has age > 3). Initialize the slot (cp_bytes, in_use=1,
 *     age=0, record_id). Set *out_is_new = 1. Return the slot index.
 *
 * When eviction kicks in and the evicted slot had a non-null texture
 * pointer in g_font.textures[evicted_slot], we DO NOT release the
 * texture in this layer (no D3D dependency). Caller hooks an
 * `font_alloc_release_cb` if it needs Release-on-evict semantics.
 *
 * Returns FONT_SLOT_NONE if the codepoint isn't representable
 * (the underlying record_id was FONT_RECORD_NONE).
 *
 * `out_record_id` is set to the fontidx index for the resolved slot
 * — pass NULL if not needed. `out_is_new` may also be NULL.
 */
int font_slot_alloc(uint8_t cp_byte0, uint8_t cp_byte1,
                    int *out_record_id, int *out_is_new);

/*
 * Optional release callback invoked from font_slot_alloc when a slot
 * is evicted (existing in_use=1 slot with age > 3 reassigned to a new
 * codepoint). The Win32 driver hooks this to IDirect3DTexture8::Release
 * on g_font.textures[evicted_slot] before nulling the pointer.
 *
 * On Linux/tests the hook stays NULL → the pointer is nulled but the
 * "Release" never fires.
 */
typedef void (*font_alloc_release_fn)(int slot_id);
extern font_alloc_release_fn g_font_alloc_release_cb;

#endif /* OPENRECET_FONT_ALLOC_H */
