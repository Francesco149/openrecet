/*
 * scene_gameover.c — see scene_gameover.h.  Engine FUN_0049d2e5 (enter),
 * FUN_0049d8a4 (mode 2 tick), FUN_0049d55c (the restart reset of the work
 * record), FUN_0049d325.
 */
#include "scene_gameover.h"

#include <string.h>

#include "fade.h"                  /* FUN_004526f5 / FUN_004528b3 / FUN_0045281c */
#include "save_bank.h"             /* the save-file header DAT_056e5770 */
#include "save_io.h"               /* FUN_004905a8 */
#include "save_work.h"
#include "scene.h"                 /* g_scene_state */
#include "scene_day_end.h"         /* FUN_0048d954, DAT_0438b798 / b79c */
#include "scene_pause.h"           /* DAT_06a499a8 */
#include "scene1_intro_dialogue.h" /* FUN_0044ba2c */
#include "scene1_tutorial_dispatch.h" /* DAT_0438b928 */
#include "title_save_dialog.h"     /* FUN_00435612 */
#include "worker_load.h"           /* FUN_00452cde */

static int32_t s_9643578;   /* DAT_09643578 — zeroed at entry (render side) */
static int32_t s_exit_t;    /* DAT_0964357c — the exit countdown */
static int32_t s_wait;      /* DAT_09643580 — waits on FUN_004808fb */
static int32_t s_step;      /* DAT_09643584 */

#define W_MODE_DW   46937   /* +0x2dd64 */
#define W_DAY_DW    45307   /* +0x2b10c */
#define W_2CE04_DW  45953   /* +0x2ce04 */
#define W_2DE34_DW  46989   /* +0x2de34 */

int32_t scene_gameover_step(void)   { return s_step; }
int32_t scene_gameover_exit_t(void) { return s_exit_t; }

static uint8_t *work(void) { return save_work_bank_at(save_work_active_slot()); }
static int32_t rd(const uint8_t *b, int off) { int32_t v; memcpy(&v, b + off, 4); return v; }
static void wr(uint8_t *b, int off, int32_t v) { memcpy(b + off, &v, 4); }

void scene_gameover_enter(void)
{
    s_9643578 = 0;
    s_exit_t = 0;
    s_wait = 0;
    title_save_dialog_cursor_set_visible(0);   /* FUN_00435612 */
    s_step = 0;
    const uint8_t *w = work();
    if (w != NULL && rd(w, W_2CE04_DW * 4) == 2)
        s_step = 5;
}

/* FUN_0049d325: clear the 20 × 12-byte records at +0x275d0 and the 10 ×
 * 20-byte records at +0x2a600 */
static void work_clear_lists(uint8_t *w)
{
    uint8_t *p = w + 161232;
    for (int i = 0; i < 20; i++, p += 12) {
        wr(p, -8, 0);
        wr(p, -4, 0);
        p[0] = p[1] = p[2] = 0;
    }
    p = w + 173568;
    for (int j = 0; j < 10; j++, p += 20) {
        wr(p, -8, 0);
        wr(p, -4, 0);
        wr(p, 0, 0);
        memset(p + 4, 0, 4);
        wr(p, 8, 0);
    }
}

