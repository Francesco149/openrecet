// Trace Studio v3 — native viewer.
//
// Dear ImGui + Win32 + Direct3D9, cross-built 32-bit with mingw (the process matches
// the real 32-bit d3d8.dll the replay core loads). The d3d9 device hosts the UI; two
// d3d8 replay cores (port + retail) render kept frames on demand into d3d9 textures
// (~3 ms each, resident) which ImGui shows as the port|retail|diff panels. The diff is
// computed live on the CPU from the two readbacks. NO PNG bake, no stale intermediates:
// the two containers (named by view.json) are the only artifacts; frames are replayed
// live and synced by the STORED identity join (view.json's per-column port/retail idx).
//
//   viewer.exe <view.json>              interactive 3-panel scrub
//   viewer.exe --shot out.bmp <view.json>   headless one-frame render (self-verify/feed)
//
// Build: nix develop --command make -C tools/trace_studio_v3/viewer
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "replay_core.h"
#include <nlohmann/json.hpp>
#include <d3d9.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
using json = nlohmann::json;

static LPDIRECT3D9       g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp;

// ── the loaded view ──
// one genuinely-divergent texture in a frame's draw-program (orv3_draws material_diff)
struct DivTex { std::string tex; int port_tris = 0, retail_tris = 0, port_draws = 0, retail_draws = 0; };
struct Col {
    int offset = 0, port_idx = -1, retail_idx = -1;
    std::string label;               // identity key "ANCHOR#occ+delta" (meta v2; may be empty)
    int port_present = -1, retail_present = -1;
    int port_draws = -1, retail_draws = -1, port_calls = -1, retail_calls = -1;
    std::string gap;                 // "", "port", or "retail"
    int gt8 = -1, maxd = 0; double meanabs = 0;   // computed diff metric (gt8<0 = not computed)
    // baked draw-program semantic diff (orv3_draws → view.json)
    std::string draw_verdict;        // "ALIGNED" | "BATCHING" | "DIVERGENT" | "" (gap/none)
    int port_tris = -1, retail_tris = -1, n_textures = -1, n_batched = -1;
    std::vector<DivTex> divergent;
    // engine state (the v2 game-state panel): once-per-frame flow-trace fields, port
    // vs retail, identity-keyed (orv3_state → view.json). Empty unless a --state drive.
    bool has_state = false;
    json sport, sretail;             // {field: value} per side (f32-normalised in the bake)
};
static std::vector<Col> g_cols;
static OrV3Replay *g_port = nullptr, *g_retail = nullptr;
static LPDIRECT3DTEXTURE9 g_tport = nullptr, g_tretail = nullptr, g_tdiff = nullptr;
static uint8_t *g_diffbuf = nullptr;
static int g_w = 0, g_h = 0, g_cur = 0;
static int g_load_stretch = 0;
static std::string g_scenario, g_anchor, g_verdict;
static bool g_show[3] = {true, true, true};   // port, retail, diff
// game-state panel (the v2 StatePanel): once-per-frame engine fields, port-vs-retail,
// diff-highlighted — populated only by a --state capture (else the opt-in hint shows).
static bool g_has_state = false;       // any column carries state
static bool g_show_state = true;       // panel expanded
static bool g_state_diff_only = false; // show only port≠retail fields
static char g_state_filter[64] = {0};  // substring filter on field names
static const int AMP = 6;
// draw-stepping (N3): render only the first g_draw_step draws of each side (render_upto)
// so a frame can be watched building up draw-by-draw / a divergent draw isolated.
static bool g_step_on = false;
static bool g_solo = false;          // step mode: solo a single draw [J,J+1) vs prefix [0,K)
static int  g_draw_step = 0;
static Col  g_stepmetric;            // scratch diff metric while stepping (don't clobber the column)
// pixel→draw pick (N3e): click a panel pixel → the draw that last painted it
static std::string g_pick_side;
static int g_pick_x = -1, g_pick_y = -1, g_pick_draw = -2;   // -2 none, -1 never-painted, ≥0 draw idx

// ── notes / crop regions (the v2 edits.jsonl "notes" loop, native) ──
// In note mode the user drags a box on a panel + types a note to flag a divergence
// for Claude. Persisted to a WINDOWS-LOCAL json (view.json's notes_path) — the viewer
// is a Windows process and CANNOT fopen-write a \\wsl.localhost UNC path; orv3_notes.py
// reads the same file from WSL. Keyed by the identity label (stable across re-windows).
struct Note { int id = 0; std::string label, side, text; int col = -1; bool hasbox = false; float box[4] = {0,0,0,0}; };
static std::vector<Note> g_notes;
static std::string g_notes_path;        // Windows-local; "" ⇒ notes unavailable (old view.json)
static int  g_note_next_id = 1;
static bool g_note_mode = false;        // drag-to-box arming (vs left-click = pick draw)
static bool g_show_notes = true;        // overlay existing note boxes on the panels
struct Draft {                          // the in-progress note (placing a box / typing text)
    bool placing = false, editing = false, hasbox = false, focus = false;
    std::string side, label; int col = -1;
    ImVec2 r0, r1, rmin, rmax;          // drag start/cur + the panel rect, SCREEN space
    float box[4] = {0,0,0,0};           // capture px
    char text[512] = {0};
};
static Draft g_draft;

static void destroy_device();
static void show_column(int i);

static bool create_device(HWND hwnd, UINT W, UINT H)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;
    ZeroMemory(&g_pp, sizeof(g_pp));
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_pp.BackBufferWidth = W; g_pp.BackBufferHeight = H;
    g_pp.EnableAutoDepthStencil = TRUE;
    g_pp.AutoDepthStencilFormat = D3DFMT_D16;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev));
}

static LPDIRECT3DTEXTURE9 make_tex()
{
    LPDIRECT3DTEXTURE9 t = nullptr;   // X8 ⇒ ImGui draws the alpha-less frame opaque
    g_dev->CreateTexture(g_w, g_h, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    return t;
}

static void upload(LPDIRECT3DTEXTURE9 tex, const uint8_t *bgra)
{
    if (!tex || !bgra) return;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int r = 0; r < g_h; r++)
            memcpy((uint8_t *)lr.pBits + (size_t)r * lr.Pitch, bgra + (size_t)r * g_w * 4, (size_t)g_w * 4);
        tex->UnlockRect(0);
    }
}

