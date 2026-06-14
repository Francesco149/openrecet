/*
 * anchor_trace.c — see anchor_trace.h.
 *
 * Edge-triggered anchor emitter. The anchor set is a static table of
 * (name, edge-predicate) pairs; `anchor_trace_tick` walks it once per
 * frame and emits each anchor whose predicate sees a rising edge between
 * the previous and current world snapshot.
 *
 * Scene-state values mirror src/scene.h (kept local so the module stays
 * Win32-free / engine-global-free and unit-testable in isolation).
 */
#include "anchor_trace.h"

#define ANCHOR_SCENE_TITLE   0
#define ANCHOR_SCENE_INGAME  1
/* SCENE_STATE_WORLDMAP (8) is written then overwritten with INGAME inside
 * a single sim tick during NEW-GAME init (scene.c scene_post_fade_init), so
 * no observer ever sees that transient flip between frames — we don't anchor
 * on it. (The real, persistent mode-8 world map is reached via the door-exit
 * and IS a distinct rendered scene; it just isn't an anchor today.) */

/* A "playable HOUSE" frame: in-game AND the loading overlay has dropped. */
static int is_house_freeroam(const struct anchor_world *w)
{
    return w->scene_state == ANCHOR_SCENE_INGAME && !w->loading_active;
}

/* ─── edge predicates ──────────────────────────────────────────────────
 * Each returns nonzero when its anchor's rising edge fired between the
 * previous snapshot `p` and the current one `c`. */

static int ev_new_game(const struct anchor_world *p, const struct anchor_world *c)
{
    /* Title → in-game: the "new game" (or continue) commit. The engine
     * passes through LOADING in the same tick, so the observable edge is
     * TITLE → INGAME directly. */
    return p->scene_state == ANCHOR_SCENE_TITLE
        && c->scene_state == ANCHOR_SCENE_INGAME;
}

static int ev_loading_start(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->loading_active && c->loading_active;
}

static int ev_loading_end(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->loading_active && !c->loading_active;
}

static int ev_house_freeroam(const struct anchor_world *p, const struct anchor_world *c)
{
    /* Rising edge of the compound "playable HOUSE" predicate. Fires once
     * when the in-game scene first becomes load-free — i.e. the moment a
     * render-parity capture against HOUSE is meaningful. Robust whether
     * the load-overlay drops before or after the INGAME flip. */
    return !is_house_freeroam(p) && is_house_freeroam(c);
}

/* The MARKET scene (engine mode 6 — the Merchant's Guild + its ichiba variant)
 * became active, i.e. the worldmap→guild transition's (non-deterministic-length)
 * scene LOAD just finished and mode 6 is up. The generic LOADING_END can't sync
 * this: 4+ of them fire (intro / worldmap / guild loads) and the guild one's
 * length differs run-to-run (port ~8f vs retail ~88f), so a window rebased on
 * "LOADING_END" picks the wrong/seamed one. MARKET_ENTER is the SPECIFIC,
 * unambiguous "guild scene up after its load" sync point a market-menu trace
 * {wait}s on so the post-load cutscene + menu frames line up. Fires once per
 * mode-6 entry (scene_state is the raw g_scene_state; 6 == the Market scene). */
#define ANCHOR_SCENE_MARKET 6
static int ev_market_enter(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->scene_state != ANCHOR_SCENE_MARKET
        && c->scene_state == ANCHOR_SCENE_MARKET;
}

static int ev_text_anim_start(const struct anchor_world *p, const struct anchor_world *c)
{
    /* A new dialogue line begins its typewriter reveal. The engine forces
     * the reveal counter to 1 on the new-line frame (DAT_073a3e00, 0x46c9a2);
     * it only equals 1 then (init 0, then climbs 1,2,…0x800), so reveal==1
     * with a different previous value is the exact per-line edge — and fires
     * the first line too (0→1). Gated on the current frame's dialogue-active
     * (the reveal globals are stale outside dialogue). Recurs per line. */
    return c->dlg_active && c->text_reveal == 1 && p->text_reveal != 1;
}

static int ev_text_anim_end(const struct anchor_world *p, const struct anchor_world *c)
{
    /* The current line finished scrolling and settled: the fully-revealed
     * flag rises 0→1 (DAT_073a3e04, 0x46c9a2). The deterministic "text
     * animation done, awaiting advance" edge. Recurs per line. */
    return c->dlg_active && c->text_revealed && !p->text_revealed;
}

