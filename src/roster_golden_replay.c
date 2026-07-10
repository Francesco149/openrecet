/*
 * roster_golden_replay.c — see roster_golden_replay.h.
 *
 * The bit-exact gate for cs_roster_scan: load the retail-captured working
 * arena, run the ported scan for each seed, and dump {count, eligible[],
 * queue[], rng_draws} in the same shape as roster_scan_capture.py's golden,
 * so a diff proves (or refutes) RNG/output parity offline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "roster_golden_replay.h"
#include "customer_service.h"
#include "save_work.h"
#include "save_bank.h"   /* SAVE_BANK_STRIDE_BYTES */
#include "rng.h"

#define REPLAY_ELIG_CAP  52
#define REPLAY_QUEUE_CAP 30

static void run_one(FILE *out, const uint8_t *arena, uint32_t seed, int first)
{
    /* Restore the exact captured arena into working slot 0 (the scan mutates
     * story flags + closeness, so every seed starts from the clean input). */
    uint8_t *slot0 = (uint8_t *)save_work_dwords_at(0);
    memcpy(slot0, arena, SAVE_BANK_STRIDE_BYTES);

    rng_seed(seed);
    unsigned long c0 = rng_call_count();
    customer_service_session_init();          /* prologue + cs_roster_scan */
    unsigned long draws = rng_call_count() - c0;

    int count = customer_service_queue_count();

    fprintf(out, "%s\n    {\n", first ? "" : ",");
    fprintf(out, "      \"seed\": %u,\n", seed);
    fprintf(out, "      \"count\": %d,\n", count);

    fprintf(out, "      \"eligible\": [");
    for (int i = 0, n = 0; i < REPLAY_ELIG_CAP; i++) {
        int v = customer_service_eligible(i);
        if (v == -2 || v == -1) break;
        fprintf(out, "%s%d", n++ ? ", " : "", v);
    }
    fprintf(out, "],\n");

    fprintf(out, "      \"queue\": [");
    int qn = count < REPLAY_QUEUE_CAP ? count : REPLAY_QUEUE_CAP;
    for (int i = 0; i < qn; i++) {
        fprintf(out, "%s{\"kyaku\": %d, \"item_slot\": %d, \"kind\": %d}",
                i ? ", " : "",
                customer_service_queue_kyaku(i),
                customer_service_queue_item_slot(i),
                customer_service_queue_kind(i));
    }
    fprintf(out, "],\n");

    fprintf(out, "      \"rng_draws\": %lu\n    }", draws);
}

void roster_golden_replay_maybe(void)
{
    const char *arena_path = getenv("OPENRECET_ROSTER_GOLDEN");
    if (arena_path == NULL || arena_path[0] == '\0')
        return;

    FILE *af = fopen(arena_path, "rb");
    if (af == NULL) {
        fprintf(stderr, "roster-replay: cannot open arena %s\n", arena_path);
        exit(2);
    }
    static uint8_t arena[SAVE_BANK_STRIDE_BYTES];
    size_t got = fread(arena, 1, sizeof arena, af);
    fclose(af);
    if (got != sizeof arena) {
        fprintf(stderr, "roster-replay: short arena read %zu/%u\n",
                got, (unsigned)sizeof arena);
        exit(2);
    }

    const char *seeds_env = getenv("OPENRECET_ROSTER_SEEDS");
    const char *out_path  = getenv("OPENRECET_ROSTER_OUT");
    if (out_path == NULL || out_path[0] == '\0')
        out_path = "roster_port_out.json";

    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "roster-replay: cannot write %s\n", out_path);
        exit(2);
    }

    customer_service_set_roster_replay(1);

    fprintf(out, "{\n  \"function\": \"cs_roster_scan\",\n  \"results\": [");

    /* Parse the comma seed list (default "1"). */
    char buf[256];
    if (seeds_env == NULL || seeds_env[0] == '\0') seeds_env = "1";
    snprintf(buf, sizeof buf, "%s", seeds_env);
    int first = 1;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ') tok++;
        if (*tok == '\0') continue;
        uint32_t seed = (uint32_t)strtoul(tok, NULL, 0);
        run_one(out, arena, seed, first);
        first = 0;
    }

    fprintf(out, "\n  ]\n}\n");
    fclose(out);
    fprintf(stderr, "roster-replay: wrote %s\n", out_path);
    exit(0);
}
