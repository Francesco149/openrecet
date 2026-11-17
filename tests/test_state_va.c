/*
 * test_state_va.c — `--state-va` VA selection (src/state_va.c): mapped VAs log
 * in order as `post N va=val`, an unmapped VA is rejected, and parsing never
 * disturbs the caller's strtok (main.c's parse_cmdline is mid-strtok).
 */
#include "t.h"
#include <stdio.h>
#include <string.h>
#include "rng.h"
#include "state_va.h"

int test_state_va_logs_selected_in_order(void)
{
    char out[128] = "";
    FILE *fp = tmpfile();
    T_ASSERT(fp != NULL);
    rng_seed(0x1234u);
    T_ASSERT_EQ_I(state_va_select("6023a0,438b1c0", NULL), 2);
    state_va_log(fp, 7);
    rewind(fp);
    T_ASSERT(fgets(out, sizeof out, fp) != NULL);
    fclose(fp);
    T_ASSERT(strncmp(out, "post 7 6023a0=4660 438b1c0=", 27) == 0);
    state_va_select("", NULL);
    return 0;
}

int test_state_va_rejects_unmapped(void)
{
    /* an unmapped VA is skipped (reported via `bad`), the rest still log */
    uint32_t bad = 0;
    T_ASSERT_EQ_I(state_va_select("6023a0,deadbe", &bad), 1);
    T_ASSERT_EQ_I((int)bad, 0xdeadbe);
    return 0;
}

int test_state_va_keeps_outer_strtok(void)
{
    char argv[] = "--state-va 6023a0,438cc08 --state-log x.log";
    char *t = strtok(argv, " ");
    T_ASSERT(t && strcmp(t, "--state-va") == 0);
    t = strtok(NULL, " ");
    T_ASSERT_EQ_I(state_va_select(t, NULL), 2);
    t = strtok(NULL, " ");
    T_ASSERT(t && strcmp(t, "--state-log") == 0);
    state_va_select("", NULL);
    return 0;
}

/* the block hash translates a host pointer only at a pointer field: an actor float whose
 * bits fall in a block's host range hashes raw (obsidian-3f-5f-pumpkin f3255 false split),
 * a records-B owner pointing into g_actor hashes as the retail VA */
#include "actor_table.h"
#include "scene1_records.h"
static uint32_t fnv1(uint32_t v) { return (2166136261u ^ v) * 16777619u; }
int test_state_va_block_hash_pointer_fields_only(void)
{
    uint8_t save[4];
    const uint32_t host = (uint32_t)(uintptr_t)(g_actor[1].b + 0x10);   /* inside g_actor */
    const uint32_t va = 0x76b970u + 0x3fc;
    memcpy(save, g_actor[0].b + 0x3fc, 4);
    memcpy(g_actor[0].b + 0x3fc, &host, 4);
    T_ASSERT_EQ_I((int)state_va_block_hash(va, 4), (int)fnv1(host));
    memcpy(g_actor[0].b + 0x3fc, save, 4);

    const int32_t old = g_scene1_records_b[SCENE1_RECORDS_B_OFF_OWNER_A];
    g_scene1_records_b[SCENE1_RECORDS_B_OFF_OWNER_A] = (int32_t)host;
    T_ASSERT_EQ_I((int)state_va_block_hash(0x69324b0u + 4u * SCENE1_RECORDS_B_OFF_OWNER_A, 4),
                  (int)fnv1(0x76b970u + ACTOR_SIZE + 0x10));
    g_scene1_records_b[SCENE1_RECORDS_B_OFF_OWNER_A] = old;
    return 0;
}
