/*
 * font_alloc.c — slot allocator + codepoint→record-id lookup.
 *
 * Source: FUN_0047cbcb @ 0x47cbcb. See font_alloc.h for the design,
 * and font.h for the slot-table layout this module operates on.
 *
 * The engine's allocator returns a "slot - 12" pointer for offset
 * convenience of the downstream draw_text. We don't reproduce that
 * pointer trickery — slot_alloc returns a plain slot index and lets
 * the consumer index g_font.slots[] / g_font.textures[] directly.
 */

#include "font_alloc.h"

#include <stddef.h>

#include "font.h"
#include "font_atlas.h"

font_alloc_release_fn g_font_alloc_release_cb;

int font_codepoint_to_record_id(uint8_t cp_byte0, uint8_t cp_byte1)
{
    /* Single-byte (high bit clear) → direct index. */
    if ((cp_byte0 & 0x80) == 0) {
        return (int)cp_byte0;
    }

    uint16_t cp16 = ((uint16_t)cp_byte0 << 8) | cp_byte1;

    /* Codepoints in [0x80..0x883e] route through the special table.
     * The engine's outer check is `(cp << 8 | next_byte) < 0x883f`
     * — for double-byte that's the same as cp16 < 0x883f.
     *
     * Note the table-scan terminates on a 0-lead-byte sentinel. Our
     * embedded copy preserves all 288 entries (no sentinel needed
     * thanks to the fixed count). */
    if (cp16 < 0x883f) {
        for (int i = 0; i < FONT_ATLAS_SPECIAL_TABLE_COUNT; i++) {
            if (font_atlas_special_table[i * 2]     == cp_byte0 &&
                font_atlas_special_table[i * 2 + 1] == cp_byte1) {
                return 256 + i;
            }
        }
        /* Not in table — no glyph available. (Engine returns NULL
         * pointer here at line 79719's `return (char *)0x0;`.) */
        return FONT_RECORD_NONE;
    }

    /* SJIS double-byte 0x883f..0xffff — direct offset. */
    return (int)cp16 - 0x861f;
}

/* Scan all slots for an existing entry with matching codepoint bytes.
 * Returns the slot index on hit (and resets its age), or FONT_SLOT_NONE
 * on miss.
 *
 * Engine's match logic for double-byte: both bytes must match. For
 * single-byte: only cp_byte0 is checked (cp_byte1 doesn't matter; the
 * slot's stored cp_byte1 should be 0 from init/alloc). */
static int find_existing(uint8_t cp_byte0, uint8_t cp_byte1)
{
    int is_double_byte = (cp_byte0 & 0x80) != 0;
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        struct font_slot *s = &g_font.slots[i];
        if (s->in_use != 1) continue;
        if (s->cp_byte0 != cp_byte0) continue;
        if (is_double_byte && s->cp_byte1 != cp_byte1) continue;
        /* Hit. Engine resets _DAT_073dfcfc to 0 here — we don't have
         * a port for that global yet (it's a per-frame stat counter
         * for the unused debug overlay). */
        s->age = 0;
        return i;
    }
    return FONT_SLOT_NONE;
}

/* Find a free slot (in_use == 0). Engine walks slots 0..199 in order
 * and returns the first hit. Returns FONT_SLOT_NONE if all full. */
static int find_free(void)
{
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        if (g_font.slots[i].in_use == 0) return i;
    }
    return FONT_SLOT_NONE;
}

/* Find the first slot with age > 3 (eviction candidate). The engine
 * scans in slot order, NOT in age order — first eligible wins, ages
 * aren't compared globally. */
static int find_evictable(void)
{
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        if ((int)g_font.slots[i].age > 3) return i;
    }
    return FONT_SLOT_NONE;
}

int font_slot_alloc(uint8_t cp_byte0, uint8_t cp_byte1,
                    int *out_record_id, int *out_is_new)
{
    /* Compute the record id once up front. If it's NONE, the engine
     * never even allocates a slot for the codepoint. */
    int record_id = font_codepoint_to_record_id(cp_byte0, cp_byte1);
    if (record_id == FONT_RECORD_NONE) {
        if (out_record_id) *out_record_id = FONT_RECORD_NONE;
        if (out_is_new)    *out_is_new    = 0;
        return FONT_SLOT_NONE;
    }

    /* Phase 1: existing-entry search. */
    int slot = find_existing(cp_byte0, cp_byte1);
    if (slot != FONT_SLOT_NONE) {
        if (out_record_id) *out_record_id = record_id;
        if (out_is_new)    *out_is_new    = 0;
        return slot;
    }

    /* Phase 2: free-slot search. */
    slot = find_free();

    /* Phase 3: eviction (only if no free slot). */
    if (slot == FONT_SLOT_NONE) {
        slot = find_evictable();
        if (slot != FONT_SLOT_NONE) {
            /* Engine calls texture->Release before nulling the pointer.
             * We delegate via the optional release callback so the
             * Win32 path can do the right thing without dragging the
             * D3D dependency into this module. */
            if (g_font_alloc_release_cb) {
                g_font_alloc_release_cb(slot);
            }
            g_font.textures[slot] = NULL;
        }
    }

    /* All 200 slots are in_use AND none is past age threshold → fail.
     * Engine prints "OVER FONT" debug; we just return NONE silently. */
    if (slot == FONT_SLOT_NONE) {
        if (out_record_id) *out_record_id = FONT_RECORD_NONE;
        if (out_is_new)    *out_is_new    = 0;
        return FONT_SLOT_NONE;
    }

    /* Initialize the slot with the new codepoint + record. */
    struct font_slot *s = &g_font.slots[slot];
    s->in_use    = 1;
    s->cp_byte0  = cp_byte0;
    s->cp_byte1  = (cp_byte0 & 0x80) ? cp_byte1 : 0;
    s->age       = 0;
    s->record_id = (uint32_t)record_id;
    /* slot_id stays at its init value (== slot index). */

    if (out_record_id) *out_record_id = record_id;
    if (out_is_new)    *out_is_new    = 1;
    return slot;
}
