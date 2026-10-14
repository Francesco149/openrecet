/*
 * speech_bubble.c — see speech_bubble.h.  Engine: FUN_00406159 (open),
 * FUN_00406212 (close), FUN_0040622f (hide), FUN_004060ff (reset part),
 * FUN_0040697d / FUN_00406990 / FUN_004069a6 (setters), the FUN_00406584
 * bubble block (tick) and FUN_0040c4eb (draw).
 */
#include "speech_bubble.h"

#include <string.h>

#include "audio.h"

static int   s_close;                                            /* DAT_00647e20 */
static char  s_lines[SPEECH_BUBBLE_LINES][SPEECH_BUBBLE_LINE_LEN]; /* DAT_00647e24 */
static float s_x, s_y;                                           /* DAT_00648224/228 */
static int   s_line_x[SPEECH_BUBBLE_LINES];                      /* DAT_0064822c..238 */
static int   s_nlines;                                           /* DAT_00648244 */
static int   s_opened;                                           /* DAT_00648250 */
static int   s_timed;                                            /* DAT_00648254 */
static int   s_age;                                              /* DAT_00648258 */
static int   s_pop;                                              /* DAT_0064825c */
static int   s_mirror_a, s_mirror_b;                             /* DAT_00648260/264 */
static float s_unused_268;                                       /* DAT_00648268 */
static float s_text_scale = 0.5f;                                /* DAT_00529700 (.data) */

void speech_bubble_open(float x, float y, const char *text)
{
    s_mirror_a = 0;
    s_mirror_b = 0;
    s_timed = 0;
    s_unused_268 = 0.0f;
    s_age = 1;
    if (s_pop != 0) {
        audio_play_se_by_id(0x172);
    } else {
        s_pop = 1;
        audio_play_se_by_id(0x174);
    }
    s_opened = 1;
    s_x = x;
    s_y = y;
    s_close = 0;

    /* Split on '<' (the "<BR>" tag: the '<' ends the line, the next 3 bytes
     * are skipped).  The engine walks at most 256 source bytes and writes
     * without a line-count bound; the 4 × 256 buffer is clamped here. */
    int src = 0, col = 0, line = 0;
    while (src < SPEECH_BUBBLE_LINE_LEN) {
        char c = text[src];
        if (c == 0)
            break;
        if (c == '<') {
            if (line < SPEECH_BUBBLE_LINES && col < SPEECH_BUBBLE_LINE_LEN)
                s_lines[line][col] = 0;
            src += 3;
            line++;
            col = 0;
        } else {
            if (line < SPEECH_BUBBLE_LINES && col < SPEECH_BUBBLE_LINE_LEN)
                s_lines[line][col] = c;
            col++;
        }
        src++;
    }
    if (line < SPEECH_BUBBLE_LINES && col < SPEECH_BUBBLE_LINE_LEN)
        s_lines[line][col] = 0;
    s_nlines = line + 1;
    (void)s_opened;
    (void)s_unused_268;
}

void speech_bubble_close(int se)
{
    s_close = 1;
    if (se != 0)
        audio_play_se_by_id(0x174);
}

void speech_bubble_hide(void)
{
    s_close = 0;
    s_age = 0;
    s_pop = 0;
}

void speech_bubble_reset(void)
{
    s_age = 0;
    s_pop = 0;
    memset(s_line_x, 0, sizeof s_line_x);
    s_timed = 0;
}

void speech_bubble_set_mirror(int a, int b)
{
    s_mirror_a = a;
    s_mirror_b = b;
}

void speech_bubble_set_text_scale(float s) { s_text_scale = s; }

void speech_bubble_set_pos(float x, float y)
{
    s_x = x;
    s_y = y;
}

void speech_bubble_tick(void)
{
    if (s_age == 0)
        return;
    s_age++;
    s_pop++;
    if (s_timed == 1 && s_age == 180) {
        s_close = 1;
        audio_play_se_by_id(0x174);
    }
    if (s_close > 0 && ++s_close == 10) {
        s_close = 0;
        s_age = 0;
        s_pop = 0;
    }
}