/* ─── extra/effect-sprite lifecycle (fx_alpha edges) ───────────────────────
 * The four phases of any dialogue effect sprite's fade, off the aggregate
 * fx_alpha (max alpha over active index>=2 standees). For an instant-appear
 * sprite (e.g. the sigh, whose col snaps alpha to 255 the frame it shows),
 * START and FADED_IN coincide; for one that ramps, they straddle the fade-in.
 * All recur per sprite. "FULL" = 255 (the engine's opaque). */
static int ev_fx_start(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->fx_alpha == 0 && c->fx_alpha > 0;            /* sprite appears */
}
static int ev_fx_faded_in(const struct anchor_world *p, const struct anchor_world *c)
{
    return c->fx_alpha >= 255 && p->fx_alpha < 255;        /* reached full opacity */
}
static int ev_fx_fadeout(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->fx_alpha >= 255 && c->fx_alpha < 255 && c->fx_alpha > 0; /* leaves full */
}
static int ev_fx_end(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->fx_alpha > 0 && c->fx_alpha == 0;            /* fully gone */
}

/* Dialogue line shown/dismissed edges — frame the between-lines (box-gone) gap. */
static int ev_dlg_line_clear(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->dlg_line_present && !c->dlg_line_present;    /* line dismissed */
}
static int ev_dlg_line_show(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->dlg_line_present && c->dlg_line_present;    /* next line shown */
}

/* Conversation pose (engine-quirks §86) entered / left — the player actor state
 * field rises to / falls from 6 (the Recette listen-pose state). The blink
 * cycle resets on this edge, so it is the per-effect anchor §85 prescribes for
 * phase-aligned captures. Recurs per conversation. */
#define ANCHOR_CONV_POSE_STATE 6
static int ev_conv_pose_start(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->conv_pose_state != ANCHOR_CONV_POSE_STATE
        && c->conv_pose_state == ANCHOR_CONV_POSE_STATE;
}
static int ev_conv_pose_end(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->conv_pose_state == ANCHOR_CONV_POSE_STATE
        && c->conv_pose_state != ANCHOR_CONV_POSE_STATE;
}
/* Rising edge of the eyes-closed (cell 39) blink frame — the post-load sync
 * point for phase comparison (the pose-entry edge lands in the load fade). */
static int ev_conv_pose_blink(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->conv_pose_blink && c->conv_pose_blink;
}

/* Rising edge of "the opening prologue has fully ended" — the player just gained
 * free control (intro state machine reached D_DONE, after the 2nd ESC→confirm
 * skip or natural completion). This is the sync point to rebase a recorded
 * free-roam walk onto: unlike HOUSE_FREEROAM (load-overlay drop, still mid-iv1_2
 * conversation), it lands at controllable free roam. Fires once per prologue. */
static int ev_freeroam_start(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->intro_done && c->intro_done;
}

/* Returned to the title / main menu (the quit-to-title step of a save→reload
 * flow). Rising edge of scene_state == TITLE from any non-TITLE state — the
 * quit-to-title passes through LOADING (observed: the recorder saw scene go
 * INGAME→LOADING→TITLE, so a strict INGAME→TITLE edge missed it). The BOOT
 * baseline seed suppresses a spurious fire at the initial title. Lets a TAS
 * trace rebase the title-menu load-slot navigation onto this sync point. */
static int ev_title_return(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->scene_state != ANCHOR_SCENE_TITLE
        && c->scene_state == ANCHOR_SCENE_TITLE;
}

/* The in-game PAUSE menu opened / closed (DAT_0438b150 0→1 / 1→0). The save and
 * "quit to title" options are reached from here; anchoring the open lets the
 * menu-navigation segment re-sync instead of drifting from the preceding
 * free-roam/LOADING anchor. Recurs per pause. */
static int ev_pause_open(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->pause_active && c->pause_active;
}
static int ev_pause_close(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->pause_active && !c->pause_active;
}

/* The in-game PAUSE menu's async asset load (FUN_00473a3e — the 20-sprite pause
 * load) just COMPLETED while paused (scene mode 9): the menu is now navigable.
 * Expressed as the load-end edge GATED on the pause mode, so it never fires
 * prematurely on the ramp==3 frame (scene flips to 9 and the worker marks
 * loading on the SAME frame ⇒ loading_active is already true there). This is
 * the robust sync point for pause-SUBMENU navigation: PAUSE_OPEN fires pre-load
 * AND only on the port (retail's pause never sets the b150 cursor flag the
 * PAUSE_OPEN edge keys on), and the pause load stretches a DIFFERENT frame count
 * per side (port ~1f, retail ~1800f), so a nav segment must rebase HERE — not on
 * PAUSE_OPEN or the recurring LOADING_END. Fires once per pause open. */
