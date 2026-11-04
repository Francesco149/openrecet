/* rh_play.c — free-play capture support (docs/plans/free-play-capture.md FPC-4).
 *
 * --cut      end the run at the tick boundary after
 *              - a save commit FUN_004905a8(slot >= 0) whose saved bank's day (dword 0xb0fb) is past the
 *                day the game started on (exit 10, "cut-save"), or
 *              - a game start after the process's first one: the title's start counter dword_964351C
 *                rising 0 -> 1 (Continue / New Game / New Game+ / survival; the title init FUN_0049a3a3
 *                zeroes it) (exit 11, "cut-start").
 *            The cut ends at the same point as a plain `--frames N` replay (the frames check), so the
 *            piece's trace replays to the identical state; <run>\cut.json carries the reason and the
 *            RESUME intent (the --preamble of the next piece).
 * --preamble CODE[:BANK[:TOGGLE]]
 *            drive the title menu to that game start (main-menu item code: 1 Continue, 4 New Game+,
 *            6 survival with TOGGLE, 0 / 5 new game; BANK = the save bank for the picker codes), reading
 *            retail's menu state each tick: taps alternate with release ticks so every tap is an edge.
 *            Turbo (no realtime pacing) until the game start, then --record hands over to real input.
 *            The taps are recorded / flattened like any input: the trace never depends on this driver.
 *
 * Retail state (IDA all_ida.c, FUN_0049a59e title update ~L111700..112450):
 *   main menu  items dword_9643358[dword_9643510], cursor dword_9643540; input runs when the submenu
 *              fade dword_9643520 == 0, no selection anim dword_9643544, no survival menu dword_9643550;
 *              that branch increments dword_9643518 every tick it runs (attract movie at 7110)
 *   picker     dword_9643524 == 1 at fade 10, no close dword_9643528, no scroll anim 9538 / 953c;
 *              cursor dword_9643530 indexes the bank list dword_9643380[dword_005d1bbc]
 *   survival   dword_9643550 == 8 (+ no 9554 / 955c anim): 0xC edges toggle dword_9643558, A confirms
 *   edges      the game's dword_73dddd4 = pressed | held-repeat << 16; a tap of UP 0x4 / DOWN 0x8 /
 *              A 0x10 surfaces in both on its press tick */
#include "rhost.h"

#define VA_SAVE_COMMIT 0x4905a8u
#define R(va) (*(volatile int32_t *)(uintptr_t)(va))
#define SAVE_ARENA_BANK0 0x56e6280u
#define WORK_BANK0       0x44e3798u
#define BANK_STRIDE      0x2dfc8u
#define BANK_DAY_OFF     (0xb0fbu * 4)
#define T_START   0x964351cu   /* game-start counter (0 at the title; 1.. once a start is chosen) */
#define T_IDLE    0x9643518u
#define T_FADE    0x9643520u
#define T_SUB     0x9643524u
#define T_CLOSE   0x9643528u
#define T_PCUR    0x9643530u
#define T_SCROLL1 0x9643538u
#define T_SCROLL3 0x964353cu
#define T_MCUR    0x9643540u
#define T_SEL     0x9643544u
#define T_SURV    0x9643550u
#define T_SURVOUT 0x9643554u
#define T_SURVTGL 0x9643558u
#define T_SURVGO  0x964355cu
#define T_ITEMS   0x9643358u
#define T_NITEMS  0x9643510u
#define T_LIST    0x9643380u
#define T_NLIST   0x5d1bbcu
#define T_LASTBANK 0x56e578cu
#define DAT_WORKSLOT 0x438b1e0u
#define K_UP 0x4
#define K_DOWN 0x8
#define K_A 0x10
#define PREAMBLE_MAX_FRAMES 6000u

