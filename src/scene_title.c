/*
 * scene_title.c — title-screen scene module.
 *
 * Engine source: FUN_004733d5 (texture loader).
 *
 * Asset paths extracted from the unpacked binary's .rdata at
 *   VA 0x005c8688..0x005c86fc
 * via `tools/analyze/pe.py str <VA>`; the (w, h) pairs are the
 * literal arguments FUN_0047193c was called with — they match
 * each file's native resolution (as confirmed by spot-decoding
 * with `sprite_load` against `vendor/original`).
 *
 * The engine's first argument to FUN_0047193c (a "slot/category"
 * tag — 2 for these 7 entries) is the unload-grouping key. We do
 * not need it: our `scene_title_unload_assets` simply releases
 * every slot owned by this module.
 */

#include "scene_title.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"        /* audio_play_se_by_id for settings SE feedback */
#include "audio_fade.h"   /* slider get/set + apply for BGM/SE-A/SE-B rows */
#include "call_trace.h"   /* CALL_TRACE_ENTER_STUB at scene_title entry points */
#include "fade.h"         /* scene-fade phase-1 trigger + done query */
#include "save_bank.h"    /* save_header_set_*_slider for persistence */
#include "scene.h"        /* g_scene_state transition on fade complete */
#include "settings.h"     /* non-audio rows 3 & 4 */
#include "sim.h"          /* g_sim_buttons for scene_title_sim_default */
#include "title_save_dialog.h" /* FUN_00434d6a/4356cd/etc. — title-frame cluster */
#include "title_continue_picker.h" /* continue/load slot picker (DAT_09643524==1) */

scene_title_menu_t  g_scene_title_menu;
scene_title_anim_t  g_scene_title_anim;

const scene_title_asset_t scene_title_assets[SCENE_TITLE_TEX_COUNT] = {
    [SCENE_TITLE_TEX_BG2]     = { "bmp/title_bg2.bmp",     1024, 1024 },
    [SCENE_TITLE_TEX_01]      = { "bmp/title01.tga",        512,  256 },
    [SCENE_TITLE_TEX_FUKI]    = { "bmp/title_fuki.tga",     512, 1024 },
    [SCENE_TITLE_TEX_WAKU]    = { "bmp/title_waku.tga",    1024,  512 },
    [SCENE_TITLE_TEX_PAUSE]   = { "bmp/pause.tga",         1024,  512 },
    [SCENE_TITLE_TEX_RESULT]  = { "bmp/result_bord01.tga",  512,  256 },
    [SCENE_TITLE_TEX_DUNGEON] = { "bmp/dungeonbord.tga",   1024,  512 },
    [SCENE_TITLE_TEX_ITEM_WIN]= { "bmp/item_win.tga",      1024, 1024 },
};

/* ─── menu init (FUN_0049a43d) ───────────────────────────────────────── */

void scene_title_menu_init(const scene_title_save_t *save,
                           scene_title_menu_t *out)
{
    memset(out, 0, sizeof *out);

    /* The engine's literal "uVar1" bitmask. bit 0 = any cleared adv;
     * bit 1 = any bank has adv8 cleared. uVar1 == 3 ↔ both set. */
    const int uVar1 = (save->has_any_adv_cleared ? 1 : 0)
                    | (save->has_any_adv8_cleared ? 2 : 0);

    int count = 0;
    int *m    = out->items;

    /* Slot 0: New Game vs. (Continue + New). Engine flips between
     * the two flavours via item-code 0 vs. 5+4. */
    if ((uVar1 & 1) == 0) {
        m[count++] = SCENE_TITLE_MENU_NEW_GAME;          /* 0 */
    } else {
        m[count++] = SCENE_TITLE_MENU_CONT_HAS_SAVE;     /* 5 */
        m[count++] = SCENE_TITLE_MENU_NEW_HAS_SAVE;      /* 4 */
    }

    /* Survival unlock — engine literally checks `uVar1 == 3`, not
     * a bit test. Both flags must be set (Adventure 2 cleared on a
     * bank that also has Adventure 8 cleared). */
    if (uVar1 == 3) {
        m[count++] = SCENE_TITLE_MENU_SURVIVAL;          /* 6 */
    }

    /* Quick-Continue: scan-for-any-populated-bank result. Adds
     * item 1 once and sets the default cursor to it. */
    if (save->has_any_score) {
        out->default_cursor = count;
        m[count++] = SCENE_TITLE_MENU_CONTINUE_ANY;      /* 1 */
    }

    /* Ranking is always present. */
    m[count++] = SCENE_TITLE_MENU_RANKING;               /* 7 */

    /* Hidden character (DAT_056e5788) — note the engine *also*
     * unlocks this when (uVar1 & 1) is set, not only via the
     * dedicated flag. Engine quirk; reproduced. */
    if (save->hidden_char_unlocked || (uVar1 & 1)) {
        m[count++] = SCENE_TITLE_MENU_HIDDEN_CHAR;       /* 8 */
    }

    m[count++] = SCENE_TITLE_MENU_OPTIONS;               /* 2 */
    m[count++] = SCENE_TITLE_MENU_EXIT;                  /* 3 */

    out->count = count;

    /* Y-stride / Y-origin table from the count-based switch at the
     * tail of FUN_0049a43d:
     *
     *   count == 8 → stride 27, origin -36
     *   count == 7 → stride 30, origin -36
     *   count == 6 → stride 33, origin -30
     *   else       → stride 33, origin -16
     */
    if (count == 8) {
        out->y_stride = 27.0f;
        out->y_origin = -36.0f;
    } else if (count == 7) {
        out->y_stride = 30.0f;
        out->y_origin = -36.0f;
    } else if (count == 6) {
        out->y_stride = 33.0f;
        out->y_origin = -30.0f;
    } else {
        out->y_stride = 33.0f;
        out->y_origin = -16.0f;
    }
}

void scene_title_menu_init_fresh(scene_title_menu_t *out)
{
    const scene_title_save_t empty = {0};
    scene_title_menu_init(&empty, out);
}

/* ─── sim init + bare-path sim (FUN_0049a3a3 + FUN_0049a59e) ─────────── */

void scene_title_anim_init_fresh(scene_title_anim_t *out)
{
    /* FUN_0049a3a3 line-for-line: all counters zero, fold-out flag set. */
    memset(out, 0, sizeof *out);
    out->menu_folding_out = 1;
    /* `pending_action` lives outside the engine's BSS-zero region — our
     * own outbox field. -1 = no action pending. */
    out->pending_action   = SCENE_TITLE_ACTION_NONE;
    /* Continue/load is off by default; -1 = no slot loaded. (memset
     * already zeroed continue_mode.) */
    out->continue_load_bank = -1;
    /* Submenu fields land in their BSS-zero engine defaults: state=0
     * (main menu), cursor=0, dirty=0. memset above already zeroes
     * them — listed here for documentation. */
}

