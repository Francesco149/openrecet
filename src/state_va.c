/*
 * state_va.c — see state_va.h.  Add a row when a new port mirror of a retail
 * global is needed for a frame-by-frame diff; keep the mapping exact (a row is a
 * claim that the port variable IS that engine global, value for value).
 */
#include "state_va.h"

#include <stdlib.h>
#include <string.h>

#include "customer_service.h"
#include "floor_layout.h"
#include "rng.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"
#include "scene1_companion_ctrl.h"
#include "scene1_counter_menu.h"
#include "scene1_particles_tick.h"
#include "scene1_player_ctrl.h"
#include "scene1_top_hud.h"
#include "sim.h"
#include "tick.h"

static int32_t get_rng(void)   { return (int32_t)g_rng_seed; }
static int32_t get_scene(void) { return g_scene_state; }
static int32_t get_cc08(void)  { return player_ctrl_cc08(); }
static int32_t get_b1cc(void)  { return customer_service_b1cc(); }
static int32_t get_b524(void)  { return customer_service_b524(); }
static int32_t get_gsim(void)  { return (int32_t)g_sim_frame_count; }
static int32_t get_tickn(void) { return (int32_t)g_tick.frame_count; }
static int32_t get_tprev(void) { return (int32_t)g_tick.prev_ms; }
static int32_t get_b74c(void)  { return counter_menu_slide(); }
static int32_t get_b754(void)  { return counter_menu_timer(); }
static int32_t get_cc0c(void)  { return counter_menu_cursor(); }
static int32_t get_ttctr(void) { return scene1_top_hud_tooltip_ctr(); }
static int32_t get_fl_w(void)  { return g_floor_w; }
static int32_t get_fl_h(void)  { return g_floor_h; }
static int32_t get_fl_n(void)  { return g_floor_ncells; }
static int32_t get_fl_tot(void) { return g_floor_rooms_total; }
static int32_t fbits(float f) { union { float f; int32_t i; } u = { .f = f }; return u.i; }
static int32_t get_wst(void)  { return scene1_companion_wander_state(); }
static int32_t get_wt(void)   { return scene1_companion_wander_t(); }
static int32_t get_wtx(void)  { return fbits(scene1_companion_wander_tx()); }
static int32_t get_wtz(void)  { return fbits(scene1_companion_wander_tz()); }
static int32_t get_tx(void)   { return fbits(g_scene1_actor_pos[2][0]); }
static int32_t get_ty(void)   { return fbits(g_scene1_actor_pos[2][1]); }
static int32_t get_tz(void)   { return fbits(g_scene1_actor_pos[2][2]); }
static int32_t tear_rec(int k) { const int32_t *r = player_ctrl_actor_record(2); return r ? r[k] : 0; }
static int32_t get_tsel(void) { return tear_rec(5); }
static int32_t get_tfac(void) { return tear_rec(6); }
static int32_t get_tfrm(void) { return tear_rec(4); }
static int32_t get_mexp(void)
{
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    return w ? (int32_t)w[SAVE_BANK_FIELD_MERCHANT_EXP] : 0;
}