// amplified white diff into `out` (BGRA, opaque) + the gt8/meanabs/maxd metric — the
// same law as pixel_diff.amplified_diff/gt8, so a v3 metric matches a v2 one.
static void diff_into(const uint8_t *p, const uint8_t *r, uint8_t *out, Col &c)
{
    long gt8 = 0; int maxd = 0; double sum = 0;
    const int N = g_w * g_h;
    for (int i = 0; i < N; i++) {
        int db = abs(p[i*4]-r[i*4]), dg = abs(p[i*4+1]-r[i*4+1]), dr = abs(p[i*4+2]-r[i*4+2]);
        int mx = db > dg ? (db > dr ? db : dr) : (dg > dr ? dg : dr);
        int s = db + dg + dr;
        if (mx > 8) gt8++;
        if (mx > maxd) maxd = mx;
        sum += s;
        if (out) { int v = s * AMP; if (v > 255) v = 255; out[i*4] = out[i*4+1] = out[i*4+2] = (uint8_t)v; out[i*4+3] = 255; }
    }
    c.gt8 = (int)gt8; c.maxd = maxd; c.meanabs = sum / (3.0 * N);
}

// ── diff-metric fill: gt8/meanabs per column for the ribbon heat + worst-frame ──
// Each column needs 2 resident renders (~3 ms) + a px loop. At thousands of columns a
// synchronous pass at open is ~15 s of black screen, so the interactive viewer fills the
// metrics in the BACKGROUND (pump_metrics — a time-budgeted slice per UI frame): the
// window is responsive instantly and the ribbon colours in over ~1-2 s. The headless
// --shot path still computes synchronously (precompute_metrics) so a self-verify/feed
// shot shows the full ribbon. The current column is always computed EAGERLY by
// show_column, so scrubbing/worst-of-seen are never gated on the background fill.
static int g_metric_cursor = 0;   // background fill walks columns once; == size ⇒ done

static bool compute_col_metric(Col &c)
{
    if (c.port_idx < 0 || c.retail_idx < 0 || c.gt8 >= 0) return false;  // gap / already done
    const uint8_t *p = orv3_replay_render(g_port, c.port_idx);
    const uint8_t *r = orv3_replay_render(g_retail, c.retail_idx);
    if (!p || !r) return false;
    diff_into(p, r, nullptr, c);
    return true;
}

static void precompute_metrics() { for (auto &c : g_cols) compute_col_metric(c); }

// both-sides columns still missing a metric (the progress readout / done test).
static int metrics_pending()
{
    int n = 0;
    for (auto &c : g_cols) if (c.port_idx >= 0 && c.retail_idx >= 0 && c.gt8 < 0) n++;
    return n;
}

