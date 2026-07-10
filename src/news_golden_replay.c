/*
 * news_golden_replay.c — see news_golden_replay.h.
 *
 * The bit-exact gate for news_daily_update: load the retail-captured
 * working arena, run the ported generator for each seed, and dump the
 * complete mutated-output set in the same shape as news_gen_capture.py's
 * golden, so a diff proves (or refutes) RNG/output parity offline.
 * Headline text is dumped as HEX (SJIS bodies from the user's news.txt —
 * byte-exact compare, no JSON-escaping ambiguity).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "news_golden_replay.h"
#include "news_daily.h"
#include "save_work.h"
#include "save_bank.h"
#include "rng.h"

static void dump_hex(FILE *out, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        fprintf(out, "%02x", p[i]);
}

static void run_one(FILE *out, const uint8_t *arena, uint32_t seed, int first)
{
    /* Restore the exact captured arena into working slot 0 — the generator
     * mutates the news list, headlines, offsets and pair TTLs, so every
     * seed starts from the clean input. */
    uint8_t *slot0 = (uint8_t *)save_work_dwords_at(0);
    memcpy(slot0, arena, SAVE_BANK_STRIDE_BYTES);

    rng_seed(seed);
    unsigned long c0 = rng_call_count();
    news_daily_update(slot0);
    unsigned long draws = rng_call_count() - c0;

    const int32_t *w = (const int32_t *)slot0;
    int hl = w[SAVE_BANK_FIELD_NEWS_HL_COUNT];

    fprintf(out, "%s\n    {\n", first ? "" : ",");
    fprintf(out, "      \"seed\": %u,\n", seed);

    fprintf(out, "      \"list\": [");
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        const uint8_t *e = slot0 + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc;
        fprintf(out,
                "%s{\"target\": %d, \"id\": %d, \"trend\": %d, \"dur\": %d}",
                i ? ", " : "",
                *(const int32_t *)e, *(const int32_t *)(e + 4),
                (int)(int8_t)e[8], (int)(int8_t)e[9]);
    }
    fprintf(out, "],\n");

    fprintf(out, "      \"hl_count\": %d,\n", hl);
    fprintf(out, "      \"headlines\": [");
    for (int i = 0; i < hl; i++) {
        const uint8_t *row = slot0 + SAVE_BANK_NEWS_HL_TEXT_BYTE_OFF
                             + i * SAVE_BANK_NEWS_HL_ROW_BYTES;
        int len = 0;
        while (len < SAVE_BANK_NEWS_HL_ROW_BYTES && row[len] != 0)
            len++;
        fprintf(out, "%s\"", i ? ", " : "");
        dump_hex(out, row, len);
        fprintf(out, "\"");
    }
    fprintf(out, "],\n");

    fprintf(out, "      \"offsets\": [");
    for (int i = 0; i < hl; i++)
        fprintf(out, "%s%d", i ? ", " : "",
                w[SAVE_BANK_FIELD_NEWS_HL_OFFS + i]);
    fprintf(out, "],\n");
    fprintf(out, "      \"offsets_total\": %d,\n",
            w[SAVE_BANK_FIELD_NEWS_HL_TOTAL]);

    fprintf(out, "      \"pairs\": [");
    const int16_t *pp =
        (const int16_t *)(slot0 + SAVE_BANK_NEWS_PAIRS_BYTE_OFF);
    for (int i = 0; i < SAVE_BANK_NEWS_PAIRS_COUNT * 2; i++)
        fprintf(out, "%s%d", i ? ", " : "", (int)pp[i]);
    fprintf(out, "],\n");

    fprintf(out, "      \"rng_draws\": %lu,\n", draws);
    fprintf(out, "      \"final_seed\": %u\n    }", (unsigned)g_rng_seed);
}

void news_golden_replay_maybe(void)
{
    const char *arena_path = getenv("OPENRECET_NEWS_GOLDEN");
    if (arena_path == NULL || arena_path[0] == '\0')
        return;

    FILE *af = fopen(arena_path, "rb");
    if (af == NULL) {
        fprintf(stderr, "news-replay: cannot open arena %s\n", arena_path);
        exit(2);
    }
    static uint8_t arena[SAVE_BANK_STRIDE_BYTES];
    size_t got = fread(arena, 1, sizeof arena, af);
    fclose(af);
    if (got != sizeof arena) {
        fprintf(stderr, "news-replay: short arena read %zu/%u\n",
                got, (unsigned)sizeof arena);
        exit(2);
    }

    static char seeds_buf[1 << 20];
    const char *seeds_env = getenv("OPENRECET_NEWS_SEEDS");
    const char *seeds_file = getenv("OPENRECET_NEWS_SEEDS_FILE");
    if (seeds_file && seeds_file[0]) {
        FILE *sf = fopen(seeds_file, "r");
        if (sf) {
            size_t g = fread(seeds_buf, 1, sizeof seeds_buf - 1, sf);
            seeds_buf[g] = '\0';
            fclose(sf);
            seeds_env = seeds_buf;
        }
    }
    const char *out_path = getenv("OPENRECET_NEWS_OUT");
    if (out_path == NULL || out_path[0] == '\0')
        out_path = "news_port_out.json";

    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "news-replay: cannot write %s\n", out_path);
        exit(2);
    }

    fprintf(out, "{\n  \"function\": \"news_daily_update\",\n  \"results\": [");

    if (seeds_env == NULL || seeds_env[0] == '\0') seeds_env = "1";
    if (seeds_env != seeds_buf)
        snprintf(seeds_buf, sizeof seeds_buf, "%s", seeds_env);
    int first = 1;
    for (char *tok = strtok(seeds_buf, ", \t\r\n"); tok;
         tok = strtok(NULL, ", \t\r\n")) {
        uint32_t seed = (uint32_t)strtoul(tok, NULL, 0);
        run_one(out, arena, seed, first);
        first = 0;
    }

    fprintf(out, "\n  ]\n}\n");
    fclose(out);
    fprintf(stderr, "news-replay: wrote %s\n", out_path);
    exit(0);
}