#define ANCHOR_SCENE_PAUSE 9
static int ev_pause_ready(const struct anchor_world *p, const struct anchor_world *c)
{
    return p->loading_active && !c->loading_active
        && c->scene_state == ANCHOR_SCENE_PAUSE;
}

/* The pause SAVE submenu (type 3) just became navigable — sub_anim reached 10
 * with Save selected (rising edge of save_picker_active). PAUSE_READY syncs the
 * menu OPEN, but the async pause-asset load makes the open ramp land at a
 * per-side-variable phase, so PAUSE_READY+offset nav inputs reach the picker at
 * a DIFFERENT picker-time per side (the selected card breathes out of phase). A
 * nav segment rebases HERE so the inputs are picker-time-relative on both sides.
 * Fires once per Save-submenu open. */
static int ev_save_picker_ready(const struct anchor_world *p, const struct anchor_world *c)
{
    return !p->save_picker_active && c->save_picker_active;
}

struct anchor_def {
    const char *name;
    int (*fired)(const struct anchor_world *prev, const struct anchor_world *cur);
};

/* Table order = emission order when several fire on the same frame.
 * NEW_GAME / LOADING_START before LOADING_END / HOUSE_FREEROAM is the
 * causal order. */
static const struct anchor_def g_anchors[] = {
    { "NEW_GAME",        ev_new_game        },
    { "LOADING_START",   ev_loading_start   },
    { "LOADING_END",     ev_loading_end     },
    { "HOUSE_FREEROAM",  ev_house_freeroam  },
    { "MARKET_ENTER",    ev_market_enter    },
    { "TEXT_ANIM_START", ev_text_anim_start },
    { "TEXT_ANIM_END",   ev_text_anim_end   },
    { "EXTRA_SPRITE_START",    ev_fx_start    },
    { "EXTRA_SPRITE_FADED_IN", ev_fx_faded_in },
    { "EXTRA_SPRITE_FADEOUT",  ev_fx_fadeout  },
    { "EXTRA_SPRITE_END",      ev_fx_end      },
    { "DLG_LINE_CLEAR",        ev_dlg_line_clear },
    { "DLG_LINE_SHOW",         ev_dlg_line_show  },
    { "CONV_POSE_START",       ev_conv_pose_start },
    { "CONV_POSE_END",         ev_conv_pose_end   },
    { "CONV_POSE_BLINK",       ev_conv_pose_blink },
    { "FREEROAM_START",        ev_freeroam_start  },
    { "PAUSE_OPEN",            ev_pause_open      },
    { "PAUSE_CLOSE",           ev_pause_close     },
    { "PAUSE_READY",           ev_pause_ready     },
    { "SAVE_PICKER_READY",     ev_save_picker_ready },
    { "TITLE_RETURN",          ev_title_return    },
};
#define ANCHOR_COUNT ((int)(sizeof(g_anchors) / sizeof(g_anchors[0])))

void anchor_trace_tick(struct anchor_trace_state *st, uint32_t frame,
                       struct anchor_world cur,
                       anchor_sink_fn sink, void *user)
{
    if (!st->initialized) {
        /* First tick: mark t0 and seed the baseline. No edge predicates
         * run — there is no "previous" world to edge against, and firing
         * them against a zero baseline would spuriously emit (e.g. a
         * boot that begins with the loading overlay already up). */
        st->initialized = 1;
        st->prev        = cur;
        if (sink) sink("BOOT", frame, user);
        return;
    }

    for (int i = 0; i < ANCHOR_COUNT; i++) {
        if (g_anchors[i].fired(&st->prev, &cur)) {
            if (sink) sink(g_anchors[i].name, frame, user);
        }
    }
    st->prev = cur;
}

void anchor_trace_sink_jsonl(const char *name, uint32_t frame, void *user)
{
    FILE *fp = (FILE *)user;
    if (!fp) return;
    fprintf(fp, "{\"anchor\":\"%s\",\"frame\":%u}\n", name, frame);
}
