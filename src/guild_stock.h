/*
 * guild_stock — FUN_00490e56: the Merchants' Guild's per-item stock.
 *
 * 600 item slots: each item record's stock_info[0] (DAT_095d3810 = the item
 * table DAT_095d37d0 + 0x40, stride 0x2cc) is its restock kind; the work bank
 * holds {i16 count, i16 timer} per slot at +0x295f8 (DAT_0450cd90), the guild
 * buy list's quantity cap (scene1_display_menu.c).
 *
 * mode 1 (Continue, FUN_0049a59e): re-init only when slot 0's count is 0 or a
 *   kind-4 slot holds a count >= 2.
 * mode 0 (new game FUN_0049d36d, a new day's morning FUN_0048670f, the restart
 *   FUN_0049a59e): clears work +0x2dde0, then the init pass when slot 0's count
 *   is 0, else the daily pass.
 * init: kind 0 → 100; 1 → rand % 3 + 3; 2 → rand, 0; 4 → 0 (no draw);
 *   other → rand, 1 — over 1024 slots: the pass runs 424 records past the
 *   item table, reading kinds from the memory beyond it and writing counts
 *   up to work +0x2a5f4 (engine quirk §163).
 * daily: kind 0 → 100; 1 → +1 every 5 days, up to 10; 2 → rand % 3, and 1
 *   after i % 8 + 15 days; 4 → max(rand % 3 - 1, 0), 1 after i % 8 + 20 days;
 *   5 / 6 / other → while 0, 1 after 1 / 2 / 3 days.
 */
#ifndef GUILD_STOCK_H
#define GUILD_STOCK_H

#define GUILD_STOCK_ITEMS 600
#define GUILD_STOCK_OFF   0x295f8   /* work bank: {i16 count, i16 timer} × 600 */
/* the init pass's bound (cmp edi, 0x9686810 at 0x491037): 1024 strides of
 * 0x2cc from DAT_095d3810 — 424 past the item table, so it writes up to +0x2a5f4 */
#define GUILD_STOCK_INIT_ITEMS 1024

void guild_stock_daily(int mode);

/* The low byte of retail's DirectMusic segment pointer DAT_09643038[16] that
 * the init overrun reads as slot 638's kind: 0x30 unless told (a heap address,
 * 8-aligned; 0 when DirectMusic failed to load the segment, e.g. under wine). */
void guild_stock_set_dmusic_ptr_lo(int lo);

#endif