void scene_gameover_restart_reset(uint8_t *w, int keep)
{
    const uint8_t v29 = w[179325], v28 = w[179326], v27 = w[179329], v26 = w[179385];
    const uint8_t v25 = w[179425], v24 = w[179443], v23 = w[179345], v22 = w[179461];
    const uint8_t v5 = w[179344], v21 = w[179462], v20 = w[179473];
    const int v4 = w[179331] == 0;
    wr(w, 181220, rd(w, 181220) + 1);   /* the restart count */
    wr(w, 12, 1000);                    /* the gold */
    wr(w, 181212, 0);
    wr(w, 181224, 7);
    wr(w, 181228, 1);                   /* day 1 */
    wr(w, 181232, 0);
    wr(w, 183812, 0);
    const uint8_t v11 = w[179341] != 0, v12 = w[179403] != 0, v13 = w[179390] != 0;
    const uint8_t v14 = w[179414] != 0;
    uint8_t v15 = 0;
    if (w[179395] == 1) v15 = w[179396] == 1;
    const uint8_t v16 = w[179434] != 0, v17 = w[179439] != 0, v18 = w[179432] != 0;
    const uint8_t v19 = w[179435] != 0;
    for (int k = 0; k < 8; k++)
        if (w[179276 + k] != 0 || w[179480 + k] != 0)
            w[188080 + k] = 1;
    memset(w + 178972, 0, 0x400);
    static const int k_set[] = {
        179288, 179289, 179290, 179292, 179293, 179294, 179296, 179298, 179301, 179302, 179305,
        179306, 179307, 179311, 179313, 179314, 179315, 179316, 179317, 179318, 179320, 179321,
        179322, 179346,
    };
    for (size_t k = 0; k < sizeof k_set / sizeof k_set[0]; k++) w[k_set[k]] = 1;
    if (keep == 2) {
        w[179325] = v29; w[179326] = v28; w[179329] = v27;
        w[179385] = v26; w[179425] = v25; w[179443] = v24;
    }
    w[179345] = v23; w[179461] = v22; w[179462] = v21; w[179473] = v20;
    w[179496] = !v4;
    w[179497] = v11; w[179498] = v12; w[179499] = v13; w[179500] = v14; w[179501] = v15;
    w[179502] = v16; w[179503] = v17; w[179504] = v18; w[179505] = v19;
    w[179344] = v5;
    wr(w, 181228, 1);
    for (int i = 0; i < 8; i++) {
        uint8_t *p = w + 184068 + 108 * i;
        wr(p, -4, 0); wr(p, 0, 0); wr(p, 4, 0); wr(p, 8, 0);
    }
    const int32_t n = rd(w, 178968);
    for (int32_t i = 0; i < n; i++) {
        const int32_t it = rd(w, 24 + 4 * i);
        for (int j = 0; j < 8; j++)
            if (it >> 6 == j + 3401) w[179480 + j] = 1;
    }
    if (w[179282] != 0) w[179477] = 1;
    work_clear_lists(w);
    day_end_day_start_reset((int32_t *)(void *)w);   /* FUN_0048d954 */
}

void scene_gameover_tick(void)
{
    uint8_t *w = work();
    if (w == NULL) return;
    if (s_exit_t <= 0) {
        if (s_step >= 8) s_exit_t = 1;
    } else {
        ++s_exit_t;
        if (s_exit_t == 10) fade_phase1_start(0, 17);          /* FUN_004526f5 */
        if (s_exit_t > 10 && fade_is_done()) {
            scene1_beat_set_flag(1);                           /* DAT_0438b928 = 1 */
            g_scene_state = rd(w, W_MODE_DW * 4) == 2 ? 0 : 1;
            /* FUN_0047357c: texture group 4 released — render side */
            worker_load_spawn();                               /* FUN_00452cde */
            fade_phase_out_start(0, 17);                       /* FUN_0045281c */
        }
    }
    if (s_wait != 0) {
        /* FUN_004808fb's wait: DAT_09643580 is only ever written 0 */
        s_wait = 0;
        ++s_step;
        return;
    }
    if (s_step == 1)
        scene1_intro_dialogue_start_single(0, g_day_end_short ? 1 : 3);
    if (!g_day_end_short && !g_day_end_ending) {
        ++s_step;
        return;
    }
    if (s_step == 5) {
        if (rd(w, W_MODE_DW * 4) == 2) {
            uint8_t *h = save_arena_base();
            const int32_t day = rd(w, W_DAY_DW * 4);
            const int off = rd(w, W_2DE34_DW * 4) != 0 ? 0x98c : 0x980;   /* DAT_056e60fc / 60f0 */
            if (h != NULL && rd(h, off) < day) {
                wr(h, off, day);
                save_io_commit_slot(-1);                       /* FUN_004905a8(-1) */
            }
        }
        scene_gameover_restart_reset(w, 1);                    /* FUN_0049d55c(w, 1) */
        g_pause_saved_mode = 2;                                /* DAT_06a499a8 = 2 */
    }
    int at7 = s_step == 7;
    if (s_step < 7) at7 = ++s_step == 7;
    if (at7) {
        if (rd(w, W_MODE_DW * 4) != 2) scene1_intro_dialogue_start_single(0, 2);
        ++s_step;
    }
}
