/*
 * dialogue_macros.c — the <S>/<I>/<Y>/<D1>/<DA>/<T> dialogue text-substitution
 * buffers + the inline expansion (FUN_00465db4 pass 1, all.c:62697-62815).
 *
 * dlg_macro_expand() is pure (reads g_dlg_macro, writes a dst buffer) so it is
 * host-testable; font_draw_text_box calls it before the <BR>-split / reveal.
 */
#include "dialogue_macros.h"

#include <string.h>

dlg_macro_t g_dlg_macro[DLG_MAC_COUNT];

void dlg_macro_set(enum dlg_macro_id id, const char *text)
{
    if ((unsigned)id >= (unsigned)DLG_MAC_COUNT) return;
    dlg_macro_t *m = &g_dlg_macro[id];
    if (!text || text[0] == '\0') { m->text[0] = '\0'; m->len = 0; return; }
    size_t n = strlen(text);
    if (n > (size_t)(DLG_MACRO_BUFSZ - 1)) n = DLG_MACRO_BUFSZ - 1;
    memcpy(m->text, text, n);
    m->text[n] = '\0';
    m->len = (int)n;
}

void dlg_macro_reset(enum dlg_macro_id id)
{
    if ((unsigned)id >= (unsigned)DLG_MAC_COUNT) return;
    g_dlg_macro[id].text[0] = '\0';
    g_dlg_macro[id].len = 0;
}

/*
 * Expand the macro tags in `src` into `dst` (NUL-terminated, <= dstsz-1 bytes).
 * Port of FUN_00465db4's pass 1: each recognized tag "<S>/<I>/<Y>/<T>" (3 bytes)
 * or "<D1>/<Dx>" (4 bytes) is consumed WHOLE — the engine writes the '<', then
 * the tag block advances the src cursor past the tag PREFIX and the shared
 * `iVar4-1` + LAB_00465f24 `iVar6++/iVar4++` tail consumes the trailing '>' and
 * leaves the dst cursor advanced by exactly the macro length (an empty macro
 * therefore drops the whole tag: the written '<' is overwritten next step).
 * An unrecognized '<' (e.g. "<BR>") falls through as a literal for pass 2.
 */
void dlg_macro_expand(const char *src, char *dst, size_t dstsz)
{
    if (dstsz == 0) return;
    int d = 0, s = 0;
    const int cap = (int)dstsz - 1;
    /* engine caps the walk at 256 outer iterations (the local_8 float counter). */
    for (int iter = 0; iter < 256 && d < cap; iter++) {
        char c = src[s];
        dst[d] = c;                         /* engine writes even the '<' first */
        if (c == '\0') break;
        if (c == '<') {
            char t = src[s + 1];
            const dlg_macro_t *m = NULL;
            int fold = 0;                   /* <Dx> leading-char fold to 'I'/'B' */
            if      (t == 'S') { m = &g_dlg_macro[DLG_MAC_S];  s += 3; }
            else if (t == 'I') { m = &g_dlg_macro[DLG_MAC_I];  s += 3; }
            else if (t == 'Y') { m = &g_dlg_macro[DLG_MAC_Y];  s += 3; }
            else if (t == 'T') { m = &g_dlg_macro[DLG_MAC_T];  s += 3; }
            else if (t == 'D') {
                if (src[s + 2] == '1') { m = &g_dlg_macro[DLG_MAC_D1]; s += 4; }
                else                   { m = &g_dlg_macro[DLG_MAC_DA]; s += 4; fold = 1; }
            } else {
                /* unrecognized (e.g. <BR>): keep the literal '<'. */
                d++; s++;
                continue;
            }
            if (m && m->len > 0) {
                int n = m->len;
                if (d + n > cap) n = cap - d;
                for (int i = 0; i < n; i++) {
                    char ch = m->text[i];
                    /* <Dx>: engine folds the first emitted char (when dst is at
                     * the buffer start) to 'I' (source 'i') else 'B'. */
                    if (fold && d == 0 && i == 0) ch = (m->text[0] == 'i') ? 'I' : 'B';
                    dst[d++] = ch;
                }
            }
            /* dst NOT advanced for an empty macro → the '<' is overwritten next
             * iteration, so the tag drops. */
            continue;
        }
        d++; s++;
    }
    dst[d] = '\0';
}
