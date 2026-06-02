/*
 * scene1_intro_dialogue.c — see the header. Drives iv1_1.ivt → iv1_2.ivt
 * through the scene1_dialogue_run interpreter and surfaces the TEXT_ANIM
 * anchor state.
 */
#include "scene1_intro_dialogue.h"

#include "scene1_dialogue.h"
#include "scene1_dialogue_run.h"

#include <stddef.h>   /* NULL */

/* The opening prologue is scene selector 1, sub 1 then 2 (DAT_005c7a2c=1,
 * DAT_005c7a30 = 1 → 2). Pinned via runs/intro-script-probe. */
#define IVE_OPENING_SCENE 1
#define IVE_OPENING_SUB1  1
#define IVE_OPENING_SUB2  2

/* Inter-script loading bracket (iv1_1 → iv1_2). Retail raises the load overlay
 * for 68 frames here (Frida `…retail-20260601T193256Z`: LOADING_START #2 @ 4581,
 * LOADING_END #2 @ 4649) → the 2nd LOADING/HOUSE_FREEROAM pair. See
 * docs/findings/opening-prologue.md "the script-load / gate / transition
 * subsystem". PORT-DEBT: the 68 is synthetic — iv1_2's bg/chr/se assets aren't
 * loaded or rendered yet, so there is no real async load to time against; the
 * faithful part is the *structure* (a loading bracket in the right place, after
 * iv1_1's last line and before iv1_2's first). The residual ~35 frames of
 * gap #16 (retail 389 vs this ~354) is the deferred shatter/melt transition
 * (FUN_0045281c/004526f5), which is render and lands with the visual pass. */
#define IVE_INTERSCRIPT_LOAD_FRAMES 68

enum { D_IDLE = 0, D_SCRIPT1, D_LOAD, D_SCRIPT2, D_DONE };

static int                g_state    = D_IDLE;
static int                g_load_ctr = 0;     /* frames elapsed in D_LOAD */
static struct ive_program g_prog;   /* reused per script (~160 KiB) */
static struct ive_runtime g_rt;
static unsigned           g_script_gen = 0;  /* bumps per loaded script (asset reload key) */

void scene1_intro_dialogue_arm(void)
{
    g_state       = D_SCRIPT1;
    g_load_ctr    = 0;
    g_rt.active   = 0;     /* lazy-load iv1_1 on the first tick */
    g_rt.complete = 0;
}

void scene1_intro_dialogue_reset(void)
{
    g_state       = D_IDLE;
    g_load_ctr    = 0;
    g_rt.active   = 0;
    g_rt.complete = 0;
}

/* Load script `sub` of the opening scene and arm the interpreter on it.
 * Returns 1 on success; 0 (and the caller gives up) if the script is missing. */
static int start_script(int sub)
{
#ifdef _WIN32
    /* scene1_dialogue_load pulls the .ivt from the storage layer (Win32-only).
     * In the host test build there is no storage, so the driver stays dormant;
     * the interpreter itself is exercised directly in test_scene1_dialogue_run. */
    if (!scene1_dialogue_load(IVE_OPENING_SCENE, sub, &g_prog))
        return 0;
    ive_runtime_init(&g_rt, &g_prog);
    g_script_gen++;   /* the render pass reloads bg/chr assets on a new gen */
    return 1;
#else
    (void)sub;
    return 0;
#endif
}

void scene1_intro_dialogue_tick(uint16_t held)
{
    switch (g_state) {
    case D_SCRIPT1:
        if (!g_rt.active && !g_rt.complete && !start_script(IVE_OPENING_SUB1)) {
            g_state = D_DONE;
            return;
        }
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete) {       /* end: → ret 3; ive sets active=0 too */
            g_state       = D_LOAD; /* raise the inter-script loading bracket */
            g_load_ctr    = 0;
            g_rt.complete = 0;     /* clear so the script-2 lazy-load fires */
        }
        break;

    case D_LOAD:
        /* The iv1_1→iv1_2 load overlay. scene1_intro_dialogue_loading() reads
         * D_LOAD, which main.c folds into anchor_world.loading_active — so this
         * window drives LOADING_START/END #2 and (via the in-game-and-load-free
         * predicate) HOUSE_FREEROAM #2, replacing the scene1_intro_events stub.
         * The interpreter is idle here (dlg inactive), matching retail's gate==2
         * loading state. `held` is unused while loading. */
        (void)held;
        if (++g_load_ctr >= IVE_INTERSCRIPT_LOAD_FRAMES)
            g_state = D_SCRIPT2;   /* next tick lazy-loads iv1_2 */
        break;

    case D_SCRIPT2:
        if (!g_rt.active && !g_rt.complete && !start_script(IVE_OPENING_SUB2)) {
            g_state = D_DONE;
            return;
        }
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete)
            g_state = D_DONE;
        break;

    case D_IDLE:
    case D_DONE:
    default:
        break;
    }
}

