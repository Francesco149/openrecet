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
#include "scene_day_end.h"
#include "scene_title.h"
#include "scene1_actor_say.h"
#include "scene1_companion_ctrl.h"
#include "scene1_counter_menu.h"
#include "scene1_day_close.h"
#include "scene1_display_menu.h"
#include "scene1_particles_tick.h"
#include "scene1_player_ctrl.h"
#include "scene1_top_hud.h"
#include "sim.h"
#include "speech_bubble.h"
#include "tick.h"

static int32_t get_rng(void)   { return (int32_t)g_rng_seed; }
static int32_t get_scene(void) { return g_scene_state; }
static int32_t get_cc08(void)  { return player_ctrl_cc08(); }
static int32_t get_b1cc(void)  { return customer_service_b1cc(); }
static int32_t get_b524(void)  { return customer_service_b524(); }
static int32_t get_b534(void)  { return customer_service_b534(); }
static int32_t get_b544(void)  { return customer_service_b544(); }
static int32_t get_b584(void)  { return customer_service_round(); }
static int32_t get_b574(void)  { return customer_service_offer(); }
static int32_t get_bb8(void)   { return customer_service_player_ask(); }
static int32_t get_b5a8(void)  { return customer_service_b5a8(); }
static int32_t get_b56c(void)  { return customer_service_b56c(); }
static int32_t get_b520(void)  { return customer_service_b520(); }
static int32_t get_db054(void) { return scene1_companion_db054(); }
static int32_t get_ttfc(void) { return (int32_t)g_scene_title_anim.frame_counter; }
static int32_t get_dm_tab(void)  { return display_menu_dbg_cur_tab(); }
static int32_t get_dm_ntab(void) { return display_menu_dbg_num_tabs(); }
static int32_t get_dm_cur0(void) { return display_menu_dbg_tab_cursor0(); }
static int32_t get_dm_cnt0(void) { return display_menu_dbg_tab_count0(); }
static int32_t get_dm_l0(void)   { return display_menu_dbg_list(0); }
static int32_t get_dm_l2(void)   { return display_menu_dbg_list(2); }
static int32_t get_dm_l4(void)   { return display_menu_dbg_list(4); }
static int32_t get_b5d8(void)  { return customer_service_b5d8(); }
static int32_t get_b548(void)  { return customer_service_b548(); }
static int32_t get_cc04(void)  { return player_ctrl_cc04(); }
static int32_t get_b318(void)  { return customer_service_b318(); }
static int32_t get_b528(void)  { return customer_service_b528(); }
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

static int32_t get_b4e0(void)  { return day_close_b4e0(); }
static int32_t get_2eec(void)  { return day_close_variant(); }
static int32_t get_b764(void)  { return actor_say_age(); }
static int32_t get_b76c(void)  { return actor_say_msg(); }
static int32_t get_bage(void)  { return speech_bubble_age(); }
static int32_t get_db048(void) { return player_ctrl_db048(); }
static int32_t prec(int k) { const int32_t *r = player_ctrl_actor_record(0); return r ? r[k] : 0; }
static int32_t get_psel(void) { return prec(5); }
static int32_t get_pfac(void) { return prec(6); }
static int32_t get_px(void)   { return fbits(g_scene1_actor_pos[0][0]); }
static int32_t get_pz(void)   { return fbits(g_scene1_actor_pos[0][2]); }

static int32_t get_clkph(void)  { return fbits(scene1_top_hud_clock_phase()); }
static int32_t get_de_t(void)   { return day_end_frame(); }
static int32_t get_de_p(void)   { return day_end_profit_shown(); }
static int32_t get_de_pt(void)  { return day_end_profit(); }
static int32_t get_de_s(void)   { return day_end_score_shown(); }
static int32_t get_de_a(void)   { return day_end_after(); }
static int32_t get_de_r(void)   { return day_end_rating(); }