// advance the background fill for up to `budget_ms`, then yield to the UI. Renders leave
// the resident cores' buffers on a background column, but the panels show uploaded COPIES
// (and any seek/pick re-renders the current column), so the display is never corrupted.
static void pump_metrics(double budget_ms)
{
    if (g_metric_cursor >= (int)g_cols.size()) return;
    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    while (g_metric_cursor < (int)g_cols.size()) {
        compute_col_metric(g_cols[g_metric_cursor++]);
        QueryPerformanceCounter(&now);
        if ((double)(now.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart >= budget_ms) break;
    }
}

static int col_maxdraws(const Col &c) { int m = c.port_draws > c.retail_draws ? c.port_draws : c.retail_draws; return m > 0 ? m : 0; }
// draws ACTUALLY issued for a side at the current step/solo setting (for the readout)
static int issued(int ndraws)
{
    if (!g_step_on || ndraws < 0) return ndraws;
    if (g_solo) return ndraws > 0 ? 1 : 0;
    return g_draw_step < ndraws ? g_draw_step : ndraws;   // prefix length
}
// render one side at the current setting: full / prefix [0,K) / solo [J,J+1) (one draw).
static const uint8_t *render_side(OrV3Replay *rep, int idx, int ndraws)
{
    if (idx < 0 || !rep) return nullptr;
    if (!g_step_on) return orv3_replay_render(rep, idx);
    if (g_solo) {
        int j = g_draw_step < 0 ? 0 : (g_draw_step > ndraws - 1 ? ndraws - 1 : g_draw_step);
        return orv3_replay_render_range(rep, idx, j, j + 1);
    }
    return orv3_replay_render_upto(rep, idx, g_draw_step < ndraws ? g_draw_step : ndraws);
}

// pixel→draw pick: which draw LAST changed pixel (px,py) of `rep`'s frame `idx`?
// Linear scan of the prefixes (render_upto K=0..ndraws); the largest K whose pixel
// differs from K-1 means draw K-1 owns it. -1 = never painted (stayed the clear). One
// click = ndraws+1 resident renders (~150-250 ms for the HOUSE) — fine for a click.
static int pick_draw(OrV3Replay *rep, int idx, int ndraws, int px, int py)
{
    if (!rep || idx < 0 || px < 0 || py < 0 || px >= g_w || py >= g_h) return -2;
    size_t off = ((size_t)py * g_w + px) * 4;
    uint32_t prev = 0; int owner = -1;
    for (int k = 0; k <= ndraws; k++) {
        const uint8_t *buf = orv3_replay_render_upto(rep, idx, k);
        if (!buf) return -2;
        uint32_t v; memcpy(&v, buf + off, 4);
        if (k > 0 && v != prev) owner = k - 1;
        prev = v;
    }
    return owner;
}

// run a pick on `side`, then SOLO the owning draw (click a pixel → see the draw that
// painted it, isolated). owner -1 (background) just reports + restores the view.
static void do_pick(const char *side, OrV3Replay *rep, int idx, int ndraws, int px, int py)
{
    g_pick_side = side; g_pick_x = px; g_pick_y = py;
    g_pick_draw = pick_draw(rep, idx, ndraws, px, py);
    if (g_pick_draw >= 0) { g_step_on = true; g_solo = true; g_draw_step = g_pick_draw; }
    show_column(g_cur);   // re-render (pick_draw left rep->buf at the full frame)
}

// render column `i` into the three panel textures (+ refresh its diff image/metric).
// When stepping, each side renders a draw PREFIX (build-up) or a SOLO draw (isolation);
// the diff is the STEPPED diff, and the precomputed full-frame column metric is preserved
// (a scratch Col absorbs the stepped metric so the ribbon heat never gets corrupted).
static void show_column(int i)
{
    if (i < 0 || i >= (int)g_cols.size()) return;
    g_cur = i;
    Col &c = g_cols[i];
    const uint8_t *p = render_side(g_port,   c.port_idx,   c.port_draws);
    const uint8_t *r = render_side(g_retail, c.retail_idx, c.retail_draws);
    if (p) upload(g_tport, p);
    if (r) upload(g_tretail, r);
    if (p && r) { Col &m = g_step_on ? g_stepmetric : c; diff_into(p, r, g_diffbuf, m); upload(g_tdiff, g_diffbuf); }
}

// ── notes persistence (json array at g_notes_path, the Windows-local file) ──
static void load_notes()
{
    g_notes.clear(); g_note_next_id = 1;
    if (g_notes_path.empty()) return;
    std::ifstream f(g_notes_path.c_str());
    if (!f) return;                                 // no file yet = no notes (fine)
    json arr; try { f >> arr; } catch (const std::exception &) { return; }
    if (!arr.is_array()) return;
    for (auto &jn : arr) {
        Note n;
        n.id = jn.value("id", 0);
        n.label = jn.value("label", "");
        n.side = jn.value("side", "");
        n.text = jn.value("text", "");
        n.col = jn.value("col", -1);
        if (jn.contains("box") && jn["box"].is_array() && jn["box"].size() == 4) {
            n.hasbox = true;
            for (int k = 0; k < 4; k++) n.box[k] = jn["box"][k].get<float>();
        }
        if (n.id >= g_note_next_id) g_note_next_id = n.id + 1;
        g_notes.push_back(n);
    }
}

static void save_notes()
{
    if (g_notes_path.empty()) return;
    json arr = json::array();
    for (auto &n : g_notes) {
        json jn = { {"id", n.id}, {"label", n.label}, {"side", n.side},
                    {"text", n.text}, {"col", n.col} };
        if (n.hasbox) jn["box"] = { (int)n.box[0], (int)n.box[1], (int)n.box[2], (int)n.box[3] };
        else jn["box"] = nullptr;
        arr.push_back(jn);
    }
    std::ofstream f(g_notes_path.c_str(), std::ios::trunc);
    if (!f) { fprintf(stderr, "notes: cannot write %s\n", g_notes_path.c_str()); return; }
    f << arr.dump(1);
}

// column whose identity label == `label` (the stable across-window key), or -1.
static int col_of_label(const std::string &label)
{
    if (label.empty()) return -1;
    for (size_t i = 0; i < g_cols.size(); i++) if (g_cols[i].label == label) return (int)i;
    return -1;
}

// a finished rubber-band drag → the draft's capture-px box (clamped, min/max-ordered).
// A near-zero drag (a click) becomes a whole-frame note (hasbox=false).
static void finalize_draft_box()
{
    float W = g_draft.rmax.x - g_draft.rmin.x, H = g_draft.rmax.y - g_draft.rmin.y;
    auto cx = [&](float x){ float v = (x - g_draft.rmin.x) / W * g_w; return v < 0 ? 0 : (v > g_w ? g_w : v); };
    auto cy = [&](float y){ float v = (y - g_draft.rmin.y) / H * g_h; return v < 0 ? 0 : (v > g_h ? g_h : v); };
    float x0 = cx(g_draft.r0.x), x1 = cx(g_draft.r1.x), y0 = cy(g_draft.r0.y), y1 = cy(g_draft.r1.y);
    g_draft.box[0] = x0 < x1 ? x0 : x1; g_draft.box[1] = y0 < y1 ? y0 : y1;
    g_draft.box[2] = x0 < x1 ? x1 : x0; g_draft.box[3] = y0 < y1 ? y1 : y0;
    g_draft.hasbox = (g_draft.box[2] - g_draft.box[0] >= 3) || (g_draft.box[3] - g_draft.box[1] >= 3);
    g_draft.placing = false; g_draft.editing = true; g_draft.focus = true; g_draft.text[0] = 0;
}

// commit the draft to g_notes + persist. Side "frame" (whole-frame button) records no side.
static void commit_draft()
{
    Note n;
    n.id = g_note_next_id++; n.label = g_draft.label;
    n.side = g_draft.side == "frame" ? "" : g_draft.side;
    n.text = g_draft.text; n.col = g_draft.col; n.hasbox = g_draft.hasbox;
    for (int k = 0; k < 4; k++) n.box[k] = g_draft.box[k];
    g_notes.push_back(n); save_notes();
    g_draft = Draft{};
}

static bool load_view(const char *path)
{
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    json m; try { f >> m; } catch (const std::exception &e) { fprintf(stderr, "json: %s\n", e.what()); return false; }

    g_scenario = m.value("scenario", "");
    g_anchor = m.value("anchor", "");
    g_verdict = m.value("verdict", "");
    g_load_stretch = m.value("load_stretch", 0);
    g_has_state = m.value("has_state", false);
    g_notes_path = m.value("notes_path", "");   // Windows-local notes file (writable)
    std::string pc = m.value("port_container", ""), rc = m.value("retail_container", "");
    char err[128] = {0};
    g_port = orv3_replay_open(pc.c_str(), err, sizeof err);
    if (!g_port) { fprintf(stderr, "port container: %s\n", err); return false; }
    g_retail = orv3_replay_open(rc.c_str(), err, sizeof err);
    if (!g_retail) { fprintf(stderr, "retail container: %s\n", err); return false; }
    g_w = orv3_replay_width(g_port); g_h = orv3_replay_height(g_port);

    for (auto &jf : m["frames"]) {
        Col c;
        c.offset = jf.value("offset", 0);
        c.label = jf.value("label", "");
        c.gap = jf.value("gap", json()).is_string() ? jf.value("gap", "") : "";
        c.port_idx = jf.value("port_idx", json()).is_number() ? jf["port_idx"].get<int>() : -1;
        c.retail_idx = jf.value("retail_idx", json()).is_number() ? jf["retail_idx"].get<int>() : -1;
        c.port_present = jf.value("port_present", json()).is_number() ? jf["port_present"].get<int>() : -1;
        c.retail_present = jf.value("retail_present", json()).is_number() ? jf["retail_present"].get<int>() : -1;
        c.port_draws = jf.value("port_draws", json()).is_number() ? jf["port_draws"].get<int>() : -1;
        c.retail_draws = jf.value("retail_draws", json()).is_number() ? jf["retail_draws"].get<int>() : -1;
        c.port_calls = jf.value("port_calls", json()).is_number() ? jf["port_calls"].get<int>() : -1;
        c.retail_calls = jf.value("retail_calls", json()).is_number() ? jf["retail_calls"].get<int>() : -1;
        // baked draw-program semantic diff (present only on both-sides columns)
        c.draw_verdict = jf.value("draw_verdict", "");
        c.port_tris = jf.value("port_tris", json()).is_number() ? jf["port_tris"].get<int>() : -1;
        c.retail_tris = jf.value("retail_tris", json()).is_number() ? jf["retail_tris"].get<int>() : -1;
        c.n_textures = jf.value("n_textures", json()).is_number() ? jf["n_textures"].get<int>() : -1;
        c.n_batched = jf.value("n_batched", json()).is_number() ? jf["n_batched"].get<int>() : -1;
        if (jf.contains("divergent") && jf["divergent"].is_array())
            for (auto &jd : jf["divergent"]) {
                DivTex dt;
                dt.tex = jd.value("tex", "");
                dt.port_tris = jd.value("port_tris", 0); dt.retail_tris = jd.value("retail_tris", 0);
                dt.port_draws = jd.value("port_draws", 0); dt.retail_draws = jd.value("retail_draws", 0);
                c.divergent.push_back(dt);
            }
        if (jf.contains("state") && jf["state"].is_object()) {
            c.has_state = true;
            c.sport   = jf["state"].value("port", json::object());
            c.sretail = jf["state"].value("retail", json::object());
        }
        g_cols.push_back(c);
    }
    if (g_cols.empty()) { fprintf(stderr, "view has no frames\n"); return false; }

    g_tport = make_tex(); g_tretail = make_tex(); g_tdiff = make_tex();
    g_diffbuf = (uint8_t *)malloc((size_t)g_w * g_h * 4);
    g_metric_cursor = 0;        // diff metrics fill in the background (pump_metrics); the
                                // --shot path calls precompute_metrics() for a full ribbon.
    load_notes();
    show_column(0);
    return true;
}

// meanabs → green→yellow→red (ABSOLUTE scale, matches the v2 DiffRibbon).
static ImU32 heat(double meanabs)
{
    double t = meanabs / 6.0; if (t < 0) t = 0; if (t > 1) t = 1;
    int r = t < 0.5 ? (int)(510 * t) : 255;
    int g = t < 0.5 ? 200 : (int)(200 * (1 - (t - 0.5) * 2));
    return IM_COL32(r, g, 78, 255);
}

static void seek(int i) { if (i < 0) i = 0; if (i >= (int)g_cols.size()) i = (int)g_cols.size() - 1; if (i != g_cur) show_column(i); }

// exit draw-stepping AND clear any pixel-pick → back to the full frame (the "un-pick").
static void clear_view() { g_step_on = false; g_solo = false; g_pick_draw = -2; show_column(g_cur); }

// one panel: a labelled image (or a gap placeholder), scaled to `panel_w`. If `rep` is
// non-null, a left-click runs a pixel→draw pick on that side (maps the click to a frame
// pixel, finds + solos the owning draw).
static void panel(const char *label, LPDIRECT3DTEXTURE9 tex, bool present, float panel_w,
                  OrV3Replay *rep = nullptr, int frame_idx = -1, int ndraws = 0, const char *sidename = "")
{
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.92f, 1.0f), "%s", label);
    float h = panel_w * g_h / g_w;
    if (present) {
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(panel_w, h));
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        // left-click = pixel→draw pick — UNLESS arming a note (then a click/drag = box).
        if (!g_note_mode && rep && ImGui::IsItemClicked()) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            int px = (int)((mp.x - mn.x) / (mx.x - mn.x) * g_w);
            int py = (int)((mp.y - mn.y) / (mx.y - mn.y) * g_h);
            do_pick(sidename, rep, frame_idx, ndraws, px, py);
        }
        // note mode: drag a crop box on THIS side (rubber-band → text editor on release)
        if (g_note_mode && !g_notes_path.empty()) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && !g_draft.placing && !g_draft.editing) {
                g_draft.placing = true; g_draft.side = sidename; g_draft.col = g_cur;
                g_draft.label = g_cols[g_cur].label;
                g_draft.r0 = g_draft.r1 = ImGui::GetIO().MousePos; g_draft.rmin = mn; g_draft.rmax = mx;
            }
            if (g_draft.placing && g_draft.side == sidename) {
                g_draft.r1 = ImGui::GetIO().MousePos;
                dl->AddRect(g_draft.r0, g_draft.r1, IM_COL32(255, 210, 80, 255), 0, 0, 2.0f);
                if (ImGui::IsMouseReleased(0)) finalize_draft_box();
            }
        }
        // overlay existing note boxes pinned to THIS column (by label) + side.
        if (g_show_notes) {
            const std::string &lbl = g_cols[g_cur].label;
            for (auto &n : g_notes) {
                bool here = n.hasbox && n.side == sidename &&
                            (lbl.empty() ? n.col == g_cur : n.label == lbl);
                if (!here) continue;
                ImVec2 a(mn.x + n.box[0] / g_w * (mx.x - mn.x), mn.y + n.box[1] / g_h * (mx.y - mn.y));
                ImVec2 b(mn.x + n.box[2] / g_w * (mx.x - mn.x), mn.y + n.box[3] / g_h * (mx.y - mn.y));
                dl->AddRect(a, b, IM_COL32(120, 224, 120, 255), 0, 0, 2.0f);
                char tag[16]; snprintf(tag, sizeof tag, "#%d", n.id);
                dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(140, 240, 140, 255), tag);
            }
        }
    } else {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + panel_w, p.y + h), IM_COL32(28, 24, 24, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + panel_w/2 - 30, p.y + h/2), IM_COL32(255, 180, 84, 255), "(gap)");
        ImGui::Dummy(ImVec2(panel_w, h));
    }
    ImGui::EndGroup();
}

