/*
 * study_toggles.c — six runtime kill-switches for the HOUSE lighting
 * tricks.  See study_toggles.h for the trick list + hook-site map.
 *
 * NOT engine code — a study/recording tool layered on the port.  All
 * toggles default ON (retail); the trace/TAS/parity paths never call
 * flip/parse, so untouched behaviour is bit-identical.
 */
#include "study_toggles.h"

#include <stdio.h>
#include <string.h>

int g_study_toggles[STUDY_T_COUNT] = { 1, 1, 1, 1, 1, 1 };

static const char *const STUDY_NAME[STUDY_T_COUNT] = {
    "mod2x",     /* SHIFT+1 — ×2 room brightness (MODULATE2X room combiner) */
    "keylight",  /* SHIFT+2 — directional key light (maplight diffuse)      */
    "ambient",   /* SHIFT+3 — ambient fill (maplight ambient)               */
    "fog",       /* SHIFT+4 — scene fog                                     */
    "hikari",    /* SHIFT+5 — the five god-ray planes (pass-3 draws)        */
    "blob",      /* SHIFT+6 — character blob shadows                        */
};

void study_toggle_flip(int t)
{
    if (t < 0 || t >= STUDY_T_COUNT) return;
    g_study_toggles[t] = !g_study_toggles[t];
    printf("study-toggle: %s %s\n", STUDY_NAME[t],
           g_study_toggles[t] ? "ON (retail)" : "OFF");
    fflush(stdout);
}

void study_toggles_parse_off_list(const char *list)
{
    /* Local comma scanner — main.c's argv loop is mid-strtok, so no
     * strtok here. */
    if (!list) return;
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        int hit = 0;
        for (int t = 0; t < STUDY_T_COUNT; t++) {
            if (n == strlen(STUDY_NAME[t]) && strncmp(p, STUDY_NAME[t], n) == 0) {
                if (g_study_toggles[t]) study_toggle_flip(t);
                hit = 1;
                break;
            }
        }
        if (!hit && n > 0)
            printf("study-toggle: unknown trick '%.*s' (know: mod2x keylight "
                   "ambient fog hikari blob)\n", (int)n, p);
        if (!e) break;
        p = e + 1;
    }
    fflush(stdout);
}