/* Engine button-mask bits (see input.h "input_binding_mask" docs):
 *   0x04 = UP, 0x08 = DOWN, 0x10 = A. The sim reads UP/DOWN from
 *   `held` (auto-repeat) and A from `pressed` (rising edge). */
#define TITLE_INPUT_RIGHT  SCENE_TITLE_INPUT_RIGHT
#define TITLE_INPUT_LEFT   SCENE_TITLE_INPUT_LEFT
#define TITLE_INPUT_UP     SCENE_TITLE_INPUT_UP
#define TITLE_INPUT_DOWN   SCENE_TITLE_INPUT_DOWN
#define TITLE_INPUT_A      SCENE_TITLE_INPUT_A
#define TITLE_INPUT_B      SCENE_TITLE_INPUT_B

/* SE resource IDs used by the title scene (FUN_00499519 call sites in
 * FUN_0049a59e). 0x143 = confirm / back, 0x146 = cursor tick. */
#define TITLE_SE_CONFIRM   0x0143
#define TITLE_SE_CURSOR    0x0146

/* Settings submenu — 6 rows. Codes match the per-row dispatch in
 * FUN_0049a59e lines 371-475. */
#define SETTINGS_ROW_BGM       0
#define SETTINGS_ROW_SE_A      1
#define SETTINGS_ROW_SE_B      2
#define SETTINGS_ROW_SLIDER3   3
#define SETTINGS_ROW_SLIDER4   4
#define SETTINGS_ROW_CLEAR     5
#define SETTINGS_ROW_COUNT     6

/* ── settings submenu producer (FUN_0049a59e lines 371-475) ──────────
 *
 * Runs once per frame while `submenu_state == 2 && cursor_anim == 10`.
 * Reads the per-frame input masks, mutates the slider state, plays SE
 * feedback. Returns nothing — all state lives in `anim` + audio_fade
 * module + settings module.
 *
 * Bit-for-bit behaviour:
 *   - A or B pressed (rising edge): exit. Sets dirty=2 if was 1 (save
 *     on exit) else 3 (no-save exit). SE 0x143. Engine: line 379-386.
 *   - Row 5 + A: would open the "Clear all data?" modal. Modal flow
 *     isn't ported (no save IO yet); we play the entry SE (0x143)
 *     and consume the press but don't actually exit — engine fidelity
 *     for the input gate, deferred for the modal itself.
 *   - UP held (auto-repeat): cursor = (cursor + 5) % 6. SE 0x146.
 *   - DOWN held: cursor = (cursor + 7) % 6. SE 0x146.
 *   - LEFT held: dec slider at current row, clamped to 0 floor. Plays
 *     SE 0x146 (rows 1/3/4) or the filename feedback (row 2; engine
 *     uses FUN_0049933c on a specific path. Deferred — we play 0x146
 *     instead. See findings/title-settings-submenu.md #50). For row
 *     0 (BGM) NO SE plays; instead `audio_fade_apply(BGM)` re-applies
 *     the running music volume (engine line 457: FUN_00499583).
 *   - RIGHT held: symmetric inc, max 9 (audio rows) / 2 (slider3)
 *     / 1 (slider4).
 *   - Any successful slider change: set dirty = 1.
 *
 * The engine processes LEFT then RIGHT in an if/else (LEFT takes
 * precedence); we keep that ordering.
 */
static void scene_title_settings_apply_slider(int row, int delta, int *out_changed)
{
    *out_changed = 0;
    switch (row) {
    case SETTINGS_ROW_BGM: {
        int v = audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM);
        int nv = v + delta;
        if (nv < 0 || nv > 9) return;
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, nv);
        save_header_set_bgm_slider(nv);  /* persist via save_io_write_arena at shutdown */
        audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);
        *out_changed = 1;
        return;
    }
    case SETTINGS_ROW_SE_A: {
        int v = audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A);
        int nv = v + delta;
        if (nv < 0 || nv > 9) return;
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, nv);
        save_header_set_se_slider(nv);
        audio_play_se_by_id(TITLE_SE_CURSOR);
        *out_changed = 1;
        return;
    }
    case SETTINGS_ROW_SE_B: {
        int v = audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B);
        int nv = v + delta;
        if (nv < 0 || nv > 9) return;
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B, nv);
        save_header_set_se_b_slider(nv);
        /* Engine plays a filename-based SE here (FUN_0049933c against
         * "re_sys01a_b" w/ inc-vs-dec path variants). Filename SE
         * loading isn't ported (separate from the resource-baked SE
         * table). Fall back to the universal cursor SE so the user
         * gets *some* feedback. Documented deviation. */
        audio_play_se_by_id(TITLE_SE_CURSOR);
        *out_changed = 1;
        return;
    }
    case SETTINGS_ROW_SLIDER3: {
        int v = settings_get_slider3();
        int nv = v + delta;
        if (nv < 0 || nv > SETTINGS_SLIDER3_MAX) return;
        settings_set_slider3(nv);
        audio_play_se_by_id(TITLE_SE_CURSOR);
        *out_changed = 1;
        return;
    }
    case SETTINGS_ROW_SLIDER4: {
        int v = settings_get_slider4();
        int nv = v + delta;
        if (nv < 0 || nv > SETTINGS_SLIDER4_MAX) return;
        settings_set_slider4(nv);
        audio_play_se_by_id(TITLE_SE_CURSOR);
        *out_changed = 1;
        return;
    }
    case SETTINGS_ROW_CLEAR:
        /* No slider on the clear-data row. */
        return;
    }
}

static void scene_title_settings_step(scene_title_anim_t *anim,
                                      uint16_t pressed,
                                      uint16_t held)
{
    const int row = (int)anim->submenu_cursor;

    /* Row 5 + A: clear-data prompt. Engine opens a modal via
     * FUN_00434def; we play the entry SE and consume the press but
     * don't actually trigger any clear (no save IO). The next frame
     * will re-check `pressed` — A is a rising edge so it doesn't
     * re-fire. */
    if (row == SETTINGS_ROW_CLEAR && (pressed & TITLE_INPUT_A)) {
        audio_play_se_by_id(TITLE_SE_CONFIRM);
        return;
    }

    /* A or B pressed: exit. Engine sets dirty 1→2 (exit-save) or
     * 0→3 (exit-no-save). The actual save + slide-out runs the
     * NEXT frame from the top-of-sim dispatch (`settings_dirty != 0`
     * branch). */
    if (pressed & (TITLE_INPUT_A | TITLE_INPUT_B)) {
        audio_play_se_by_id(TITLE_SE_CONFIRM);
        anim->settings_dirty = (anim->settings_dirty == 1) ? 2 : 3;
        return;
    }

    /* Cursor move. Engine checks UP and DOWN independently (not
     * else-if). On a real D-pad they're mutually exclusive, but we
     * mirror the engine's structure. */
    if (held & TITLE_INPUT_UP) {
        anim->submenu_cursor = (anim->submenu_cursor + 5) % SETTINGS_ROW_COUNT;
        audio_play_se_by_id(TITLE_SE_CURSOR);
        /* Ease the hand to the new row (FUN_00435710): x=168, y=row·40+168. */
        title_save_dialog_cursor_slide(168.0f,
                                       (float)anim->submenu_cursor * 40.0f + 168.0f);
    }
    if (held & TITLE_INPUT_DOWN) {
        anim->submenu_cursor = (anim->submenu_cursor + 7) % SETTINGS_ROW_COUNT;
        audio_play_se_by_id(TITLE_SE_CURSOR);
        title_save_dialog_cursor_slide(168.0f,
                                       (float)anim->submenu_cursor * 40.0f + 168.0f);
    }

    /* LEFT precedence over RIGHT (engine line 398). */
    int changed = 0;
    if (held & TITLE_INPUT_LEFT) {
        scene_title_settings_apply_slider((int)anim->submenu_cursor, -1, &changed);
    } else if (held & TITLE_INPUT_RIGHT) {
        scene_title_settings_apply_slider((int)anim->submenu_cursor, +1, &changed);
    }
    if (changed) {
        anim->settings_dirty = 1;
    }
}

