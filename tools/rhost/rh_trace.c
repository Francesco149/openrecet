/* rh_trace.c — input for rhost: scenario trace replay, human recording, flattening.
 *
 * Replay: tests/scenarios/<s>/trace.jsonl through the port's own parser/state
 * machine (src/input_segtrace.c). Anchor-segmented traces resolve their {wait}s
 * against the anchor predicates below (C port of the Frida agent's anchorTick);
 * a FLAT trace (no {wait}) is plain frame-absolute input from boot. Because rhost
 * is deterministic, every anchor resolves to a fixed frame, so --flatten writes the
 * replay as a flat trace that needs no anchors and replays identically.
 * Honoured ops: frame/buttons, wait, esc, rngseed. Cross-process pins
 * ({phasepin} {gsimpin} {bgnpcpin} {playtimepin} {*loadpin}) are ignored: they
 * existed to align two processes and retail runs natural here.
 *
 * Recording (--record, realtime): the real DInput poll result (slot-0 mask) is
 * logged per change, ESC keydowns via a WndProc subclass → a flat trace. */
#include "rhost.h"
#include "input_segtrace.h"

static struct input_segtrace g_seg; static int g_seg_on;
static uint32_t g_done_at, g_last_seg_change, g_last_seg = 0xffffffffu;
static long g_anchor_count, g_esc_count;
static HWND g_hwnd;
static FILE *g_flat;                     /* --flatten / --record output */
static uint16_t g_flat_last = 0xffff;
static char g_status[256];

static void flat_open(const char *path, const char *why)
{
    g_flat = fopen(path, "w");
    if (!g_flat) { rh_log("cannot write %s\n", path); return; }
    fprintf(g_flat, "# rhost flat trace (%s): frame-absolute from boot; replays deterministically in rhost\n", why);
    if (O.has_seed) fprintf(g_flat, "{\"rngseed\": [0, %u]}\n", O.seed);
}

static BOOL CALLBACK find_wnd(HWND h, LPARAM lp)
{ DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(h)) { *(HWND *)lp = h; return FALSE; } return TRUE; }
static HWND game_hwnd(void) { if (!g_hwnd) EnumWindows(find_wnd, (LPARAM)&g_hwnd); return g_hwnd; }

static void seg_esc(void *u)
{
    (void)u;
    HWND h = game_hwnd();
    if (h) { PostMessageA(h, WM_KEYDOWN, VK_ESCAPE, 0x00010001); PostMessageA(h, WM_KEYUP, VK_ESCAPE, 0xC0010001); }
    g_esc_count++;
    if (g_flat) fprintf(g_flat, "{\"esc\": %u}\n", g_frame);
}
static void seg_rngseed(uint32_t v, void *u)
{
    (void)u; G32(DAT_RNG) = v;
    if (g_flat) fprintf(g_flat, "{\"rngseed\": [%u, %u]}\n", g_frame, v);
}

/* ---- anchors: C port of tools/frida/openrecet-agent.js anchorTick ---- */
static struct { int init, scene, loading, reveal, revflag, linep, conv, blink, cc08, pause, tpick,
                 fx, savep, encp, optp, itemp, tset, tenc, trec, tsurv; } A;