void scene1_intro_dialogue_skip_to_end(void)
{
    /* The ESC skip-event "Yes" teardown at port altitude. CORRECTION (user-
     * confirmed 2026-06-02): a skip ends only the *current* script, not the
     * whole prologue. The opening is a two-script sequence (iv1_1 → iv1_2);
     * skipping iv1_1 advances to iv1_2 (the 2nd dialogue, which plays over the
     * HOUSE free-roam map), and only skipping iv1_2 drops into free control.
     *
     * Engine mechanism: the skip teardown forces the running script to its
     * end:-equivalent — the dialogue gate (DAT_0438b1c8) drops to 0, then
     * FUN_0044baad arms the queued next script (DAT_06a4706c), exactly as a
     * natural end: does. So at this altitude a skip == "the current script
     * just completed": we replay the same state-machine transition the normal
     * g_rt.complete path takes in scene1_intro_dialogue_tick (D_SCRIPT1 →
     * D_LOAD → D_SCRIPT2, D_SCRIPT2 → D_DONE). Idempotent across states.
     *
     * PORT-DEBT(simplified, FUN_00473c03): the engine teardown also restores the
     * snapshot resume state (DAT_06a499a8 + FUN_00473c03's camera/player reseat),
     * irrelevant here — the dialogue plays over the already-spawned HOUSE. */
    switch (g_state) {
    case D_SCRIPT1:
        /* End iv1_1 → raise the inter-script load bracket → iv1_2 (same as the
         * natural g_rt.complete transition in _tick). */
        g_state       = D_LOAD;
        g_load_ctr    = 0;
        g_rt.active   = 0;
        g_rt.complete = 0;
        break;
    case D_LOAD:
        /* Skipping during the inter-script load → go straight to iv1_2. */
        g_state    = D_SCRIPT2;
        g_load_ctr = 0;
        break;
    case D_SCRIPT2:
    default:
        /* End iv1_2 (the last script) → free-roam. */
        g_state       = D_DONE;
        g_load_ctr    = 0;
        g_rt.active   = 0;
        g_rt.complete = 0;
        break;
    }
}

int scene1_intro_dialogue_active(void)
{
    return ((g_state == D_SCRIPT1 || g_state == D_SCRIPT2) && g_rt.active) ? 1 : 0;
}

int scene1_intro_dialogue_covers_screen(void)
{
    /* iv1_1 (D_SCRIPT1) is the opening cutscene: its script paints a full-
     * screen bg, so the engine's FUN_0046c869 returns non-zero and
     * FUN_004547ab suppresses the whole scene + HUD render behind it.  iv1_2
     * (D_SCRIPT2) is an overlay over the live HOUSE map (no bg → scene + HUD
     * drawn behind it).  We hold this for the whole D_SCRIPT1 phase (incl. the
     * pre-load frame) so the persistent top HUD never flashes over the
     * cutscene.  The D_LOAD bracket is covered separately by the nowloading
     * gate. */
    return (g_state == D_SCRIPT1) ? 1 : 0;
}

int scene1_intro_dialogue_skippable(void)
{
    /* FUN_0046c2cb gate: a line is up and skip_prompt (DAT_073a3e18, bumped
     * every dialogue frame by FUN_0046c320) is past 1. */
    return (scene1_intro_dialogue_active() && g_rt.scene.skip_prompt > 1) ? 1 : 0;
}

int scene1_intro_dialogue_loading(void)
{
    return (g_state == D_LOAD) ? 1 : 0;
}

int32_t scene1_intro_dialogue_text_reveal(void)
{
    return scene1_intro_dialogue_active() ? g_rt.reveal : 0;
}

int scene1_intro_dialogue_line_present(void)
{
    /* DAT_073a6a38 >= 0 — a dialogue line is currently shown (the box is open /
     * opening). Goes < 0 the frame a line is cleared (<C> / MSG_CLEAR), i.e. the
     * box-dismissed edge. Gated on dialogue-active. Feeds the DLG_LINE_CLEAR /
     * DLG_LINE_SHOW anchors that frame the between-lines (box-gone) gap. */
    return (scene1_intro_dialogue_active() && g_rt.line_row >= 0) ? 1 : 0;
}

int32_t scene1_intro_dialogue_fx_alpha(void)
{
    /* Max alpha (0-255) over the active "extra/effect" standees — index >= 2
     * (chr 0/1 are the persistent speaker characters; 4/5/6+ are the sigh / zzz
     * / sweat-drop pop-ups + the kuro fade-from-black). Gated on dialogue-active
     * so the EXTRA_SPRITE_* anchors don't fire on stale state. The scan range
     * mirrors the retail agent (anchorTick) so both sides agree. */
    if (!scene1_intro_dialogue_active())
        return 0;
    int maxa = 0;
    for (int i = 2; i <= 31 && i < IVE_STANDEE_COUNT; i++) {
        const int32_t *f = g_rt.scene.standees[i].field;
        if (f[IVE_ST_ACTIVE] == 0)
            continue;
        int a = (int)ive_word_f(f[18]);   /* current alpha */
        if (a < 0)   a = 0;
        if (a > 255) a = 255;
        if (a > maxa) maxa = a;
    }
    return maxa;
}

int scene1_intro_dialogue_text_revealed(void)
{
    return scene1_intro_dialogue_active() ? (g_rt.revealed ? 1 : 0) : 0;
}

const struct ive_runtime *scene1_intro_dialogue_runtime(void)
{
    return scene1_intro_dialogue_active() ? &g_rt : NULL;
}

const struct ive_program *scene1_intro_dialogue_program(void)
{
    return scene1_intro_dialogue_active() ? g_rt.prog : NULL;
}

unsigned scene1_intro_dialogue_generation(void)
{
    return g_script_gen;
}
