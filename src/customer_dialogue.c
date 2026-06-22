/*
 * customer_dialogue.c — per-kyaku runtime dialogue buffer: the fN.txt parser
 * + the per-record store.  See customer_dialogue.h for the layout/offsets.
 *
 * Port of the dialogue half of FUN_00475270's per-customer script load
 * (all.c:74568-74715).  The engine reads each record's `file:` script via the
 * storage layer, then walks it line-by-line; this module is the line walk +
 * the `msgNN:` block (the only block the picker FUN_00460a1a consumes).  The
 * storage read + the per-record orchestration live in tables.c (reusing
 * load_via_storage), so this stays a pure, host-testable parser.
 *
 * Faithful to the engine's FIXED-WIDTH field reads (line+6 sprite, line+9
 * voice marker, line+0x2a voice digits, line+0x2d text); real data is always
 * `msgNN:SS:Vvv:...` (2-digit NN/SS, `s`+2-digit voice).  Safety divergence vs
 * the engine (which reads those fixed offsets unconditionally into a 0x25c
 * scratch): we require the line to be >= 13 bytes before reading them, so a
 * truncated line is skipped rather than reading stale scratch.  Vendor data
 * never trips this.
 */
#include "customer_dialogue.h"

#include <stdlib.h>          /* malloc / free / atoi */

#include "tables_kyaku.h"    /* KYAKU_COUNT — the per-record store size */

static int is_digit_c(char c) { return c >= '0' && c <= '9'; }

void kyaku_dialogue_parse(const char *blob, size_t size, kyaku_dialogue_t *out)
{
    char   line[0x400];
    size_t i = 0;

    while (i < size && blob[i] != '\0') {
        /* Pull one line (up to \r/\n / end-of-blob) into a NUL-terminated
         * scratch, then swallow the EOL run. */
        size_t n = 0;
        while (i < size && blob[i] != '\0' &&
               blob[i] != '\r' && blob[i] != '\n') {
            if (n < sizeof(line) - 1)
                line[n++] = blob[i];
            i++;
        }
        line[n] = '\0';
        while (i < size && (blob[i] == '\r' || blob[i] == '\n'))
            i++;

        /* blank / `/`-comment line → skip (engine LAB_00477aa8 guard). */
        if (n == 0 || line[0] == '/')
            continue;

        /* msgNN:SS:Vvv:text  (engine key DAT_005caedc "msg" + "msg%02d:"). */
        if (n >= 13 &&
            line[0] == 'm' && line[1] == 's' && line[2] == 'g' &&
            is_digit_c(line[3]) && is_digit_c(line[4]) && line[5] == ':') {
            int type = (line[3] - '0') * 10 + (line[4] - '0');
            if (type < 0 || type >= KYAKU_DLG_TYPES)   /* engine warns >0x1d, drops */
                continue;
            int variant = out->count[type];
            if (variant >= KYAKU_DLG_VARIANTS)         /* engine warns >0x13, drops */
                continue;
            int s = variant + type * KYAKU_DLG_VARIANTS;

            out->sprite[s] = atoi(line + 6);           /* "SS" up to ':' (+0x26) */

            /* voice marker at +0x29: "sno" → none (-1), else `s`+digits (+0x2a). */
            if (line[9] == 's' && line[10] == 'n' && line[11] == 'o')
                out->voice[s] = -1;
            else
                out->voice[s] = atoi(line + 10);

            /* text at +0x2d, copied raw (<BR>/<C> intact) up to 0xff bytes. */
            {
                const char *src = line + 13;
                int t = 0;
                while (src[t] != '\0' && t < KYAKU_DLG_TEXT_LEN - 1) {
                    out->text[s][t] = src[t];
                    t++;
                }
                out->text[s][t] = '\0';
            }

            out->count[type] = variant + 1;
        }
        /* grp (standee art → scene_buy) + se (audio) blocks are ignored here. */
    }
}

/* ── per-record store ─────────────────────────────────────────────────────── */

static kyaku_dialogue_t *g_kyaku_dialogue[KYAKU_COUNT];

void kyaku_dialogue_set(int rec_index, kyaku_dialogue_t *dlg)
{
    if ((unsigned)rec_index >= (unsigned)KYAKU_COUNT) {
        free(dlg);
        return;
    }
    free(g_kyaku_dialogue[rec_index]);
    g_kyaku_dialogue[rec_index] = dlg;
}

const kyaku_dialogue_t *kyaku_dialogue_get(int rec_index)
{
    if ((unsigned)rec_index >= (unsigned)KYAKU_COUNT)
        return NULL;
    return g_kyaku_dialogue[rec_index];
}

void kyaku_dialogue_free_all(void)
{
    for (int i = 0; i < KYAKU_COUNT; i++) {
        free(g_kyaku_dialogue[i]);
        g_kyaku_dialogue[i] = NULL;
    }
}
