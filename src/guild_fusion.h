/*
 * guild_fusion.h — the Merchants' Guild Fusion's bag arithmetic (pure C, so the
 * P1 oracle tools/oracle/p1_fusion.c can run it against retail).
 *   FUN_00469118  guild_count_item    how many of item id the bag holds
 *   FUN_00469155  guild_consume_item  take qty of item id out, picking by +
 * `bank` is a working bank as dwords (the item table at dword 6, its count at
 * SAVE_BANK_FIELD_ITEM_COUNT).
 */
#ifndef OPENRECET_GUILD_FUSION_H
#define OPENRECET_GUILD_FUSION_H

#include <stdint.h>

int guild_count_item(const uint32_t *bank, int id);
int guild_consume_item(uint32_t *bank, int id, int qty, int plus);

#endif
