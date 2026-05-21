/*
 * font.h — engine font system, 200-slot LRU glyph-texture cache.
 *
 * Source references:
 *   - FUN_0047c228 @ 0x47c228  ("init fontsys ok" anchor)
 *   - FUN_0047c29d @ 0x47c29d  (per-tick LRU age, called from sim_a)
 *   - FUN_0047c3a5 @ 0x47c3a5  (atlas loader — fontdata.bin / fontidx.bin)
 *   - FUN_0047c474 @ 0x47c474  (GDI atlas builder — Win32-only)
 *   - FUN_0047cbcb @ 0x47cbcb  (slot allocator + evictor)
 *   - FUN_0047cf22 @ 0x47cf22  (per-glyph D3D texture upload)
 *   - FUN_0047ca05 @ 0x47ca05  (draw_text — the consumer)
 *
 * This header covers the LRU cache state + lifecycle. The loader,
 * builder, allocator, uploader and draw_text are added in subsequent
 * commits and share this state.
 *
 * Cache layout (mirroring the engine's flat globals):
 *   - `slots[200]` — one struct font_slot per cache line. Engine
 *     globals span DAT_073de664..DAT_073dfc44 (28 bytes/slot).
 *   - `textures[200]` — parallel IDirect3DTexture8* table. Engine
 *     global at DAT_073dde44. Stored as void* so Linux tests compile.
 *
 * The 28-byte per-slot stride breaks down as (from the c228 init
 * write pattern and the c29d/cbcb readers):
 *
 *   +0  uint32 slot_id   — set to the slot index 0..199 at init.
 *   +4  uint8  cp_byte0  — first SJIS byte of the cached codepoint
 *                          (set by the allocator when a slot is filled)
 *   +5  uint8  cp_byte1  — second SJIS byte (0 for single-byte codepts)
 *   +6  uint16 _pad6
 *   +8  uint32 in_use    — 1 once allocated, 0 when free or evicted
 *   +12 uint32 age       — incremented every sim_a frame while in_use
 *   +16 uint32 record_id — fontidx[record_id] supplies width/height/etc
 *   +20 uint32 _pad20
 *   +24 uint32 _pad24
 *
 * The +16..+24 trio is reconstructed from FUN_0047cbcb's readers; the
 * exact role of pad20/pad24 is TBD but they're definitely touched by
 * the allocator. The atlas builder + slot allocator commits will
 * confirm the layout and rename if needed.
 */

#ifndef OPENRECET_FONT_H
#define OPENRECET_FONT_H

#include <stdint.h>

#define FONT_SLOT_COUNT 200

struct font_slot {
    uint32_t slot_id;    /* 0..199, set at init                     */
    uint8_t  cp_byte0;   /* first SJIS byte of cached codepoint     */
    uint8_t  cp_byte1;   /* second SJIS byte, 0 if single-byte      */
    uint16_t _pad6;
    uint32_t in_use;     /* 1 = slot owns a glyph, 0 = free         */
    uint32_t age;        /* sim_a ticks since last touch            */
    uint32_t record_id;  /* index into fontidx.bin (filled later)   */
    uint32_t _pad20;
    uint32_t _pad24;
};

struct font_state {
    /* 200 slot records. Engine spans DAT_073de664..DAT_073dfc44. */
    struct font_slot slots[FONT_SLOT_COUNT];

    /* 200 parallel D3D texture pointers. Engine global DAT_073dde44.
     * Kept as void* so this module is Linux-buildable; the Win32
     * upload path in font_upload.c casts to IDirect3DTexture8*. */
    void *textures[FONT_SLOT_COUNT];

    /* The two engine "default size" globals at DAT_073b18bc /
     * DAT_073b18c0, both set to 0x2a by FUN_0047c228. Likely
     * runtime-mutable font size knobs; no readers ported yet. */
    int32_t default_height;
    int32_t default_height2;
};

extern struct font_state g_font;

/*
 * FUN_0047c228 port — zero the 200-slot cache + texture pointer table,
 * seed slot_id with each entry's index, set both default-height globals
 * to 42. Idempotent (safe to call again on a soft re-init).
 */
void font_init(void);

/*
 * FUN_0047c29d port — per-frame aging. Walks the slot table, increments
 * `age` on every in-use entry, and (in the engine) emits a debug
 * overlay line via FUN_00451874. The debug call is a no-op stub in our
 * skeleton so this function just bumps ages.
 *
 * Called from sim_step_a once per frame, right after the button-state
 * ring update — matches FUN_004536cb's ordering.
 */
void font_age_tick(void);

#endif /* OPENRECET_FONT_H */
