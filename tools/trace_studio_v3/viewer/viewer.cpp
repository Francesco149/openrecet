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
using json = nlohmann::json;

static LPDIRECT3D9       g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp;

// ── the loaded view ──
// one genuinely-divergent texture in a frame's draw-program (orv3_draws material_diff)
struct DivTex { std::string tex; int port_tris = 0, retail_tris = 0, port_draws = 0, retail_draws = 0; };
struct Col {
    int offset = 0, port_idx = -1, retail_idx = -1;
    int port_present = -1, retail_present = -1;
    int port_draws = -1, retail_draws = -1, port_calls = -1, retail_calls = -1;
    std::string gap;                 // "", "port", or "retail"
    int gt8 = -1, maxd = 0; double meanabs = 0;   // computed diff metric (gt8<0 = not computed)
    // baked draw-program semantic diff (orv3_draws → view.json)
    std::string draw_verdict;        // "ALIGNED" | "BATCHING" | "DIVERGENT" | "" (gap/none)
    int port_tris = -1, retail_tris = -1, n_textures = -1, n_batched = -1;
    std::vector<DivTex> divergent;
};
static std::vector<Col> g_cols;
static OrV3Replay *g_port = nullptr, *g_retail = nullptr;
static LPDIRECT3DTEXTURE9 g_tport = nullptr, g_tretail = nullptr, g_tdiff = nullptr;
static uint8_t *g_diffbuf = nullptr;
static int g_w = 0, g_h = 0, g_cur = 0;
static int g_load_stretch = 0;
static std::string g_scenario, g_anchor, g_verdict;
static bool g_show[3] = {true, true, true};   // port, retail, diff
static const int AMP = 6;
// draw-stepping (N3): render only the first g_draw_step draws of each side (render_upto)
// so a frame can be watched building up draw-by-draw / a divergent draw isolated.
static bool g_step_on = false;
static bool g_solo = false;          // step mode: solo a single draw [J,J+1) vs prefix [0,K)
static int  g_draw_step = 0;
static Col  g_stepmetric;            // scratch diff metric while stepping (don't clobber the column)

static void destroy_device();

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

