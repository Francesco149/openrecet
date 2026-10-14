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