static struct {
    int preamble;                         /* driving the title (until the first game start) */
    int code, bank, toggle;               /* --preamble intent */
    int tap; int32_t idle_prev; uint32_t taps;
    int32_t prev_start; int starts;
    int s_code, s_bank, s_toggle;         /* the first game start */
    int day_pending; int32_t start_day;
    int cut;                              /* 0 / 10 save / 11 start */
    uint32_t cut_frame; int cut_slot, cut_day, c_code, c_bank, c_toggle;
    int saves;
} P = { .start_day = -1, .cut_slot = -1 };
static char g_status[160];

static int picker_code(int c) { return c == 1 || c == 4 || c == 6; }

int rh_play_preamble_active(void) { return P.preamble; }

/* one tap per two ticks: the release tick makes the next tap an edge again */
uint16_t rh_play_preamble_mask(void)
{
    if (g_frame > PREAMBLE_MAX_FRAMES)
        rh_fatal("preamble %d:%d:%d: no game start by f%u (%u taps)", P.code, P.bank, P.toggle, g_frame, P.taps);
    int32_t idle = R(T_IDLE); int ran = idle != P.idle_prev; P.idle_prev = idle;
    if (P.tap) { P.tap = 0; return 0; }
    if (R(DAT_SCENE) != 0 || R(T_START) != 0) return 0;
    uint16_t m = 0;
    if (R(T_FADE) == 0 && R(T_SEL) == 0 && R(T_SURV) == 0 && ran && idle > 1 && idle < 7000) {
        int n = R(T_NITEMS), cur = R(T_MCUR), have = 0;
        for (int i = 0; i < n && i < 16; i++) have |= R(T_ITEMS + 4 * i) == P.code;
        if (!have) rh_fatal("preamble: the title menu has no item %d (%d items)", P.code, n);
        m = R(T_ITEMS + 4 * cur) == P.code ? K_A : K_DOWN;
    } else if (P.code == 6 && R(T_SURV) == 8 && R(T_SURVOUT) == 0 && R(T_SURVGO) == 0 && R(T_FADE) == 0) {
        m = R(T_SURVTGL) == P.toggle ? K_A : K_UP;
    } else if (R(T_SUB) == 1 && R(T_FADE) == 10 && R(T_CLOSE) == 0 && R(T_SCROLL1) == 0 && R(T_SCROLL3) == 0) {
        int n = R(T_NLIST), cur = R(T_PCUR), want = -1;
        for (int i = 0; i < n && i < 100; i++) if (R(T_LIST + 4 * i) == P.bank) { want = i; break; }
        if (want < 0) rh_fatal("preamble: bank %d is not in the picker list (%d entries)", P.bank, n);
        if (cur == want && !R(SAVE_ARENA_BANK0 + (uint32_t)P.bank * BANK_STRIDE + 8))
            rh_fatal("preamble: bank %d is empty", P.bank);
        m = cur < want ? K_DOWN : cur > want ? K_UP : K_A;
    }
    if (m) { P.tap = 1; P.taps++; }
    return m;
}

/* FUN_004905a8(slot): copy the working bank to save bank `slot` (-1: header only), write save.dat */
static void *__cdecl save_commit_hook(int slot)
{
    void *r = ((void *(__cdecl *)(int))VA_SAVE_COMMIT)(slot);
    P.saves++;
    if (slot >= 0 && slot < 100) {
        int day = R(SAVE_ARENA_BANK0 + (uint32_t)slot * BANK_STRIDE + BANK_DAY_OFF);
        int cut = O.cut && !P.cut && P.starts > 0 && !P.day_pending && day > P.start_day;
        rh_log("play: save commit f%u slot=%d day=%d start_day=%d%s\n", g_frame, slot, day, P.start_day, cut ? " -> cut" : "");
        if (cut) { P.cut = 10; P.cut_frame = g_frame; P.cut_slot = slot; P.cut_day = day; }
    } else
        rh_log("play: save commit f%u slot=%d (header)\n", g_frame, slot);
    return r;
}