// format a state field value for display (readable floats, plain ints/strings).
static std::string fmt_state_val(const json &v)
{
    char b[48];
    if (v.is_null())           return "-";
    // %.9g: f32 carries ~7 sig digits, so 9 makes a 1-ULP divergence (e.g. cx
    // 0.600000024 vs 0.599999964) VISIBLE rather than both rounding to "0.6".
    if (v.is_number_float())   { snprintf(b, sizeof b, "%.9g", v.get<double>()); return b; }
    if (v.is_number_integer()) { snprintf(b, sizeof b, "%lld", (long long)v.get<int64_t>()); return b; }
    if (v.is_boolean())        return v.get<bool>() ? "true" : "false";
    if (v.is_string())         return v.get<std::string>();
    return v.dump();
}

// the GAME-STATE panel — the v2 StatePanel, native. The current column's once-per-frame
// engine fields (rng/rngcalls, player+companion px/py/anim, title menu, dialogue box),
// retail vs port, port≠retail rows highlighted, with a name filter + diffs-only toggle.
// Populated only by a --state capture (else an opt-in hint). Floats are f32-normalised in
// the bake, so a highlighted row is a REAL divergence (e.g. the rngcalls phase offset),
// not f32-repr noise. This is the engine-state half of the divergence story the d3d
// draw-program panel doesn't see — and the bridge to flow_diff (same call_trace.jsonl).
static void game_state_panel(const Col &c)
{
    ImGui::Separator();
    if (!g_has_state) {
        ImGui::TextDisabled("game state: (re-drive with --state — house_capture / port_capture / "
                            "orv3_window --state — to capture the once-per-frame engine fields)");
        return;
    }
    std::vector<std::string> keys;
    for (auto it = c.sretail.begin(); it != c.sretail.end(); ++it) keys.push_back(it.key());
    for (auto it = c.sport.begin();   it != c.sport.end();   ++it)
        if (!c.sretail.contains(it.key())) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    int ndiff = 0;
    for (auto &k : keys)
        if (c.sport.contains(k) && c.sretail.contains(k) && c.sport[k] != c.sretail[k]) ndiff++;
    char hdr[96];
    snprintf(hdr, sizeof hdr, "game state — %d fields, %d differ###gamestate", (int)keys.size(), ndiff);
    ImGui::SetNextItemOpen(g_show_state, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader(hdr)) return;
    if (!c.has_state) { ImGui::TextDisabled("(no engine state at this column — a gap, or outside the state window)"); return; }
    ImGui::SetNextItemWidth(160); ImGui::InputText("filter##state", g_state_filter, sizeof g_state_filter);
    ImGui::SameLine(); ImGui::Checkbox("diffs only", &g_state_diff_only);
    ImGui::SameLine(); ImGui::TextDisabled("(red = port != retail)");
    if (ImGui::BeginTable("gstate", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame |
                          ImGuiTableFlags_ScrollY, ImVec2(0, 210))) {
        ImGui::TableSetupColumn("field"); ImGui::TableSetupColumn("retail"); ImGui::TableSetupColumn("port");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto &k : keys) {
            if (g_state_filter[0] && k.find(g_state_filter) == std::string::npos) continue;
            bool hp = c.sport.contains(k), hr = c.sretail.contains(k);
            bool differ = hp && hr && c.sport[k] != c.sretail[k];
            if (g_state_diff_only && !differ) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k.c_str());
            ImVec4 col = differ ? ImVec4(1, 0.42f, 0.42f, 1) : ImVec4(0.84f, 0.86f, 0.88f, 1);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", hr ? fmt_state_val(c.sretail[k]).c_str() : "-");
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", hp ? fmt_state_val(c.sport[k]).c_str() : "-");
        }
        ImGui::EndTable();
    }
}