/* ── settings exit handler (FUN_0049a59e top + LAB_0049a5d3) ────────
 *
 * Runs at the top of every sim tick. When `settings_dirty == 2 or 3`,
 * fold back to the main menu. Dirty=2 also triggers save-back (which
 * is stubbed — no save IO). Cursor on main menu seeks to the OPTIONS
 * row so the user lands where they came from. */
static void scene_title_settings_exit_handler(scene_title_anim_t *anim,
                                              const scene_title_menu_t *menu)
{
    if (anim->submenu_state != 2) return;
    if (anim->settings_dirty != 2 && anim->settings_dirty != 3) return;

    if (anim->settings_dirty == 2) {
        /* Engine: FUN_004905a8(-1) writes save.dat + _save.dat. Save
         * IO not ported yet — sliders are kept in audio_fade module
         * state and rebuild from recet.ini next boot. Documented as
         * a deferred follow-up. */
    }

    anim->select_phase     = 0;
    anim->menu_folding_out = 1;        /* start slide-OUT */
    /* Engine calls FUN_00435612 (cursor sprite off) + FUN_0049a43d
     * (rebuild main menu). Our menu is pre-built and reused; turn the
     * shared hand cursor off so it doesn't bleed onto the main menu. */
    title_save_dialog_cursor_set_visible(0);

    /* Seek main-menu cursor to the OPTIONS row (code 2). Engine
     * mirrors this so the user returns to the row they entered from. */
    anim->cursor_pos = 0;
    for (int i = 0; i < menu->count; i++) {
        if (menu->items[i] == SCENE_TITLE_MENU_OPTIONS) {
            anim->cursor_pos = (uint32_t)i;
            break;
        }
    }

    anim->settings_dirty = 0;
    anim->submenu_state  = 0;
}