typedef struct { uint32_t va; const char *name; int32_t (*get)(void); } state_va_t;
static const state_va_t k_map[] = {
    { 0x6023a0u,  "rng (DAT_006023a0)",               get_rng   },
    { 0x438b1c0u, "scene state (DAT_0438b1c0)",       get_scene },
    { 0x438cc08u, "house mode cc08 (DAT_0438cc08)",   get_cc08  },
    { 0x438b1ccu, "cs load phase b1cc (DAT_0438b1cc)", get_b1cc },
    { 0x730b524u, "cs idle counter b524 (DAT_0730b524)", get_b524 },
    { 0x730b534u, "cs sub-state b534 (DAT_0730b534)", get_b534 },
    { 0x730b544u, "cs state timer b544 (DAT_0730b544)", get_b544 },
    { 0x730b584u, "cs haggle round b584 (DAT_0730b584)", get_b584 },
    { 0x730b574u, "cs customer price b574 (DAT_0730b574)", get_b574 },
    { 0x5c6bb8u,  "cs player price bb8 (DAT_005c6bb8)", get_bb8 },
    { 0x730b5a8u, "cs transaction kind b5a8 (DAT_0730b5a8)", get_b5a8 },
    { 0x730b56cu, "cs customer b56c (DAT_0730b56c)", get_b56c },
    { 0x730b520u, "cs leave phase b520 (DAT_0730b520)", get_b520 },
    { 0x56db054u, "companion phase counter db054 (DAT_056db054)", get_db054 },
    { 0x9643518u, "title frame counter (DAT_09643518)", get_ttfc },
    { 0x734b968u, "display menu tab (DAT_0734b968)", get_dm_tab },
    { 0x731f404u, "display menu tab count (DAT_0731f404)", get_dm_ntab },
    { 0x7337850u, "display menu tab-0 cursor (DAT_07337850)", get_dm_cur0 },
    { 0x7337210u, "display menu tab-0 entries (DAT_07337210)", get_dm_cnt0 },
    { 0x731f598u, "display menu list[0] (DAT_0731f598)", get_dm_l0 },
    { 0x731f5a0u, "display menu list[2] (DAT_0731f5a0)", get_dm_l2 },
    { 0x731f5a8u, "display menu list[4] (DAT_0731f5a8)", get_dm_l4 },
    { 0x730b5d8u, "cs oder / want-list index b5d8 (DAT_0730b5d8)", get_b5d8 },
    { 0x730b548u, "cs text-reveal budget b548 (DAT_0730b548)", get_b548 },
    { 0x438cc04u, "house sub-mode cc04 (DAT_0438cc04)", get_cc04 },
    { 0x730b318u, "cs queue head b318 (DAT_0730b318)", get_b318 },
    { 0x730b528u, "cs customers advanced b528 (DAT_0730b528)", get_b528 },
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
    { 0x438b4e0u, "day-close b4e0 (DAT_0438b4e0)",     get_b4e0 },
    { 0x74b2eecu, "day-close variant (DAT_074b2eec)",  get_2eec },
    { 0x438b764u, "actor-say age (DAT_0438b764)",      get_b764 },
    { 0x438b76cu, "actor-say msg (DAT_0438b76c)",      get_b76c },
    { 0x648258u,  "speech bubble age (DAT_00648258)",  get_bage },
    { 0x438b7d4u, "clock phase bits (DAT_0438b7d4)",   get_clkph },
    { 0x74b2f58u, "day-end frame (DAT_074b2f58)",      get_de_t },
    { 0x74b2f1cu, "day-end profit shown (DAT_074b2f1c)", get_de_p },
    { 0x74b2f20u, "day-end profit (DAT_074b2f20)",     get_de_pt },
    { 0x74b2f24u, "day-end score shown (DAT_074b2f24)", get_de_s },
    { 0x74b2f2cu, "day-end after-count (DAT_074b2f2c)", get_de_a },
    { 0x74b2f34u, "day-end rating (DAT_074b2f34)",     get_de_r },
    { 0x56db048u, "player pose hold db048 (DAT_056db048)", get_db048 },
    { 0x56daafcu, "player anim sel (DAT_056daafc)",    get_psel },
    { 0x56dab00u, "player facing (DAT_056dab00)",      get_pfac },
    { 0x56da1d8u, "player x bits (DAT_056da1d8)",      get_px },
    { 0x56da1e0u, "player z bits (DAT_056da1e0)",      get_pz },
};
#define K_N ((int)(sizeof k_map / sizeof k_map[0]))

/* Any dword of the working save banks (DAT_044e3798 + slot·0x2dfc8 + off) is
 * mirrored generically: the port's save_work arena has the engine layout. */
#define WORK_BANK_VA   0x44e3798u
#define WORK_BANK_SIZE 0x2dfc8u
#define WORK_BANKS     8u
typedef struct { uint32_t va; int32_t (*get)(void); } sel_t;
static sel_t s_sel[16];
static int s_nsel;

static int32_t bank_dword(uint32_t va)
{
    uint32_t rel = va - WORK_BANK_VA;
    const uint32_t *w = save_work_dwords_at((int)(rel / WORK_BANK_SIZE));
    return w ? (int32_t)w[(rel % WORK_BANK_SIZE) / 4] : 0;
}

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
        int in_bank = va >= WORK_BANK_VA && va < WORK_BANK_VA + WORK_BANKS * WORK_BANK_SIZE &&
                      ((va - WORK_BANK_VA) % WORK_BANK_SIZE) % 4 == 0;
        if (k == K_N && !in_bank) {
            if (bad) *bad = va;
            s_nsel = 0;
            return -1;
        }
        if (s_nsel < (int)(sizeof s_sel / sizeof s_sel[0]))
            s_sel[s_nsel++] = (sel_t){ va, k < K_N ? k_map[k].get : NULL };
    }
    return s_nsel;
}

void state_va_log(FILE *fp, uint32_t frame)
{
    if (!fp || s_nsel == 0) return;
    fprintf(fp, "post %u", (unsigned)frame);
    for (int i = 0; i < s_nsel; i++)
        fprintf(fp, " %x=%d", (unsigned)s_sel[i].va,
                (int)(s_sel[i].get ? s_sel[i].get() : bank_dword(s_sel[i].va)));
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
    static char out[2048];
    size_t o = 0;
    out[0] = '\0';
    for (int k = 0; k < K_N && o < sizeof out; k++)
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%x %s", k ? ", " : "",
                              (unsigned)k_map[k].va, k_map[k].name);
    if (o < sizeof out)
        snprintf(out + o, sizeof out - o, ", any work-bank dword 44e3798..");
    return out;
}
