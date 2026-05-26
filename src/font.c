/*
 * font.c — engine font system init + per-tick aging.
 *
 * See font.h for layout + source-address mapping. Subsequent commits
 * add the atlas loader, the GDI atlas builder, the slot allocator, the
 * glyph texture-upload path, and draw_text. Each will live in its own
 * translation unit (font_atlas.c, font_alloc.c, font_upload.c,
 * font_draw.c) and operate on the shared `g_font` state declared here.
 */

#include "font.h"
#include "call_trace.h"

#include <string.h>

struct font_state g_font;

void font_init(void)
{
    /* Both engine writes:
     *   _DAT_073b18bc = 0x2a;
     *   _DAT_073b18c0 = 0x2a;
     * Mirror them. The other 200-entry-table-clear loops are below. */
    g_font.default_height  = 0x2a;
    g_font.default_height2 = 0x2a;

    /* DAT_073dde44 table — zero all 200 texture pointers. The engine's
     * loop is `for (i = 200; i; --i) *puVar4++ = 0;`. The Release call
     * on the existing texture (if any) is the allocator's job — at
     * init time the BSS table is already null. */
    memset(g_font.textures, 0, sizeof g_font.textures);

    /* DAT_073de664 table — 200 slots, each cleared to {id=i, ?, ?, in_use=0,
     * age=0, ...}. The engine's loop only writes three of the seven
     * dwords (+0, +8, +12) because the rest are .bss-zero. We memset
     * the whole struct to keep the meaning clean for future readers. */
    memset(g_font.slots, 0, sizeof g_font.slots);
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        g_font.slots[i].slot_id = (uint32_t)i;
    }
}

void font_age_tick(void)
{
    /* E.2 probe — FUN_0047c29d @ 0x47c29d. */
    CALL_TRACE_ENTER(0x47c29du);

    /* FUN_0047c29d body (minus the dead debug computation).
     *
     * Engine's loop walks &DAT_073de670 (slot[0].age) stepping by 7
     * dwords. For each entry, if the dword just before it
     * (slot[i].in_use at offset +8) is non-zero, increment the age and
     * increment a local `total` counter used by the debug overlay.
     *
     * After the aging loop the engine also picks a candidate slot for
     * the next allocation (free if any, else first slot with age > 1),
     * and sprintf-s "OVER FONT"/"%d" + "TOTAL =%d" into a buffer for
     * FUN_00451874. FUN_00451874 is a stub (`return;`) in our skeleton,
     * so the candidate scan has no observable effect — we drop it. If
     * the debug overlay ever gets wired, restore this logic from
     * docs/decompiled/by-address/47c29d.c lines 23-43. */
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        if (g_font.slots[i].in_use) {
            g_font.slots[i].age++;
        }
    }
}