void scene_title_sim(scene_title_anim_t *anim,
                     const scene_title_menu_t *menu,
                     uint16_t pressed,
                     uint16_t held)
{
    if (!anim || !menu) return;

    /* Engine FUN_0049a59e top: handle a pending settings exit. Runs
     * before the cursor_anim ramp so the slide-out animation can
     * begin on this same frame. */
    scene_title_settings_exit_handler(anim, menu);

    /* Engine FUN_0049a59e L53-77: fade-out countdown. Once the player
     * commits to NEW GAME / CONTINUE, `fade_counter` ticks every frame.
     * Menu input and cursor_anim ramp are gated out — only pulse_phase
     * continues (BG scroll keeps going). At 0x1e the engine fires the
     * scene-transition fade quad (FUN_004526f5 → fade_phase1_start);
     * the title's fade_counter keeps ticking. When fade_is_done()
     * returns 1, the engine transitions scene_state through 8
     * (LOADING) into the destination scene's init path. Destination
     * init isn't ported, so we set LOADING and let fade_render keep
     * drawing the fully-opaque black quad indefinitely. */
    if (anim->fade_counter > 0) {
        anim->fade_counter++;
        if (anim->fade_counter == 0x1e) {
            /* Engine FUN_0049a59e L65: FUN_004526f5(0, 0x11). Engine
             * also writes DAT_0438b1e0 = 0 + calls FUN_00435c98
             * (game-state reset for scene 1) — both deferred until
             * the destination scene lands. */
            fade_phase1_start(0, 0x11);
        }
        if (anim->fade_counter >= 0x1e && fade_is_done()) {
            /* Engine FUN_0049a59e L63-77: LOADING → (save-bank reset
             * chain) → INGAME, all in one tick. The save-bank chain is
             * deferred (no readers ported yet); scene_post_fade_init()
             * collapses the transition to its observable endpoint. The
             * ingame scene's placeholder renderer takes over in
             * main.c's render_dispatch on the next frame. */
            scene_post_fade_init();
        }
        anim->pulse_phase++;
        return;
    }

    /* FUN_0049a3a3 line 239-250: cursor_anim slides toward 0 when
     * `menu_folding_out` is set, toward 10 when clear. */
    if (anim->menu_folding_out) {
        if (anim->cursor_anim > 0) {
            anim->cursor_anim--;
        }
    } else {
        if (anim->cursor_anim < 10) {
            anim->cursor_anim++;
        }
    }

    /* Submenu input — gated on cursor_anim == 10 (fully slid in).
     * Engine: lines 251-475 of 49a59e.c — only state==2 (settings) is
     * wired here; states 1/3/4 fall through with no input. */
    if (anim->cursor_anim == 10 && anim->submenu_state == 2) {
        scene_title_settings_step(anim, pressed, held);
        anim->pulse_phase++;
        return;
    }

    /* Continue/load slot picker — engine state DAT_09643524 == 1
     * (FUN_0049a59e L100795). A on an occupied slot has already loaded
     * the save into the working arena (title_continue_picker_step);
     * we just start the fade into the in-game scene. B returns to the
     * main menu. */
    if (anim->cursor_anim == 10 && anim->submenu_state == 1) {
        int load_bank = -1;
        title_picker_result_t r =
            title_continue_picker_step((uint16_t)pressed, (uint16_t)held,
                                       &load_bank);
        if (r == TITLE_PICKER_LOAD) {
            /* Engine FUN_0049a59e L100907: DAT_0964351c++ (fade) after
             * FUN_00490259. DAT_0438bed4 stays 0 (CONTINUE). */
            anim->continue_mode      = 1;
            anim->continue_load_bank = load_bank;
            anim->fade_counter       = 1;
        } else if (r == TITLE_PICKER_CANCEL) {
            /* Back out: slide the main menu back in. */
            anim->submenu_state    = 0;
            anim->menu_folding_out = 1;
        }
        anim->pulse_phase++;
        return;
    }

    /* Main menu input — gated on cursor_anim == 0 && submenu_state == 0.
     * Engine: line 491-552 (`else` of all the submenu state branches).
     */
    if (anim->cursor_anim == 0 && anim->submenu_state == 0) {
        if (anim->select_phase == 0) {
            anim->frame_counter++;

            /* Past 0x1bc6 (7110) the engine stops accepting cursor
             * input — input handling sits inside this `<` gate. At
             * frame == 0x1be4 (7140) the engine would attempt to
             * play recet_op.wmv (attract loop); on success the scene
             * transitions, on failure frame_counter is reset to 0.
             * Neither path is wired in the bare slice — frame_counter
             * just keeps incrementing past the input window. */
            if (anim->frame_counter < 0x1bc6) {
                if (pressed & TITLE_INPUT_A) {
                    /* Start the select countdown. Engine plays the confirm
                     * SE 0x143 here via FUN_00499519. */
                    anim->select_phase = 1;
                    audio_play_se_by_id(TITLE_SE_CONFIRM);
                } else if (menu->count > 0) {
                    /* UP / DOWN move the cursor with engine wrap math:
                     *   UP   → (count - 1 + cursor) % count
                     *   DOWN → (count + 1 + cursor) % count
                     * Engine plays the cursor SE 0x146 on either move. The
                     * move (and so the SE) keys off `held` and auto-repeats
                     * while the direction is down — confirmed against retail
                     * (the title cursor scrolls + beeps continuously while
                     * held). Same per-move-SE pattern as the settings submenu
                     * above. (Repeat cadence is the held auto-repeat rate; the
                     * port currently moves once per frame — matching the
                     * engine's throttled repeat is a shared input-fidelity
                     * follow-up, see the settings submenu.) */
                    if (held & TITLE_INPUT_UP) {
                        anim->cursor_pos = (anim->cursor_pos
                                            + (uint32_t)(menu->count - 1))
                                           % (uint32_t)menu->count;
                        audio_play_se_by_id(TITLE_SE_CURSOR);
                    } else if (held & TITLE_INPUT_DOWN) {
                        anim->cursor_pos = (anim->cursor_pos
                                            + (uint32_t)(menu->count + 1))
                                           % (uint32_t)menu->count;
                        audio_play_se_by_id(TITLE_SE_CURSOR);
                    }
                }
            }
        } else {
            /* Select-countdown branch. Engine (FUN_0049a59e L521-594):
             *   DAT_09643544 += 1;
             *   if (DAT_09643544 != 0xf) return;
             *   iVar1 = (&DAT_09643358)[DAT_09643540];   // menu item code
             *   switch (iVar1) {
             *     case 3:  PostMessageA(hwnd, WM_CLOSE, 0, 0);          // EXIT
             *     case 2:  DAT_09643524 = 2; DAT_09643528 = 0;          // OPTIONS
             *     case 7:  FUN_0049f012(1); DAT_09643524 = 3; ...       // RANKING
             *     case 8:  DAT_09643524 = 4; ...                        // HIDDEN
             *     case 0, 5: DAT_0964351c += 1; DAT_0438bed4 = 1;       // NEW (loading transition)
             *     case 1, 4: FUN_0049b537(); DAT_09643524 = 1; ...      // CONTINUE
             *     case 6:  DAT_09643550 += 1; ...                       // SURVIVAL
             *   }
             *
             * The engine does NOT reset DAT_09643544 here — it stays at
             * 0xf, but the dispatched action either closes the window
             * (EXIT) or sets `menu_folding_out = 0` so cursor_anim
             * starts incrementing, which gates this entire `cursor_anim
             * == 0` block out on subsequent frames.
             *
             * OPTIONS (code 2) is handled inline: state→2, cursor=0,
             * menu_folding_out=0 (start slide-in). Other codes publish
             * to `pending_action` for main.c.
             *
             * Dispatch is one-shot: fires on the frame select_phase
             * crosses 0→0xf (i.e., reaches 15 for the first time). After
             * that the phase pins at 0xf and the dispatch path is
             * skipped — the engine increments past 0xf instead of
             * pinning, but it gates the dispatch on `== 0xf` so the
             * net effect is the same. The pin is a port choice so
             * consumers can read the latched 0xf value any time. */
            if (anim->select_phase < 0xf) {
                anim->select_phase++;
                if (anim->select_phase == 0xf
                    && menu->count > 0
                    && anim->cursor_pos < (uint32_t)menu->count) {
                    const int code = menu->items[anim->cursor_pos];
                    if (code == SCENE_TITLE_MENU_OPTIONS) {
                        /* Engine FUN_0049a59e L534-543: enter settings. */
                        anim->submenu_state    = 2;
                        anim->submenu_cursor   = 0;
                        anim->menu_folding_out = 0;   /* slide submenu in */
                        /* Snap the shared hand cursor to settings row 0
                         * and show it (FUN_00435693). Target x is the
                         * absolute 168; y = row·40 + 168. */
                        title_save_dialog_cursor_snap(168.0f, 168.0f);
                    } else if (code == SCENE_TITLE_MENU_NEW_GAME
                            || code == SCENE_TITLE_MENU_CONT_HAS_SAVE) {
                        /* Engine FUN_0049a59e L101072: codes {0,5} →
                         * NEW game (DAT_0438bed4 = 1). Start the scene
                         * fade-out; the counter ticks at the top of
                         * every subsequent sim frame and the post-fade
                         * commit runs the fresh-bank new-game path. */
                        anim->continue_mode = 0;
                        anim->fade_counter  = 1;
                    } else if (code == SCENE_TITLE_MENU_CONTINUE_ANY
                            || code == SCENE_TITLE_MENU_NEW_HAS_SAVE) {
                        /* Engine FUN_0049a59e L101099: codes {1,4} →
                         * open the continue/load slot picker
                         * (DAT_09643524 = 1, DAT_0438bed4 = 0). The
                         * picker seeds its cursor from the last-used
                         * save slot and slides in like the settings
                         * submenu. (Code 4 = new-into-slot; only its
                         * load path is wired — see picker PORT-DEBT.) */
                        title_continue_picker_open(
                            code, save_header_get_last_slot());
                        anim->submenu_state    = 1;
                        anim->menu_folding_out = 0;   /* slide picker in */
                    } else if (anim->pending_action == SCENE_TITLE_ACTION_NONE) {
                        anim->pending_action = code;
                    }
                }
            }
        }
    }

    /* LAB_0049b415: tail. Engine increments DAT_0964352c and calls
     * FUN_004356cd (a 3-line shake-effect helper that's a no-op at
     * BSS-zero; stubbed). */
    anim->pulse_phase++;
}

