/* P1 — the guild Fusion's bag arithmetic: retail vs port on random bags.
 *   FUN_00469118 cdecl(int id)                 vs guild_count_item
 *   FUN_00469155 cdecl(int id, int qty, int plus) vs guild_consume_item
 * Both read the working bank DAT_044e3798 + 188360·DAT_0438b1e0 (slot 0 here):
 * the item table at +0x18 (20000 dwords), the count at dword 0xaec6.  Each case
 * fills a bag with a few ids at random + (low nibble), runs both, and compares
 * the return and every dword of the table + count. */
#include "retail_map.h"
#include <string.h>
#include "guild_fusion.h"
#include "save_bank.h"
RETAIL_HOLE_DEFINE
#define G32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define BANK_VA 0x44e3798u

static uint32_t s_rng = 12345;
static uint32_t rnd(void) { s_rng = s_rng * 1103515245u + 12345u; return (s_rng >> 8) & 0xffffff; }

int main(int argc, char **argv)
{
    retail_image_t img;
    if (retail_map(argc > 1 ? argv[1] : "recettear.unpacked.exe", &img, 1)) return 1;
    G32(0x438b1e0) = 0;
    uint32_t *rb = (uint32_t *)(uintptr_t)BANK_VA;
    static uint32_t pb[SAVE_BANK_STRIDE_DWORDS];
    const int ids[4] = { 1201, 3302, 5101, 1203 };
    long n = 0, mis = 0, took = 0;
    for (int c = 0; c < 20000; c++) {
        for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) rb[SAVE_BANK_ITEM_TABLE_DWORD + i] = 0xffffffffu;
        const int len = (int)(rnd() % 40);
        for (int i = 0; i < len; i++) {
            const int id = ids[rnd() % 4];
            rb[SAVE_BANK_ITEM_TABLE_DWORD + i] = ((uint32_t)id << 6) | (rnd() % 16) | (rnd() % 8 == 0 ? 0x10 : 0);
        }
        rb[SAVE_BANK_FIELD_ITEM_COUNT] = (uint32_t)len;
        memcpy(pb, rb, sizeof pb);
        const int id = rnd() % 5 == 0 ? -1 : ids[rnd() % 4];
        const int qty = (int)(rnd() % 6), plus = rnd() % 3 == 0 ? (int)(rnd() % 20) : 15;
        const int rc = ((int (__cdecl *)(int))0x469118)(id), pc = guild_count_item(pb, id);
        const int rr = ((int (__cdecl *)(int, int, int))0x469155)(id, qty, plus);
        const int pr = guild_consume_item(pb, id, qty, plus);
        n++;
        took += rb[SAVE_BANK_FIELD_ITEM_COUNT] != (uint32_t)len;
        if (rc != pc || rr != pr || memcmp(rb + SAVE_BANK_ITEM_TABLE_DWORD, pb + SAVE_BANK_ITEM_TABLE_DWORD, 4 * SAVE_BANK_ITEM_TABLE_COUNT) ||
            rb[SAVE_BANK_FIELD_ITEM_COUNT] != pb[SAVE_BANK_FIELD_ITEM_COUNT]) {
            if (mis++ < 5)
                printf("  case %d id=%d qty=%d plus=%d: count %d/%d ret %d/%d n %u/%u\n", c, id, qty, plus,
                       rc, pc, rr, pr, rb[SAVE_BANK_FIELD_ITEM_COUNT], pb[SAVE_BANK_FIELD_ITEM_COUNT]);
        }
    }
    printf("fusion count+consume: %ld/%ld identical (%ld cases took items)\n", n - mis, n, took);
    return mis != 0;
}