int         speech_bubble_age(void)        { return s_age; }
int         speech_bubble_pop(void)        { return s_pop; }
int         speech_bubble_close_ctr(void)  { return s_close; }
int         speech_bubble_line_count(void) { return s_nlines; }
int         speech_bubble_mirror(void)     { return s_mirror_a; }
float       speech_bubble_x(void)          { return s_x; }
float       speech_bubble_y(void)          { return s_y; }
float       speech_bubble_text_scale(void) { return s_text_scale; }

const char *speech_bubble_line(int i)
{
    if (i < 0 || i >= SPEECH_BUBBLE_LINES)
        return "";
    return s_lines[i];
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "font_draw.h"
#include "render_quad.h"
#include "sysassets.h"

/* FUN_0040c4eb.  item_win.tga (DAT_073d8748) holds the bubble art: the speech
 * balloon at src (0,608)-(280,703) and the timed variant's small box at
 * (64,672)-(96,704). */
void speech_bubble_render(struct IDirect3DDevice8 *dev_in, int suppress)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (!dev || suppress || s_age == 0)
        return;
    const sprite_t *tex = &g_sysassets.item_win_tga;
    float x = s_x, y = s_y;
    float t = (float)s_pop / 10.0f;
    if (t > 1.0f)
        t = 1.0f;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);
    if (s_timed != 0) {
        const float src[4] = { 64.0f, 672.0f, 96.0f, 704.0f };
        const float dst[4] = { x - 8.0f, y - 8.0f, t * 144.0f, 38.4f };
        render_quad_add(dst, src, tex->width, tex->height, 0x9bffffffu);
    } else {
        if (s_close > 0)
            t = (float)(10 - s_close) / 10.0f;
        if (s_mirror_a != 0) {
            const float k = t * 0.8f;
            const float src[4] = { 0.0f, 703.0f, 280.0f, 608.0f };
            const float dst[4] = { x - k * 24.0f + 204.8f - k * 256.0f, y - 9.6f,
                                   k * 280.0f, 76.8f };
            render_quad_add_mirrored(dst, src, tex->width, tex->height, 0xffffffffu);
        } else {
            const float src[4] = { 0.0f, 608.0f, 280.0f, 703.0f };
            const float dst[4] = { x - 32.0f - t * 24.0f * 0.8f, y - 9.6f,
                                   t * 224.0f, 76.8f };
            render_quad_add(dst, src, tex->width, tex->height, 0xffffffffu);
        }
    }
    render_quad_flush(dev);

    float line_h = (s_nlines == 4) ? 40.0f : 44.0f;
    render_quad_state_setup(dev);                                     /* FUN_0049b425 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    if (s_age <= 10) {
        render_quad_flush(dev);
        return;
    }

    float top = 0.0f, pitch = 20.0f;
    switch (s_nlines) {
    case 1: top = 30.0f;                break;
    case 2: top = 12.0f; pitch = 30.0f; break;
    case 3: top =  7.0f; pitch = 22.0f; break;
    default:                            break;
    }
    if (s_text_scale > 0.5f)
        top -= (s_text_scale - 0.5f) * line_h * 0.5f;
    const uint32_t argb = 0x00ffffffu | ((uint32_t)(255 - 25 * s_close) << 24);
    int xoff = 0;
    for (int i = 0; i < s_nlines && i < SPEECH_BUBBLE_LINES; i++) {
        xoff += s_line_x[i];
        float lx = x;
        if (s_mirror_a != 0)
            lx = x + 16.0f;
        float ly = ((float)i * pitch - 5.0f + top) * 0.8f + y;
        font_draw_text(dev_in, lx - 6.4f, ly, s_lines[i], argb, s_text_scale * 1.6f);
    }
    (void)xoff;   /* the engine sums the (always-0) DAT_0064822c offsets but never applies them */
    render_quad_flush(dev);
}

#endif /* _WIN32 */