typedef struct { uint32_t va; const char *name; int32_t (*get)(void); } state_va_t;
static const state_va_t k_map[] = {
    { 0x6023a0u,  "rng (DAT_006023a0)",               get_rng   },
    { 0x438b1c0u, "scene state (DAT_0438b1c0)",       get_scene },
    { 0x438cc08u, "house mode cc08 (DAT_0438cc08)",   get_cc08  },
    { 0x438b1ccu, "cs load phase b1cc (DAT_0438b1cc)", get_b1cc },
    { 0x730b524u, "cs idle counter b524 (DAT_0730b524)", get_b524 },
    { 0x438b8ccu, "sim frame counter gsim (DAT_0438b8cc)", get_gsim },
    { 0x73dfcfcu, "tick frame_count (DAT_073dfcfc)",  get_tickn },
    { 0x73de61cu, "tick prev_ms (DAT_073de61c)",      get_tprev },
    { 0x438b74cu, "counter-menu slide b74c (DAT_0438b74c)", get_b74c },
    { 0x438b754u, "counter-menu timer b754 (DAT_0438b754)", get_b754 },
    { 0x438cc0cu, "counter-menu cursor cc0c (DAT_0438cc0c)", get_cc0c },
    { 0x529704u,  "top-HUD tooltip counter (DAT_00529704)", get_ttctr },
    { 0x5cc2e8u,  "floor layout W (DAT_005cc2e8)",    get_fl_w  },
    { 0x5cc2ecu,  "floor layout H (DAT_005cc2ec)",    get_fl_h  },
    { 0x73e1540u, "floor layout cells (DAT_073e1540)", get_fl_n },
    { 0x438b4b0u, "floor rooms total (DAT_0438b4b0)", get_fl_tot },
    { 0x450fb8cu, "work slot 0 merchant exp (DAT_0450fb8c)", get_mexp },
    { 0x438be6cu, "Tear wander state (DAT_0438be6c)",  get_wst },
    { 0x438be70u, "Tear wander timer (DAT_0438be70)",  get_wt },
    { 0x438be74u, "Tear wander target x bits (DAT_0438be74)", get_wtx },
    { 0x438be78u, "Tear wander target z bits (DAT_0438be78)", get_wtz },
    { 0x56da1f0u, "Tear x bits (DAT_056da1f0)",        get_tx },
    { 0x56da1f4u, "Tear y bits (DAT_056da1f4)",        get_ty },
    { 0x56da1f8u, "Tear z bits (DAT_056da1f8)",        get_tz },
    { 0x56dab54u, "Tear anim sel (DAT_056dab54)",      get_tsel },
    { 0x56dab58u, "Tear facing (DAT_056dab58)",        get_tfac },
    { 0x56dab50u, "Tear anim frame (DAT_056dab50)",    get_tfrm },
};
#define K_N ((int)(sizeof k_map / sizeof k_map[0]))

static const state_va_t *s_sel[16];
static int s_nsel;

int state_va_select(const char *list, uint32_t *bad)
{
    /* no strtok: the caller (main.c parse_cmdline) is mid-strtok over argv */
    const char *t = list ? list : "";
    s_nsel = 0;
    for (; *t; t += (*t == ',')) {
        char *end;
        uint32_t va = (uint32_t)strtoul(t, &end, 16);
        if (end == t) break;
        t = end;
        int k = 0;
        while (k < K_N && k_map[k].va != va) k++;
        if (k == K_N) {
            if (bad) *bad = va;
            s_nsel = 0;
            return -1;
        }
        if (s_nsel < (int)(sizeof s_sel / sizeof s_sel[0]))
            s_sel[s_nsel++] = &k_map[k];
    }
    return s_nsel;
}

void state_va_log(FILE *fp, uint32_t frame)
{
    if (!fp || s_nsel == 0) return;
    fprintf(fp, "post %u", (unsigned)frame);
    for (int i = 0; i < s_nsel; i++)
        fprintf(fp, " %x=%d", (unsigned)s_sel[i]->va, (int)s_sel[i]->get());
    fputc('\n', fp);
#if defined(__i386__) || defined(__x86_64__)
    /* x87 control word (rhost logs retail's per frame): "cw N xxxx" on change */
    static unsigned s_last_cw = 0x10000u;
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    if (cw != s_last_cw) {
        fprintf(fp, "cw %u %04x\n", (unsigned)frame, (unsigned)cw);
        s_last_cw = cw;
    }
#endif
}

const char *state_va_known(void)
{
    static char out[512];
    size_t o = 0;
    out[0] = '\0';
    for (int k = 0; k < K_N && o < sizeof out; k++)
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%x %s", k ? ", " : "",
                              (unsigned)k_map[k].va, k_map[k].name);
    return out;
}