static void draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("##v3", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (g_cols.empty()) { ImGui::Text("no view loaded — usage: viewer.exe <view.json>"); ImGui::End(); return; }
    Col &c = g_cols[g_cur];

    // header
    bool aligned = g_verdict.rfind("ALIGNED", 0) == 0;
    ImGui::TextColored(ImVec4(0.42f, 0.71f, 1.0f, 1.0f), "trace studio v3");
    ImGui::SameLine(); ImGui::Text("· %s · %s ·", g_scenario.c_str(), g_anchor.c_str());
    ImGui::SameLine(); ImGui::TextColored(aligned ? ImVec4(0.41f,0.82f,0.51f,1) : ImVec4(1,0.71f,0.33f,1), "%s", g_verdict.c_str());
    ImGui::SameLine(); ImGui::Text("· %dx%d · %d cols · load stretch %+d · %.0f fps",
                                   g_w, g_h, (int)g_cols.size(), g_load_stretch, ImGui::GetIO().Framerate);
    if (int mp = metrics_pending()) {   // background diff-metric fill still running
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "· filling diff metrics (%d left)", mp);
    }

    // toolbar: panel toggles + scrub
    const char *names[3] = {"port", "retail", "diff"};
    for (int k = 0; k < 3; k++) { if (k) ImGui::SameLine(); ImGui::Checkbox(names[k], &g_show[k]); }
    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); if (ImGui::Button("|<")) seek(0);
    ImGui::SameLine(); if (ImGui::Button("<")) seek(g_cur - 1);
    int sel = g_cur; ImGui::SameLine(); ImGui::SetNextItemWidth(-260);
    if (ImGui::SliderInt("##scrub", &sel, 0, (int)g_cols.size() - 1)) seek(sel);
    ImGui::SameLine(); if (ImGui::Button(">")) seek(g_cur + 1);
    ImGui::SameLine(); if (ImGui::Button(">|")) seek((int)g_cols.size() - 1);
    ImGui::SameLine(); ImGui::Text("col %d/%d · %s", g_cur, (int)g_cols.size() - 1,
                     c.label.size() ? c.label.c_str() : std::to_string(c.offset).c_str());

    // notes toolbar + inline draft editor — flag a divergence for Claude to inspect.
    // In note mode a drag on a panel becomes a crop box; 'note frame' flags the whole
    // frame. Persisted to the Windows-local notes file (orv3_notes.py reads it on WSL).
    ImGui::BeginDisabled(g_notes_path.empty());
    ImGui::Checkbox("note mode (m)", &g_note_mode);
    ImGui::SameLine(); ImGui::Checkbox("show notes", &g_show_notes);
    ImGui::SameLine(); if (ImGui::Button("note frame")) {
        g_draft = Draft{}; g_draft.editing = true; g_draft.focus = true;
        g_draft.side = "frame"; g_draft.col = g_cur; g_draft.label = g_cols[g_cur].label;
    }
    ImGui::EndDisabled();
    if (g_notes_path.empty()) {
        ImGui::SameLine(); ImGui::TextDisabled("(notes need a fresh view.json — re-run orv3_window)");
    } else if (g_note_mode && !g_draft.editing) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "drag a box on a panel to flag a region");
    }
    if (g_draft.editing) {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "NEW NOTE");
        ImGui::SameLine(); ImGui::Text("@ %s [%s]%s:",
            g_draft.label.empty() ? ("col " + std::to_string(g_draft.col)).c_str() : g_draft.label.c_str(),
            g_draft.side == "frame" ? "frame" : g_draft.side.c_str(),
            g_draft.hasbox ? "" : " whole frame");
        if (g_draft.hasbox) { ImGui::SameLine(); ImGui::TextDisabled("[%.0f,%.0f,%.0f,%.0f]",
            g_draft.box[0], g_draft.box[1], g_draft.box[2], g_draft.box[3]); }
        if (g_draft.focus) { ImGui::SetKeyboardFocusHere(); g_draft.focus = false; }
        ImGui::SetNextItemWidth(-220);
        bool enter = ImGui::InputText("##ntext", g_draft.text, sizeof g_draft.text, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(); bool save = ImGui::Button("save");
        ImGui::SameLine(); bool cancel = ImGui::Button("cancel");
        if (cancel) g_draft = Draft{};
        else if ((enter || save) && g_draft.text[0]) commit_draft();
    }

    // draw-step row (N3): step through draws (render_upto / render_range) to watch a
    // frame build up (prefix) or ISOLATE one draw (solo) — each side independently.
    int maxd = col_maxdraws(c);
    int slmax = g_solo ? (maxd > 0 ? maxd - 1 : 0) : maxd;     // solo: draw INDEX; prefix: LENGTH
    if (ImGui::Checkbox("draw step", &g_step_on)) { if (!g_step_on) g_pick_draw = -2; if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
    ImGui::SameLine(); ImGui::BeginDisabled(!g_step_on);
    if (ImGui::Checkbox("solo", &g_solo)) { if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(-510);
    if (ImGui::SliderInt("##drawstep", &g_draw_step, 0, slmax, g_solo ? "draw #%d (solo)" : "first %d draws")) {
        if (g_draw_step < 0) g_draw_step = 0; show_column(g_cur);
    }
    ImGui::SameLine(); if (ImGui::Button("-")) { if (g_draw_step > 0) g_draw_step--; show_column(g_cur); }
    ImGui::SameLine(); if (ImGui::Button("+")) { if (g_draw_step < slmax) g_draw_step++; show_column(g_cur); }
    ImGui::EndDisabled();
    // back to the whole frame (un-step / un-pick) — always enabled, the clear way out
    ImGui::SameLine(); ImGui::BeginDisabled(!g_step_on);
    if (ImGui::Button("full frame (f)")) clear_view();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("issuing  port %d/%d · retail %d/%d", issued(c.port_draws), c.port_draws,
                issued(c.retail_draws), c.retail_draws);

    // 3-panel row
    int nshow = (g_show[0] ? 1 : 0) + (g_show[1] ? 1 : 0) + (g_show[2] ? 1 : 0);
    if (nshow < 1) nshow = 1;
    float avail = ImGui::GetContentRegionAvail().x;
    float pw = (avail - (nshow - 1) * 8.0f) / nshow;
    bool first = true;
    if (g_show[0]) { panel("port (click=pick draw)", g_tport, c.port_idx >= 0, pw, g_port, c.port_idx, c.port_draws, "port"); first = false; }
    if (g_show[1]) { if (!first) ImGui::SameLine(); panel("retail (click=pick draw)", g_tretail, c.retail_idx >= 0, pw, g_retail, c.retail_idx, c.retail_draws, "retail"); first = false; }
    if (g_show[2]) { if (!first) ImGui::SameLine(); panel("diff", g_tdiff, c.port_idx >= 0 && c.retail_idx >= 0, pw); }

    // diff ribbon (one cell per column, heat = meanabs, click to seek)
    ImGui::Spacing();
    ImVec2 rp = ImGui::GetCursorScreenPos();
    float rw = ImGui::GetContentRegionAvail().x, rh = 16, cw = rw / g_cols.size();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < g_cols.size(); i++) {
        ImU32 col = g_cols[i].gap.size() ? IM_COL32(60, 60, 70, 255)
                  : g_cols[i].gt8 < 0 ? IM_COL32(17, 22, 28, 255) : heat(g_cols[i].meanabs);
        dl->AddRectFilled(ImVec2(rp.x + i * cw, rp.y), ImVec2(rp.x + (i + 1) * cw, rp.y + rh), col);
    }
    dl->AddRect(ImVec2(rp.x + g_cur * cw - 1, rp.y - 1), ImVec2(rp.x + (g_cur + 1) * cw + 1, rp.y + rh + 1), IM_COL32(108, 182, 255, 255), 0, 0, 2);
    ImGui::InvisibleButton("##ribbon", ImVec2(rw, rh));
    if (ImGui::IsItemActive() || ImGui::IsItemClicked())
        seek((int)((ImGui::GetIO().MousePos.x - rp.x) / cw));

    // worst / next + per-frame state
    int worst = 0; for (size_t i = 0; i < g_cols.size(); i++) if (g_cols[i].gt8 > g_cols[worst].gt8) worst = (int)i;
    if (ImGui::Button("worst (w)")) seek(worst);
    ImGui::SameLine(); if (ImGui::Button("next gt8>0 (n)")) { for (size_t i = g_cur + 1; i < g_cols.size(); i++) if (g_cols[i].gt8 > 0) { seek((int)i); break; } }
    ImGui::SameLine(); ImGui::TextDisabled("worst: %s gt8=%dpx",
        g_cols[worst].label.size() ? g_cols[worst].label.c_str() : std::to_string(g_cols[worst].offset).c_str(),
        g_cols[worst].gt8);

    ImGui::Separator();
    const Col &dm = g_step_on ? g_stepmetric : c;   // stepped diff vs full-frame diff
    ImGui::Text("identity: %s   |   diff%s: gt8 %d px · meanabs %.4f · max|d| %d%s",
                c.label.size() ? c.label.c_str() : (g_anchor + "+" + std::to_string(c.offset)).c_str(),
                g_step_on ? " (stepped)" : "",
                dm.gt8, dm.meanabs, dm.maxd, c.gap.size() ? "  (GAP)" : "");
    if (ImGui::BeginTable("state", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("field"); ImGui::TableSetupColumn("retail"); ImGui::TableSetupColumn("port");
        ImGui::TableHeadersRow();
        struct Row { const char *k; int r, p; bool flag; };
        Row rows[] = {
            {"present", c.retail_present, c.port_present, false},  // load stretch ⇒ differ, expected
            {"draws", c.retail_draws, c.port_draws, true},
            {"calls", c.retail_calls, c.port_calls, true},
        };
        for (auto &rw2 : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", rw2.k);
            bool diff = rw2.flag && rw2.r != rw2.p && rw2.r >= 0 && rw2.p >= 0;
            ImVec4 col = diff ? ImVec4(1, 0.42f, 0.42f, 1) : ImVec4(0.84f, 0.86f, 0.88f, 1);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", rw2.r);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", rw2.p);
        }
        ImGui::EndTable();
    }

    // draw-program semantic diff (N3): pixels can be ALIGNED while the render PROGRAM
    // differs — the v3 insight v2 is blind to. Show the material verdict + the
    // genuinely-divergent textures (one-sided / differing geometry), with an isolate.
    if (!c.draw_verdict.empty()) {
        bool va = c.draw_verdict == "ALIGNED", vb = c.draw_verdict == "BATCHING";
        ImVec4 dvc = va ? ImVec4(0.41f, 0.82f, 0.51f, 1) : vb ? ImVec4(1, 0.85f, 0.4f, 1) : ImVec4(1, 0.55f, 0.33f, 1);
        ImGui::Text("draw program:"); ImGui::SameLine();
        ImGui::TextColored(dvc, "%s", c.draw_verdict.c_str()); ImGui::SameLine();
        ImGui::Text("· %d textures · %d batched (split vs batched, geometry identical)", c.n_textures, c.n_batched);
        for (size_t k = 0; k < c.divergent.size(); k++) {
            const DivTex &d = c.divergent[k];
            const char *side = d.port_tris == 0 ? "retail-only" : d.retail_tris == 0 ? "port-only" : "differs";
            ImGui::TextColored(ImVec4(1, 0.55f, 0.33f, 1),
                "  ! %s tex %s : port %dpr/%ddr  retail %dpr/%ddr",
                side, d.tex.c_str(), d.port_tris, d.port_draws, d.retail_tris, d.retail_draws);
        }
    }

    // engine-state panel — the v2 game-state table, port vs retail (needs a --state capture)
    game_state_panel(c);

    // pixel→draw pick readout (+ the un-pick button right where the eye is)
    if (g_pick_draw != -2) {
        if (g_pick_draw >= 0)
            ImGui::TextColored(ImVec4(0.42f, 0.71f, 1, 1), "pick: %s pixel (%d,%d) <- draw #%d  (solo'd; 'solo' off = draws before it, 'full frame'/f = whole frame)",
                               g_pick_side.c_str(), g_pick_x, g_pick_y, g_pick_draw);
        else
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "pick: %s pixel (%d,%d) : background (no draw painted it)",
                               g_pick_side.c_str(), g_pick_x, g_pick_y);
        ImGui::SameLine(); if (ImGui::SmallButton("un-pick")) clear_view();
    }

    // notes list — the flagged-divergence queue (seek to / delete). Claude reads the
    // same file via orv3_notes.py; this IS the authoritative per-scenario gap list.
    if (!g_notes_path.empty()) {
        ImGui::Separator();
        char hdr[80]; snprintf(hdr, sizeof hdr, "notes (%d)###notes", (int)g_notes.size());
        if (ImGui::CollapsingHeader(hdr)) {
            if (g_notes.empty())
                ImGui::TextDisabled("none yet — toggle 'note mode' + drag a box on a panel, or 'note frame'.");
            int del = -1;
            for (size_t i = 0; i < g_notes.size(); i++) {
                Note &n = g_notes[i];
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("seek")) { int ci = col_of_label(n.label); seek(ci >= 0 ? ci : n.col); }
                ImGui::SameLine(); if (ImGui::SmallButton("del")) del = (int)i;
                ImGui::SameLine();
                bool oncur = n.label.empty() ? n.col == g_cur : n.label == g_cols[g_cur].label;
                ImGui::TextColored(oncur ? ImVec4(0.55f, 0.9f, 0.55f, 1) : ImVec4(0.8f, 0.82f, 0.84f, 1),
                    "#%d %s%s%s — %s", n.id, n.side.empty() ? "frame" : n.side.c_str(),
                    n.label.empty() ? "" : (" @" + n.label).c_str(),
                    n.hasbox ? "" : " (whole)", n.text.c_str());
                ImGui::PopID();
            }
            if (del >= 0) { g_notes.erase(g_notes.begin() + del); save_notes(); }
        }
    }

    ImGui::TextDisabled("keys: ,/. ±1 · arrows ±10 · Home/End · 1/2/3 panels · [ ] draw± · s solo · click=pick · f full frame · w worst · n next · m note mode");
    ImGui::End();
}

