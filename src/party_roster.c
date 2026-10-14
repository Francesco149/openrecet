/*
 * party_roster.c — see party_roster.h.
 */
#include "party_roster.h"

#include <stdint.h>
#include <stddef.h>   /* NULL */

#include "save_work.h"   /* save_work_dwords_at / save_work_active_slot */

#define PARTY_MET_OFF  0x2bc4c   /* DAT_0450f3e4+k — adventurer k recruited  */
#define PARTY_ALT_OFF  0x2bd18   /* DAT_0450f4b0+k — the second unlock flag   */

static int s_count = 0;                     /* DAT_0741bed8 */
static int s_ids[PARTY_ROSTER_MAX];         /* DAT_07477e74[] */

void party_roster_rebuild(void)
{
    const uint8_t *bb = (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    s_count = 0;
    if (bb == NULL)
        return;
    for (int k = 0; k < PARTY_ROSTER_MAX; k++)
        if (bb[PARTY_MET_OFF + k] != 0 || bb[PARTY_ALT_OFF + k] != 0)
            s_ids[s_count++] = k;
}

int party_roster_count(void) { return s_count; }

int party_roster_row_id(int row)
{
    if (row <= 0 || row > s_count)
        return 0;
    return s_ids[row - 1];
}

void party_roster_reset(void) { s_count = 0; }
