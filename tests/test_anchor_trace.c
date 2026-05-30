/*
 * test_anchor_trace.c — edge logic of the TAS anchor emitter
 * (src/anchor_trace.c). Pure: drives the module with hand-built
 * world snapshots and asserts which anchors fire on which frame.
 */
#define _GNU_SOURCE   /* fmemopen */
#include "t.h"
#include "anchor_trace.h"
#include "nowloading.h"
#include "scene1_intro_events.h"
#include "worker_load.h"

#include <string.h>

/* Recording sink: append every emitted anchor to a fixed buffer. */
struct rec {
    int   n;
    char  name[32][24];
    uint32_t frame[32];
};

static void rec_sink(const char *name, uint32_t frame, void *user)
{
    struct rec *r = (struct rec *)user;
    if (r->n >= 32) return;
    snprintf(r->name[r->n], sizeof r->name[0], "%s", name);
    r->frame[r->n] = frame;
    r->n++;
}

static struct anchor_world W(int32_t scene, int loading)
{
    struct anchor_world w = { scene, loading };
    return w;
}

/* First tick always emits exactly BOOT, regardless of the snapshot, and
 * runs no edge predicates (no spurious loading edge from a zero prev). */
int test_anchor_first_tick_emits_only_boot(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    /* Even though loading is already active at boot, no LOADING_START. */
    anchor_trace_tick(&st, 0, W(0 /*TITLE*/, 1 /*loading*/), rec_sink, &r);

    T_ASSERT_EQ_I(r.n, 1);
    T_ASSERT(strcmp(r.name[0], "BOOT") == 0);
    T_ASSERT_EQ_U(r.frame[0], 0);
    return 0;
}

/* The boot asset-load: loading active at boot, then drops → LOADING_END
 * fires on the falling edge, at the frame it dropped. */
int test_anchor_boot_loading_end(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0, W(0, 1), rec_sink, &r);   /* BOOT, baseline loading=1 */
    anchor_trace_tick(&st, 1, W(0, 1), rec_sink, &r);   /* still loading: nothing */
    anchor_trace_tick(&st, 7, W(0, 0), rec_sink, &r);   /* drop: LOADING_END@7 */

    T_ASSERT_EQ_I(r.n, 2);
    T_ASSERT(strcmp(r.name[1], "LOADING_END") == 0);
    T_ASSERT_EQ_U(r.frame[1], 7);
    return 0;
}

/* The new-game → house flow: TITLE→INGAME, then the load overlay raises
 * and drops. NEW_GAME and HOUSE_FREEROAM bracket the load. */
int test_anchor_new_game_to_house(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0,   W(0, 0), rec_sink, &r);   /* BOOT (title, idle) */
    /* Player commits new game on frame 100: scene flips TITLE→INGAME and
     * the load overlay raises in the same observed frame. */
    anchor_trace_tick(&st, 100, W(1, 1), rec_sink, &r);   /* NEW_GAME + LOADING_START */
    anchor_trace_tick(&st, 150, W(1, 1), rec_sink, &r);   /* loading: nothing */
    /* Load done on frame 158: overlay drops while in-game → LOADING_END
     * + HOUSE_FREEROAM both fire. */
    anchor_trace_tick(&st, 158, W(1, 0), rec_sink, &r);   /* LOADING_END + HOUSE_FREEROAM */

    /* BOOT, NEW_GAME, LOADING_START, LOADING_END, HOUSE_FREEROAM */
    T_ASSERT_EQ_I(r.n, 5);
    T_ASSERT(strcmp(r.name[0], "BOOT") == 0);
    T_ASSERT(strcmp(r.name[1], "NEW_GAME") == 0);
    T_ASSERT_EQ_U(r.frame[1], 100);
    T_ASSERT(strcmp(r.name[2], "LOADING_START") == 0);
    T_ASSERT_EQ_U(r.frame[2], 100);
    T_ASSERT(strcmp(r.name[3], "LOADING_END") == 0);
    T_ASSERT_EQ_U(r.frame[3], 158);
    T_ASSERT(strcmp(r.name[4], "HOUSE_FREEROAM") == 0);
    T_ASSERT_EQ_U(r.frame[4], 158);
    return 0;
}

/* HOUSE_FREEROAM is a rising edge of a compound predicate: it must fire
 * exactly once even if the in-game/load-free state persists for many
 * frames, and must not re-fire while held. */
int test_anchor_house_freeroam_fires_once(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0,  W(0, 0), rec_sink, &r);   /* BOOT */
    anchor_trace_tick(&st, 10, W(1, 0), rec_sink, &r);   /* NEW_GAME + HOUSE_FREEROAM */
    anchor_trace_tick(&st, 11, W(1, 0), rec_sink, &r);   /* held: nothing */
    anchor_trace_tick(&st, 12, W(1, 0), rec_sink, &r);   /* held: nothing */

    /* BOOT, NEW_GAME@10, HOUSE_FREEROAM@10 — and nothing after. */
    T_ASSERT_EQ_I(r.n, 3);
    T_ASSERT(strcmp(r.name[2], "HOUSE_FREEROAM") == 0);
    T_ASSERT_EQ_U(r.frame[2], 10);
    return 0;
}