static int fx_alpha(int dlg)
{
    if (!dlg) return 0;
    int maxa = 0;
    for (int i = 2; i <= 31; i++) {
        uint32_t sb = 0x073a3e70u + i * 0x70u;
        if (*(volatile int32_t *)(uintptr_t)(sb + 0x2c) == 0) continue;
        int a = (int)GF(sb + 0x48);
        if (a < 0) a = 0; else if (a > 255) a = 255;
        if (a > maxa) maxa = a;
    }
    return maxa;
}
static void anchor_fire(const char *name, uint32_t frame)
{
    g_anchor_count++;
    if (O.log_anchors) rh_log("anchor %s @%u rng=%08x\n", name, frame, G32(DAT_RNG));
    if (g_seg_on) input_segtrace_on_anchor(&g_seg, name, frame);
}
static void anchors_tick(uint32_t frame)
{
    #define R(va) (*(volatile int32_t *)(uintptr_t)(va))
    int scene = R(0x438b1c0);
    int loading = R(0x6a49958) != 0 || R(0x6a49960) != 0;
    int dlg = R(0x438b1c8) == 1;
    int reveal = R(0x73a3e00), revflag = R(0x73a3e04);
    int linep = dlg && R(0x73a6a38) >= 0;
    int conv = R(0x56daafc), blink = conv == 6 && R(0x56daaf8) == 1;
    int cc08 = R(0x438cc08), pause = R(0x438b150) != 0;
    int tsub = (scene == 0 && R(0x9643520) == 10) ? R(0x9643524) : 0;
    int tpick = tsub == 1, tset = tsub == 2, tenc = tsub == 3, trec = tsub == 4;
    int tsurv = scene == 0 && R(0x9643520) == 0 && R(0x9643524) == 0 && R(0x9643550) == 8;
    int fx = fx_alpha(dlg);
    int savep = 0, encp = 0, optp = 0, itemp = 0;
    if (scene == 9 && R(0x74b2880) == 10) {
        int sel = R(0x74b2878);
        if (sel >= 0 && sel < 8) { int t = R(0x74b2844 + sel * 4); savep = t == 3; encp = t == 6; optp = t == 2; itemp = t == 1; }
    }
    if (!A.init) {
        A.init = 1; anchor_fire("BOOT", frame);
    } else {
        if (A.scene == 0 && scene == 1) anchor_fire("NEW_GAME", frame);
        if (!A.loading && loading) anchor_fire("LOADING_START", frame);
        if (A.loading && !loading) anchor_fire("LOADING_END", frame);
        if (!(A.scene == 1 && !A.loading) && (scene == 1 && !loading)) anchor_fire("HOUSE_FREEROAM", frame);
        if (A.scene != 6 && scene == 6) anchor_fire("MARKET_ENTER", frame);
        if (A.cc08 != 4 && cc08 == 4) anchor_fire("CUSTOMER_SERVICE_ENTER", frame);
        if (dlg && reveal == 1 && A.reveal != 1) anchor_fire("TEXT_ANIM_START", frame);
        if (dlg && revflag != 0 && A.revflag == 0) anchor_fire("TEXT_ANIM_END", frame);
        if (A.linep && !linep) anchor_fire("DLG_LINE_CLEAR", frame);
        if (!A.linep && linep) anchor_fire("DLG_LINE_SHOW", frame);
        if (A.conv != 6 && conv == 6) anchor_fire("CONV_POSE_START", frame);
        if (A.conv == 6 && conv != 6) anchor_fire("CONV_POSE_END", frame);
        if (!A.blink && blink) anchor_fire("CONV_POSE_BLINK", frame);
        if (!A.pause && pause) anchor_fire("PAUSE_OPEN", frame);
        if (A.pause && !pause) anchor_fire("PAUSE_CLOSE", frame);
        if (A.loading && !loading && scene == 9) anchor_fire("PAUSE_READY", frame);
        if (A.scene != 0 && scene == 0) anchor_fire("TITLE_RETURN", frame);
        if (A.fx == 0 && fx > 0) anchor_fire("EXTRA_SPRITE_START", frame);
        if (fx >= 255 && A.fx < 255) anchor_fire("EXTRA_SPRITE_FADED_IN", frame);
        if (A.fx >= 255 && fx < 255 && fx > 0) anchor_fire("EXTRA_SPRITE_FADEOUT", frame);
        if (A.fx > 0 && fx == 0) anchor_fire("EXTRA_SPRITE_END", frame);
        if (!A.savep && savep) anchor_fire("SAVE_PICKER_READY", frame);
        if (!A.encp && encp) anchor_fire("ENCYCLOPEDIA_READY", frame);
        if (!A.optp && optp) anchor_fire("OPTIONS_READY", frame);
        if (!A.itemp && itemp) anchor_fire("ITEMS_READY", frame);
        if (!A.tpick && tpick) anchor_fire("TITLE_PICKER_READY", frame);
        if (!A.tset && tset) anchor_fire("TITLE_SETTINGS_READY", frame);
        if (!A.tenc && tenc) anchor_fire("TITLE_ENCYCLOPEDIA_READY", frame);
        if (!A.trec && trec) anchor_fire("TITLE_RECORDS_READY", frame);
        if (!A.tsurv && tsurv) anchor_fire("TITLE_SURVIVAL_READY", frame);
    }
    A.scene = scene; A.loading = loading; A.reveal = reveal; A.revflag = revflag; A.linep = linep;
    A.conv = conv; A.blink = blink; A.cc08 = cc08; A.pause = pause; A.tpick = tpick;
    A.fx = fx; A.savep = savep; A.encp = encp; A.optp = optp; A.itemp = itemp;
    A.tset = tset; A.tenc = tenc; A.trec = trec; A.tsurv = tsurv;
    #undef R
}

/* ---- legacy "<frame> <hexmask>" script ---- */
static struct { uint32_t frame; uint16_t mask; } *g_m; static int g_nm, g_im; static uint16_t g_msticky;

