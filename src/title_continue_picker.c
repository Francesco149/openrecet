/*
 * title_continue_picker.c — see title_continue_picker.h.
 *
 * Engine sources (Ghidra all.c):
 *   FUN_0049b537 @ 0x49b537 (31 B)   — slot-index array init
 *   FUN_0049a59e @ 0x49a59e L100795+ — the DAT_09643524==1 picker body
 *
 * SE ids (via FUN_00499519): 0x146 cursor, 0x143 confirm, 0x13d back,
 * 0x16a error (empty slot).
 */

#include "title_continue_picker.h"

#include "audio.h"        /* audio_play_se_by_id */
#include "save_bank.h"    /* occupied test + last-slot header field */
#include "save_work.h"    /* save_work_load_slot */
#include "scene_title.h"  /* SCENE_TITLE_INPUT_* bit masks */

title_continue_picker_t g_title_continue_picker;

#define SE_CURSOR   0x146
#define SE_CONFIRM  0x143
#define SE_BACK     0x13d
#define SE_ERROR    0x16a

/* ── FUN_0049b537 + entry setup ── */
void title_continue_picker_open(int entry_menu_code, int last_used_slot)
{
    title_continue_picker_t *p = &g_title_continue_picker;

    /* FUN_0049b537: identity slot-index array + slot_count = 100. */
    for (int i = 0; i < TITLE_PICKER_SLOTS; i++) {
        p->slot_index[i] = i;
    }
    p->slot_count   = TITLE_PICKER_SLOTS;
    p->hscroll_anim = 0;
    p->vscroll_anim = 0;
    p->overwrite_mode = 0;
    p->prompt_pending = 0;

    /* Engine FUN_0049a59e L101103: cursor = last-used slot, scroll =
     * last-2 (clamped >= 0). */
    p->cursor = last_used_slot;
    p->scroll = last_used_slot - 2;
    if (p->scroll < 0) {
        p->scroll = 0;
    }

    /* Codes 4 (new-into-slot) and 6 (survival) reset to the top and
     * queue the "choose a file" prompt. PORT-DEBT(subset): their
     * confirm-time fresh-init + FUN_0049b4f4 survival seed are not
     * ported — we only model the cursor reset + prompt + mode flag. */
    if (entry_menu_code == SCENE_TITLE_MENU_NEW_HAS_SAVE ||
        entry_menu_code == SCENE_TITLE_MENU_SURVIVAL) {
        p->overwrite_mode = 1;
        p->cursor = 0;
        p->scroll = 0;
        p->prompt_pending = 1;
    }
}

/* ── per-tick picker body (FUN_0049a59e L100795) ── */
title_picker_result_t title_continue_picker_step(uint16_t pressed,
                                                 uint16_t held,
                                                 int *out_load_bank)
{
    title_continue_picker_t *p = &g_title_continue_picker;

    /* Within-column scroll slide in progress: |anim| grows each frame
     * until ±5, then commits one row of scroll. Input blocked. */
    if (p->hscroll_anim != 0) {
        if (p->hscroll_anim < 0) p->hscroll_anim--;
        if (p->hscroll_anim > 0) p->hscroll_anim++;
        if (p->hscroll_anim == -5) { p->scroll--; p->hscroll_anim = 0; }
        if (p->hscroll_anim ==  5) { p->scroll++; p->hscroll_anim = 0; }
        return TITLE_PICKER_NONE;
    }
    /* Between-column scroll slide (±3 rows), with end clamps. */
    if (p->vscroll_anim != 0) {
        if (p->vscroll_anim < 0) p->vscroll_anim--;
        if (p->vscroll_anim > 0) p->vscroll_anim++;
        if (p->vscroll_anim == -5) {
            p->scroll -= 3;
            p->vscroll_anim = 0;
            if (p->scroll >= 0) return TITLE_PICKER_NONE;
            p->scroll = 0;
        }
        if (p->vscroll_anim == 5) {
            p->scroll += 3;
            p->vscroll_anim = 0;
            if (p->slot_count - 3 < p->scroll) p->scroll = p->slot_count - 3;
        }
        return TITLE_PICKER_NONE;
    }

    /* Queued "please choose a file" toast (rendered in M2). */
    if (p->prompt_pending) {
        p->prompt_pending = 0;
        /* PORT-DEBT(render): engine calls FUN_00434ceb to push the
         * toast text into the title_save_dialog text buffers; the
         * dialog render is deferred (M2). */
    }

    /* B — cancel out of the picker. */
    if (pressed & SCENE_TITLE_INPUT_B) {
        audio_play_se_by_id(SE_BACK);
        return TITLE_PICKER_CANCEL;
    }

    /* No A this frame → handle d-pad movement (held, continuous). */
    if ((pressed & SCENE_TITLE_INPUT_A) == 0) {
        const int vis = (p->slot_count < 3) ? p->slot_count : 3;

        if ((held & SCENE_TITLE_INPUT_LEFT) == 0) {
            if ((held & SCENE_TITLE_INPUT_RIGHT) == 0) {
                if ((held & SCENE_TITLE_INPUT_UP) == 0) {
                    /* DOWN: cursor +1 within the column. */
                    if ((held & SCENE_TITLE_INPUT_DOWN) &&
                        p->cursor < p->slot_count - 1) {
                        audio_play_se_by_id(SE_CURSOR);
                        p->cursor++;
                        if (vis - 1 < p->cursor - p->scroll) p->hscroll_anim = 1;
                    }
                } else if (0 < p->cursor) {
                    /* UP: cursor -1. */
                    audio_play_se_by_id(SE_CURSOR);
                    p->cursor--;
                    if (p->cursor - p->scroll < 0) p->hscroll_anim = -1;
                }
            } else {
                /* RIGHT: cursor +3 (next column). */
                const int last = p->slot_count - 1;
                if (p->cursor < last && p->scroll < p->slot_count - 3) {
                    audio_play_se_by_id(SE_CURSOR);
                    p->cursor += 3;
                    if (last < p->cursor) p->cursor = last;
                    if (vis - 3 < p->cursor - p->scroll) p->vscroll_anim = 1;
                }
            }
        } else if (0 < p->cursor && 0 < p->scroll) {
            /* LEFT: cursor -3 (prev column). */
            audio_play_se_by_id(SE_CURSOR);
            p->cursor -= 3;
            if (p->cursor < 0) p->cursor = 0;
            if (p->cursor - p->scroll < 0) p->vscroll_anim = -1;
        }
        return TITLE_PICKER_NONE;
    }

    /* A — confirm. Load the occupied slot under the cursor. */
    const int sel = p->slot_index[p->cursor];
    const uint32_t *bank = save_bank_dwords_at(sel);
    if (!bank || bank[SAVE_BANK_FIELD_OCCUPIED] == 0) {
        /* Empty slot — can't continue. */
        audio_play_se_by_id(SE_ERROR);
        return TITLE_PICKER_NONE;
    }

    /* FUN_00490259 — load the chosen save bank into the active working
     * slot, then remember it as the last-used slot. */
    save_work_load_slot(sel);
    audio_play_se_by_id(SE_CONFIRM);
    save_header_set_last_slot(sel);
    if (out_load_bank) {
        *out_load_bank = sel;
    }
    return TITLE_PICKER_LOAD;
}
