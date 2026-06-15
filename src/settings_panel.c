/*
 * settings_panel.c — the shared config-panel render (FUN_0049c050).
 * See settings_panel.h.  Transcribed 1:1 from objdump @0x49c050; the
 * label/format/value strings were recovered from the PE .data/.rdata
 * (the decompile dropped them), the register-built diffuse args from
 * the colour formulas at L47-76 (all evaluate to the same yellow/grey
 * pair 0xff7f7f00 / 0xff7f7f7f, collapsed here).
 */
#ifdef _WIN32

#include <stdio.h>      /* snprintf */
#include <stdint.h>
#include <d3d8.h>

#include "settings_panel.h"
#include "render_quad.h"   /* render_quad_bind / _add / _flush */
#include "font_draw.h"     /* font_draw_text / _centered (FUN_0047ca05 / d14c) */
#include "settings.h"      /* settings_get_slider3 / slider4 */
#include "audio_fade.h"    /* audio_fade_get_slider (BGM/SE-A/SE-B) */
#include "scene.h"         /* g_scene_state (DAT_0438b1c0) */

/* selected row = yellow, others = grey (engine: the three bit-twiddle
 * colour inlines at FUN_0049c050 L47-76 all reduce to this pair). */
#define SP_COL_SEL   0xff7f7f00u
#define SP_COL_NORM  0xff7f7f7fu

void settings_panel_render(IDirect3DDevice8 *dev,
                           const sprite_t *dungeonbord,
                           const sprite_t *savewindow,
                           float slide_x, float base_y,
                           int cursor_row, int saving_flag)
{
    /* left-column labels (rows 0..4; row 5 = "Clear Save Data" is title-only,
     * drawn centered). */
    static const char *const labels[5] = {
        "Music", "Sound", "Voice", "Message Speed", "Unread Text Skip",
    };
    static const char *const slider3_words[3] = { "SLOW", "MED", "FAST" };
    static const char *const slider4_words[2] = { "OFF", "ON" };

    /* ── panel backdrop: dungeonbord.tga src(0,0,320,360) →
     *    dst(slide_x+160, base_y+32, 320, 360), MODULATE (engine L23-34). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    render_quad_bind(dev, dungeonbord);
    {
        const float dst[4] = { slide_x + 160.0f, base_y + 32.0f, 320.0f, 360.0f };
        const float src[4] = { 0.0f, 0.0f, 320.0f, 360.0f };
        render_quad_add(dst, src, dungeonbord->width, dungeonbord->height, 0xffffffffu);
    }
    render_quad_flush(dev);

    /* ── switch to MODULATE2X for the text (engine L35-36 writes ADDSIGNED
     *    then MODULATE2X back-to-back; the second wins). font_draw_text
     *    inherits the active COLOROP, so the grey/yellow diffuses double. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    /* in-game pause (scene 9) = 5 rows; title (scene 0) = 6 (engine L37-42). */
    const int   row_count = (g_scene_state == 0) ? 6 : 5;
    const float label_x   = slide_x + 208.0f;
    const float label_y   = base_y  + 112.0f;

    for (int i = 0; i < row_count; i++) {
        const uint32_t col = (i == cursor_row) ? SP_COL_SEL : SP_COL_NORM;
        if (i == 5) {
            /* "Clear Save Data" — centered at (slide_x+320, base_y+312)
             * (engine L43-45: (param_1-208)+320, (param_2)+200). */
            font_draw_text_centered(dev, slide_x + 320.0f, base_y + 312.0f,
                                    "Clear Save Data", col, 1.0f);
        } else {
            font_draw_text(dev, label_x, label_y + (float)i * 40.0f,
                           labels[i], col, 1.0f);
        }
    }

    /* ── value column at slide_x+400 (engine L62: (param_1-48)+240). The
     *    selected row's value is yellow, the rest grey (engine L62-90). */
    const float value_x = slide_x + 400.0f;
    char buf[16];

    snprintf(buf, sizeof buf, "%d", audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM));
    font_draw_text(dev, value_x, label_y +   0.0f, buf,
                   (cursor_row == 0) ? SP_COL_SEL : SP_COL_NORM, 1.0f);

    snprintf(buf, sizeof buf, "%d", audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A));
    font_draw_text(dev, value_x, label_y +  40.0f, buf,
                   (cursor_row == 1) ? SP_COL_SEL : SP_COL_NORM, 1.0f);

    snprintf(buf, sizeof buf, "%d", audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B));
    font_draw_text(dev, value_x, label_y +  80.0f, buf,
                   (cursor_row == 2) ? SP_COL_SEL : SP_COL_NORM, 1.0f);

    int s3 = settings_get_slider3();
    if (s3 < 0) s3 = 0; else if (s3 > 2) s3 = 2;
    font_draw_text(dev, value_x, label_y + 120.0f, slider3_words[s3],
                   (cursor_row == 3) ? SP_COL_SEL : SP_COL_NORM, 1.0f);

    const uint32_t row4_col = (cursor_row == 4) ? SP_COL_SEL : SP_COL_NORM;
    int s4 = settings_get_slider4();
    if (s4 < 0) s4 = 0; else if (s4 > 1) s4 = 1;
    font_draw_text(dev, value_x, label_y + 160.0f, slider4_words[s4],
                   row4_col, 1.0f);

    /* ── "Saving" overlay during a dirty-exit save (engine L77-90): a
     *    savewindow.tga banner dst(64,160,512,128) under MODULATE2X grey,
     *    then the centered word at (320,208) scale 1.2 in the row-4 colour. */
    if (saving_flag && savewindow) {
        render_quad_bind(dev, savewindow);
        {
            const float dst[4] = { 64.0f, 160.0f, 512.0f, 128.0f };
            const float src[4] = { 0.0f, 0.0f, 512.0f, 128.0f };
            render_quad_add(dst, src, savewindow->width, savewindow->height, 0xff7f7f7fu);
        }
        render_quad_flush(dev);
        font_draw_text_centered(dev, 320.0f, 208.0f, "Saving", row4_col, 1.2f);
    }

    /* restore MODULATE (engine L91). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

#endif /* _WIN32 */