static bool save_bmp(const char *path, const uint8_t *bgra, int W, int H, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t img = (uint32_t)W * H * 4, off = 14 + 40;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M'; *(uint32_t*)&hdr[2]=off+img; *(uint32_t*)&hdr[10]=off;
    *(uint32_t*)&hdr[14]=40; *(int32_t*)&hdr[18]=W; *(int32_t*)&hdr[22]=H;
    *(uint16_t*)&hdr[26]=1; *(uint16_t*)&hdr[28]=32; *(uint32_t*)&hdr[34]=img;
    fwrite(hdr, 1, 54, f);
    for (int r = H - 1; r >= 0; r--) fwrite(bgra + (size_t)r * pitch, 1, (size_t)W * 4, f);
    fclose(f);
    return true;
}

static void imgui_init(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_dev);
}

static void begin_frame() { ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); }
static void end_frame()
{
    ImGui::EndFrame(); ImGui::Render();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(16, 18, 22), 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) { ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); g_dev->EndScene(); }
}

static void shutdown_all()
{
    if (g_port) orv3_replay_close(g_port);
    if (g_retail) orv3_replay_close(g_retail);
    free(g_diffbuf);
    if (g_tport) g_tport->Release();
    if (g_tretail) g_tretail->Release();
    if (g_tdiff) g_tdiff->Release();
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    destroy_device();
}
static void destroy_device()
{
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_d3d) { g_d3d->Release(); g_d3d = nullptr; }
}