void rh_play_install(void)
{
    if (O.preamble) {
        if (sscanf(O.preamble, "%d:%d:%d", &P.code, &P.bank, &P.toggle) < 1 || P.code < 0 || P.code > 8 ||
            (picker_code(P.code) && (P.bank < 0 || P.bank > 99)))
            rh_fatal("--preamble %s: want CODE[:BANK[:TOGGLE]] (1 / 4 / 6 need a bank 0..99)", O.preamble);
        P.preamble = 1;
    }
    if (O.cut || O.record) {
        int n = redirect_calls(VA_SAVE_COMMIT, save_commit_hook);
        if (n != 6) rh_fatal("save commit FUN_004905a8: %d call sites (want 6)", n);
    }
    rh_log("play: cut=%d preamble=%s\n", O.cut, O.preamble ? O.preamble : "-");
}

/* after each tick: the game-start edge, the start day, the preamble's handover */
void rh_play_after_tick(void)
{
    int32_t s = R(T_START);
    if (P.prev_start == 0 && s > 0) {
        int code = R(T_ITEMS + 4 * R(T_MCUR)), bank = picker_code(code) ? R(T_LASTBANK) : -1, tgl = code == 6 ? R(T_SURVTGL) : 0;
        P.starts++;
        rh_log("play: game start #%d f%u code=%d bank=%d toggle=%d\n", P.starts, g_frame, code, bank, tgl);
        if (P.starts == 1) {
            P.s_code = code; P.s_bank = bank; P.s_toggle = tgl; P.day_pending = 1;
            if (P.preamble) {
                if (code != P.code || bank != P.bank || tgl != P.toggle)
                    rh_fatal("preamble %d:%d:%d started %d:%d:%d", P.code, P.bank, P.toggle, code, bank, tgl);
                P.preamble = 0;
                rh_log("play: preamble done f%u (%u taps): real input\n", g_frame, P.taps);
                rh_play_handover();
            }
        } else if (O.cut && !P.cut) {
            P.cut = 11; P.cut_frame = g_frame; P.c_code = code; P.c_bank = bank; P.c_toggle = tgl;
        }
    }
    P.prev_start = s;
    if (P.day_pending && R(DAT_SCENE) != 0) {   /* in game: the working bank holds the started day */
        P.start_day = R(WORK_BANK0 + (uint32_t)R(DAT_WORKSLOT) * BANK_STRIDE + BANK_DAY_OFF);
        P.day_pending = 0;
        rh_log("play: in game f%u scene=%d day=%d\n", g_frame, (int)R(DAT_SCENE), P.start_day);
    }
}

int rh_play_cut_pending(void) { return P.cut; }
const char *rh_play_cut_reason(void) { return P.cut == 10 ? "cut-save" : P.cut == 11 ? "cut-start" : NULL; }

/* <run>\cut.json: what ended the piece + the next piece's --preamble (resume) */
void rh_play_finish(const char *why)
{
    if (!O.cut) return;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\cut.json", O.rundir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"reason\": \"%s\", \"frame\": %u, \"starts\": %d, \"saves\": %d, \"start\": \"%d:%d:%d\", \"start_day\": %d",
            why, g_frame, P.starts, P.saves, P.s_code, P.s_bank, P.s_toggle, P.start_day);
    if (P.cut == 10)   /* resume the bank just saved, the way this game started (survival saves load from 6) */
        fprintf(f, ", \"slot\": %d, \"day\": %d, \"resume\": \"%d:%d:%d\"", P.cut_slot, P.cut_day,
                P.s_code == 6 ? 6 : 1, P.cut_slot, P.s_code == 6 ? P.s_toggle : 0);
    else if (P.cut == 11)
        fprintf(f, ", \"resume\": \"%d:%d:%d\"", P.c_code, P.c_bank, P.c_toggle);
    fprintf(f, "}\n");
    fclose(f);
}

const char *rh_play_status(void)
{
    snprintf(g_status, sizeof g_status, "starts=%d saves=%d start_day=%d cut=%s", P.starts, P.saves, P.start_day,
             P.cut == 10 ? "save" : P.cut == 11 ? "start" : "-");
    return g_status;
}
