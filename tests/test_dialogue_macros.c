/*
 * test_dialogue_macros.c — the <I>/<Y>/<S>/<D…>/<T> dialogue macro expansion
 * (dialogue_macros.c, FUN_00465db4 pass 1).  Guards the substitution + the
 * "consume the whole tag incl '>'" behaviour (the old stub leaked the '>').
 */
#include "t.h"
#include <string.h>
#include "../src/dialogue_macros.h"

/* The customer-service post-sale line: "<I>" → item, "<Y>" → "%dpix"; <BR>
 * survives as a literal for pass 2's line split. */
int test_dlg_macro_expand_item_pix(void)
{
    dlg_macro_reset(DLG_MAC_I);
    dlg_macro_reset(DLG_MAC_Y);
    dlg_macro_set(DLG_MAC_I, "Steel Sword");
    dlg_macro_set(DLG_MAC_Y, "3000pix");

    char out[256];
    dlg_macro_expand("Yay! I sold<BR><I><BR>for <Y>!", out, sizeof out);
    T_ASSERT(strcmp(out, "Yay! I sold<BR>Steel Sword<BR>for 3000pix!") == 0);
    return 0;
}

/* An unset macro expands to nothing AND consumes the trailing '>' (the bug the
 * old stub had: src+=2 left a stray '>'). */
int test_dlg_macro_expand_drops_unset(void)
{
    dlg_macro_reset(DLG_MAC_I);
    dlg_macro_reset(DLG_MAC_Y);

    char out[256];
    dlg_macro_expand("a<I>b<Y>c", out, sizeof out);
    T_ASSERT(strcmp(out, "abc") == 0);     /* no stray '>' */
    return 0;
}

/* <BR> is NOT an expanded tag — its '<' falls through literally so pass 2 can
 * split on it.  A <D1> with an unset buffer drops cleanly too. */
int test_dlg_macro_expand_preserves_br(void)
{
    dlg_macro_reset(DLG_MAC_D1);

    char out[256];
    dlg_macro_expand("x<BR>y<D1>z", out, sizeof out);
    T_ASSERT(strcmp(out, "x<BR>yz") == 0);
    return 0;
}

/* set() records strlen and reset() clears it (drives the expansion's len gate). */
int test_dlg_macro_set_reset(void)
{
    dlg_macro_set(DLG_MAC_S, "hello");
    T_ASSERT_EQ_I(g_dlg_macro[DLG_MAC_S].len, 5);
    T_ASSERT(strcmp(g_dlg_macro[DLG_MAC_S].text, "hello") == 0);

    dlg_macro_reset(DLG_MAC_S);
    T_ASSERT_EQ_I(g_dlg_macro[DLG_MAC_S].len, 0);

    dlg_macro_set(DLG_MAC_S, "");          /* empty → unset */
    T_ASSERT_EQ_I(g_dlg_macro[DLG_MAC_S].len, 0);
    return 0;
}