// compute the diff metric for EVERY column at load (cheap: ~3ms render ×2 + a px loop),
// so the diff ribbon + worst-frame are immediately meaningful.
static void precompute_metrics()
{
    for (auto &c : g_cols) {
        if (c.port_idx < 0 || c.retail_idx < 0) continue;   // honest gap — no diff
        const uint8_t *p = orv3_replay_render(g_port, c.port_idx);
        const uint8_t *r = orv3_replay_render(g_retail, c.retail_idx);
        if (p && r) diff_into(p, r, nullptr, c);
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

static bool load_view(const char *path)
{
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    json m; try { f >> m; } catch (const std::exception &e) { fprintf(stderr, "json: %s\n", e.what()); return false; }

    g_scenario = m.value("scenario", "");
    g_anchor = m.value("anchor", "");
    g_verdict = m.value("verdict", "");
    g_load_stretch = m.value("load_stretch", 0);
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
        g_cols.push_back(c);
    }
    if (g_cols.empty()) { fprintf(stderr, "view has no frames\n"); return false; }

    g_tport = make_tex(); g_tretail = make_tex(); g_tdiff = make_tex();
    g_diffbuf = (uint8_t *)malloc((size_t)g_w * g_h * 4);
    precompute_metrics();
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

// one panel: a labelled image (or a gap placeholder), scaled to `panel_w`.
static void panel(const char *label, LPDIRECT3DTEXTURE9 tex, bool present, float panel_w)
{
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.92f, 1.0f), "%s", label);
    float h = panel_w * g_h / g_w;
    if (present)
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(panel_w, h));
    else {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + panel_w, p.y + h), IM_COL32(28, 24, 24, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + panel_w/2 - 30, p.y + h/2), IM_COL32(255, 180, 84, 255), "(gap)");
        ImGui::Dummy(ImVec2(panel_w, h));
    }
    ImGui::EndGroup();
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
    ImGui::SameLine(); ImGui::Text("col %d/%d · offset %d", g_cur, (int)g_cols.size() - 1, c.offset);

    // draw-step row (N3): step through draws (render_upto / render_range) to watch a
    // frame build up (prefix) or ISOLATE one draw (solo) — each side independently.
    int maxd = col_maxdraws(c);
    int slmax = g_solo ? (maxd > 0 ? maxd - 1 : 0) : maxd;     // solo: draw INDEX; prefix: LENGTH
    if (ImGui::Checkbox("draw step", &g_step_on)) { if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
    ImGui::SameLine(); ImGui::BeginDisabled(!g_step_on);
    if (ImGui::Checkbox("solo", &g_solo)) { if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(-440);
    if (ImGui::SliderInt("##drawstep", &g_draw_step, 0, slmax, g_solo ? "draw #%d (solo)" : "first %d draws")) {
        if (g_draw_step < 0) g_draw_step = 0; show_column(g_cur);
    }
    ImGui::SameLine(); if (ImGui::Button("-")) { if (g_draw_step > 0) g_draw_step--; show_column(g_cur); }
    ImGui::SameLine(); if (ImGui::Button("+")) { if (g_draw_step < slmax) g_draw_step++; show_column(g_cur); }
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
    if (g_show[0]) { panel("port", g_tport, c.port_idx >= 0, pw); first = false; }
    if (g_show[1]) { if (!first) ImGui::SameLine(); panel("retail", g_tretail, c.retail_idx >= 0, pw); first = false; }
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
    ImGui::SameLine(); ImGui::TextDisabled("worst: offset %d gt8=%dpx", g_cols[worst].offset, g_cols[worst].gt8);

    ImGui::Separator();
    const Col &dm = g_step_on ? g_stepmetric : c;   // stepped diff vs full-frame diff
    ImGui::Text("identity: %s+%d   |   diff%s: gt8 %d px · meanabs %.4f · max|d| %d%s",
                g_anchor.c_str(), c.offset, g_step_on ? " (stepped)" : "",
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

    ImGui::TextDisabled("keys: ,/. ±1 · arrows ±10 · Home/End · 1/2/3 panels · [ ] draw± · s solo · w worst · n next");
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

static int do_shot(const char *out, const char *view, int W, int H, int col, int draw_step)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer_shot"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "orv3", WS_OVERLAPPEDWINDOW, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, W, H)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (view && !load_view(view)) return 2;
    if (draw_step >= 0) { g_step_on = true; g_draw_step = draw_step; }   // headless draw-step verify
    if (col > 0) seek(col); else if (g_step_on) show_column(g_cur);      // apply col/step before the shot (g_solo may be preset)
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
    if (ImGui::IsKeyPressed(ImGuiKey_W)) { int wbest = 0; for (size_t i = 0; i < g_cols.size(); i++) if (g_cols[i].gt8 > g_cols[wbest].gt8) wbest = (int)i; seek(wbest); }
    if (ImGui::IsKeyPressed(ImGuiKey_N)) { for (size_t i = g_cur + 1; i < g_cols.size(); i++) if (g_cols[i].gt8 > 0) { seek((int)i); break; } }
    // [ ] step draws (engages draw-stepping); s toggles solo; clamp to this column's max
    int maxd = col_maxdraws(g_cols[g_cur]);
    int slmax = g_solo ? (maxd > 0 ? maxd - 1 : 0) : maxd;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))  { g_step_on = true; if (g_draw_step > 0)     g_draw_step--; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) { g_step_on = true; if (g_draw_step < slmax) g_draw_step++; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) { g_solo = !g_solo; g_step_on = true; if (g_draw_step > slmax) g_draw_step = slmax; show_column(g_cur); }
}

static int do_interactive(const char *view)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.style = CS_CLASSDC; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "trace studio v3", WS_OVERLAPPEDWINDOW,
                              60, 40, 1400, 920, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, 1400, 920)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
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
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    shutdown_all(); DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *shot = nullptr, *view = nullptr;
    int col = 0, draw_step = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (strcmp(argv[i], "--col") == 0 && i + 1 < argc) col = atoi(argv[++i]);
        else if (strcmp(argv[i], "--draw-step") == 0 && i + 1 < argc) draw_step = atoi(argv[++i]);
        else if (strcmp(argv[i], "--solo") == 0) g_solo = true;
        else view = argv[i];
    }
    if (shot) return do_shot(shot, view, 1400, 900, col, draw_step);
    return do_interactive(view);
}