/* ---- recording: WndProc subclass logs ESC ---- */
static WNDPROC g_orig_wp;
static LRESULT CALLBACK rec_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN && w == VK_ESCAPE && !(l & 0x40000000)) rh_trace_record_esc(g_frame);
    /* closing the window ends the recording at THIS frame, before retail's exit
     * prompt/shutdown mutate state, so a replay to the same frame compares equal */
    if (m == WM_CLOSE) rh_finish("window-closed");
    return CallWindowProcA(g_orig_wp, h, m, w, l);
}

void rh_trace_init(void)
{
    if (O.trace) {
        if (!input_segtrace_load(O.trace, &g_seg)) rh_fatal("trace parse failed: %s", O.trace);
        input_segtrace_set_rngseed_cb(&g_seg, seg_rngseed, NULL);
        input_segtrace_set_esc_cb(&g_seg, seg_esc, NULL);
        g_seg_on = 1;
        rh_log("trace %s: %u segments\n", O.trace, (unsigned)g_seg.n_segs);
    } else if (O.masks) {
        FILE *f = fopen(O.masks, "r"); unsigned fr, m; int cap = 0;
        while (f && fscanf(f, "%u %x", &fr, &m) == 2) {
            if (g_nm == cap) { cap = cap ? cap * 2 : 256; g_m = realloc(g_m, cap * sizeof *g_m); }
            g_m[g_nm].frame = fr; g_m[g_nm].mask = (uint16_t)m; g_nm++;
        }
        if (f) fclose(f); else rh_fatal("cannot read %s", O.masks);
    }
    if (O.record) flat_open(O.record, "human recording");
    else if (O.flatten) flat_open(O.flatten, O.trace ? O.trace : "mask script");
}

/* called from the input hook (pre-sim) — returns the scripted mask */
uint16_t rh_trace_mask(uint32_t frame, int *have)
{
    uint16_t m = 0;
    *have = 1;
    if (g_seg_on) m = input_segtrace_tick(&g_seg, frame, NULL, NULL);
    else if (g_nm) { while (g_im < g_nm && g_m[g_im].frame <= frame) g_msticky = g_m[g_im++].mask; m = g_msticky; }
    else *have = 0;
    if (*have && g_flat && !O.record && m != g_flat_last) { fprintf(g_flat, "{\"frame\": %u, \"buttons\": \"0x%04x\"}\n", frame, m); g_flat_last = m; }
    return m;
}

void rh_trace_record(uint32_t frame, uint16_t mask)
{
    if (!g_flat) return;
    if (!g_orig_wp && game_hwnd()) g_orig_wp = (WNDPROC)(LONG_PTR)SetWindowLongA(g_hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)rec_wndproc);
    if (mask != g_flat_last) { fprintf(g_flat, "{\"frame\": %u, \"buttons\": \"0x%04x\"}\n", frame, mask); fflush(g_flat); g_flat_last = mask; }
}
void rh_trace_record_esc(uint32_t frame) { if (g_flat) { fprintf(g_flat, "{\"esc\": %u}\n", frame); fflush(g_flat); } }

void rh_trace_after_tick(uint32_t frame)
{
    anchors_tick(frame);
    if (g_seg_on && g_seg.cur_seg != g_last_seg) { g_last_seg = g_seg.cur_seg; g_last_seg_change = frame; }
    if (g_seg_on && !g_done_at && g_seg.n_segs && g_seg.cur_seg + 1 >= g_seg.n_segs &&
        g_seg.cur_entry >= g_seg.segs[g_seg.cur_seg].n_entries)
        g_done_at = frame + O.tail;
    if (!g_seg_on && g_nm && !g_done_at && g_im >= g_nm) g_done_at = frame + O.tail;
}
int rh_trace_done(uint32_t frame) { return g_done_at && frame >= g_done_at; }
/* an anchor-segmented trace whose current {wait} never resolves would spin forever */
int rh_trace_stalled(uint32_t frame) { return g_seg_on && !g_done_at && O.stall_frames && frame - g_last_seg_change > O.stall_frames; }

void rh_trace_finish(void)
{
    if (g_flat) { fprintf(g_flat, "{\"frame\": %u, \"buttons\": \"0x%04x\"}\n", g_frame, g_flat_last == 0xffff ? 0 : g_flat_last); fclose(g_flat); g_flat = NULL; }
}

const char *rh_trace_status(void)
{
    if (g_seg_on)
        snprintf(g_status, sizeof g_status, "seg %u/%u last_anchor=%s complete=%d anchors=%ld esc=%ld",
                 (unsigned)g_seg.cur_seg, (unsigned)g_seg.n_segs, g_seg.base_anchor[0] ? g_seg.base_anchor : "-",
                 g_done_at != 0, g_anchor_count, g_esc_count);
    else
        snprintf(g_status, sizeof g_status, "masks %d/%d complete=%d anchors=%ld", g_im, g_nm, g_done_at != 0, g_anchor_count);
    return g_status;
}