static int do_shot(const char *out, const char *view, int W, int H, int col, int draw_step, int pick_x, int pick_y)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer_shot"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "orv3", WS_OVERLAPPEDWINDOW, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, W, H)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (view && !load_view(view)) return 2;
    precompute_metrics();   // headless: full ribbon/worst for the self-verify shot
    if (draw_step >= 0) { g_step_on = true; g_draw_step = draw_step; }   // headless draw-step verify
    if (col > 0) seek(col); else if (g_step_on) show_column(g_cur);      // apply col/step before the shot (g_solo may be preset)
    if (pick_x >= 0 && pick_y >= 0) {                                    // headless pixel→draw pick verify (port side)
        Col &c = g_cols[g_cur];
        do_pick("port", g_port, c.port_idx, c.port_draws, pick_x, pick_y);
        fprintf(stderr, "pick port (%d,%d) -> draw #%d\n", pick_x, pick_y, g_pick_draw);
    }
    begin_frame(); draw_ui(); end_frame();

    IDirect3DSurface9 *bb = nullptr, *sys = nullptr; int rc = 2;
    if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb
     && SUCCEEDED(g_dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) && sys
     && SUCCEEDED(g_dev->GetRenderTargetData(bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            rc = save_bmp(out, (const uint8_t *)lr.pBits, W, H, lr.Pitch) ? 0 : 2;
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release();
    if (bb) bb->Release();
    shutdown_all(); DestroyWindow(hwnd);
    fprintf(stderr, rc == 0 ? "shot -> %s (%dx%d)\n" : "shot FAILED\n", out, W, H);
    return rc;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    // keep the backbuffer == the client area on resize (else the mouse↔render skew +
    // non-integer Present scale return — the cursor-offset / squished-font bug).
    if (msg == WM_SIZE && g_dev && wp != SIZE_MINIMIZED) {
        g_pp.BackBufferWidth = LOWORD(lp); g_pp.BackBufferHeight = HIWORD(lp);
        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_dev->Reset(&g_pp);
        ImGui_ImplDX9_CreateDeviceObjects();
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void handle_keys()
{
    if (g_cols.empty()) return;
    if (ImGui::IsAnyItemActive()) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Comma)) seek(g_cur - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Period)) seek(g_cur + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) seek(g_cur - 10);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) seek(g_cur + 10);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) seek(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) seek((int)g_cols.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_1)) g_show[0] = !g_show[0];
    if (ImGui::IsKeyPressed(ImGuiKey_2)) g_show[1] = !g_show[1];
    if (ImGui::IsKeyPressed(ImGuiKey_3)) g_show[2] = !g_show[2];
    if (ImGui::IsKeyPressed(ImGuiKey_M) && !g_notes_path.empty()) g_note_mode = !g_note_mode;
    if (ImGui::IsKeyPressed(ImGuiKey_W)) { int wbest = 0; for (size_t i = 0; i < g_cols.size(); i++) if (g_cols[i].gt8 > g_cols[wbest].gt8) wbest = (int)i; seek(wbest); }
    if (ImGui::IsKeyPressed(ImGuiKey_N)) { for (size_t i = g_cur + 1; i < g_cols.size(); i++) if (g_cols[i].gt8 > 0) { seek((int)i); break; } }
    // [ ] step draws (engages draw-stepping); s toggles solo; clamp to this column's max
    int maxd = col_maxdraws(g_cols[g_cur]);
    int slmax = g_solo ? (maxd > 0 ? maxd - 1 : 0) : maxd;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))  { g_step_on = true; if (g_draw_step > 0)     g_draw_step--; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) { g_step_on = true; if (g_draw_step < slmax) g_draw_step++; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) { g_solo = !g_solo; g_step_on = true; if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) clear_view();   // full frame — un-step / un-pick
}