void scene_title_sim_default(void)
{
    /* E.2 probe — FUN_0049a59e @ 0x49a59e. Marked STUB because the
     * engine's title sim is 3719 B and we port only the menu cursor
     * + button handling subset.  Skipped engine logic includes:
     * save/load dialog state machine, DAT_09643520/24/28/40/44/60
     * settings-overlay state, FUN_00499519 SE pings on every cursor
     * move, attract-loop timer (DAT_09643518), and the "saving"
     * overlay path.  The call-count parity holds (we still fire
     * once per title frame), but the body is far from complete.
     *
     * Field-bearing stub (flow_diff): declare the title sim's persisted
     * menu state at entry — the values retail reads from its DAT_096435xx
     * globals at FUN_0049a59e's onEnter (mapping in scene_title.h).  These
     * are read pre-body (this frame's input == last frame's output), so a
     * [data] divergence here means the port's title-menu state stopped
     * tracking retail — the canonical signal that a skipped body branch is
     * load-bearing.  Joined to tools/flow/retail_fields.json by field name. */
    CALL_TRACE_BEGIN_STUB(0x49a59eu);
    CALL_TRACE_U32("frame_counter",   g_scene_title_anim.frame_counter);
    CALL_TRACE_U32("cursor_pos",      g_scene_title_anim.cursor_pos);
    CALL_TRACE_U32("cursor_anim",     g_scene_title_anim.cursor_anim);
    CALL_TRACE_U32("select_phase",    g_scene_title_anim.select_phase);
    CALL_TRACE_U32("pulse_phase",     g_scene_title_anim.pulse_phase);
    CALL_TRACE_I32("menu_folding_out", g_scene_title_anim.menu_folding_out);
    CALL_TRACE_I32("submenu_state",   g_scene_title_anim.submenu_state);
    CALL_TRACE_U32("submenu_cursor",  g_scene_title_anim.submenu_cursor);
    CALL_TRACE_I32("settings_dirty",  g_scene_title_anim.settings_dirty);
    CALL_TRACE_U32("fade_counter",    g_scene_title_anim.fade_counter);
    CALL_TRACE_END();

    /* Engine FUN_0049a59e L100567: `if (FUN_00434d6a() == -1) return;` —
     * save/load dialog gate.  Our port ignores the return for now (the
     * dialog never opens — its writers aren't ported); calling the
     * function alone is what trace parity needs. */
    (void)title_save_dialog_gate_tick();

    /* Dispatch off the global button ring (sim.c wrote it earlier in
     * the same sim_step_a call). The engine's button masks live at
     * DAT_073dddd4 / DAT_073dddd6 — player 0 only at this layer; the
     * second player's mask is never read by the title sim. */
    scene_title_sim(&g_scene_title_anim,
                    &g_scene_title_menu,
                    g_sim_buttons[0].pressed,
                    g_sim_buttons[0].held);

    /* Engine FUN_0049a59e L101201 tail: FUN_004356cd — anim counter
     * tick + shake interpolation step.  Runs regardless of which
     * branch the sim took above. */
    title_save_dialog_anim_tick();
}

#ifdef _WIN32

static sprite_t g_tex[SCENE_TITLE_TEX_COUNT];

int g_scene_title_assets_loaded = 0;

int scene_title_load_assets(IDirect3DDevice8 *dev)
{
    int loaded = 0;
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        const scene_title_asset_t *a = &scene_title_assets[i];
        if (sprite_load(dev, a->path, a->expected_w, a->expected_h,
                        &g_tex[i])) {
            loaded++;
        }
    }
    g_scene_title_assets_loaded = (loaded == SCENE_TITLE_TEX_COUNT) ? 1 : 0;
    return loaded;
}

const sprite_t *scene_title_get(int slot)
{
    if (slot < 0 || slot >= SCENE_TITLE_TEX_COUNT) {
        static const sprite_t empty = {0};
        return &empty;
    }
    return &g_tex[slot];
}

void scene_title_unload_assets(void)
{
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        sprite_destroy(&g_tex[i]);
    }
}

/* ─── render (FUN_0049c644 — bare path only) ─────────────────────────── */

#include "font_draw.h"
#include "render_quad.h"

/* Lookup table at PE 0x005d1cd4 — 9 dwords mapping menu-code →
 * "cursor tile index" in fuki.tga. Extracted via
 *   tools/analyze/pe.py bytes 0x005d1cd4 36
 * Indexing is (row = idx / 4, col = idx % 4). Each tile is 224x128
 * pixels in fuki.tga; rows lay out vertically at 128px stride from
 * top y = 336 (=0x150). */
static const int title_cursor_glyph_lut[9] = {
    0, 1, 2, 3, 4, 0, 7, 6, 5,
};

/* Helper — bind one of our 7 textures and forward to render_quad_add,
 * WITHOUT flushing.  The engine accumulates same-texture quads into one
 * DrawPrimitiveUP batch and flushes only at a texture/state boundary; a
 * caller that draws several quads from the same sheet (the menu-item loop,
 * the selected-row decoration tiles) uses this + one trailing
 * render_quad_flush so the batch shape matches retail (FUN_0049c644 emits
 * the menu items as a single vcount=24 flush, the decoration tiles as one
 * vcount=18 flush). */
static void title_quad_add(IDirect3DDevice8 *dev, int slot,
                           float dx, float dy, float dw, float dh,
                           float sx0, float sy0, float sx1, float sy1,
                           uint32_t color)
{
    const sprite_t *s = &g_tex[slot];
    if (!s->tex) return;
    /* The engine sets the texture *before* each quad-add — every
     * quad in this scene rebinds. We follow the same pattern so
     * the flush emits all quads with their last-bound texture
     * (which the engine relies on too — DrawPrimitiveUP with a
     * single stage 0 binding per flush window). */
    render_quad_bind(dev, s);
    const float dst[4] = { dx, dy, dw, dh };
    const float src[4] = { sx0, sy0, sx1, sy1 };
    render_quad_add(dst, src, s->width, s->height, color);
}

/* Single-quad draw: add + immediate flush.  Used for the standalone
 * background images, each of which is a distinct texture and so flushes on
 * its own in retail too (one vcount=6 per quad). */
static void title_quad(IDirect3DDevice8 *dev, int slot,
                       float dx, float dy, float dw, float dh,
                       float sx0, float sy0, float sx1, float sy1,
                       uint32_t color)
{
    title_quad_add(dev, slot, dx, dy, dw, dh, sx0, sy0, sx1, sy1, color);
    render_quad_flush(dev);
}

