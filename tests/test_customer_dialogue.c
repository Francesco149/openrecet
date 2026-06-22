/*
 * test_customer_dialogue.c — the per-kyaku fN.txt dialogue parser (L1c,
 * customer_dialogue.c, port of FUN_00475270's msg block all.c:74646-74707).
 * Verifies the fixed-width `msgNN:SS:Vvv:text` parse → count[type] / sprite[s]
 * / voice[s] / text[s] at flat slot s = variant + type*0x14, plus the store.
 */
#include "t.h"
#include <string.h>
#include <stdlib.h>
#include "../src/customer_dialogue.h"

/* CRLF blob mirroring the real kyaku/<name>.txt shape: grp/se header blocks
 * (ignored), a `/` comment + blank line (skipped), then msg lines exercising
 * sprite ids, the "sno" no-voice marker vs a numeric voice, multiple variants
 * of one type (count increments), and a <C> tag kept raw in the text. */
static const char *DLG_BLOB =
    "grp00:ivent/02tear_00.tga\r\n"
    "se00:bin/se/foo.bin\r\n"
    "/a comment line\r\n"
    "\r\n"
    "msg00:00:sno:What would you<BR>pay for this?\r\n"
    "msg01:01:s09:How about this?\r\n"
    "msg05:01:s01:Yes, an excellent price.\r\n"
    "msg05:03:s00:I agree, fair price.\r\n"
    "msg06:05:s00:Too high!<C>Try again.\r\n";

int test_kyaku_dialogue_parse_fields(void)
{
    kyaku_dialogue_t *d = (kyaku_dialogue_t *)calloc(1, sizeof(*d));
    T_ASSERT(d != NULL);
    kyaku_dialogue_parse(DLG_BLOB, strlen(DLG_BLOB), d);

    /* per-type variant counts (grp/se/comment/blank contributed nothing). */
    T_ASSERT_EQ_I(d->count[0], 1);
    T_ASSERT_EQ_I(d->count[1], 1);
    T_ASSERT_EQ_I(d->count[5], 2);   /* two msg05 variants */
    T_ASSERT_EQ_I(d->count[6], 1);
    T_ASSERT_EQ_I(d->count[2], 0);   /* untouched type */

    /* type 0, variant 0 → s = 0: sprite 0, "sno" → voice -1, <BR> kept. */
    T_ASSERT_EQ_I(d->sprite[0], 0);
    T_ASSERT_EQ_I(d->voice[0], -1);
    T_ASSERT(strcmp(d->text[0], "What would you<BR>pay for this?") == 0);

    /* type 1 → s = 1*0x14 = 20: sprite 1, numeric voice "s09" → 9. */
    T_ASSERT_EQ_I(d->sprite[20], 1);
    T_ASSERT_EQ_I(d->voice[20], 9);
    T_ASSERT(strcmp(d->text[20], "How about this?") == 0);

    /* type 5 → s = 100 / 101: two variants, distinct sprite/voice/text. */
    T_ASSERT_EQ_I(d->sprite[100], 1);
    T_ASSERT_EQ_I(d->voice[100], 1);
    T_ASSERT(strcmp(d->text[100], "Yes, an excellent price.") == 0);
    T_ASSERT_EQ_I(d->sprite[101], 3);
    T_ASSERT_EQ_I(d->voice[101], 0);
    T_ASSERT(strcmp(d->text[101], "I agree, fair price.") == 0);

    /* type 6 → s = 120: the <C> continuation tag is stored RAW (the picker,
     * not the loader, splits it). */
    T_ASSERT_EQ_I(d->sprite[120], 5);
    T_ASSERT(strcmp(d->text[120], "Too high!<C>Try again.") == 0);

    free(d);
    return 0;
}

/* A line whose variant count would exceed 0x14 (the engine MessageBox cap) is
 * dropped, and a type >= 0x1e is dropped — neither overflows the slot grid. */
int test_kyaku_dialogue_parse_caps(void)
{
    kyaku_dialogue_t *d = (kyaku_dialogue_t *)calloc(1, sizeof(*d));
    T_ASSERT(d != NULL);

    /* 22 variants of type 3 — only the first 20 (0x14) are kept. */
    char blob[2048];
    int n = 0;
    for (int v = 0; v < 22; v++)
        n += snprintf(blob + n, sizeof(blob) - n,
                      "msg03:0%d:sno:line %d\r\n", v % 10, v);
    /* a type past the 0x1d warn / 0x1e grid cap is ignored, not written. */
    n += snprintf(blob + n, sizeof(blob) - n, "msg40:00:sno:overflow\r\n");
    kyaku_dialogue_parse(blob, (size_t)n, d);

    T_ASSERT_EQ_I(d->count[3], KYAKU_DLG_VARIANTS);   /* capped at 20 */
    free(d);
    return 0;
}

/* The per-record store: set/get round-trip + out-of-range guard. */
int test_kyaku_dialogue_store(void)
{
    kyaku_dialogue_free_all();
    T_ASSERT(kyaku_dialogue_get(1) == NULL);

    kyaku_dialogue_t *d = (kyaku_dialogue_t *)calloc(1, sizeof(*d));
    T_ASSERT(d != NULL);
    kyaku_dialogue_parse(DLG_BLOB, strlen(DLG_BLOB), d);
    kyaku_dialogue_set(1, d);

    const kyaku_dialogue_t *got = kyaku_dialogue_get(1);
    T_ASSERT(got == d);
    T_ASSERT_EQ_I(got->count[5], 2);

    T_ASSERT(kyaku_dialogue_get(-1) == NULL);    /* out-of-range → NULL, no crash */
    T_ASSERT(kyaku_dialogue_get(99999) == NULL);

    kyaku_dialogue_free_all();
    T_ASSERT(kyaku_dialogue_get(1) == NULL);
    return 0;
}