static int do_interactive(const char *view)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.style = CS_CLASSDC; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "trace studio v3", WS_OVERLAPPEDWINDOW,
                              60, 40, 1400, 920, nullptr, nullptr, wc.hInstance, nullptr);
    // The backbuffer MUST match the CLIENT area, not the 1400x920 window outer size:
    // ImGui's DisplaySize + mouse coords are client-space, so a window-sized backbuffer
    // gets Present-scaled into the (shorter) client area → rendered content sits above
    // where the mouse registers (clicks land low). Size the device to the client rect.
    RECT crc; GetClientRect(hwnd, &crc);
    UINT cw = crc.right - crc.left, ch = crc.bottom - crc.top;
    if (!create_device(hwnd, cw ? cw : 1400, ch ? ch : 920)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (view && !load_view(view)) { fprintf(stderr, "load_view failed\n"); return 2; }
    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);

    bool running = true;
    while (running) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m); DispatchMessageA(&m);
            if (m.message == WM_QUIT) running = false;
        }
        if (!running) break;
        begin_frame(); handle_keys(); draw_ui(); end_frame();
        pump_metrics(8.0);   // background diff-metric fill (~8 ms/frame) — non-blocking open
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    shutdown_all(); DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *shot = nullptr, *view = nullptr;
    int col = 0, draw_step = -1, pick_x = -1, pick_y = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (strcmp(argv[i], "--col") == 0 && i + 1 < argc) col = atoi(argv[++i]);
        else if (strcmp(argv[i], "--draw-step") == 0 && i + 1 < argc) draw_step = atoi(argv[++i]);
        else if (strcmp(argv[i], "--solo") == 0) g_solo = true;
        else if (strcmp(argv[i], "--state-diffs") == 0) g_state_diff_only = true;  // headless: show only port≠retail state rows
        else if (strcmp(argv[i], "--pick") == 0 && i + 2 < argc) { pick_x = atoi(argv[++i]); pick_y = atoi(argv[++i]); }
        else view = argv[i];
    }
    if (shot) return do_shot(shot, view, 1400, 900, col, draw_step, pick_x, pick_y);
    return do_interactive(view);
}
