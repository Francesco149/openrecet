/*
 * tools/state_diff/oracle.c — local "ground truth" for state-forcing
 * differential tests.
 *
 * Exposes pure-C ports (rng.c, audio_fade.c) on stdin/stdout so a
 * Python driver can compare their outputs to whatever the same code in
 * the retail binary produced (via Frida NativeFunction).
 *
 * Build: `make oracle` from this directory, or via the parent Makefile
 * once that target lands. Links statically; no Win32 dependencies.
 *
 * Stdin protocol — one command per line:
 *
 *   rng_seq <seed_hex> <n>
 *       Seed the LCG to `seed_hex` (32-bit, big-endian written as hex
 *       without "0x"), call rng_next15 `n` times, print one u32 per line
 *       (the raw post-step seed before the >>16 & 0x7fff shift). This
 *       matches what `read_u32(DAT_006023a0)` reads from the retail
 *       binary after each call — the rawer comparison surface.
 *
 *   fade_compute <slider>
 *       Print audio_fade_compute(slider, 0) on a single line.
 *
 *   quit
 *       Exit with status 0.
 *
 * On unknown command, prints `error: ...` and continues. Caller is
 * expected to use `quit` to stop the oracle cleanly.
 */

#include "rng.h"
#include "audio_fade.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* audio_fade.c calls audio_trace_emit_fade_start via audio_fade_apply(),
 * but we never go through audio_fade_apply() in this oracle — we only
 * call audio_fade_compute(). Even so the link step needs the symbol
 * because audio_fade.o references it. Stub it as a no-op. */
void audio_trace_emit_fade_start(int channel, int slider, int32_t centibel)
{
    (void)channel; (void)slider; (void)centibel;
}

static int parse_u32_hex(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || *end != '\0') return 0;
    *out = (uint32_t)v;
    return 1;
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = (int)v;
    return 1;
}

static void cmd_rng_seq(char *args)
{
    char *seed_s = strtok(args, " \t");
    char *n_s    = strtok(NULL, " \t");
    uint32_t seed;
    int      n;
    if (!seed_s || !n_s || !parse_u32_hex(seed_s, &seed) || !parse_int(n_s, &n) || n < 0) {
        printf("error: usage: rng_seq <seed_hex> <n>\n");
        return;
    }
    rng_seed(seed);
    for (int i = 0; i < n; i++) {
        (void)rng_next15();           /* discard the 15-bit output ... */
        printf("%08x\n", g_rng_seed); /* ... we compare the raw post-step seed,
                                        which is the actual engine global the
                                        retail binary updates. */
    }
}

static void cmd_fade_compute(char *args)
{
    char *slider_s = strtok(args, " \t");
    int   slider;
    if (!slider_s || !parse_int(slider_s, &slider)) {
        printf("error: usage: fade_compute <slider>\n");
        return;
    }
    /* target_centibel = 0 matches FUN_00499583's hardcoded behaviour
     * (it always computes against full target). See audio_fade.c:96. */
    int32_t centibel = audio_fade_compute(slider, 0);
    printf("%d\n", centibel);
}

int main(void)
{
    /* Line-buffered stdout so the Python driver sees each response
     * without having to wait on stdio buffering. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline. */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        char *cmd  = strtok(line, " \t");
        char *rest = strtok(NULL, "");  /* remainder of the line */
        if (!cmd) continue;

        if (strcmp(cmd, "quit") == 0) {
            return 0;
        } else if (strcmp(cmd, "rng_seq") == 0) {
            cmd_rng_seq(rest ? rest : "");
        } else if (strcmp(cmd, "fade_compute") == 0) {
            cmd_fade_compute(rest ? rest : "");
        } else {
            printf("error: unknown command: %s\n", cmd);
        }
    }
    return 0;
}
