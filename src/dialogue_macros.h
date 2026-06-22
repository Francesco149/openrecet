/*
 * dialogue_macros.h — the <S>/<I>/<Y>/<D1>/<DA>/<T> text-substitution buffers
 * that font_draw_text_box (FUN_00465db4 pass 1) expands inline into dialogue.
 *
 * The engine holds each macro's value in a fixed global buffer + a length var;
 * a dialogue line like "Yay! I sold <I> for <Y>!" is expanded at draw time by
 * copying the buffer for each tag.  The port models the 6 buffers as a shared
 * table written by the dialogue contexts (e.g. customer_service.c at a sale)
 * and read by font_draw.c.  An unset macro (len 0) makes the tag expand to
 * nothing — exactly the engine's `if (DAT_0730xxxx != 0)` guard.
 *
 * Engine source buffers / length vars (FUN_00465db4 all.c:62702-62804):
 *   <S>  DAT_0730b2bc / DAT_0730b300
 *   <I>  DAT_0730b154 / DAT_0730ac90   (item name, set by FUN_004607f3)
 *   <Y>  DAT_06a5d518 / DAT_0730b150   ("%dpix" sale price)
 *   <D1> DAT_0730ac70 / DAT_0730b274
 *   <DA> DAT_0730ac80 / DAT_0730b2fc   (leading char folded to 'I'/'B')
 *   <T>  DAT_06a5d408 / DAT_06a5d44c
 */
#ifndef OPENRECET_DIALOGUE_MACROS_H
#define OPENRECET_DIALOGUE_MACROS_H

#include <stddef.h>   /* size_t */

enum dlg_macro_id {
    DLG_MAC_S = 0,   /* <S> */
    DLG_MAC_I,       /* <I>  item name */
    DLG_MAC_Y,       /* <Y>  pix amount */
    DLG_MAC_D1,      /* <D1> */
    DLG_MAC_DA,      /* <D…> (non-D1) — first char folded to 'I'/'B' */
    DLG_MAC_T,       /* <T> */
    DLG_MAC_COUNT
};

#define DLG_MACRO_BUFSZ 256

typedef struct dlg_macro {
    char text[DLG_MACRO_BUFSZ];
    int  len;                    /* strlen(text); 0 = unset → tag drops */
} dlg_macro_t;

/* The shared table (engine: the fixed DAT_0730xxxx / DAT_06a5xxxx buffers). */
extern dlg_macro_t g_dlg_macro[DLG_MAC_COUNT];

/* Set macro `id` to `text` (copies + records strlen).  NULL/empty → reset. */
void dlg_macro_set(enum dlg_macro_id id, const char *text);

/* Reset macro `id` to unset (len 0 → its tag expands to nothing). */
void dlg_macro_reset(enum dlg_macro_id id);

/*
 * Expand the <S>/<I>/<Y>/<D1>/<Dx>/<T> tags in `src` into `dst` (NUL-terminated,
 * <= dstsz-1 bytes) using the current macro buffers.  Port of FUN_00465db4 pass
 * 1; <BR> survives as a literal.  Pure → host-testable.
 */
void dlg_macro_expand(const char *src, char *dst, size_t dstsz);

#endif /* OPENRECET_DIALOGUE_MACROS_H */