/* A re-entered load (e.g. leaving the house and returning) re-fires the
 * loading edges and HOUSE_FREEROAM again — edges, not one-shots. */
int test_anchor_reentrant_loading(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0,  W(1, 0), rec_sink, &r);   /* BOOT, in-game free */
    /* Wait — BOOT seeds in-game/free as the baseline, so no HOUSE_FREEROAM
     * yet. Drop into a load, then back out. */
    anchor_trace_tick(&st, 20, W(1, 1), rec_sink, &r);   /* LOADING_START@20 */
    anchor_trace_tick(&st, 40, W(1, 0), rec_sink, &r);   /* LOADING_END + HOUSE_FREEROAM@40 */

    T_ASSERT_EQ_I(r.n, 4);
    T_ASSERT(strcmp(r.name[1], "LOADING_START") == 0);
    T_ASSERT(strcmp(r.name[2], "LOADING_END") == 0);
    T_ASSERT(strcmp(r.name[3], "HOUSE_FREEROAM") == 0);
    T_ASSERT_EQ_U(r.frame[3], 40);
    return 0;
}

/* ── intro-events stub: the port fires HOUSE_FREEROAM TWICE ──────────────
 *
 * Models one sim frame's gate logic (scene1_intro_events_tick BEFORE the
 * worker-busy check, then sim_step_a's "clear the overlay when not busy"),
 * samples the resulting world into anchor_trace, and asserts the stub turns
 * the port's single new-game load into the retail-shaped double
 * HOUSE_FREEROAM the TAS segtrace waits on. See src/scene1_intro_events.c. */
static void ie_model_frame(struct anchor_trace_state *st, uint32_t frame,
                           struct rec *r)
{
    /* sim_step_a order: intro tick first (may raise/drop the gate), then the
     * worker-busy check clears the overlay when the worker is idle. */
    scene1_intro_events_tick();
    if (!worker_load_busy())
        nowloading_set_active(0);

    struct anchor_world w = W(1 /*INGAME*/, nowloading_is_active());
    anchor_trace_tick(st, frame, w, rec_sink, r);
}

int test_anchor_intro_events_double_house_freeroam(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    nowloading_reset();
    worker_load_reset();
    scene1_intro_events_reset();

    /* Frame 0: BOOT baseline at the title, idle. */
    anchor_trace_tick(&st, 0, W(0 /*TITLE*/, 0), rec_sink, &r);

    /* Frame 1: new-game commit. scene_post_fade_init flips to INGAME, kicks
     * the first load (busy + overlay up), and arms the stub. */
    worker_load_begin();
    scene1_intro_events_arm();
    ie_model_frame(&st, 1, &r);          /* NEW_GAME + LOADING_START */

    /* Frame 2: the first (real) load completes — the worker thread drops
     * busy; the gate clears this frame. */
    worker_load_end();
    ie_model_frame(&st, 2, &r);          /* LOADING_END + HOUSE_FREEROAM #1 */

    /* Frames 3.. : the stub observes free-roam, waits out its delay, then
     * raises + drops the second-event load. Run enough frames to cover the
     * whole sequence. */
    for (uint32_t f = 3; f <= 30; f++)
        ie_model_frame(&st, f, &r);

    /* Count the HOUSE_FREEROAM firings — the property under test. */
    int n_hf = 0, n_ls = 0;
    for (int i = 0; i < r.n; i++) {
        if (strcmp(r.name[i], "HOUSE_FREEROAM") == 0) n_hf++;
        if (strcmp(r.name[i], "LOADING_START")  == 0) n_ls++;
    }
    T_ASSERT_EQ_I(n_hf, 2);   /* the whole point: two playable-HOUSE edges */
    T_ASSERT_EQ_I(n_ls, 2);   /* each bracketed by its own load */

    /* And the sequence is well-formed: first HF strictly precedes the second
     * LOADING_START (so a segtrace's 2nd `wait HOUSE_FREEROAM`, entered on
     * HF#1, resolves on HF#2 and not HF#1). */
    int idx_hf1 = -1, idx_ls2 = -1, seen_ls = 0;
    for (int i = 0; i < r.n; i++) {
        if (idx_hf1 < 0 && strcmp(r.name[i], "HOUSE_FREEROAM") == 0)
            idx_hf1 = i;
        if (strcmp(r.name[i], "LOADING_START") == 0) {
            if (++seen_ls == 2) idx_ls2 = i;
        }
    }
    T_ASSERT(idx_hf1 >= 0 && idx_ls2 >= 0);
    T_ASSERT(idx_hf1 < idx_ls2);

    scene1_intro_events_reset();
    worker_load_reset();
    nowloading_reset();
    return 0;
}

/* The JSONL convenience sink emits the exact shared wire format. */
int test_anchor_jsonl_sink_format(void)
{
    char buf[64];
    FILE *fp = fmemopen(buf, sizeof buf, "w");
    T_ASSERT(fp != NULL);
    anchor_trace_sink_jsonl("HOUSE_FREEROAM", 3258, fp);
    fclose(fp);
    T_ASSERT(strcmp(buf, "{\"anchor\":\"HOUSE_FREEROAM\",\"frame\":3258}\n") == 0);
    return 0;
}