/* ─── settings submenu render (FUN_0049c050) ─────────────────────────
 *
 * Draws the settings panel: dungeonbord BG + 6 row labels + 5 slider
 * value strings + (dormant) Saving overlay. Caller-positioned via the
 * (origin_x, origin_y) pair that the engine derives from cursor_anim
 * for the slide-in animation.
 *
 * Engine quirks reproduced:
 *   - Two SetTextureStageState writes back-to-back at FUN_0049c050 L35-36
 *     (ADDSIGNED=8 then MODULATE2X=5); the second wins. We just write
 *     MODULATE2X once.
 *   - Row count is 5 in-game (when DAT_0438b1c0 != 0) but 6 on the
 *     title scene; this function only services the title path so we
 *     hard-code 6 with a comment for when pause-menu lands.
 *   - The five row-color formulas at L47-76 of FUN_0049c050 are three
 *     different bit-twiddle inlines that all evaluate to the same
 *     yellow/grey pair (0xff7f7f00 / 0xff7f7f7f) — we collapse them.
 *   - "Saving" overlay (param_4 != 0) is dormant in our port because
 *     the settings-exit handler clears `settings_dirty` synchronously
 *     before the next render frame. Engine reproduces a one-frame
 *     visible blip when save IO runs; we drop the visuals entirely
 *     until savewindow.tga loading is wired (would need a boot-time
 *     texture set, not just title-scene assets).
 *
 * `cursor_row` is the active settings row 0..5. `saving_flag` mirrors
 * the engine's `DAT_09643560 == 2` test — wired through for fidelity
 * even though the branch never fires today.
 */
static void scene_title_settings_render_panel(IDirect3DDevice8 *dev,
                                              float ox, float oy,
                                              int cursor_row,
                                              int saving_flag)
{
    static const char *const row_labels[5] = {
        "Music", "Sound", "Voice", "Message Speed", "Unread Text Skip",
    };
    static const char *const slider3_labels[3] = { "SLOW", "MED", "FAST" };
    static const char *const slider4_labels[2] = { "OFF", "ON" };

    /* Panel BG (320×360 region of dungeonbord.tga) — MODULATE blend
     * is already active from the caller's restore at L706. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    title_quad(dev, SCENE_TITLE_TEX_DUNGEON,
               ox + 160.0f, oy + 32.0f, 320.0f, 360.0f,
                 0.0f,   0.0f, 320.0f, 360.0f,
               0xFFFFFFFFu);

    /* Switch to MODULATE2X so the per-row diffuse colors brighten
     * against the panel BG (engine FUN_0049c050 L36). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    const int row_count = 6;       /* title scene; in-game would pass 5 */
    const float label_x = ox + 208.0f;
    const float label_y = oy + 112.0f;

    for (int i = 0; i < row_count; i++) {
        const uint32_t color = (i == cursor_row) ? 0xFF7F7F00u
                                                 : 0xFF7F7F7Fu;
        if (i == 5) {
            /* Clear Save Data — centered at (ox+320, oy+312). */
            font_draw_text_centered(dev, ox + 320.0f, oy + 312.0f,
                                    "Clear Save Data", color, 1.0f);
        } else {
            font_draw_text(dev, label_x, label_y + (float)i * 40.0f,
                           row_labels[i], color, 1.0f);
        }
    }

    /* Slider value column at ox+400 — engine: (param_1_new - 48) + 240. */
    char buf[16];
    const float value_x = ox + 400.0f;

    snprintf(buf, sizeof buf, "%d",
             audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM));
    font_draw_text(dev, value_x, label_y +   0.0f, buf,
                   (cursor_row == 0) ? 0xFF7F7F00u : 0xFF7F7F7Fu, 1.0f);

    snprintf(buf, sizeof buf, "%d",
             audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A));
    font_draw_text(dev, value_x, label_y +  40.0f, buf,
                   (cursor_row == 1) ? 0xFF7F7F00u : 0xFF7F7F7Fu, 1.0f);

    snprintf(buf, sizeof buf, "%d",
             audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B));
    font_draw_text(dev, value_x, label_y +  80.0f, buf,
                   (cursor_row == 2) ? 0xFF7F7F00u : 0xFF7F7F7Fu, 1.0f);

    int s3 = settings_get_slider3();
    if (s3 < 0) s3 = 0; else if (s3 > 2) s3 = 2;
    font_draw_text(dev, value_x, label_y + 120.0f, slider3_labels[s3],
                   (cursor_row == 3) ? 0xFF7F7F00u : 0xFF7F7F7Fu, 1.0f);

    int s4 = settings_get_slider4();
    if (s4 < 0) s4 = 0; else if (s4 > 1) s4 = 1;
    const uint32_t row4_color = (cursor_row == 4) ? 0xFF7F7F00u
                                                  : 0xFF7F7F7Fu;
    font_draw_text(dev, value_x, label_y + 160.0f, slider4_labels[s4],
                   row4_color, 1.0f);

    /* Dormant "Saving" overlay — see header comment. Engine: L77-90.
     * `saving_flag` is the only way this branch fires; the settings
     * exit handler currently clears `settings_dirty` synchronously
     * so the caller always passes 0. Left wired for future save-IO
     * port. */
    (void)saving_flag;

    /* Restore MODULATE — engine: L91. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* ── Continue / load slot-picker panel (submenu_state == 1) ──
 *
 * PORT-DEBT(render): the faithful engine picker (FUN_0049b556, 2810 B)
 * draws a 3-column grid of slot cells from the DAT_073d8748 /
 * DAT_073da020 textures with per-cell brightness pulse. This is a
 * functional vertical-list stand-in: navigable (the cursor + scroll
 * come straight from the ported picker state machine) and showing each
 * save's real summary, so the load flow is verifiable. Retire when the
 * faithful grid renderer ports. */
static void scene_title_continue_render_panel(IDirect3DDevice8 *dev,
                                              float ox, float oy)
{
    const title_continue_picker_t *p = &g_title_continue_picker;

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    title_quad(dev, SCENE_TITLE_TEX_DUNGEON,
               ox + 120.0f, oy + 16.0f, 400.0f, 420.0f,
                 0.0f,   0.0f, 320.0f, 360.0f,
               0xFFFFFFFFu);

    font_draw_text_centered(dev, ox + 320.0f, oy + 28.0f,
                            p->overwrite_mode ? "NEW GAME — CHOOSE FILE"
                                              : "LOAD GAME",
                            0xFFFFFFFFu, 1.0f);

    const int VISIBLE = 8;
    int top = p->scroll;
    if (top > p->slot_count - VISIBLE) top = p->slot_count - VISIBLE;
    if (top < 0) top = 0;

    const float row_x = ox + 168.0f;
    const float row_y = oy + 72.0f;
    char buf[64];

    for (int r = 0; r < VISIBLE; r++) {
        const int slot = top + r;
        if (slot >= p->slot_count) break;
        const int bank = p->slot_index[slot];
        const uint32_t *bd = save_bank_dwords_at(bank);
        const uint32_t color = (slot == p->cursor) ? 0xFFFFFF00u
                                                   : 0xFF9F9F9Fu;
        if (bd && bd[SAVE_BANK_FIELD_OCCUPIED] != 0) {
            snprintf(buf, sizeof buf, "%2d   %u G   Day %u",
                     bank + 1,
                     (unsigned)bd[SAVE_BANK_FIELD_GOLD],
                     (unsigned)bd[SAVE_BANK_FIELD_DAY_INDEX]);
        } else {
            snprintf(buf, sizeof buf, "%2d   - empty -", bank + 1);
        }
        font_draw_text(dev, row_x, row_y + (float)r * 42.0f, buf, color, 1.0f);
    }
}

