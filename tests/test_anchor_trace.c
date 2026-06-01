/*
 * test_anchor_trace.c — edge logic of the TAS anchor emitter
 * (src/anchor_trace.c). Pure: drives the module with hand-built
 * world snapshots and asserts which anchors fire on which frame.
 */
#define _GNU_SOURCE   /* fmemopen */
#include "t.h"
#include "anchor_trace.h"

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

/* ── the opening prologue fires HOUSE_FREEROAM TWICE ─────────────────────
 *
 * Retail (Frida `…retail-20260601T193256Z`) brackets the new-game→playable
 * path with two load overlays → two HOUSE_FREEROAM edges:
 *   #1 = the new-game HOUSE scene load (worker_load); iv1_1 runs after it.
 *   #2 = the iv1_1→iv1_2 inter-script load (src/scene1_intro_dialogue.c's
 *        D_LOAD bracket, ~68 frames), now driving anchor_world.loading_active
 *        via scene1_intro_dialogue_loading() — this replaced the old
 *        scene1_intro_events double-load stub.
 * The TAS segtraces `wait HOUSE_FREEROAM` twice, the 2nd resolving on #2.
 * Both loading windows are Win32-runtime concerns; here we drive the anchor
 * module with the world shape they produce and assert the edge logic turns it
 * into the retail-shaped double bracket. */
int test_anchor_dialogue_double_house_freeroam(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    /* Frame 0: BOOT baseline at the title, idle. */
    anchor_trace_tick(&st, 0, W(0 /*TITLE*/, 0), rec_sink, &r);

    /* Frame 1: new-game commit → INGAME with the scene load up (overlay raised
     * the same tick scene_post_fade_init flips state). NEW_GAME + LOADING_START #1. */
    anchor_trace_tick(&st, 1, W(1 /*INGAME*/, 1 /*loading*/), rec_sink, &r);

    /* Frame 2: the scene load completes. LOADING_END #1 + HOUSE_FREEROAM #1. */
    anchor_trace_tick(&st, 2, W(1, 0), rec_sink, &r);

    /* Frames 3..9: iv1_1 dialogue runs, scene load-free (no load anchors). */
    for (uint32_t f = 3; f <= 9; f++)
        anchor_trace_tick(&st, f, W(1, 0), rec_sink, &r);

    /* Frame 10: iv1_1 ends; the dialogue raises its inter-script loading
     * bracket (D_LOAD). LOADING_START #2. */
    anchor_trace_tick(&st, 10, W(1, 1), rec_sink, &r);

    /* Frames 11..77: the 68-frame bracket holds (loading stays up). */
    for (uint32_t f = 11; f <= 77; f++)
        anchor_trace_tick(&st, f, W(1, 1), rec_sink, &r);

    /* Frame 78: bracket drops; iv1_2 begins. LOADING_END #2 + HOUSE_FREEROAM #2. */
    anchor_trace_tick(&st, 78, W(1, 0), rec_sink, &r);

    /* Count the HOUSE_FREEROAM / LOADING firings — the property under test. */
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
    return 0;
}

/* ─── opening-prologue dialogue anchors (TEXT_ANIM_START/END) ───────────────
 * Build a world with the dialogue fields set. Scene/loading don't matter for
 * these edges (gated only on dlg_active), so default them to INGAME/idle. */
static struct anchor_world WD(int dlg_active, int32_t reveal, int revealed)
{
    struct anchor_world w = {0};
    w.scene_state   = 1; /* INGAME */
    w.dlg_active    = dlg_active;
    w.text_reveal   = reveal;
    w.text_revealed = revealed;
    return w;
}

/* Count how many times `name` fired in a recording. */
static int rec_count(const struct rec *r, const char *name)
{
    int n = 0;
    for (int i = 0; i < r->n; i++)
        if (strcmp(r->name[i], name) == 0) n++;
    return n;
}

/* TEXT_ANIM_START fires on the reveal counter's reset to 1 (new line), and
 * recurs for each line — including the first (0→1). It does NOT fire while the
 * counter merely climbs (1,2,3,…). */
int test_anchor_text_start_per_line(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0, WD(0, 0, 0), rec_sink, &r);   /* BOOT, baseline */
    /* First line: reveal 0 → 1 (START), then climbs (no edge). */
    anchor_trace_tick(&st, 1, WD(1, 1, 0), rec_sink, &r);   /* START */
    anchor_trace_tick(&st, 2, WD(1, 2, 0), rec_sink, &r);
    anchor_trace_tick(&st, 3, WD(1, 3, 0), rec_sink, &r);
    /* Skip-to-full clamps at 0x800 — still no START. */
    anchor_trace_tick(&st, 4, WD(1, 0x800, 1), rec_sink, &r);
    /* Next line: counter forced back to 1 → START again. */
    anchor_trace_tick(&st, 5, WD(1, 1, 0), rec_sink, &r);   /* START */
    anchor_trace_tick(&st, 6, WD(1, 2, 0), rec_sink, &r);

    T_ASSERT_EQ_I(rec_count(&r, "TEXT_ANIM_START"), 2);
    return 0;
}

/* TEXT_ANIM_END fires on the fully-revealed flag's 0→1 rise, once per line. */
int test_anchor_text_end_rising_edge(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0, WD(0, 0, 0), rec_sink, &r);   /* BOOT */
    anchor_trace_tick(&st, 1, WD(1, 1, 0), rec_sink, &r);   /* revealing */
    anchor_trace_tick(&st, 2, WD(1, 40, 0), rec_sink, &r);  /* revealing */
    anchor_trace_tick(&st, 3, WD(1, 80, 1), rec_sink, &r);  /* END (0→1) */
    anchor_trace_tick(&st, 4, WD(1, 81, 1), rec_sink, &r);  /* held, no edge */
    /* New line: flag drops back to 0, then rises again → 2nd END. */
    anchor_trace_tick(&st, 5, WD(1, 1, 0), rec_sink, &r);
    anchor_trace_tick(&st, 6, WD(1, 80, 1), rec_sink, &r);  /* END */

    int end_idx = -1;
    for (int i = 0; i < r.n; i++)
        if (strcmp(r.name[i], "TEXT_ANIM_END") == 0) { end_idx = i; break; }
    T_ASSERT(end_idx >= 0);
    T_ASSERT_EQ_U(r.frame[end_idx], 3);     /* first END on frame 3 */
    T_ASSERT_EQ_I(rec_count(&r, "TEXT_ANIM_END"), 2);
    return 0;
}

/* Both text edges are gated on dlg_active: identical reveal-state movement
 * with dialogue inactive fires nothing (the globals are stale out of dialogue). */
int test_anchor_text_gated_on_dlg_active(void)
{
    struct anchor_trace_state st = {0};
    struct rec r = {0};

    anchor_trace_tick(&st, 0, WD(0, 0, 0), rec_sink, &r);   /* BOOT */
    anchor_trace_tick(&st, 1, WD(0, 1, 0), rec_sink, &r);   /* reveal==1 but inactive */
    anchor_trace_tick(&st, 2, WD(0, 80, 1), rec_sink, &r);  /* flag 0→1 but inactive */

    T_ASSERT_EQ_I(rec_count(&r, "TEXT_ANIM_START"), 0);
    T_ASSERT_EQ_I(rec_count(&r, "TEXT_ANIM_END"), 0);
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