void scene_title_render(IDirect3DDevice8 *dev,
                        const scene_title_menu_t *menu,
                        const scene_title_anim_t *anim)
{
    if (!dev || !menu || !anim) return;

    /* E.2 probe — FUN_0049c644 @ 0x49c644. Marked STUB because the
     * engine's title render is 3233 B and we port only the BG + menu-
     * items subset.  Skipped engine logic includes: save/load dialog
     * frame, settings/options panel, attract-loop fade, the
     * DAT_09643518 timeout banner, and the cursor-shake overlay. */
    CALL_TRACE_ENTER_STUB(0x49c644u);

    render_quad_state_setup(dev);

    /* ── background bg2.bmp ────────────────────────────────────────────
     * Vertical scroll: src.top_y = 360 - (frame * 360 / 7140). On a
     * fresh boot (frame_counter == 0) that's exactly 360, so we sample
     * the 640x480 window at (0, 360)..(640, 840) of the 1024x1024
     * texture. */
    const float scroll_y = 360.0f
        - ((float)anim->frame_counter * 360.0f) / 7140.0f;
    title_quad(dev, SCENE_TITLE_TEX_BG2,
               0.0f, 0.0f, 640.0f, 480.0f,
               0.0f, scroll_y, 640.0f, scroll_y + 480.0f,
               0xFFFFFFFF);

    /* ── waku frame overlay (full screen, opaque tex with alpha cutouts) */
    title_quad(dev, SCENE_TITLE_TEX_WAKU,
               0.0f, 0.0f, 640.0f, 480.0f,
               0.0f, 0.0f, 640.0f, 480.0f,
               0xFFFFFFFF);

    /* ── fuki corner element ──────────────────────────────────────────
     * 416x32 strip pulled from (0, 992)..(416, 1024) of fuki.tga. */
    title_quad(dev, SCENE_TITLE_TEX_FUKI,
               112.0f, 448.0f, 416.0f, 32.0f,
               0.0f, 992.0f, 416.0f, 1024.0f,
               0xFFFFFFFF);

    /* ── title01 animated band ────────────────────────────────────────
     * The engine derives a base offset `local_14 = -cursor_anim * 64`
     * once and reuses it for the band, the menu items, and the
     * selected-row decoration tiles. At fresh boot (cursor_anim == 0)
     * `slide` is 0. */
    const float slide = -(float)(int)anim->cursor_anim * 64.0f;
    title_quad(dev, SCENE_TITLE_TEX_01,
               slide + 64.0f, 0.0f, 512.0f, 256.0f,
               0.0f, 0.0f, 512.0f, 256.0f,
               0xFFFFFFFF);

    /* ── menu items loop ──────────────────────────────────────────────
     * Each item is a 160x32 tile (scale 1.0 selected, 0.8 not) pulled
     * from fuki.tga at (224, code*32)..(384, (code+1)*32). Y position
     * is `index * y_stride + y_origin + 288 - 16*scale`. Colour for
     * the selected slot pulses via sin(select_phase*pi/15); other
     * items use the bit-pattern 0x95 (= 149) as a flat grey. Both
     * paths build a `0xFF | r<<16 | g<<8 | b` greyscale.
     *
     * Blend is D3DTOP_ADDSIGNED (= 8), not D3DTOP_ADD (= 7) — engine
     * FUN_0049c644 L80 passes 8 as the third arg to
     * SetTextureStageState(0, D3DTSS_COLOROP, …). D3DTOP_ADD clips
     * highlights to white and the menu items look washed out;
     * D3DTOP_ADDSIGNED subtracts 0.5 from one term first, preserving
     * the per-item contrast the engine intends. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    const int selected_idx = (int)anim->cursor_pos;
    for (int i = 0; i < menu->count; i++) {
        const int code  = menu->items[i];
        float scale     = 0.8f;
        /* Engine stores the unselected brightness as the float literal
         * 1.33123e-43 (FUN_0049c644 `else` branch) — a denormal whose 32-bit
         * pattern reinterpreted as an int is 95 (0x5f), the grey byte.  An
         * earlier port read this as "0x95" (a hex/decimal slip: 95 decimal is
         * 0x5f, not 0x95=149), which over-brightened every unselected item.
         * Confirmed 1:1 vs retail via the flow-trace (render_quad_add diffuse
         * = 0xff5f5f5f); cross-checked by the selected-clamp denormal
         * 3.57331e-43 = bit pattern 255 = 0xff.  See engine-quirks. */
        uint32_t bright = 0x5f;  /* 95 = bits of the engine's 1.33123e-43 */

        if (i == selected_idx) {
            scale = 1.0f;
            /* Two sin pulses modulate brightness. Ghidra hides the
             * FPU scale factor inside __ftol; the engine (FUN_0049c644
             * @ 0x49c8ce..0x49c95d) writes `0x7f - iVar1` and
             * `0x20 - iVar1`, where iVar1 = ftol(f32(sin) * SCALE) and
             * the SCALE multiplicands are NEGATIVE float32 constants
             * read straight from .rdata:
             *   SCALE1 @ 0x519468 = -128.0   (select pulse)
             *   SCALE2 @ 0x519820 =  -32.0   (idle pulse)
             * trunc(-x) == -trunc(x), so `0x7f - ftol(sin*-128)`
             * collapses to `0x7f + ftol(sin*128)`. The effective
             * formulas are therefore:
             *   v  = 0x7f + 128 * sin(select_phase * π / 15)
             *   v += 0x20 +  32 * sin((pulse_phase % 45) * 2π / 45)
             * (NB the engine fst's sin to float32 BEFORE the *SCALE
             * fmul — sinf() already returns float32, so this matches.)
             * On a fresh boot (both phases zero) v = 0x9f, slightly
             * brighter than the unselected 0x5f default — enough for
             * ADDSIGNED to show the item as "selected". At press
             * midpoint sin(a)=1 → v peaks near 0xff (clamped).
             *
             * The select scale was previously misread as 127 (a guess);
             * objdump'd as -128 on 2026-06-05 after flow_diff surfaced a
             * 1-LSB select-glyph divergence (port 249 / retail 250) on
             * the title-z-press frame-35 select countdown. Frida-read the
             * render-time counters (select=6,pulse=36, identical both
             * sides) to rule out a phase offset, then read the binary.
             * -128 reproduces retail bit-exactly across the whole ramp;
             * 127 missed only that one frame. See engine-quirks. */
            const float a = (float)anim->select_phase * 3.1415927f / 15.0f;
            const float b = (float)(anim->pulse_phase % 0x2d)
                          * 6.2831855f / 45.0f;
            int v = 0x7f + (int)(sinf(a) * 128.0f);
            v +=    0x20 + (int)(sinf(b) *  32.0f);
            if (v > 0xff) v = 0xff;
            if (v < 0)    v = 0;
            bright = (uint32_t)v;
        }

        const uint32_t color = 0xff000000u
                             | (bright << 16) | (bright << 8) | bright;
        const float dst_x = slide + 320.0f - scale * 80.0f;
        const float dst_y = (float)i * menu->y_stride + menu->y_origin
                          + 288.0f - scale * 16.0f;
        title_quad_add(dev, SCENE_TITLE_TEX_FUKI,
                       dst_x, dst_y, scale * 160.0f, scale * 32.0f,
                       224.0f, (float)(code * 32),
                       384.0f, (float)((code + 1) * 32),
                       color);
    }
    /* One flush for the whole menu-item batch (retail FUN_0049c644: a
     * single vcount = count*6 DrawPrimitiveUP under the ADDSIGNED state,
     * right before COLOROP is restored to MODULATE for the decorations). */
    render_quad_flush(dev);

    /* ── selected-item highlight overlay ──────────────────────────────
     * Restores MODULATE. Then draws three decoration tiles on top of
     * the selected menu row:
     *   1. Top-corner strip at (224, 32)..(384, 64) of fuki.tga, sized
     *      224x112 dst, positioned at selected_y + 216.
     *   2. The big cursor-glyph (224x128) pulled from a 4-col grid at
     *      (lut/4 * 224, lut%4 * 128 + 336) of fuki.tga.
     *   3. A small label strip (192x16) from (0, 144)..(192, 160). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    if (menu->count > 0) {
        const int sel = selected_idx;
        const int code = menu->items[sel];
        const float sy = (float)sel * menu->y_stride + menu->y_origin;
        const int lut = (code >= 0 && code < 9)
                          ? title_cursor_glyph_lut[code] : 0;

        /* Tiles 1-3 are all FUKI sheet quads under MODULATE — retail
         * (FUN_0049c644) accumulates them into ONE vcount=18 flush. Add
         * without flushing; one render_quad_flush after tile 3. */

        /* Tile 1 — decorative outline frame (224×112) pulled from
         * (0, 0)..(224, 112) of fuki. dst.x = slide + 32. */
        title_quad_add(dev, SCENE_TITLE_TEX_FUKI,
                       slide + 32.0f, sy + 216.0f, 224.0f, 112.0f,
                        0.0f,  0.0f, 224.0f, 112.0f,
                       0xFFFFFFFF);

        /* Tile 2 — the BIG label glyph (e.g. "New Game") via the
         * 9-entry LUT at PE 0x005d1cd4. fuki has a 4-column grid of
         * 224×128 tiles starting at y = 0x150 (336). dst overlaps
         * tile 1 at the same (x, y) — modulate blend stacks them. */
        const float glyph_x0 = (float)((lut / 4) * 0xe0);
        const float glyph_y0 = (float)((lut % 4) * 0x80 + 0x150);
        const float glyph_x1 = (float)(((lut / 4) + 1) * 0xe0);
        const float glyph_y1 = (float)((lut % 4) * 0x80 + 0x1d0);
        title_quad_add(dev, SCENE_TITLE_TEX_FUKI,
                       slide + 32.0f, sy + 216.0f, 224.0f, 128.0f,
                       glyph_x0, glyph_y0, glyph_x1, glyph_y1,
                       0xFFFFFFFF);

        /* Tile 3 — small 192×16 ribbon below the label at
         * (slide + 224, sy + 296). Source (0, 144)..(192, 160). */
        title_quad_add(dev, SCENE_TITLE_TEX_FUKI,
                       slide + 224.0f, sy + 296.0f, 192.0f, 16.0f,
                       0.0f, 144.0f, 192.0f, 160.0f,
                       0xFFFFFFFF);
        render_quad_flush(dev);   /* one flush for tiles 1-3 (vcount=18) */
    }

    /* Settings submenu overlay — engine FUN_0049c644 lines 229-256
     * dispatches to FUN_0049c050 when cursor_anim > 0 and submenu_state
     * == 2. The panel slides in from the right as cursor_anim ramps
     * 0 → 10, anchored at x = 640 - cursor_anim * 64. Fade-in (state==4)
     * and load-game (state==1) branches in the same arm of the engine
     * stay deferred — their producers haven't ported. */
    if ((int)anim->cursor_anim > 0 && anim->submenu_state == 2) {
        const float ox = 640.0f - (float)(int)anim->cursor_anim * 64.0f;
        const float oy = 48.0f;            /* engine: 0x42400000 */

        scene_title_settings_render_panel(dev, ox, oy,
                                          (int)anim->submenu_cursor,
                                          anim->settings_dirty == 2);

        /* Header tab chrome — item_win.tga sub-tile drawn over the
         * top of the panel area. Engine: FUN_0049c644 L234-244. */
        title_quad(dev, SCENE_TITLE_TEX_ITEM_WIN,
                   ox + 200.0f, 48.0f, 240.0f, 80.0f,
                   448.0f, 816.0f, 688.0f, 896.0f,
                   0xFFFFFFFFu);

        /* "Options" label tile — same 160x32 menu-item tile from fuki
         * that the main menu uses, drawn at code-2 row (`OPTIONS`).
         * Engine: FUN_0049c644 L245-255. */
        title_quad(dev, SCENE_TITLE_TEX_FUKI,
                   ox + 240.0f, 68.0f, 160.0f, 32.0f,
                   224.0f, 64.0f, 384.0f, 96.0f,
                   0xFFFFFFFFu);
    }

    /* Continue / load slot picker — engine FUN_0049c644 dispatches to
     * FUN_0049b556 when cursor_anim > 0 and submenu_state == 1. Slides
     * in from the right like settings (x = 640 - cursor_anim * 64). */
    if ((int)anim->cursor_anim > 0 && anim->submenu_state == 1) {
        const float ox = 640.0f - (float)(int)anim->cursor_anim * 64.0f;
        const float oy = 48.0f;
        scene_title_continue_render_panel(dev, ox, oy);
    }

    /* Sub-menu states 3 / 4 (confirm / ranking-fade) intentionally not
     * ported here — their producers haven't landed. Deferred. */

    /* Final flush guard — restore additive→modulate already done. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* Engine FUN_0049c644 L102078-L102080 tail: three render helpers
     * for the dialog/overlay layer.  All three short-circuit on their
     * respective BSS-zero gates in normal play, but the function calls
     * themselves are unconditional — the diff tool sees count parity
     * regardless. */
    title_save_dialog_secondary_render();
    title_save_dialog_cursor_render(dev);
    title_save_dialog_render();

    /* Smoke-test "openrecet 0.1" font draw removed 2026-05-27:
     * pre_3d_trace diff showed it was responsible for +12
     * render_quad_add calls per title frame vs retail (one quad per
     * glyph).  Restore (gated on a flag, not hot path) if we need to
     * eyeball the font atlas again. */
}

#endif /* _WIN32 */
