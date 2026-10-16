/*
 * scene1_player_ctrl.c — Cpop.1: HOUSE per-frame player controller (leaf math).
 * See scene1_player_ctrl.h for the chip writeup.  Engine FUN_0048b850.
 */

#include "scene1_player_ctrl.h"
#include "party.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "call_trace.h"          /* CALL_TRACE_ENTER / _STUB */
#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices, chr_anim_tick */
#include "scene1_particles_tick.h" /* g_scene1_player_pos[3] (DAT_056da1d8/dc/e0) */
#include "scene1_records.h"        /* g_scene1_records_a — foot-dust slot-state probe */
#include "input.h"               /* g_input_state[].buttons (held d-pad mask) */
#include "collision_house.h"     /* collision_house_get — live room mesh */
#include "collision_resolve.h"   /* collision_set_player_ground_y (the cc08 == 4 ground) */
#include "stage_palette.h"       /* g_stage_palette->mode (*DAT_068dd2f0) */
#include "title_save_dialog.h"   /* title_save_dialog_gate_tick (FUN_00434d6a guard) */
#include "scene1_spawn.h"        /* scene1_spawn (FUN_00447f4f) — foot-dust emit */
#include "scene1_companion_ctrl.h" /* scene1_companion_db054 (shared DAT_056db054) */
#include "rng.h"                 /* rng_next_unit (FUN_00471089) — dust jitter */
#include "audio.h"               /* display-menu SE (FUN_0049933c file / FUN_00499519 id) */
#include "story_visitor.h"        /* the day-8 visitor (work +0x2bcd8) */
#include "dungeon_party.h"        /* FUN_004897c6 */
#include "scene1_bg_npc.h"       /* scene1_bg_npc_tick (FUN_0046f621 NPC pump) */
#include "scene1_intro_dialogue.h" /* prologue gate — suppress the walk arm */
#include "scene1_tutorial_dispatch.h" /* iv2_5→iv2_6 beat — suppress the walk arm */
#include "scene1_overlay.h"      /* scene1_overlay_spawn (FUN_00414345) — sparkle */
#include "scene1_shop_display.h" /* furniture-layout grid + cell highlight (FUN_0048960d/619f) */
#include "scene1_display_menu.h" /* cc04==1 remove-item menu update/removal (A2) */
#include "scene1_counter_menu.h" /* cc08 0/10 counter menu (Open Store / Cancel) */
#include "save_work.h"           /* working-arena display grid (dword 0x4e26) */
#include "news_daily.h"          /* g_news_ticker_timer (DAT_0438b92c) pump  */
#include "save_bank.h"           /* SAVE_BANK_FIELD_DISPLAY_GRID */
#include "sim.h"                 /* g_sim_frame_count (DAT_0438b8cc), g_sim_buttons (edge mask) */
#include "stage_load_pulse.h"    /* display-menu open/close slide (FUN_004693e3 ramp) */
#include "scene1_combat_sm.h"    /* g_scene1_combat_dat_056da1b8 (sparkle owner) */
#include "scene1_camera.h"       /* g_scene1_camera_stage_view_mode (DAT_068dd3fc[stage*0x6cf]) */
#include "fade.h"                /* FUN_004526f5/0045281c/004528b3 — the door dissolve fade + load */
#include "scene.h"               /* g_scene_state (DAT_0438b1c0) — mode 8 = world map (T1) */
#include "scene_worldmap.h"      /* scene_worldmap_set_sel_dest (FUN_0049de0e) — initial dest */
#include "worker_load.h"         /* worker_load_spawn (FUN_00452cde) — the scene-load worker */
#include "customer_service.h"    /* cc08==4 customer-service session_init / master_tick */
#include "x87.h"                 /* spill32 / spill64 (the door-zone distance) */
#include "scene1_shop_walker.h"  /* scene1_customer_npc_pump (FUN_0047019f) — chibi crowd */
#include "scene1_actor_say.h"   /* actor_say_tick (FUN_00485861, the LAB_004893ff tail) */
#include "speech_bubble.h"      /* FUN_00406212 */
#include "scene1_day_close.h"   /* the night sequence (DAT_0438b4e0) + period advance */

/* Weak default for the diagnostic grid-dump capture dir: main.c provides the
 * strong definition (g_capture_dir) in the real Windows build; the host-test
 * harness (no main.o) falls back to this NULL → the dump stays inert. */
__attribute__((weak)) const char *openrecet_capture_dir(void) { return 0; }

/* ── Cchr.2h: the player/companion actor-state model ─────────────────────
 *
 * The per-actor fields the shop-walker player draw (FUN_004552d0 L357-454,
 * the loop over actor i = 0=player, 1/2=companion) reads each frame:
 *
 *   char id  (&DAT_056da1cc)[i]      — gate: != -1 (slot in use)
 *   XZ scale (&DAT_056dae18)[i]      — gate: > 0
 *   Y  scale (&DAT_056dae24)[i]      — gate: > 0
 *   record   (&DAT_056daae8)[i*0xb]  — 11-dword sprite-state the leaf samples
 *
 * Actor *position* is the separate DAT_056da1d8 array (actor 0 ==
 * g_scene1_player_pos); the draw side combines it with these fields.
 *
 * FUN_0048b850 (the per-frame player controller; Cpop) is these globals'
 * live writer — the ring shift at 48b850.c:483-584 fills DAT_056daae8 from
 * the live input/anim state and the spawn fade-in routine settles dae18/
 * dae24 to 1.0.  Until that body lands, player_ctrl_pose_house_standing()
 * seeds actor 0: char 0, scale 1.0/1.0, record anim 0 / timer 0 / counter 0 /
 * frame 0 / facing 6 — a fresh set-anim reset, retail's actual entry write
 * (the earlier runs/cchr2b frame-17544 snapshot seed of counter 25 / frame 2 /
 * timer 5.0f was a steady-state capture, NOT the entry state — RE §21.30).
 * This replaces the per-call scene1_shop_walker_set_player_inject MVP. */

#define s_actor_char     (g_party.chr)        /* DAT_056da1cc */
#define s_actor_scale_xz (g_party.scale_xz)   /* DAT_056dae18 */
#define s_actor_scale_y  (g_party.scale_y)    /* DAT_056dae24 */
#define s_actor_record   (g_party.rec)        /* DAT_056daae8 */

/* ── W3 free-roam walk state (the live player-controller globals) ──────────
 *   s_player_vel    = (daabc, daac0, daac4)  — world velocity (x, y, z)
 *   s_player_facing = db05c                  — world facing angle (radians)
 *   s_facing_sticky = dae3c                  — diagonal-snap horizontal bias
 * Reset to the idle standing pose by player_ctrl_pose_house_standing(): the
 * idle capture shows facing +π/2 (oct 6), zero velocity. */
#define s_player_vel     (g_party.vel[0])     /* DAT_056daabc */
#define s_player_facing  (PW(0x56db05cu).f)
#define s_facing_sticky  (PW(0x56dae3cu).i)

/* ── FUN_0048670f cc08 dispatch state (engine-quirks §78, Chip 4) ───────────
 *   s_cc08 = DAT_0438cc08 — the in-game interaction state the controller
 *            dispatches on each frame.  1 = free-roam walk; the other values
 *            are the event / camera / counter / menu / dialogue arms (see the
 *            state table in plans/house-controller-unmvp.md).
 *   s_cc04 = DAT_0438cc04 — the free-roam interaction sub-state.  0 = walking;
 *            1/2 = an object/customer interaction is active (cc08==1 arm).
 * The port sets cc08 to 1 at HOUSE entry (player_ctrl_cc08_enter_freeroam =
 * FUN_004850ec) and leaves cc04 at 0; the transitions that would drive them off
 * free-roam (customer approach → cc08=4, counter open → cc08=0x32, talk → cc04)
 * are unported features, so both stay at their free-roam values in steady play.
 * That keeps the dispatch a real read of live state (not the old unconditional
 * shell) while the off-path arms remain honest stubs. */
static int   s_cc08             = 0;              /* DAT_0438cc08 (BSS 0 until FUN_004850ec) */
/* cc08 boundary-frame markers (RE §21.28): the engine entry/leave arms decide
 * the frame's behavior on the FRAME-START cc08; these flag a mid-frame flip so
 * later-running controllers (the companion) can mirror that.  Cleared at the
 * top of scene1_player_ctrl_tick each frame. */
static int   s_cc04             = 0;              /* DAT_0438cc04 */
static int   s_worldmap_exit_armed = 0;           /* DAT_074b2ec4 — world-map exit pending the dissolve fade (T1) */
static int   s_cbe8             = 0;              /* DAT_0438cbe8 — display-menu open-once latch */

/* DAT_056db048 / DAT_056db04c — the player-ctrl state machine (FUN_0048cdcc)
 * slice the HOUSE flow exercises: 0 = free-roam, 0xc = the placement-confirm
 * hands-up carry pose (26 free-roam frames after the menu closes).  While
 * non-zero: d-pad interactions (all.c:87617) and the walk impulse (all.c:
 * 87524) are gated off; the b850 move/damp still runs. */
#define s_db048          (PW(0x56db048u).i)
#define s_db04c          (PW(0x56db04cu).i)

/* DAT_056db000 / DAT_056db004 — the free-roam INTERACTION-AFFORDANCE emote
 * bubble (the "GO!" door tooltip + the talk/pick-up prompts).  Driven per frame
 * by the bVar17 door-zone / bVar3 NPC-approach detection in house_update
 * (all.c:87491-87596), consumed by the FUN_0040a765 emote-bubble draw
 * (all.c:6900-6932, scene1_hud.c).
 *   s_emote_level (db000, 0..10) — the slide-in gauge: ramps up while a prompt
 *     is in range, down off it; the bubble's sin(level·π/8) scale + the visible
 *     (level!=0) gate read it.
 *   s_emote_type  (db004)         — which prompt: the hpmp_base.tga cell index
 *     ((type%4)·48+320, (type/4)·48).  7 = the shop-door "GO!" (the only path the
 *     port drives today); 0/1 = talk / talk-with-pending-customer (faithful
 *     no-op here — no live customers); 4 = the dungeon combat prompt (combat_sm). */
#define s_emote_level    (PW(0x56db000u).i)
#define s_emote_type     (PW(0x56db004u).i)

/* Per-frame latch: set when player_ctrl_b850_move() ticks the companion inline
 * (the free-roam walk path, mirroring the engine's FUN_0048a833-nested-in-
 * FUN_0048b850).  scene1_sim.c reads it after scene1_player_ctrl_tick() to run
 * the companion fallback ONLY on the non-walk frames (dialogue / menu / pre-
 * move).  Reset at the top of scene1_player_ctrl_tick(). */
static int   s_b850_ran = 0;              /* FUN_0048b850 ran this frame */

/* ── FUN_0048b850 tail render-bank state (Chip 2; engine all.c L90242+) ─────
 * The two after-image banks the chr-sprite walker (FUN_00456f56) draws —
 *   s_trail_bank = DAT_056dab6c (walker sweep 0, always read)
 *   s_burst_bank = DAT_056dacc0 (walker sweep 1, daae0-gated)
 * — and the two 40-slot motion-history rings that feed the burst:
 *   s_pos_hist = DAT_056da1fc (3-float pos),  s_rec_hist = DAT_056da3dc
 *   (the 11-dword sprite-state record).  FUN_0048b850's tail is the engine's
 *   live writer of all four; this module owns them (the b850 state lives here)
 *   and scene1_chr_walker.c reads the banks through player_ctrl_render_bank_
 *   slot() / player_ctrl_burst_count().  Each bank slot is the PC_TRAIL_*
 *   0x44-byte record (sprite[0..10], x/y/z at 0xb..0xd, life/age at 0xe — the
 *   walker's active gate), so the layout matches what the draw side reads.
 *
 * DORMANT in HOUSE free-roam: nothing lights the banks today — no dash spawns
 * a trail record (their countdowns stay 0; the FUN_0044376a dash-alloc path is
 * a later b850 sub-chip) and the burst counter DAT_056daae0 stays 0.  So every
 * frame the ring shift runs (real work) but both bank fills find nothing to do
 * and the walker draws no after-images, matching retail's plain walk.  This is
 * the faithful replacement for the synthetic single-slot inject that
 * scene1_chr_walker.c previously hand-built. */
#define s_pos_hist       (g_party.pos_hist)   /* DAT_056da1fc */
#define s_rec_hist       (g_party.rec_hist)   /* DAT_056da3dc */
#define s_trail_bank     (g_party.trail)      /* DAT_056dab6c */
#define s_burst_bank     (g_party.burst)      /* DAT_056dacc0 */
#define s_burst_count    (PW(0x56daae0u).i)   /* burst/egg after-image counter */
#define s_decay_spawn    (PW(0x56dae14u).i)   /* trail alloc edge down-counter */

void player_ctrl_pose_house_standing(int player_char)
{
    for (int i = 0; i < PC_NUM_ACTORS; i++) {
        s_actor_char[i]     = -1;
        s_actor_scale_xz[i] = 0.0f;
        s_actor_scale_y[i]  = 0.0f;
        memset(s_actor_record[i], 0, sizeof s_actor_record[i]);
    }

    /* W3: reset the live walk state to the idle standing pose. */
    s_player_vel[0] = s_player_vel[1] = s_player_vel[2] = 0.0f;
    s_player_facing = 1.57079633f;   /* +π/2 (idle facing, oct 6) */
    s_facing_sticky = 0;

    /* Enter free-roam: cc08 = 1 (via the engine's FUN_004850ec), cc04 = 0
     * (walking, no interaction).  This is the HOUSE-entry state the dispatch in
     * scene1_player_ctrl_tick reads each frame (§78). */
    player_ctrl_cc08_enter_freeroam();
    s_cc04 = 0;
    s_cbe8 = 0;
    s_db048 = 0;                /* retail zeroes the ctrl state on scene loads */
    s_db04c = 0;
    stage_load_pulse_reset();   /* display-menu slide dormant at HOUSE entry */

    /* Clear the b850-tail render banks + history rings (scene entry empties the
     * after-image state; the rings refill from the live sample each frame). */
    memset(s_pos_hist,   0, sizeof s_pos_hist);
    memset(s_rec_hist,   0, sizeof s_rec_hist);
    memset(s_trail_bank, 0, sizeof s_trail_bank);
    memset(s_burst_bank, 0, sizeof s_burst_bank);
    s_burst_count = 0;
    s_decay_spawn = 0;

    /* actor 0 = the standing player. */
    s_actor_char[0]     = player_char;
    s_actor_scale_xz[0] = 1.0f;   /* DAT_056dae18[0] — settled spawn scale */
    s_actor_scale_y[0]  = 1.0f;   /* DAT_056dae24[0] */

    /* DAT_056daae8[0] idle pose — a FRESH set-anim reset (anim 0, frame 0,
     * counter 0, timer 0), matching retail's entry write.  The old seed
     * (frame 2 / counter 25 / timer 5.0f) was the runs/cchr2b leaf snapshot of
     * a LATER steady-state frame (HOUSE 17544); re-applied at every HOUSE
     * entry it started Recette's idle cycle 25 ticks in, a constant 15-tick
     * wrap offset vs retail's 0-reset for the first ~45 free-roam frames
     * (viewer note #24, RE §21.30).  Retail's freeroam+1 probe reads
     * pframe=0/pcnt=1 ⇒ counter AND timer 0 at entry (frame steps every 10
     * ticks from the entry on). */
    s_actor_record[0][CHR_ACTOR_ANIM]    = 0;
    s_actor_record[0][CHR_ACTOR_TIMER]   = 0;
    s_actor_record[0][CHR_ACTOR_COUNTER] = 0;
    s_actor_record[0][CHR_ACTOR_FRAME]   = 0;
    s_actor_record[0][CHR_ACTOR_FACING]  = 6;

    /* actor 2 = the bobbing fairy companion (char id 1 = DAT_056da1d4).  At
     * HOUSE free-roam entry FUN_00435c98 sets (da1cc,da1d0,da1d4) = (0,3,1),
     * then FUN_00436f97 DISABLES actor 1 (da1d0 → -1) and settles all live
     * actor scales to 1.0 with record FACING = 4 — so actor 1 stays char -1
     * (left as the loop default above; never renders) while actor 2 is the
     * live companion.  Validated against the retail capture (runs/companion-
     * truth/FINDINGS.md, engine-quirks §71).  scene1_companion_ctrl_tick is the
     * live writer of its position + anim/facing from here on. */
    s_actor_char[2]     = 1;
    s_actor_scale_xz[2] = 1.0f;
    s_actor_scale_y[2]  = 1.0f;
    s_actor_record[2][CHR_ACTOR_FACING] = 4;   /* FUN_00436f97 scene-entry default */

    /* The companion's sprite anim is left at its zero-init ANIM-START (ANIM 0,
     * FRAME 0, TIMER 0, COUNTER 0) and ADVANCED every frame by
     * scene1_companion_ctrl_tick's chr_anim_tick — Tear's idle wings FLAP in a
     * 4-frame loop, and the wing-glow leaf reads the live FRAME (frames 0/1 =
     * spread, 2 = folded, 3 = mid; ground truth runs/comp-anim-probe).
     * An earlier attempt to seed a fixed pose here (FRAME 2, matching retail's
     * mid-anim chr_leaf snapshot at frame 17544) froze the wings on one phase
     * and diverged from retail's animated wings — the fix is to animate, not to
     * pick a frame.  See docs/findings/scene1-wing-glow.md, engine-quirks §81. */

    /* Companion position (DAT_056da1f0/f4/f8 = g_scene1_actor_pos[2]): seated by
     * scene1_postload_place_actors (FUN_004851e2) with the player. */
}

int player_ctrl_actor_char(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_char[i] : -1;
}

float player_ctrl_actor_scale_xz(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_scale_xz[i] : 0.0f;
}

float player_ctrl_actor_scale_y(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_scale_y[i] : 0.0f;
}

const int32_t *player_ctrl_actor_record(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_record[i] : NULL;
}

int32_t *player_ctrl_actor_record_mut(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_record[i] : NULL;
}

/* Trace-harness phase normalization (the segtrace `{phasepin}` op) for the
 * PLAYER (actor 0).  The companion side resets its own cycle in
 * scene1_companion_ctrl_phasepin; this is the player twin.  Recette's idle
 * breathing / walk anim cycle (FRAME/TIMER/COUNTER) free-runs from a seed laid
 * at scene entry, so at free-roam onset its phase ORIGIN depends on how long the
 * (non-deterministic) pre-free-roam load/dialogue ran — the same load-dependent
 * origin the companion has (engine-quirks §94).  While WALKING the idle↔walk
 * transition reset re-origins the cycle so a walk trace is already phase-clean
 * without this; a pure-IDLE comparison (e.g. house-movement cap_00) is NOT, and
 * needs this to normalize the origin.  Zeroes FRAME/TIMER/COUNTER (not ANIM/
 * FACING — those are state, not phase) so a retail run pinned the same way is
 * phase-clean.  Trace/comparison ONLY: the shipped game keeps the engine-
 * faithful free-running cycle (retail's own origin is equally load-dependent —
 * we normalize it in the TAS harness, NOT by re-seeding the shipped game). */
void player_ctrl_phasepin(void)
{
    union { float f; int32_t i; } z = { .f = 0.0f };
    s_actor_record[0][CHR_ACTOR_FRAME]   = 0;
    s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;
    s_actor_record[0][CHR_ACTOR_COUNTER] = 0;
}

/* Render banks the chr-sprite walker (FUN_00456f56) reads each frame: sweep 0 =
 * the dash-trail bank (DAT_056dab6c), sweep 1 = the burst bank (DAT_056dacc0).
 * Each slot is a PC_TRAIL_REC_DWORDS (0x44-byte) record — sprite-state[0..10],
 * pos x/y/z at [0xb..0xd], life/age at [0xe] (the walker's > 0 active gate).
 * Returns NULL for an out-of-range sweep/idx. */
const int32_t *player_ctrl_render_bank_slot(int sweep, int idx)
{
    if (idx < 0)
        return NULL;
    if (sweep == 0 && idx < PC_TRAIL_RECORDS)
        return s_trail_bank[idx];
    if (sweep == 1 && idx < PC_BURST_RECORDS)
        return s_burst_bank[idx];
    return NULL;
}

/* The burst-bank sweep gate DAT_056daae0 (the egg/spell after-image counter the
 * walker's sweep-1 loop is conditioned on). */
int player_ctrl_burst_count(void)
{
    return s_burst_count;
}

/* Set the stored world facing db05c (the conversation-pose driver's ±π/2 write,
 * FUN_0048407f; scene1_conversation_pose.c).  Held until the next walk frame. */
void player_ctrl_set_facing_angle(float angle)
{
    s_player_facing = angle;
}

void player_ctrl_emote_reset(void) { s_emote_level = 0; s_emote_type = 0; }
int  player_ctrl_db048(void)       { return s_db048; }
void player_ctrl_set_db048(int v)  { s_db048 = v; }
int  player_ctrl_b850_ran(void)           { return s_b850_ran; }

/* FUN_00436f97 blocks 11-15 (all.c:37155-37370), the player-controller part:
 * per actor i the walk velocity (DAT_056daabc + 3·i) = 0, the record facing
 * octant (DAT_056dab00 + 0xb·i) = 4, its flags (+0x1c..+0x28) = 0, the scales
 * (DAT_056dae18[i], DAT_056dae24[i]) = 1.0; then db05c = π (DAT_056db05c),
 * db048/db04c = 0, the emote ramp (DAT_056db000/004) = 0, the after-image
 * counters (DAT_056daae0, DAT_056dae14) = 0.  The anim fields are kept. */
/* FUN_00436f97's house arm: DAT_0438cbe8 = 0 — the next display-menu open is
 * a first open (FUN_00468338(0, 1): the list cursors reset). */
void player_ctrl_display_latch_reset(void) { s_cbe8 = 0; }
int  player_ctrl_display_latch(void)       { return s_cbe8; }

void player_ctrl_stage_entry_reset(void)
{
    for (int i = 0; i < PC_NUM_ACTORS; i++) {
        s_actor_record[i][CHR_ACTOR_FACING] = 4;
        s_actor_record[i][CHR_ACTOR_FLAG7]  = 0;
        s_actor_record[i][CHR_ACTOR_FLAG8]  = 0;
        s_actor_record[i][CHR_ACTOR_FLAG9]  = 0;
        s_actor_record[i][CHR_ACTOR_AGE]    = 0;
        s_actor_scale_xz[i] = 1.0f;
        s_actor_scale_y[i]  = 1.0f;
    }
    s_player_vel[0] = s_player_vel[1] = s_player_vel[2] = 0.0f;
    s_player_facing = 3.1415927f;
    s_db048 = 0;
    s_db04c = 0;
    s_emote_level = 0;
    s_emote_type  = 0;
    s_burst_count = 0;
    s_decay_spawn = 0;
}

/* ── FUN_0048670f cc08 dispatch state writer + accessors (Chip 4) ──────────── */

/* FUN_004850ec (0x4850ec, 18 B): the canonical "enter HOUSE free-roam" setter.
 * The engine body is `DAT_074b2ec4 = 0; DAT_0438cc08 = 1;` — it clears the
 * scene-exit latch then sets the in-game state id to free-roam.  The port models
 * the cc08 write (the observable state transition); the DAT_074b2ec4 latch is
 * the unported scene-exit subsystem (read only at the dispatch's all.c:344
 * `DAT_074b2ec4 == 1` arm, itself unported), so its reset is omitted — _STUB
 * marks the body as not-yet-complete end-to-end (cf. mark-stubbed-ports).  The
 * engine calls this on every path that hands control back to the player (scene
 * entry, dialogue/menu close); the port calls it from
 * player_ctrl_pose_house_standing at HOUSE entry. */
void player_ctrl_cc08_enter_freeroam(void)
{
    CALL_TRACE_ENTER_STUB(0x4850ecu);
    s_worldmap_exit_armed = 0;   /* DAT_074b2ec4 */
    s_cc08 = 1;
}

/* Read the current cc08 dispatch state (DAT_0438cc08). */
int player_ctrl_cc08(void)
{
    return s_cc08;
}

/* Read the cc08==1 free-roam interaction sub-state (DAT_0438cc04): 0 = walking,
 * 1 = in the display-stand remove menu.  The companion db054 clock + the
 * free-roam walk arm advance only while this is 0 (the menu freezes the HOUSE
 * sim, engine-quirks §110). */
int player_ctrl_cc04(void)
{
    return s_cc04;
}

/* True when player_ctrl_b850_move() ticked the companion inline this frame (the
 * free-roam walk path).  scene1_sim.c uses it to run the companion fallback only
 * on the non-walk frames where b850 was not reached (dialogue / menu / pre-move),
 * so the companion ticks exactly once per frame. */

void player_ctrl_companion_tick_from_cs(void)
{
    party_follow_tick();                 /* FUN_0048a833 (the master tick's head, 0x4624a1) */
}

/* Debug/test hook: force the cc08 state id.  Stands in for the unported
 * state-transition writers (customer-approach → 4, counter-open → 0x32, talk →
 * the cc04 path, …) so the host suite can verify the dispatch actually gates the
 * walk on cc08 rather than driving unconditionally. */
void player_ctrl_debug_set_cc08(int state)
{
    s_cc08 = state;
    /* a forced state is not a mid-frame boundary flip — clear the RE §21.28
     * markers (in live play scene1_player_ctrl_tick clears them per frame;
     * host tests drive the companion without the player tick, so a stale
     * marker from a previous test's enter_freeroam would stick). */
}

/* ── W3 free-roam walk leaves ─────────────────────────────────────────── */

/* input.c d-pad mask bits (input_binding_mask[0..3]). */

int player_ctrl_facing_octant(float angle, float cam_yaw)
{
    /* b850 0x48bfd2: ((angle + cam_yaw + π/8) / 2π) * 8 + 8, ftol, & 7.
     * The + π/8 is a half-octant round-to-nearest bias; the + 8 keeps the
     * truncated value non-negative before the mask (faithful to the engine,
     * whose `and eax,7` runs on the post-+8 positive int). */
    float t = (angle + cam_yaw + 0.39269909f) / 6.28318548f;
    return ((int)(t * 8.0f + 8.0f)) & 7;
}

/* FUN_00486435: the house bounds the room mesh lacks — the front (z) and,
 * outside customer service, the left of the counter (small rooms, shop tier
 * work +0x2cde0 < 3) or the doorway column (large rooms) */
void player_ctrl_house_room_clamp(float *px, float *pz)
{
    const uint32_t *w = save_work_dwords_at(save_work_active_slot());
    if (w != NULL && (int32_t)w[SHOP_DISPLAY_TIER_SELECTOR] >= 3) {
        if (*pz > 17.0f) *pz = 17.0f;
        if (s_cc08 != 4 && *pz > 11.5f && *px > 5.0f && *px < 9.5f) *px = 9.5f;
    } else {
        if (*pz > 9.5f) *pz = 9.5f;
        if (s_cc08 != 4 && *pz > 7.0f && *px < -1.5f) *px = -1.5f;
    }
}

/* ── FUN_0048670f structural skeleton ───────────────────────────────────────
 *
 * Faithful outer shape of the engine's INGAME interaction controller
 * (FUN_0048670f, all.c:86539-88178; engine-quirks §75/§77/§78).  The prologue
 * guard, the periodic customer-spawn refresh, the scene-transition fade
 * handlers, and the screen-rumble tail are all OFF the HOUSE free-roam near-path
 * — they land here as structural stubs (real VA + CALL_TRACE_ENTER_STUB where
 * the engine call is a standalone function, faithful no-op bodies that are
 * correctly inert in steady free-roam — §77).  The cc08 dispatch reads the live
 * state id (DAT_0438cc08, set to free-roam by FUN_004850ec at HOUSE entry):
 * cc08==1 routes the free-roam arm (player_ctrl_cc08_freeroam_arm), every other
 * state the unported event/menu/dialogue arm.  The cc08==1 arm wraps the walk in
 * the engine's escalation/cc04/proximity/interaction guards (§78), all inert in
 * steady free-roam so control reaches the walk unchanged. */

/* FUN_0048670f prologue + scene-transition arms (all.c:86579-86721): the
 * periodic customer-spawn refresh (the DAT_005ce3c4 actor loop, gated
 * DAT_0438b8cc%8==3, spawning shop customers via FUN_004147d5) and the four
 * scene-transition fade handlers (DAT_0450f470/485/495/488 — each runs a fade
 * + its own move/clamp and early-returns).  None of these flags is set during
 * steady HOUSE free-roam (the entry fade is long done, no customer this frame),
 * so the engine falls straight through to the controller.  Inline regions of
 * FUN_0048670f (no standalone VA), modelled as a no-op that signals "no
 * transition active" → the caller falls through.  Returns nonzero only when a
 * transition consumed the frame (never, in this chip). */
static int player_ctrl_scene_transition_tick(void)
{
    return 0;
}

/* FUN_00485861 (0x485861, 280 B): the FUN_0048670f tail (LAB_004893ff) — the
 * actor-say bubble driver, gated on DAT_0438b764 (scene1_actor_say.c). */
static void player_ctrl_tail_rumble(void)
{
    CALL_TRACE_ENTER(0x485861u);
    actor_say_tick();
}

/* ── cc08==1 arm inert sub-regions (FUN_0048670f all.c:919-1313) ─────────────
 *
 * Inline regions of the free-roam arm that surround the walk, modelled here as
 * the engine's logical sub-steps.  Each is inert in steady HOUSE free-roam — its
 * inputs (shop-customer count, nearest-customer/item position, a live
 * interaction target) have no writer in the port yet — so they reduce to a no-op
 * and control reaches the walk exactly as before (the chip is bit-exact).  They
 * are NOT standalone engine functions (no per-region VA), so they carry no
 * CALL_TRACE marker — matching player_ctrl_scene_transition_tick (Chip 3).  Each
 * is the home a future chip fleshes out as the corresponding feature lands.
 *
 * merchant-rank arrivals (all_ida 97213-97245): unless the iv2 "Day N" beat is
 * still up (b928==1 && b924<192), reaching shop rank 1 (DAT_0450fb98) runs the
 * scripted buy tutorial once (f428, script file 1) and rank 2 the next lesson
 * (f429, file 2; outside game mode 2): f404 = f405 = 1, cc08 = 4, the session
 * init, and FUN_00406212(0) — then FUN_0048670f returns.
 * PORT-DEBT(rank-arrival-tail): FUN_00406212(0) (DAT_00647e20 = 1) and the rank
 * ≥6 / ≥14 FUN_0044ba2c(19,1|2,0) story triggers (f4c5/f4c6). */
#define PC_F428_RANK1_BYTE_OFF  0x2bc90   /* DAT_0450f428 — rank-1 buy lesson shown */
#define PC_F429_RANK2_BYTE_OFF  0x2bc91   /* DAT_0450f429 — rank-2 lesson (requests on) */
#define PC_F404_SELL_ACTIVE_OFF 0x2bc6c   /* DAT_0450f404 */
#define PC_F405_ARRIVED_OFF     0x2bc6d   /* DAT_0450f405 */
static int player_ctrl_cc08_customer_escalate(void)
{
    uint8_t *bb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
    if (bb == NULL)
        return 0;
    if (scene1_tutorial_dispatch_iv2_beat_active() &&
        scene1_tutorial_dispatch_iv2_beat_ctr() < 192)
        return 0;
    const int32_t *bd = (const int32_t *)bb;
    const int32_t rank = bd[SAVE_BANK_FIELD_SHOP_RANK];
    const int mode2 = bd[SAVE_BANK_FIELD_GAME_MODE] == 2;
    int file = -1;
    if (rank >= 1 && bb[PC_F428_RANK1_BYTE_OFF] == 0 && !mode2) {
        bb[PC_F428_RANK1_BYTE_OFF] = 1;
        file = 1;
    } else if (rank >= 2 && bb[PC_F429_RANK2_BYTE_OFF] == 0) {
        bb[PC_F429_RANK2_BYTE_OFF] = 1;
        if (!mode2)
            file = 2;
    }
    if (file < 0)
        return 0;
    bb[PC_F404_SELL_ACTIVE_OFF] = 1;
    s_cc08 = 4;
    bb[PC_F405_ARRIVED_OFF] = 1;
    customer_service_set_script_file(file);        /* FUN_00461bf6 */
    customer_service_session_init();               /* FUN_0045edaa */
    return 1;
}

/* world-map door-exit bank flags (T1; all.c:87531/87643, rec-relative like the
 * display flags above, base DAT_044e3798 → DAT_0450f3f2 == 0x2bc5a):
 *   DAT_0450f3f7 — door "already exited" guard (part of bVar17)
 *   DAT_0450f3f9 — world-map tutorial gate (read by the world-map init FUN_0049de20)
 *   DAT_0450f3fa — "first exit ever" latch. */
#define PC_DOOR_EXITED_BYTE_OFF       0x2bc5f   /* DAT_0450f3f7 */
#define PC_WORLDMAP_TUTORIAL_BYTE_OFF 0x2bc61   /* DAT_0450f3f9 */
#define PC_DOOR_FIRSTEXIT_BYTE_OFF    0x2bc62   /* DAT_0450f3fa */

/* bVar3 (v132, all.c:97319-97333): does the player stand at the SELL COUNTER
 * facing it, with the shop stocked?  Counter rect by shop tier (DAT_04510578):
 * tier<3 → X∈(-5,0), Z>6.9; tier>=3 → X∈(5.7,10.7), Z>15.  DAT_0450f3fd == 1 (all
 * stands stocked — the iv1_6 latch the display menu sets, or the save's).  Facing:
 * the world angle DAT_056db05c ∈ (-1.8849558, -1.2566371) (≈ -π/2 ± 0.1π, looking
 * "down" at the counter).  Shared by the Z-entry (player_ctrl_cc08_sell_counter_
 * enter) AND the free-roam "!" affordance emote (player_ctrl_cc08_proximity_detect). */
#define PC_SHOP_DISPLAY_ALLFILLED_BYTE_OFF 0x2bc65  /* DAT_0450f3fd — all stands stocked (iv1_6) */
#define PC_F3FF_SHOP_SELL_BYTE_OFF         0x2bc67  /* DAT_0450f3ff — iv1_6 sale armed (counter → sell) */

static int player_ctrl_at_sell_counter(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return 0;
    /* v132 (all.c:97319-97333): counter rect by shop tier, the all-stocked latch
     * DAT_0450f3fd == 1, facing ≈ -π/2 ± 0.1π. */
    int tier = (int)bank[SHOP_DISPLAY_TIER_SELECTOR];
    float xr = (tier < 3) ? 0.0f : 10.7f;
    float zr = (tier < 3) ? 6.9f : 15.0f;
    float px = g_scene1_player_pos[0];
    float pz = g_scene1_player_pos[2];
    if (!(px < xr && xr - 5.0f < px && pz > zr))
        return 0;
    if (((const uint8_t *)bank)[PC_SHOP_DISPLAY_ALLFILLED_BYTE_OFF] != 1)
        return 0;
    return (s_player_facing > -1.8849558f && s_player_facing < -1.2566371f);
}

/* proximity / approach detection (the bVar17/bVar3 block, all.c:87491-87596) —
 * the cc04==0 free-roam pass that raises the interaction-affordance emote bubble
 * (db000/db004; the "GO!" door tooltip + the sell-counter "!" prompt).
 *
 * Ported: the **shop-door** path (bVar17 → db004=7) AND the **sell-counter** path
 * (bVar3 → db004=0).  The engine checks bVar17 FIRST (door wins); else if bVar3
 * (at the counter facing it) it ramps up the "!" (LAB_00488906); off both — and
 * when carrying (DAT_056db048 != 0) — it routes to LAB_004885eb and ramps db000
 * down toward 0.  db000 is the bubble's slide-in gauge, db004 its cell.
 *
 * The counter cell is **0** (not 1): db004=1 is the NPC-approach prompt gated on
 * the DAT_0438be7c approach timer (all.c:87588), which stays 0 here — no live
 * customer is at the counter in free-roam (she arrives only after Z→cc08=4), so
 * (&DAT_0450f407)[iVar15] (NPC present) is 0 ⇒ be7c never arms ⇒ db004=0.  The
 * be7c NPC-approach path + the b928 scene-intro early branches remain
 * PORT-DEBT(emote-npc-approach) — inert with no live customer/item writer. */
/* DAT_0438be7c / be80: Tear in reach — a 20-tick countdown re-armed while the
 * player stands within 2 of her and faces her (|Δangle| < 0.2π or > 1.8π), once
 * the shop is open (work +0x2bc6f) and the door not taken (+0x2bc5f); be80, the
 * just-talked latch, holds it at 0 until she walks away or turns. */
static int32_t s_be7c, s_be80;

void player_ctrl_tear_reach_reset(void) { s_be80 = 1; s_be7c = 0; }
int32_t player_ctrl_tear_reach(void)    { return s_be7c; }

/* LABEL_497: the bubble slides in (db000 up to 10) */
static void emote_up(void) { if (++s_emote_level > 10) s_emote_level = 10; }

/* LABEL_448 (0x4885eb): no affordance — the bubble slides out, unless Tear is in
 * reach (LABEL_495: cell 1, slide in) */
static void player_ctrl_emote_label_448(void)
{
    if (s_be7c == 0) {
        if (s_emote_level > 0) --s_emote_level;
    } else {
        s_emote_type = 1;
        emote_up();
    }
}

static void player_ctrl_cc08_proximity_detect(int *at_door_out, int *at_counter_out)
{
    /* the whole detection sits under `DAT_056db048 == 0`; a held pose goes to
     * LABEL_448 */
    int at_door = 0, at_counter = 0;
    *at_door_out = 0;
    *at_counter_out = 0;
    if (s_db048 != 0) {
        player_ctrl_emote_label_448();
        return;
    }
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    const uint8_t *bb = (const uint8_t *)bank;
    int exited = bb != NULL ? bb[PC_DOOR_EXITED_BYTE_OFF] : 0;
    at_door = player_ctrl_at_shop_door(g_scene1_player_pos[0],
                                       g_scene1_player_pos[2], s_player_facing,
                                       scene1_camera_view_mode(), exited);
    /* v132 is computed whatever v59 is; v59 wins at the ramp and the Z arm */
    at_counter = player_ctrl_at_sell_counter();

    /* Tear (0x4887bc): dx / dz / the distance spilled, then the countdown */
    {
        static const float k_pi = 3.1415927410125732f, k2 = 2.0f;
        static const float k0p2 = 0.20000000298023224f, k1p8 = 1.7999999523162842f;
        const float dx = spill32((long double)g_scene1_actor_pos[2][0] - g_scene1_player_pos[0]);
        const float dz = spill32((long double)g_scene1_actor_pos[2][2] - g_scene1_player_pos[2]);
        const float d = spill32(spill32((long double)sqrt(spill64((long double)dz * dz + (long double)dx * dx))));
        if (s_be7c > 0) --s_be7c;
        if (bb != NULL && bb[0x2bc6f] != 0 && bb[PC_DOOR_EXITED_BYTE_OFF] == 0) {
            const float ang = spill32(x87_crt_atan2(dx, dz));
            const float q = spill32(((long double)ang - s_player_facing) / k_pi);
            if (d >= k2) {
                s_be80 = 0;
            } else if (spill32(fabsl((long double)q)) < k0p2) {
                s_be7c = 20;
            } else if (spill32(fabsl((long double)q)) <= k1p8) {
                s_be80 = 0;
            } else {
                s_be7c = 20;
            }
        }
        if (s_be80 > 0) s_be7c = 0;
    }

    if (at_door) {
        s_emote_type = 7;
        emote_up();
    } else if (at_counter) {
        s_emote_type = s_be7c != 0 ? 1 : 0;
        emote_up();
    } else {
        player_ctrl_emote_label_448();
    }
    *at_door_out = at_door;
    *at_counter_out = at_counter;
}

/* Pure emote-bubble ramp step (the LAB_00488906 / LAB_004885eb tails, all.c:
 * 87591-87596), factored out for host testing (mirrors player_ctrl_pulse_counters):
 * at an affordance — set the cell `type` and ramp `level` up to 10; off it — ramp
 * `level` down to 0.  `type` is left untouched off the affordance (the engine only
 * writes db004 on the LAB_00488906 path), so the bubble holds its cell as it slides out. */
void player_ctrl_emote_ramp_step(int at_affordance, int affordance_type,
                                 int *level, int *type)
{
    if (at_affordance) {
        *type = affordance_type;                   /* DAT_056db004 = the affordance cell */
        if (*level < 10)                           /* DAT_056db000++ (cap 10) */
            (*level)++;
    } else if (*level > 0) {                        /* LAB_004885eb decrement */
        (*level)--;
    }
}

/* Emote-bubble accessors for the FUN_0040a765 draw (scene1_hud.c). */
int player_ctrl_emote_level(void) { return s_emote_level; }
int player_ctrl_emote_type(void)  { return s_emote_type; }

/* Bank-byte offsets of the shop-display open-gate flags (relative to the working
 * record base DAT_044e3798), read off the active bank like the sparkle/item
 * grid.  DAT_0450f3f2 = displays present, DAT_0450f400 = displays suppressed. */
#define PC_SHOP_DISPLAY_PRESENT_BYTE_OFF  0x2bc5a   /* DAT_0450f3f2 - DAT_044e3798 */
#define PC_SHOP_DISPLAY_SUPPRESS_BYTE_OFF 0x2bc68   /* DAT_0450f400 - DAT_044e3798 */
/* confirm-path display-state bytes (asm 0x489224 / 0x48933c, rec-relative). */
#define PC_SHOP_DISPLAY_CHANGED_BYTE_OFF  0x2bc60   /* DAT_0450f3f8 — "display changed" */
#define PC_SHOP_DISPLAY_BACKROW_BYTE_OFF  0x2bc63   /* DAT_0450f3fb — back-row (cc00==0) dirty */

/* ── T1: shop-door exit → world map (engine FUN_0048670f) ──────────────────
 *
 * v59, the door zone (0x488616-0x48872e): within 1.8 of the door point, not
 * exited (work +0x2bc5f), facing within ±0.1π of the door's direction, past its
 * edge.  The door by the stage's scene type DAT_068dd3fc[stage·0x6cf]:
 *   0     → (3, -3.4), facing +π/2, edge x > 2.895;
 *   1..4  → (6, -6.7), facing -π,   edge z < -6.7;
 *   other → (6, -6.8), facing +π/2, edge z < -6.7.
 * All .rdata m32; the distance is _sqrt (0x5031e4) of the double sum. */
int player_ctrl_at_shop_door(float x, float z, float facing, int scene_type,
                             int already_exited)
{
    static const float k_band = 0.3141592741012573f;   /* 0x519a30 */
    float px = 6.0f, pz = -6.800000190734863f, face = 1.5707963705062866f;
    if (scene_type == 0) {
        px = 3.0f;
        pz = -3.4000000953674316f;
    } else if (scene_type > 0 && scene_type <= 4) {
        pz = -6.699999809265137f;
        face = -3.1415927410125732f;
    }
    const float dx = spill32((long double)px - x);
    const float dz = spill32((long double)pz - z);
    const float d = spill32((long double)sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
    if (!(d < 1.7999999523162842f) || already_exited)
        return 0;
    if (!((long double)face - k_band < facing) || !((long double)face + k_band > facing))
        return 0;
    if (scene_type != 0)
        return z < -6.699999809265137f;
    return x > 2.894999980926514f;
}

/* Door-exit arm (all.c:87637-87648): Z at the door zone → arm the world-map
 * transition (DAT_074b2ec4 = 1) + kick the dissolve fade-out (FUN_004526f5).
 * First exit ever (DAT_0450f3fa == 0) also raises the tutorial gate
 * DAT_0450f3f9 the world-map init reads + the exited guard DAT_0450f3f7. */
void player_ctrl_worldmap_exit_arm(void)
{
    s_worldmap_exit_armed = 1;            /* DAT_074b2ec4 = 1 */
    fade_phase1_start(0, 0x11);           /* FUN_004526f5(0,0x11) — dissolve fade-out */
    s_emote_level = 0;                    /* DAT_056db000 = 0 (asm 0x488aa8) */

    uint8_t *bb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
    if (bb != NULL && bb[PC_DOOR_FIRSTEXIT_BYTE_OFF] == 0) {
        bb[PC_DOOR_FIRSTEXIT_BYTE_OFF]    = 1;   /* DAT_0450f3fa = 1 */
        bb[PC_WORLDMAP_TUTORIAL_BYTE_OFF] = 1;   /* DAT_0450f3f9 = 1 (world-map tutorial gate) */
        bb[PC_DOOR_EXITED_BYTE_OFF]       = 1;   /* DAT_0450f3f7 = 1 (exited guard) */
    }
    /* Door-transition SE (asm 0x488ad1 / 0x488adb — Ghidra dropped both args, so
     * RE'd from objdump + a retail audio-hook ret_va backtrace: the caller is
     * FUN_0048670f, NOT the world-map sim).  Played UNCONDITIONALLY within this
     * first-exit arm (after the optional flag-set), in this order: the system
     * "whoosh" file-SE then the 0x150 click.  Both fixed (no RNG variant) ⇒
     * RNG-neutral, like the 0x143 plays — resolves the old PORT-DEBT(door-SE)
     * LCG concern (no draw to mirror).  Diagnosed via audio_diff on the
     * merchants-guild trace (was: missing se_019_id0150 + 00re_sys09). */
    audio_play_se_file("bin/se/00re/system/00re_sys09.bin");  /* FUN_0049933c(0x5cefb8) */
    audio_play_se_by_id(0x150);                               /* FUN_00499519(0x150) */
}

/* Stage-2 of the exit (all.c:86877-86888): while armed, the engine freezes the
 * HOUSE tick (goto LAB_004893ff) each frame; when the dissolve fade completes
 * (FUN_004528b3), it flips to mode 8 (the world map), kicks the post-load
 * fade-IN (FUN_0045281c), and spawns the scene-load worker (FUN_00452cde →
 * the nowloading gate → LOADING_START).  Returns 1 while the tick is frozen
 * (the caller returns early, mirroring the engine's goto). */
int player_ctrl_worldmap_exit_stage2(void)
{
    if (!s_worldmap_exit_armed)
        return 0;
    if (fade_is_done()) {                 /* FUN_004528b3 — dissolve complete */
        g_scene_state = SCENE_STATE_WORLDMAP;  /* DAT_0438b1c0 = 8 (world map) */
        /* FUN_0049de0e(0) (all.c:487090): initial selected dest = 0 (your
         * shop / home).  The world-map init (FUN_0049de20, run on the worker
         * spawned below) reads this to snap the destination cursor. */
        scene_worldmap_set_sel_dest(0);
        /* PORT-DEBT(house-teardown, FUN_00474d92): the engine frees the shop
         * textures/meshes here (all.c:487095) before the world-map load; the
         * port's per-scene sprite ownership makes this a no-op for now — the
         * world-map worker (case 8) loads its own slot-10 textures.  Revisit
         * if the shop assets need explicit teardown to match retail VRAM. */
        fade_phase_out_start(0, 0x11);    /* FUN_0045281c(0,0x11) — fade-IN */
        worker_load_spawn();              /* FUN_00452cde — load worker → LOADING_START */
        s_worldmap_exit_armed = 0;
    }
    return 1;   /* frozen this frame (engine goto LAB_004893ff). */
}

/* Test accessors. */
int  player_ctrl_worldmap_exit_armed(void) { return s_worldmap_exit_armed; }
void player_ctrl_worldmap_exit_reset(void) { s_worldmap_exit_armed = 0; }

/* ── sell-counter Z (the bVar3/v132 Z-arm, FUN_0048670f) ──────────────────────
 *
 * bVar3 (all.c:97319-97333, player_ctrl_at_sell_counter): the player stands at
 * the SELL COUNTER facing it, and DAT_0450f3fd (all stands stocked) == 1.
 *
 * Z-arm (asm 0x488bb6-0x488c18): `if (f3ff != 0) { f404=1; FUN_00461bf6(0);
 * cc08=4; FUN_0045edaa(); } else { cc08=0; b754=cc0c=cc10=0; FUN_004850fe();
 * SE 00re_sys06; FUN_00499519(0x174); }  db000=0; goto LAB_004893ff`.
 *   - f3ff != 0 (set with f3fe when iv1_6 fires, cleared at the cs leave): the
 *     tutorial's scripted sale — enter customer service directly.  The script
 *     index FUN_00461bf6 pushes 0 here (objdump 0x488bbf `push 0x0`; the
 *     autonomous/cutscene arrivals at 0x48756a/0x4877b6/0x4884c7 push 2).
 *     fileidx=0 is load-bearing: cs_queue_advance dispatches FUN_00461303
 *     (b5a8=2, the kind-select) on fileidx∉{1,2}.
 *   - f3ff == 0 (every ordinary shop day): open the counter menu (cc08 0 →
 *     "Open Store" → cc08 10 → 15 frames → cc08 4, scene1_counter_menu.c).
 * Both arms goto the tail, past FUN_004897c6 and FUN_0048a833. */
#define PC_F404_SELL_ACTIVE_BYTE_OFF  0x2bc6c   /* DAT_0450f404 — sell-active flag */

/* cc08 flipped 1→4 THIS frame (any cs entry flavor).  The engine's entry arms
 * all `goto LAB_004893ff` — PAST the per-actor anim-tick loop (FUN_004897c6)
 * and the companion controller — so the entry frame ticks/writes NO actor anim
 * (probe-proven RE §21.28: retail ccnt/pcnt freeze across frames 304 + 825).
 * Cleared at the top of scene1_player_ctrl_tick each frame; read by
 * scene1_companion_ctrl_tick to sit the companion out on the entry frame. */


/* Frame-top clear for both boundary markers — called from scene1_ingame_tick
 * (which runs on EVERY sim frame, default AND event arms) so a marker lives
 * for exactly the frame whose arm set it. */
/* The display-stand open (cc04 0→1) ends the frame at LABEL_675 (FUN_00486435 +
 * the rumble) — no FUN_004897c6 anim tick and no FUN_0048a833, so the companion
 * sits this frame out (rhost multiday-tutorials-suggest f1410: Tear's wander
 * timer holds). */


/* cc08 10→4 THIS frame via the counter menu's "Open Store".  FUN_0048670f's
 * cc08==10 case ends `goto LABEL_133 → FUN_0048a833()`, so the companion still
 * runs its normal update on this tick even though session_init just spawned the
 * d3e load (b1cc 2) — unlike the 1→4 entries, which skip it. */

static int player_ctrl_cc08_sell_counter_enter(void)
{
    if (!player_ctrl_at_sell_counter())     /* bVar3 */
        return 0;
    uint8_t *bb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
    if (bb == NULL)
        return 0;

    if (bb[PC_F3FF_SHOP_SELL_BYTE_OFF] != 0) {
        bb[PC_F404_SELL_ACTIVE_BYTE_OFF] = 1;              /* DAT_0450f404 = 1 */
        customer_service_set_script_file(0);               /* FUN_00461bf6(0) → DAT_005c6bb0 */
        s_cc08 = 4;                                        /* DAT_0438cc08 = 4 */
        customer_service_session_init();                   /* FUN_0045edaa */
    } else {
        s_cc08 = 0;                                        /* DAT_0438cc08 = 0 */
        counter_menu_open();                               /* b754 = cc0c = cc10 = 0 */
        counter_menu_build(bb);                            /* FUN_004850fe */
        audio_play_se_file("bin/se/00re/system/00re_sys06.bin");   /* FUN_0049933c */
        audio_play_se_by_id(0x174);                        /* FUN_00499519(0x174) */
    }
    s_emote_level = 0;   /* DAT_056db000 = 0 (asm 0x488c11): consume the counter "!"
                          * affordance on entry, so it doesn't linger frozen. */
    return 1;
}

/* ── autonomous first-customer customer-service entry (the f406 arm, FUN_0048670f
 * all.c:87485-87489) ─────────────────────────────────────────────────────────
 *
 * In the cc08==1 free-roam arm — AFTER the roster-arrival (b928/f428/f429) block
 * and BEFORE the cc04==0 d-pad interaction — the engine checks the forced-tutorial
 * flag: `if (f406 != 0) { cc08=4; FUN_0045edaa(); goto LAB_004893ff; }`.  This is
 * the AUTONOMOUS entry for the FIRST REAL CUSTOMER after the haggle tutorial: the
 * post-tutorial wrap-up cutscene iv1_7 ("And that is… now go sit at the counter")
 * sets f406=1 (scene1_tutorial_dispatch.c), and the next free-roam frame this fires
 * → cc08=4 + the session init (FUN_0045edaa's f406 forced-kyaku-13 branch,
 * customer_service.c:282-300, b51c stays 0 ⇒ the live machine).  The cc08==4 master
 * tick then pins the COUNTER camera (scene1_camera_cs_counter_cam) — which is why
 * retail CUTS to the counter framing at the post-fade while the port (lacking this
 * entry) stayed in the free-roam player-follow camera (RE §17, "gap (2)").
 *
 * Unlike the Z-entry (bVar3) and the roster entries (f428/f429), the engine f406
 * arm does NOT seed the script-file index (no FUN_00461bf6) and does NOT set f405
 * (player-arrival-complete) — so the arrival hop + camera ramp replay, matching
 * retail's post-load ramp.  No db000 emote clear either (the engine omits it here).
 * f406 is cleared (→ f402, the iv1_8 trigger) only at the cs LEAVE
 * (customer_service.c:1448), so it stays latched through this session — harmless,
 * since cc08==4 stops the free-roam arm from re-checking it. */
#define PC_F406_TUTORIAL_BYTE_OFF  0x2bc6e   /* DAT_0450f406 — forced/tutorial sale */

int player_ctrl_cc08_f406_entry(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return 0;
    if (((const uint8_t *)bank)[PC_F406_TUTORIAL_BYTE_OFF] == 0)
        return 0;
    s_cc08 = 4;                          /* DAT_0438cc08 = 4 */
    customer_service_session_init();     /* FUN_0045edaa (the f406 forced-kyaku-13 branch) */
    return 1;
}

/* Pure (no side effects) predicate: the f406 first-customer entry is armed —
 * iv1_7 set DAT_0450f406 and the next free-roam frame will flip cc08 1→4.  The
 * conversation-pose teardown reads this BEFORE the entry flips cc08 (the entry
 * runs later in player_ctrl_tick) to hold the pose one frame into the entry like
 * retail (RE §21.18).  Only iv1_7 sets f406, so it never fires for iv1_5/iv1_6. */
int player_ctrl_cc08_f406_pending(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    return (bank != NULL) &&
           ((const uint8_t *)bank)[PC_F406_TUTORIAL_BYTE_OFF] != 0;
}

/* The db048 == 0 button block of the free-roam arm (FUN_0048670f, IDA 97393+):
 * a press of 0x20, 0x40 or 0x10 (DAT_073dddd4, the edge mask) ALWAYS ends the
 * frame at LABEL_675 (FUN_00486435 + FUN_00485861) — no FUN_0048b850 walk, no
 * FUN_004897c6 anim tick, no FUN_0048a833 — whether or not the press acted
 * (rhost dungeon0-full f476: a Z press facing no cell holds the player and
 * db054).  `at_door` / `at_counter` are the frame's v59 / v132.  Returns 1 when
 * the frame ended here. */
static int player_ctrl_cc08_dpad_interact(int at_door, int at_counter)
{
    const uint16_t pressed = g_sim_buttons[0].pressed;
    if ((pressed & 0x70u) == 0)
        return 0;
    if (pressed & 0x20u)
        return 1;

    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return 1;
    const uint8_t *bb = (const uint8_t *)bank;
    int cbfc = shop_display_cbfc();
    int cc00 = shop_display_cc00();
    /* v68 = FUN_004860c8(cbfc, cc00); retail reads the furniture word at v68
     * even when it is -1 (the dword before the array) */
    int fidx = shop_display_furniture_index(cbfc, cc00);
    const int32_t furn = (int32_t)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + fidx];

    if (pressed & 0x40u) {
        if (!at_door && !at_counter && furn == 0 &&
            bb[PC_SHOP_DISPLAY_PRESENT_BYTE_OFF] != 0 &&
            bb[PC_SHOP_DISPLAY_SUPPRESS_BYTE_OFF] == 0 && cbfc != -1 &&
            (int32_t)bank[SAVE_BANK_FIELD_DISPLAY_GRID +
                          (uint32_t)(cbfc + cc00 * SHOP_DISPLAY_GRID_STRIDE)] != -1) {
            /* PORT-DEBT(stub, FUN_0048670f cc08 50): the faced stand's item
             * status — FUN_0047f1ce, cc08 = 50, FUN_00468286(item),
             * FUN_004681db(2), SE 710 */
        }
        return 1;
    }

    /* 0x10 (Z) */
    if (at_door) {                       /* v59 implies !+0x2bc5f */
        player_ctrl_worldmap_exit_arm();
        return 1;
    }
    /* 0x488ae7: Tear in reach — her menu (cc08 15), "Yes? What is it?" */
    if (s_be7c != 0) {
        s_db048 = 14;
        s_db04c = 0;
        party_anim_set(s_actor_record[0], 6);
        s_be7c = 0;
        s_be80 = 1;
        s_emote_level = 0;
        s_cc08 = 15;
        counter_menu_build(bb);                            /* FUN_004850fe */
        counter_menu_open();                               /* b754 = cc0c = cc10 = 0 */
        audio_play_se_file((rng_next15() & 1u) ? "bin/se/01ti/tuika/ti_nandesuka.bin"
                                               : "bin/se/01ti/tuika/ti_whats.bin");
        actor_say_set(ACTOR_SAY_WHAT_IS_IT, 0, 2, 0, -1);  /* b76c 3, b758 0, b75c 2, b760 0, b768 -1 */
        return 1;
    }
    if (at_counter) {
        player_ctrl_cc08_sell_counter_enter();
        return 1;
    }
    if (furn != 0) {
        /* PORT-DEBT(stub, FUN_0048670f cc04 2): the furniture-grid mode (cc04 = 2,
         * FUN_00468338, the 74b2ed4.. cursor, SE 323, FUN_00435710) */
        return 1;
    }
    if (bb[PC_SHOP_DISPLAY_PRESENT_BYTE_OFF] == 0 ||
        bb[PC_SHOP_DISPLAY_SUPPRESS_BYTE_OFF] != 0 || cbfc == -1)
        return 1;

    /* ===== open the cc04==1 display-stand menu (all.c:87705-87724) ===== */

    /* open SE (all.c:87705-87708): rand()%3 selects one of three baked system
     * clips, FUN_0049933c plays it.  The variant select consumes one LCG draw
     * (mirror it to keep the shared RNG stream aligned port↔retail); the play
     * itself is RNG-neutral. */
    {
        static const char *const k_open_se[3] = {
            "bin/se/00re/system/00re_sys04a.bin",
            "bin/se/00re/system/00re_sys04b.bin",
            "bin/se/00re/system/00re_sys04c.bin",
        };
        audio_play_se_file(k_open_se[rng_next15() % 3]);
    }

    /* interact pose: actor 0 anim 3 (the lean-in, keyed on daafc), then one
     * anim tick (FUN_00482a71). */
    party_anim_set(s_actor_record[0], 3);
    chr_anim_tick(s_actor_record[0], s_actor_char[0], 1.0f);

    /* cc04 = 1 + the open-once latch DAT_0438cbe8 (all.c:87718-87722). */
    int first_open = (s_cbe8 == 0);
    s_cc04 = 1;
    if (first_open)
        s_cbe8 = 1;

    /* FUN_00468338(0, first_open): open the inventory window — the list init +
     * the slide (DAT_0734b9a0=1 active + DAT_0734b98c=0 counter, ramped 0→5 by
     * the per-frame stage_load_pulse_tick).  The window holds the sim frozen
     * while cc04 != 0 (the freeroam arm routes to the cc04 menu arm below
     * instead of the walk, so neither the walk nor the companion db054 clock
     * advance — engine-quirks §110). */
    display_menu_open(0, first_open);

    /* FUN_004681ec (0x488d9d-0x488dac, right after the FUN_00468338 call):
     * pick the prompt bubble from the FACED display-grid cell — occupied
     * (dword != -1) → 2 "Exchange with what?", empty → 1 "What will you
     * place?".  Same cell read as the menu confirm (bank dword 0x4e26 +
     * cbfc + 20*cc00). */
    {
        int32_t cur = (int32_t)bank[SAVE_BANK_FIELD_DISPLAY_GRID
                                    + (uint32_t)(cbfc + cc00 * SHOP_DISPLAY_GRID_STRIDE)];
        display_menu_set_window_flag(cur != -1 ? 2 : 1);
    }
    return 1;   /* menu opened: consume the frame. */
}

/* The cc08==1 free-roam walk arm (FUN_0048670f all.c:919-1225).  Surrounds the
 * validated walk with the engine's guard structure: customer-approach
 * escalation → cc04==0 gate → proximity detection → d-pad interaction → walk.
 * All four guards are inert in steady free-roam (above), so control reaches the
 * walk identically to the pre-Chip-4 arm.
 *
 * The walk itself: read the held d-pad → walk impulse (step 1) → FUN_0048b850
 * body (clamp/octant/integrate+collide/damp + render tail) → actor anim record.
 * This is the controllable (cc08==1, cc04==0, da1bc==0) walk validated against
 * runs/w3-walk-watch — byte-identical per-frame to retail (px, vx post-damp) and
 * through the house-table-corner slide (§69/§75).  The room-bounds clamp is NOT
 * here: the engine runs it in the tail (FUN_00486435 @ LAB_004893ff). */
/* DAT_0450f3f2 (bank byte +0x2bc5a): the per-shop "display fixtures present"
 * flag.  Set by the shop-setup path (FUN_0044bd0d) and persisted in the save
 * record; gates the walk-tail cell-highlight detector (all.c:87750).  Read the
 * active working bank's byte directly (the sparkle / item-grid render use the
 * same bank).  Offset PC_SHOP_DISPLAY_PRESENT_BYTE_OFF (defined above). */
static int player_ctrl_shop_display_present(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return 0;
    return ((const uint8_t *)bank)[PC_SHOP_DISPLAY_PRESENT_BYTE_OFF] != 0;
}

/* cc04==1 display-stand menu arm (FUN_0048670f all.c:87905-88017, the else of
 * the cc04==0 walk / cc04==2 furniture-grid split).  Runs every frame the
 * remove-item menu is open: tick the picker update and act on its return —
 * 1 = confirm (the removal), 2 = cancel, 3 = pick-up arm.  The sim stays frozen
 * throughout (db054 doesn't advance because the walk arm never runs — §110); on
 * close, cc04 → 0 and the slide retracts.
 *
 * The confirm-1 path is the SAVE-relevant removal: with the cursor on the
 * index-0 "select none" entry FUN_00469a9f()==-1, so it writes -1 into the
 * faced display-grid cell (SAVE_BANK_FIELD_DISPLAY_GRID, save dword 0x4e26) and
 * returns the previously-displayed item to the inventory.  FUN_004681f6(-1)==-1
 * → the engine reads an out-of-array category that lands on the NORMAL (non-
 * counter) path; we take that path directly for the -1 "remove" case (retail
 * ground-truth: the removal writes the grid + returns the item; the counter
 * variant only fires when PLACING a 0x1451..0x14b3 item — PORT-DEBT below). */
static void player_ctrl_cc04_menu_body(void);

/* The cc04 == 1 arm: FUN_00469414(1)'s outcome, then FUN_004897c6 (the status +
 * the three anim ticks) and FUN_0048a833 on every frame of it */
static void player_ctrl_cc04_menu_arm(void)
{
    player_ctrl_cc04_menu_body();
    party_status_tick();
    party_follow_tick();
}

static void player_ctrl_cc04_menu_body(void)
{
    int r = display_menu_update(1);   /* FUN_00469414(1) */

    if (r == 2) {                     /* CANCEL (all.c:87907) */
        s_cc04 = 0;                                  /* DAT_0438cc04 = 0 */
        stage_load_pulse_set_active(0);              /* FUN_004682d0: slide out */
        title_save_dialog_cursor_set_visible(0);     /* FUN_00435612: hide cursor */
        audio_play_se_by_id(0x143);  /* FUN_00499519(0x143), LAB_0048917a — fixed id, no RNG */
        return;
    }

    if (r == 3) {                     /* pick-up arm (Z edge frame, all.c:87915) */
        /* The hands-up carry pose (all.c:87916-87929, was PORT-DEBT(A3)):
         * gated like the confirm — only when the arm is a real action (an item
         * selected OR the faced cell occupied).  db048 ← 0xc (the carry state,
         * ticked by the free-roam arm once the menu closes), counter ← 0, and
         * the player anim record → 4 (the hold-item-overhead pose), latch-gated
         * exactly like retail (DAT_056daafc != 4).  No grid / RNG / db054
         * effect; the menu arm's per-frame chr_anim_tick advances the pose
         * through the 6-frame confirm countdown. */
        int sel = display_menu_selected();            /* FUN_00469a9f */
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        int col = shop_display_cbfc();
        int row = shop_display_cc00();
        int old = (bank != NULL && col >= 0 && row >= 0)
                ? (int)bank[SAVE_BANK_FIELD_DISPLAY_GRID
                            + col + row * SHOP_DISPLAY_GRID_STRIDE]
                : -1;
        if (sel != -1 || old != -1) {
            s_db048 = 0xc;                            /* DAT_056db048 */
            s_db04c = 0;                              /* DAT_056db04c */
            if (s_actor_record[0][CHR_ACTOR_STATE] != 4) {
                union { float f; int32_t i; } z = { .f = 0.0f };
                s_actor_record[0][CHR_ACTOR_ANIM]    = 4;  /* DAT_056daae8 */
                s_actor_record[0][CHR_ACTOR_FRAME]   = 0;  /* _DAT_056daaf8 */
                s_actor_record[0][CHR_ACTOR_COUNTER] = 0;  /* _DAT_056daaf0 */
                s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;/* DAT_056daaf4 */
                s_actor_record[0][CHR_ACTOR_STATE]   = 4;  /* DAT_056daafc */
            }
        }
        audio_play_se_by_id(0x143);  /* FUN_00499519(0x143), LAB_0048917a — fixed id, no RNG */
        return;
    }

    if (r == 1) {                     /* CONFIRM / REMOVE (all.c:87932) */
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        int sel = display_menu_selected();            /* FUN_00469a9f → -1 */
        int col = shop_display_cbfc();
        int row = shop_display_cc00();

        if (bank != NULL && col >= 0 && row >= 0) {
            int cell = col + row * SHOP_DISPLAY_GRID_STRIDE;
            int old  = (int)bank[SAVE_BANK_FIELD_DISPLAY_GRID + cell];

            /* proceed only if placing (sel != -1) OR the cell is occupied
             * (all.c:87934). */
            if (sel != -1 || old != -1) {
                uint8_t *bb = (uint8_t *)bank;
                bb[PC_SHOP_DISPLAY_CHANGED_BYTE_OFF] = 1;   /* DAT_0450f3f8 = 1 */

                /* confirm SE (all.c:87938-87939): rand()&1 picks one of two
                 * baked clips (asm 0x48922b), then FUN_0049933c plays it.  The
                 * rand draw must be mirrored to keep the shared LCG aligned (the
                 * open draw is in the open gate); the play is RNG-neutral. */
                {
                    /* rand&1: 0 → 05b, 1 → 05a (verified by recapture — the
                     * exercised placements all draw 0 and retail plays 05b). */
                    static const char *const k_confirm_se[2] = {
                        "bin/se/00re/system/00re_sys05b.bin",
                        "bin/se/00re/system/00re_sys05a.bin",
                    };
                    audio_play_se_file(k_confirm_se[rng_next15() & 1]);
                }

                /* NORMAL (sword / non-counter) removal path (all.c:87941-87977,
                 * the sel==-1 case): blank the cell, return the old item. */
                bank[SAVE_BANK_FIELD_DISPLAY_GRID + cell] = (uint32_t)sel;   /* = -1 */
                display_menu_inventory_remove(bank, sel);   /* FUN_00469241(-1): no-op */
                display_menu_inventory_return(bank, old);   /* FUN_00468d22(sword) */
                /* FUN_0044bd0b — 1-byte no-op (stripped). */
                /* PORT-DEBT(A3, FUN_0048439a): the live display-appeal recompute
                 * (DAT_0438b4b8/b4bc) reads the grid + item DB but touches no
                 * save/RNG state — deferred with the menu render. */
                if (row == 0)
                    bb[PC_SHOP_DISPLAY_BACKROW_BYTE_OFF] = 1;   /* DAT_0450f3fb */

                /* iv1_6 "all wares displayed" first-stock latch (all.c:87952-
                 * 87976), D1: once, the frame every display stand is occupied.
                 * Gated on the latch (DAT_0450f3fd) still clear — a loaded,
                 * already-stocked shop already has it set, and a removal makes
                 * occupied < stands, so the remove roundtrip never trips it (which
                 * is why this block was A3 PORT-DEBT until the place path).  Arms
                 * the dispatcher's iv1_6 branch (scene1_tutorial_dispatch, D2). */
                if (bb[PC_SHOP_DISPLAY_ALLFILLED_BYTE_OFF] == 0) {
                    /* engine count (87954-87972): stands = layout-grid cells with
                     * a furniture stamp {2,3,4} (shop_display_grid_cell), occupied
                     * = item-grid cells != -1.  All filled ⇔ stands == occupied
                     * (the engine compares as float local_18 vs (float)iVar6; the
                     * counts are < 300 so the int compare is identical).  Gated on
                     * a non-empty inventory (DAT_0450f2b0 read AFTER the place
                     * decremented it): an empty inventory trivially counts done —
                     * which is what fires iv1_6 on item-display-2 (the player places
                     * their last item, itemcnt hits 0). */
                    int all_filled = 1;
                    if (bank[SAVE_BANK_FIELD_ITEM_COUNT] != 0) {
                        int stands = 0, occupied = 0;
                        for (int c = 0; c < SAVE_BANK_DISPLAY_GRID_CELLS; c++) {
                            int lay = shop_display_grid_cell(
                                          c % SAVE_BANK_DISPLAY_GRID_COLS,
                                          c / SAVE_BANK_DISPLAY_GRID_COLS);
                            if (lay > 1 && lay < 5)   stands++;
                            if ((int)bank[SAVE_BANK_FIELD_DISPLAY_GRID + c] != -1)
                                occupied++;
                        }
                        all_filled = (stands == occupied);
                    }
                    if (all_filled) {
                        bb[PC_SHOP_DISPLAY_ALLFILLED_BYTE_OFF] = 1; /* DAT_0450f3fd */
                        bb[PC_SHOP_DISPLAY_PRESENT_BYTE_OFF]   = 0; /* DAT_0450f3f2 */
                    }
                }
            }
        }

        s_cc04 = 0;                                  /* DAT_0438cc04 = 0 (close) */
        stage_load_pulse_set_active(0);              /* FUN_004682d0: slide out */
        title_save_dialog_cursor_set_visible(0);     /* FUN_00435612: hide cursor */
    }

    /* PORT-DEBT(A3): the menu-frame tail FUN_004897c6 (player buff/cooldown
     * status tick — all timers 0 in HOUSE, no RNG/save effect) + FUN_0048a833
     * (companion facing — the player is frozen, so it is a no-op here, matching
     * retail's frozen companion under the §110 db054 freeze) are not run; the
     * companion controller is gated to the walk arm, which the menu skips. */
}

/* ── cc08 0 / 10: the counter menu (FUN_0048670f case 0 asm 0x487880-0x487929,
 * case 10 asm 0x4870b9-0x487347) ──────────────────────────────────────────────
 * case 0: hold the counter pose (anim 3), wait for the slide-in (b74c >= 10),
 * then cancel (0x20 → cc08 1) / confirm (Z → cc08 10) / move the cursor; exits
 * via LABEL_132 = FUN_004897c6 (the player anim tick) + FUN_0048a833.
 * case 10: the 15-frame close, then dispatch the entry; exits via LABEL_133 =
 * FUN_0048a833 only (no player anim tick).  FUN_0048a833 is the scene1_sim
 * companion fallback; the tail (FUN_00486435/FUN_00485861) is the dispatch tail. */
static void player_ctrl_cc08_counter_menu_arm(void)
{
    if (s_cc08 == 0) {
        if (s_actor_record[0][CHR_ACTOR_STATE] != 3) {      /* DAT_056daafc != 3 */
            s_actor_record[0][CHR_ACTOR_STATE]   = 3;       /* DAT_056daafc */
            s_actor_record[0][CHR_ACTOR_FRAME]   = 0;       /* DAT_056daaf8 */
            s_actor_record[0][CHR_ACTOR_COUNTER] = 0;       /* DAT_056daaf0 = 0.0f */
            s_actor_record[0][CHR_ACTOR_TIMER]   = 0;       /* DAT_056daaf4 */
            s_actor_record[0][CHR_ACTOR_ANIM]    = 3;       /* DAT_056daae8 */
        }
        if (counter_menu_slide() >= 10) {                   /* DAT_0438b74c >= 10 */
            int in = counter_menu_input(g_sim_buttons[0].pressed, g_sim_buttons[0].held);
            if (in == COUNTER_MENU_IN_CANCEL)
                s_cc08 = 1;
            else if (in == COUNTER_MENU_IN_CONFIRM)
                s_cc08 = 10;
        }
        party_status_tick();                 /* LABEL_132: FUN_004897c6 */
        party_follow_tick();                 /* LABEL_133: FUN_0048a833 */
        return;
    }

    /* cc08 == 10: every path ends at LABEL_133 (FUN_0048a833, no anim tick) */
    int entry = counter_menu_close_tick();                  /* ++DAT_0438b754 == 15 */
    if (entry < 0) {
        party_follow_tick();
        return;
    }
    if (entry == COUNTER_MENU_CANCEL) {
        s_cc08 = 1;
    } else if (entry == COUNTER_MENU_OPEN_STORE) {
        s_cc08 = 4;                                         /* DAT_0438cc08 = 4 */
        customer_service_session_init();                    /* FUN_0045edaa */
        /* rand()%5 picks Recette's / Tear's "let's open!" voice (asm 0x4870f3). */
        static const char *const k_open_se[5] = {
            "bin/se/00re/system/00re_sys11a.bin",
            "bin/se/00re/system/00re_sys11b.bin",
            "bin/se/00re/system/00re_sys11c.bin",
            "bin/se/01ti/system/01ti_sys11a.bin",
            "bin/se/01ti/system/01ti_sys11b.bin",
        };
        audio_play_se_file(k_open_se[rng_next15() % 5u]);
    } else {
        /* PORT-DEBT(counter-menu-decor): entries 1..5 (Change Flooring / Walls /
         * Carpet / Tables, Move Tables; merchant level >= 5) set cc08 0x17 and
         * start the decor camera move (FUN_00452dc1/d85/dfd/e39 + the b7b4..b7d0
         * snapshot, SE 00re_sys04c) — the decor editor is unported, so the menu
         * just closes. */
        s_cc08 = 1;
    }
    party_follow_tick();                     /* LABEL_133: FUN_0048a833 */
}

static void player_ctrl_walk_and_highlight(void);

static void player_ctrl_cc08_freeroam_arm(void)
{
    /* customer-approach escalation: inert (no customers) → falls through. */
    if (player_ctrl_cc08_customer_escalate())
        return;

    /* f406 autonomous first-customer entry (all.c:87485): after iv1_7 sets f406=1,
     * the next free-roam frame flips cc08=4 + runs the session init → the post-fade
     * counter camera (RE §17).  Mirrors the engine order: after the escalate block,
     * before the cc04 split. */
    if (player_ctrl_cc08_f406_entry())
        return;

    /* cc04 interaction sub-state: 0 = walking; 1 = the display-stand remove-item
     * menu is open (A2) → its per-frame arm; 2 = the furniture-grid mode
     * (unported).  The menu arm holds the sim frozen until it closes (cc04→0). */
    if (s_cc04 != 0) {
        player_ctrl_cc04_menu_arm();
        return;
    }

    /* The world-map return (all_ida 97260, b928 == 2 && b924 < 60): both actors
     * drop to idle, Tear ticks (FUN_0048a833 — following, see co_follow_mode),
     * then LABEL_448: no door/counter detection, the affordance gauge decays.
     * While b924 < 45 LABEL_499 jumps to LABEL_508 — the player anim tick
     * (FUN_004897c6) and no walk; from 45 the walk below runs (and FUN_0048b850
     * ticks Tear a second time). */
    int return_beat = 0;
    if (scene1_beat_flag() == 2 && scene1_beat_counter() < 60) {
        for (int a = 0; a <= 2; a += 2) {
            int32_t *r = s_actor_record[a];
            if (r[CHR_ACTOR_STATE] != 0) {
                r[CHR_ACTOR_STATE]   = 0;
                r[CHR_ACTOR_FRAME]   = 0;
                r[CHR_ACTOR_COUNTER] = 0;
                r[CHR_ACTOR_TIMER]   = 0;
                r[CHR_ACTOR_ANIM]    = 0;
            }
        }
        party_follow_tick();                 /* FUN_0048a833 */
        player_ctrl_emote_label_448();   /* goto LABEL_448 */
        if (scene1_beat_counter() < 45) {
            party_status_tick();             /* LABEL_508: FUN_004897c6 */
            return;
        }
        return_beat = 1;
    }

    /* proximity / approach detection (v59 / v132 + the affordance ramp); the
     * return beat jumps past it (LABEL_448, both 0) */
    int at_door = 0, at_counter = 0;
    if (!return_beat)
        player_ctrl_cc08_proximity_detect(&at_door, &at_counter);

    /* the button block sits under `if (DAT_056db048 == 0)` — a held carry pose
     * swallows the presses */
    if (s_db048 == 0 && player_ctrl_cc08_dpad_interact(at_door, at_counter))
        return;

    player_ctrl_walk_and_highlight();
}

/* The FUN_0048b850 call of the cc08==1 arm (asm 0x4893c5): the walk, then the
 * faced display cell FUN_0048619f when the shop has display fixtures (work
 * +0x2bc5a), else cbfc = -1. */
static void player_ctrl_walk_and_highlight(void)
{
    player_ctrl_walk();
    if (player_ctrl_shop_display_present())
        shop_display_faced_cell();
    else
        shop_display_faced_none();
}

/* FUN_0048b850 (dungeon_party.c): the controller runs FUN_0048a833 and the
 * db054 advance itself, so the scene1_sim companion fallback stays off */
void player_ctrl_walk(void)
{
    s_b850_ran = 1;
    party_tick();
}

/* ── Chip 3c: the cc08==4 player/companion ARRIVAL anim + camera ramp ────────
 * (FUN_0048670f all.c:87366-87434, objdump 0x487e8a-0x488085).  Retires
 * PORT-DEBT(cs-arrival-anim).  Recette hops onto the merchant stool (anim 5)
 * and the camera target (= g_scene1_player_pos) ramps from the walk-stop to the
 * counter view; once she has "arrived" the at-counter pose (anim 6) settles and
 * the companion takes anim 4.  Gates read the live save bank: f405 (0x2bc6d) =
 * player-arrival-complete, f407 (0x2bc6f) = companion-arrival-complete.  Ground
 * truth: the retail house-customer-tutorial-fe530872 cache (poct 0, pang -π,
 * panim 5; px -1.5→-4.5 @0.125/f and pz 9.35→8.6 @0.05/f once db04c>10, py→0.5).
 *
 * The f406 tutorial entry (all.c:87485) sets cc08=4 WITHOUT setting f405, so the
 * tutorial always runs the arriving branch (the jump + zoom the user flagged in
 * viewer notes #2/#3); the arrived branch ports the autonomous-entry path that
 * pre-sets f405=1 (e.g. the cc08==0x1e arm at all.c:87047) for faithfulness. */
#define PC_F405_ARRIVED_BYTE_OFF            0x2bc6d   /* DAT_0450f405 — player arrival-complete */
#define PC_F407_COMPANION_ARRIVED_BYTE_OFF  0x2bc6f   /* DAT_0450f407 — companion arrival-complete */

static void player_ctrl_cs_arrival_tick(void)
{
    const uint8_t *bank =
        (const uint8_t *)save_work_dwords_at(save_work_active_slot());
    int f405 = (bank != NULL) && bank[PC_F405_ARRIVED_BYTE_OFF] != 0;
    int f407 = (bank != NULL) && bank[PC_F407_COMPANION_ARRIVED_BYTE_OFF] != 0;
    union { float f; int32_t i; } z = { .f = 0.0f };

    if (!f405) {
        /* ── arriving (all.c:87367-87405): the stool jump + camera ramp ── */
        /* player facing angle = -camera_yaw (DAT_056db05c = -DAT_073de39c). */
        s_player_facing = -g_scene1_camera_yaw;
        /* octant DAT_056dab00 = ftol(((facing+yaw) + π/8)/2π · 8 + 8) & 7.  With
         * facing = -yaw, facing+yaw == 0 ⇒ ftol(0.5+8)=8, &7 = 0 (the __ftol arg
         * Ghidra dropped at 0x487e9e; consts π/8=0x519b78, 2π=0x519398, 8=0x519378).
         * Computed (not hard-0) to stay faithful if the facing ever drifts. */
        {
            float a = (s_player_facing + g_scene1_camera_yaw + 0.3926991f)
                      / 6.2831853f * 8.0f + 8.0f;
            s_actor_record[0][CHR_ACTOR_FACING] = (int32_t)a & 7;
        }
        if (!f407)   /* companion octant DAT_056dab58 = player octant */
            s_actor_record[2][CHR_ACTOR_FACING] = s_actor_record[0][CHR_ACTOR_FACING];

        if (s_actor_record[0][CHR_ACTOR_STATE] != 5) {   /* DAT_056daafc != 5 */
            s_actor_record[0][CHR_ACTOR_FRAME]   = 0;     /* _DAT_056daaf8 */
            s_actor_record[0][CHR_ACTOR_COUNTER] = 0;     /* DAT_056daaf4 */
            s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;   /* _DAT_056daaf0 */
            s_actor_record[0][CHR_ACTOR_ANIM]    = 5;     /* DAT_056daae8 = 5 (stool jump) */
            s_actor_record[0][CHR_ACTOR_STATE]   = 5;     /* DAT_056daafc = 5 */
        }

        s_db04c++;                                        /* DAT_056db04c++ */
        if (s_db04c > 10) {
            /* camera-pos ramp by scene_type (DAT_068dd3fc[stage*0x6cf]).  The
             * tutorial HOUSE view_mode 0 (<3) → x_target 0, z_target 6.9. */
            float x_target = 0.0f, z_target = 0.0f;
            int view_mode = scene1_camera_view_mode();
            if (view_mode >= 0) {
                if (view_mode < 3)      { x_target = 0.0f;  z_target = 6.9f;  }
                else if (view_mode < 5) { x_target = 10.7f; z_target = 15.0f; }
            }
            if (g_scene1_player_pos[2] < z_target + 1.69f)   /* da1e0 += 0.05 */
                g_scene1_player_pos[2] += 0.05f;
            if (z_target + 1.7f < g_scene1_player_pos[2])    /* da1e0 -= 0.05 */
                g_scene1_player_pos[2] -= 0.05f;
            if (x_target - 4.5f < g_scene1_player_pos[0])    /* da1d8 -= 0.125 */
                g_scene1_player_pos[0] -= 0.125f;
            g_scene1_player_pos[1] = 0.5f;                   /* da1dc = 0x3f000000 */
        }
    } else {
        /* ── arrived (all.c:87406-87432): settle to the at-counter pose ── */
        if (s_actor_record[0][CHR_ACTOR_STATE] != 6) {   /* DAT_056daafc != 6 */
            s_actor_record[0][CHR_ACTOR_FRAME]   = 0;     /* _DAT_056daaf8 */
            s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;   /* _DAT_056daaf0 */
            s_actor_record[0][CHR_ACTOR_COUNTER] = 0;     /* DAT_056daaf4 */
            s_actor_record[0][CHR_ACTOR_ANIM]    = 6;     /* DAT_056daae8 = 6 */
            s_actor_record[0][CHR_ACTOR_STATE]   = 6;     /* DAT_056daafc = 6 */
        }
        if (s_actor_record[2][CHR_ACTOR_STATE] != 4) {   /* DAT_056dab54 != 4 */
            s_actor_record[2][CHR_ACTOR_FRAME]   = 0;     /* _DAT_056dab50 */
            s_actor_record[2][CHR_ACTOR_TIMER]   = z.i;   /* _DAT_056dab48 */
            s_actor_record[2][CHR_ACTOR_COUNTER] = 0;     /* _DAT_056dab4c */
            s_actor_record[2][CHR_ACTOR_ANIM]    = 4;     /* _DAT_056dab40 = 4 */
            s_actor_record[2][CHR_ACTOR_STATE]   = 4;     /* DAT_056dab54 = 4 */
        }
        /* face toward the companion side (player X vs companion X = da1f0). */
        if (g_scene1_player_pos[0] <= g_scene1_actor_pos[2][0]) {
            s_actor_record[0][CHR_ACTOR_FACING] = 6;      /* DAT_056dab00 = 6 */
            s_player_facing = 1.5707964f;                 /* DAT_056db05c = π/2 */
        } else {
            s_actor_record[0][CHR_ACTOR_FACING] = 2;      /* DAT_056dab00 = 2 */
            s_player_facing = -1.5707964f;                /* DAT_056db05c = -π/2 */
        }
    }
    /* engine: FUN_0047019f() (the in-shop browsing-customer pump) is called by
     * the CALLER right after this arrival body, before the master tick — see the
     * cc08==4 arm below (scene1_customer_npc_pump), not from inside here. */
}

/* The unported cc08 dispatch arms (FUN_0048670f all.c:358-918): the event /
 * camera / counter / menu / dialogue states (0,2,3,4,0xa,0xf,0x10,0x11,0x12,
 * 0x17,0x1e,0x32).  Each is reached only by a state transition the port doesn't
 * make yet (customer approach → 4, counter open → 0x32, …), so with cc08 pinned
 * to free-roam they are dead — they would each fall through to the common tail
 * (LAB_004893ff).  Inline regions (no per-state VA), modelled as one inert no-op
 * that lands the dispatch on the tail, faithful to the engine's gotos. */
/* LABEL_180 (0x4873c3): back to Tear's menu from a sub-window — the hand
 * cursor off, SE 0x174 */
static void tear_menu_back(void)
{
    tear_menu_set_timer(0);
    s_cc08 = 15;
    title_save_dialog_cursor_set_visible(0);               /* FUN_00435612 */
    audio_play_se_by_id(0x174);
}

/* FUN_00461bf6(kind) + the session (FUN_0045edaa): open the shop for one of
 * Tear's arrangements */
static void tear_menu_open_shop(int32_t kind)
{
    uint8_t *bb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
    if (bb) bb[PC_F404_SELL_ACTIVE_BYTE_OFF] = 1;          /* +0x2bc6c */
    s_cc08 = 4;
    if (bb) bb[0x2bc6d] = 1;
    customer_service_set_script_file(kind);                /* FUN_00461bf6 */
    customer_service_session_init();                       /* FUN_0045edaa */
}

/* cc08 15 / 16 / 17 / 18 / 30: Tear's menu (FUN_0048670f asm 0x4877cd, 0x487621,
 * 0x48734c, 0x487393, 0x487519).  15 the list (exits through LABEL_132: the
 * anim tick + FUN_0048a833), 16 the 15-tick close and the entry (LABEL_133),
 * 17 her note / 18 the lectures / 30 the shop arrangements (straight to the
 * tail).  PORT-DEBT(render-coupled, FUN_00409925): the menu, note, lecture and
 * arrangement windows are not drawn. */
static void player_ctrl_cc08_tear_menu_arm(void)
{
    const uint16_t pressed = g_sim_buttons[0].pressed, held = g_sim_buttons[0].held;
    switch (s_cc08) {
    case 15:
        tear_menu_window_close_tick();
        if (g_scene1_player_pos[0] <= g_scene1_actor_pos[2][0]) {
            s_actor_record[0][CHR_ACTOR_FACING] = 6;
            PW(0x56db05cu).f = 1.5707963705062866f;
        } else {
            s_actor_record[0][CHR_ACTOR_FACING] = 2;
            PW(0x56db05cu).f = -1.5707963705062866f;
        }
        if (counter_menu_slide() >= 10) {
            const int in = tear_menu_input(pressed, held);
            if (in == TEAR_MENU_IN_CANCEL) {
                s_cc08 = 1;
                s_db048 = 0;
                s_db04c = 0;
                speech_bubble_close(0);                    /* FUN_00406212(0) */
            } else if (in == TEAR_MENU_IN_CONFIRM) {
                s_cc08 = 16;
            }
        }
        party_status_tick();                 /* LABEL_132: FUN_004897c6 */
        party_follow_tick();                 /* LABEL_133: FUN_0048a833 */
        return;
    case 16: {
        const int entry = tear_menu_close_tick();          /* ++b754 == 15 */
        if (entry >= 0) {
            uint8_t *bb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
            switch (entry) {
            case 0: case 8: case 9:
                s_cc08 = 17;
                audio_play_se_by_id(0x144);
                break;
            case 7:
                s_cc08 = 30;
                audio_play_se_by_id(0x174);
                tear_menu_set_cursor2(0);
                tear_menu_set_timer(0);
                break;
            case 5: {
                s_cc08 = 18;
                int32_t lv = 0;
                if (bb) memcpy(&lv, bb + 4 * SAVE_BANK_FIELD_MERCHANT_LEVEL, 4);
                tear_lecture_open(lv);
                break;
            }
            case 6:
                s_cc08 = 1;
                scene1_intro_dialogue_start_single(2, 14);  /* FUN_0044ba2c(2, 14, 0) */
                s_db048 = 0;
                s_db04c = 0;
                speech_bubble_close(0);
                break;
            case 1: case 2: case 3:
                tear_menu_open_shop(entry - 1);
                speech_bubble_close(0);
                break;
            default:
                s_cc08 = 1;
                s_db048 = 0;
                s_db04c = 0;
                speech_bubble_close(0);
                break;
            }
        }
        party_follow_tick();                 /* LABEL_133 */
        return;
    }
    case 17:
        if (tear_note_tick(pressed)) {
            tear_menu_set_timer(0);
            s_cc08 = 15;
            audio_play_se_by_id(0x144);
        }
        return;
    case 18:
        if (tear_lecture_tick(pressed, held)) tear_menu_back();
        return;
    case 30: {
        int choice = 0;
        const uint8_t *bb = (const uint8_t *)save_work_dwords_at(save_work_active_slot());
        const int r = tear_open_menu_tick(bb, pressed, held, &choice);
        if (r == TEAR_MENU_IN_CANCEL) {
            tear_menu_back();
        } else if (r == TEAR_MENU_IN_CONFIRM) {
            tear_menu_open_shop(choice == 0 ? 0 : choice == 1 ? 1 : 2);
            speech_bubble_close(0);
            tear_menu_set_timer(0);
        }
        return;
    }
    default:
        return;
    }
}

static void player_ctrl_cc08_unported_arm(void)
{
    /* ── cc08==4 customer-service / haggle arm (FUN_0048670f all.c:87366-87434) ──
     * The per-frame driver while the sell/haggle mode is active.  The engine runs
     * the customer/player arrival anim (the f405 walk-toward vs at-counter pose),
     * then FUN_0047019f() + FUN_00462403() (the master tick).
     *
     * Load-gate release: session_init spawned the d3e asset-load worker (b1cc=2;
     * the master tick is inert + the render off while it loads).  The engine's
     * worker BODY (LAB_00452ae8/b13) writes DAT_0438b1cc=1 when the load
     * completes — the port models that in worker_load (g_worker_sec_state_1cc).
     * Mirror it into the customer-service load gate (notify_loaded → b1cc=1) once
     * the secondary worker is no longer pending, so the master tick + render
     * activate exactly when retail's do (at LOADING_END). */
    if (s_cc08 == 4) {
        /* d3e load release.  Retail's loader worker (LAB_00452ae8/b13) writes
         * DAT_0438b1cc = 1 itself, so the master tick sees the release on the first
         * tick after the worker finishes (rhost DEFER: the boundary right after the
         * spawn tick — buy-40pct-accept b1cc 2@851 → 1@852, b524 1@852).  The port
         * keeps the worker's write in g_worker_sec_state_1cc and mirrors it into
         * customer_service's b1cc here, BEFORE anything this tick reads b1cc.  (The
         * old frame-start snapshot taken before this release was a Frida-era
         * real-thread compensation; it ran the master tick + companion one tick
         * late — the b524 +1.)
         *
         * {csloadpin} bracket: load_pin_elapsed() is the LEFT operand so it runs
         * (and increments) every b1cc==2 frame; it returns 1 unless a pin is set,
         * so unpinned play still clears purely on the worker.  With a pin active,
         * force the real-thread worker to completion so the load clears at exactly
         * N frames. */
        if (customer_service_b1cc() == 2 && customer_service_load_pin_elapsed()) {
            if (customer_service_load_pin_active() && g_worker_sec_state_1cc == 2)
                worker_load_force_d3e_complete();
            if (g_worker_sec_state_1cc != 2)
                customer_service_notify_loaded();
        }
        int b1cc_pre = customer_service_b1cc();
        /* publish this tick's load state for later-in-frame consumers (the
         * companion ctrl, scene1_sim, runs AFTER this). */
        customer_service_note_frame_load(b1cc_pre == 2);

        /* The arrival anim + camera ramp run THROUGH the d3e bg-load (UNGATED),
         * matching retail (RE §21.10).  The d3e customer-asset load is a BACKGROUND
         * load (b1cc==2) that does NOT raise the load SCREEN (be94<0x78 stays true),
         * so retail's FUN_0048670f house_update keeps ticking: the stool-jump anim
         * starts (panim 5 @ entry+2) and the camera zooms toward the counter while
         * b1cc==2 (verified: retail panim 5 + camex ramp -1.5→-2.65 across the 24f
         * load).  Only the MASTER tick is inert during the load (it self-gates on
         * b1cc==2, below) so the b524 idle counter does not advance — that's why the
         * player can ramp onto the stool while the haggle machine waits.
         *   (The old §19/Chip-3c `b1cc != 2` gate wrapped the WHOLE body, delaying
         * the arrival ~23f to the post-load frame — rng-neutral, so the rng survey
         * never caught it, but the stool jump + camera lagged the load by its whole
         * duration; notes #1/#4-#7.  Now only the master tick is gated.) */
        {
            /* Chip 3c: the f405 player/companion arrival anim + the camera-pos
             * ramp (all.c:87367-87432, BEFORE the master tick) — sets panim 5/6
             * + the octant + slides g_scene1_player_pos toward the counter view. */
            player_ctrl_cs_arrival_tick();

            /* daf88: keep g_scene1_player_ground_y tracking the floor under the
             * cc08 player (the counter platform, y≈1.27).  The free-roam writer
             * (FUN_0048b850, party_tick) does NOT run while cc08==4, but the engine
             * still recomputes daf88 in the cc08 block (FUN_0048b850 → FUN_00483170,
             * all.c:87749).  This is the value the post-haggle wrap-up cutscene
             * FREEZES (the tick stops running once iv1_7 routes to the EVENT arm),
             * so the companion hovers at ground_y+3 ≈ 4.35 like retail instead of
             * the default-floor 3.0 (RE §18.4 / viewer note #1). */
            {
                const collision_mesh *cm = collision_house_get();
                if (cm)
                    collision_set_player_ground_y(cm, g_scene1_player_pos);
            }
        }

        /* FUN_0047019f (all.c:87432) — the in-shop browsing-customer pump runs
         * UNCONDITIONALLY in the engine's cc08==4 arm (between the arrival anim and
         * the master tick), NOT gated on the b1cc asset-load worker.  The engine
         * caller (FUN_00443… all.c:40591) gates house_update only on the be94
         * load-SCREEN counter, never on b1cc; the master tick self-gates on b1cc
         * internally, but the pump does not.  Retail's pump therefore increments
         * s_cs_frame every cc08==4 frame INCLUDING the d3e load (drilled: retail
         * npcfr increments from the f406 first-customer entry off=1 while b1cc==2).
         * The port's old whole-arm `b1cc != 2` gate (added for the arrival/camera
         * transition parity) wrongly suppressed the pump ~17f at the first-customer
         * entry → the ENTIRE spawn cadence mis-phased vs retail (the residual the
         * PORT-DEBT(cs-walker-rng-phase) note chased).  Now the pump + the arrival
         * anim + the sprite tick all run unconditionally through the load (matching
         * retail, RE §21.10); only the MASTER tick is gated (inert during b1cc==2
         * anyway, and it must not advance b524 on the load-release frame).  spawn one chibi
         * customer every 30th frame for a LIVE walk-in (f404==0; the scripted
         * tutorial f404==1 spawns none), then wander every active NPC.  shop_tier =
         * bank tier selector (DAT_04510578[stage]); walk grid = DAT_074b28e8. */
        {
            int sell_inactive = !customer_service_f404();   /* f404==0 */
            int shop_tier = 0;
            const uint32_t *cs_bank =
                save_work_dwords_at(save_work_active_slot());
            if (cs_bank != NULL)
                shop_tier = (int)cs_bank[SHOP_DISPLAY_TIER_SELECTOR];
            scene1_customer_npc_pump(sell_inactive, shop_tier);
        }

        if (b1cc_pre != 2) {
            /* the master tick (FUN_00462403): owns the b534 state switch + the
             * scripted-sell dispatch + the cinematic counter camera.  INERT during
             * the d3e load (it self-gates on b1cc==2 at its top).
             * cur/pressed/held = DAT_073dddd0/d4/d6 (g_sim_buttons[0]). */
            customer_service_master_tick(g_sim_buttons[0].cur,
                                         g_sim_buttons[0].pressed,
                                         g_sim_buttons[0].held);
        }

        /* LABEL_508: FUN_004897c6 — the status timers and the three actors' anim
         * ticks, every frame of the arm including the d3e load; Tear's
         * FUN_0048a833 ran (or not) in the master tick */
        party_status_tick();
        return;
    }
    /* Other unported cc08 states (0/2/3/0xa/0xf/0x10/0x32/…) — reached only by
     * transitions the port doesn't make yet; inert no-op (the engine's common
     * tail LAB_004893ff). */
}

/* ── FUN_0048670f prologue: shop-display "目玉商品" sparkle emitter ──────────
 *
 * (all.c:86579-86598; asm 0x486737-0x4867fa.)  Every 8th frame the engine
 * drops a sparkle over each occupied BACK-ROW display cell — template 0x3b
 * ("目玉商品" = featured display item) through the 2D-overlay particle system
 * (FUN_004147d5 → FUN_00414345 → scene1_overlay_spawn).  Found by a live retail
 * call-trace on the overlay spawn (caller ret_va 0x4867ee) + the template name;
 * full RE in docs/findings/shop-item-display-RE-status.md.
 *
 * Engine: iterate the 7 fixed back-row columns DAT_005ce3c4 = {1,2,3,4,11,12,13};
 * for each, if the furniture gate passes AND the display grid (row 0) cell is
 * occupied, spawn ONE sparkle.  Per emit, 3 rng_next_unit() in order x,y,z:
 *   x = 2*col - 9 + (r1 - 0.5)    (col 1→-7, 2→-5, 4→-1 = the user-save swords)
 *   y = r2 + 2.0                  (just above the item)
 *   z = (r3 - 0.5) - 7.0          (the back stand)
 * template 0x3b, scale 0.3 (0x3e99999a), color white, override_dur -1 → the
 * template's fade_out_default (24) → ~3 alive per item.  Owner = &DAT_056da1b8
 * (stored only; template 0x3b's type_shape 0 + shape_mode 0 never derefs it).
 *
 * PORT-DEBT: the engine's furniture-suppression gate FUN_004860c8(col,0) →
 * DAT_0450fee8[fidx]==0 is omitted.  It only differs from the grid check during
 * display-EDITING mode (DAT_0438cc08==2 — the not-yet-ported item-placement
 * menu); in steady free-roam every grid-occupied back-row cell sits on a visible
 * stand (gate==0 → emit), so grid-only is bit-faithful here, INCLUDING RNG (it
 * emits over exactly the same cells, the only LCG consumers).  Restore the exact
 * gate when the display-management UI lands. */
static const int kSparkleDisplayColumns[7] = { 1, 2, 3, 4, 11, 12, 13 };

static void player_ctrl_display_sparkle_emit(void)
{
    if ((g_sim_frame_count % 8) != 3)        /* DAT_0438b8cc % 8 == 3 */
        return;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    const int32_t *grid =
        (const int32_t *)(bank + SAVE_BANK_FIELD_DISPLAY_GRID);

    for (int i = 0; i < 7; i++) {
        int col = kSparkleDisplayColumns[i];
        if (grid[col] == -1)                 /* back-row cell empty */
            continue;

        float r1 = rng_next_unit();          /* x jitter */
        float x  = (float)(2 * col - 9) + (r1 - 0.5f);
        float r2 = rng_next_unit();          /* y */
        float y  = r2 + 2.0f;
        float r3 = rng_next_unit();          /* z jitter */
        float z  = (r3 - 0.5f) - 7.0f;

        scene1_overlay_spawn(&g_scene1_combat_dat_056da1b8,
                             x, y, z,
                             0x3b,            /* template: 目玉商品 sparkle */
                             0.3f,            /* scale_base (0x3e99999a) */
                             -1,              /* dur<1 → template fade_out_default (24) */
                             0, 0, 0);
    }
}

/* ── per-frame player-controller tick (engine FUN_0048670f driver) ─────────
 *
 * Wired into the live HOUSE frame (scene1_ingame_default_arm_tick, before
 * scene1_records_b_tick — the engine's order at FUN_00442cef L40593-40598).
 * Chip 3 gave it the faithful FUN_0048670f outer skeleton (prologue guard →
 * spawn/transition stubs → dispatch → tail); Chip 4 makes the cc08 dispatch a
 * real read of the live state id (DAT_0438cc08) instead of an unconditional
 * route, with the off-path arms as honest stubs (§78).
 *
 * cc08 is set to 1 (free-roam) at HOUSE entry by player_ctrl_cc08_enter_freeroam
 * (FUN_004850ec); the transitions that would drive it elsewhere are unported, so
 * it stays 1 in steady play and the dispatch lands on the free-roam arm — but it
 * now genuinely gates on cc08 (test_player_ctrl_dispatch_gates_on_cc08), so a
 * future writer that sets another state immediately takes effect.  The
 * controllable (cc08==1, cc04==0, da1bc==0) free-roam walk it routes to is
 * validated against runs/w3-walk-watch (per held-direction frame: impulse →
 * clamp → integrate → damp), byte-identical per-frame to the retail watch. */
void scene1_player_ctrl_tick(void)
{
    /* Clear the inline-companion latch: set again only if the free-roam walk
     * path reaches player_ctrl_b850_move() this frame (engine FUN_0048a833 nested
     * in FUN_0048b850).  scene1_sim.c reads it to decide the non-walk fallback. */
    s_b850_ran = 0;


    /* Per-frame ACTOR-STATE flow-trace payload (FUN_0048670f = the HOUSE
     * free-roam update, parent of both the player and companion controllers).
     * Read at onEnter (frame start, before this frame's update) so it mirrors
     * the retail Frida hook on 0x48670f reading the same engine globals at the
     * same point (tools/flow/retail_fields.json).  Player (actor 0) +
     * companion (actor 2) facing octant / world angle / position / anim cell —
     * the modern replacement for the old --player-pos-log px/oct/coct/… columns
     * that phase_probe.py diffed.  poct=DAT_056dab00, pang=DAT_056db05c,
     * coct=DAT_056dab58, pos arrays DAT_056da1d8[actor*3].
     *
     * GATE: emit ONLY on real free-roam frames — the SAME condition the
     * freeroam-walk arm runs under (below).  The retail FUN_0048670f effectively
     * only feeds the trace in free-roam; the port's tick is also called every
     * frame through the iv1_1/iv1_2 prologue, where the player sits immobile at
     * the pose_house_standing init state (px -0.30, pcnt 0, coct 4) and the
     * walk arm is gated off.  Emitting there too floods the trace with stale
     * pose rows that flow_diff mis-pairs against retail's clean free-roam rows —
     * it surfaced as a PHANTOM companion-facing coct 6/4 "divergence" (the live
     * free-roam rows are bit-identical to retail; the pose rows are not).  See
     * docs/findings/flow-trace-cheatsheet.md. */
    if (s_actor_char[0] != -1 &&
        ((s_cc08 == 1 &&
          !scene1_intro_dialogue_active() && !scene1_intro_dialogue_loading() &&
          !scene1_intro_dialogue_posing())
         || s_cc08 == 4)) {
        CALL_TRACE_BEGIN(0x48670fu);
        {
            const int32_t *r0 = s_actor_record[0];
            const int32_t *r2 = s_actor_record[2];
            CALL_TRACE_F32("px",    g_scene1_player_pos[0]);
            CALL_TRACE_F32("py",    g_scene1_player_pos[1]);
            CALL_TRACE_F32("pz",    g_scene1_player_pos[2]);
            /* Chip 3c camera-framing probe (engine _DAT_073de31c..330 = the
             * final smoothed eye/lookat): chasing the cc08==4 counter-camera
             * residual (RE §8.7.2 — px/pz match retail but the framing differs).
             * Joined to the retail 0x48670f probe's camex/camez/camlx/camlz. */
            CALL_TRACE_F32("camex", g_scene1_camera_eye[0]);
            CALL_TRACE_F32("camey", g_scene1_camera_eye[1]);   /* eye Y — the PITCH (angle) */
            CALL_TRACE_F32("camez", g_scene1_camera_eye[2]);
            CALL_TRACE_F32("camlx", g_scene1_camera_lookat[0]);
            CALL_TRACE_F32("camly", g_scene1_camera_lookat[1]); /* lookat Y — the PITCH (angle) */
            CALL_TRACE_F32("camlz", g_scene1_camera_lookat[2]);
            CALL_TRACE_I32("cmode", g_scene1_camera_char_mode);
            CALL_TRACE_I32("poct",  r0[CHR_ACTOR_FACING]);
            CALL_TRACE_F32("pang",  s_player_facing);
            CALL_TRACE_I32("panim", r0[CHR_ACTOR_ANIM]);
            CALL_TRACE_I32("pframe",r0[CHR_ACTOR_FRAME]);
            CALL_TRACE_I32("pcnt",  r0[CHR_ACTOR_COUNTER]);
            CALL_TRACE_F32("cx",    g_scene1_actor_pos[2][0]);
            CALL_TRACE_F32("cz",    g_scene1_actor_pos[2][2]);
            CALL_TRACE_I32("coct",  r2[CHR_ACTOR_FACING]);
            CALL_TRACE_I32("canim", r2[CHR_ACTOR_ANIM]);
            CALL_TRACE_I32("cframe",r2[CHR_ACTOR_FRAME]);
            /* DIAGNOSTIC (RE §21.28 chr_anim seed-origin): the companion's raw
             * anim counter + timer — pins WHICH frame each side ticks/resets
             * the wing cycle (the ±1 cframe boundary blips).  Retail mirrors:
             * ccnt 0x56dab4c, ctimer 0x56dab48. */
            CALL_TRACE_I32("ccnt",  r2[CHR_ACTOR_COUNTER]);
            {
                union { int32_t i; float f; } tmr = { .i = r2[CHR_ACTOR_TIMER] };
                CALL_TRACE_F32("ctimer", tmr.f);
            }
            /* DIAGNOSTIC (RE §21.28, notes #20/#22): the cs browsing-customer
             * SLOT 0 anim/frame/counter (engine DAT_073a6e50, stride 0x90) —
             * the walk-cycle body-pose phase where facing+path already match. */
            {
                const int32_t *n0 = scene1_customer_npc_slot(0);
                CALL_TRACE_I32("n0anim", n0 ? n0[0] : -1);
                CALL_TRACE_I32("n0frm",  n0 ? n0[4] : -1);
                CALL_TRACE_I32("n0cnt",  n0 ? n0[3] : -1);
            }
            /* db054 = the {phasepin}-zeroed per-scene counter — the shared clock
             * flow_diff --align-field uses to pair port↔retail frames on a
             * load-stretched HOUSE capture (port ~475 vs retail ~14285). */
            CALL_TRACE_I32("db054", scene1_companion_db054());
            /* g_sim_frame_count (DAT_0438b8cc) — the 目玉-sparkle gate counter.
             * Diff'd vs db054 to see whether the two co-advancing clocks hold the
             * same relative phase after {phasepin} (the dust↔sparkle RNG-order
             * question; engine-quirks §112). */
            CALL_TRACE_I32("gsim", (int32_t)g_sim_frame_count);
            /* DIAGNOSTIC (rng-survey §21.7): the LCG STATE (DAT_006023a0 =
             * g_rng_seed) — to pin where the rng VALUE stream misaligns vs
             * retail at the f406 entry (the cs-walker burst value-gap). */
            CALL_TRACE_I32("rngst", (int32_t)g_rng_seed);
            /* the INGAME interaction state id (DAT_0438cc08): 1=free-roam,
             * 4=customer-service/haggle, 0x32=counter, … — the retail probe's key
             * dispatch field (was port-omitted; flow_diff now compares it). */
            CALL_TRACE_I32("cc08", s_cc08);
            /* shop-display interaction state (the cc04==1 remove-item menu): the
             * sub-state gate + the highlighted display cell the open gate fires
             * off of.  Mirrors the retail 0x48670f hook's cc04/cbfc/cc00 fields
             * (tools/flow/retail_fields.json). */
            CALL_TRACE_I32("cc04", s_cc04);
            CALL_TRACE_I32("cbfc", shop_display_cbfc());
            CALL_TRACE_I32("cc00", shop_display_cc00());
            /* faced display-grid item id (SAVE_BANK_FIELD_DISPLAY_GRID cell):
             * the A2 removal writes -1 here on the confirm frame.  Port-only
             * (the retail 0x48670f hook does not yet declare it). */
            {
                const uint32_t *gb = save_work_dwords_at(save_work_active_slot());
                int gcol = shop_display_cbfc(), grow = shop_display_cc00();
                int gv = -2;   /* no cell faced */
                if (gb != NULL && gcol >= 0 && grow >= 0)
                    gv = (int)gb[SAVE_BANK_FIELD_DISPLAY_GRID
                                 + gcol + grow * SHOP_DISPLAY_GRID_STRIDE];
                CALL_TRACE_I32("gridc", gv);
            }
            /* cs-flow tutorial flags (the iv1_7 wrap-up / iv1_8 chain, P2/P3).
             * f400 (0x2bc68)=iv1_7 trigger / shop-display-suppress (dual-use);
             * f401 (0x2bc69)=iv1_7-done gate; f406 (0x2bc6e)=cs-active → iv1_8.
             * iv1_7 fires iff f401==0 && f400==1 (FUN_0044bd0d all.c:45715); f400's
             * ONLY writer is the cs leave/dissolve (all.c:60389) so it is 0 at a
             * fresh LOAD (probe-confirmed: settled §12's "does cad868 have f400
             * set?" read — it does not; the frame-231 hang was an env/9p confound).
             * Port-side (the retail 0x48670f probe can declare the same bytes when
             * a join is needed); tracks the f400 flip post-haggle + the f406→f402
             * iv1_8 transition. */
            {
                const uint32_t *fb = save_work_dwords_at(save_work_active_slot());
                const uint8_t  *fbb = (const uint8_t *)fb;
                CALL_TRACE_I32("f400", fb ? fbb[0x2bc68] : -1);
                CALL_TRACE_I32("f401", fb ? fbb[0x2bc69] : -1);
                CALL_TRACE_I32("f406", fb ? fbb[0x2bc6e] : -1);
            }
            /* customer-service / haggle state (cc08==4) — the retail 0x48670f
             * probe's DAT_0730bXXX + DAT_005c6bXX fields (tools/flow/
             * retail_fields.json).  0/-1 in free-roam (no session); populated
             * once the sell mode is entered.  These are what flow_diff --verdict
             * checks for the customer-service trajectory (greeting b534=1@off90,
             * base 3000→1200, offer b574=1536/b584=1@off2440). */
            CALL_TRACE_I32("b534", customer_service_b534());
            CALL_TRACE_I32("b5a8", customer_service_b5a8());
            CALL_TRACE_I32("b56c", customer_service_b56c());
            CALL_TRACE_I32("b574", customer_service_offer());
            CALL_TRACE_I32("b5e0", customer_service_variant());  /* last cs_pick_line variant */
            CALL_TRACE_I32("b584", customer_service_round());
            CALL_TRACE_I32("b590", customer_service_b590());
            CALL_TRACE_I32("ask",  customer_service_player_ask());
            CALL_TRACE_I32("base", customer_service_base_price());
            CALL_TRACE_I32("b520", customer_service_b520());
            CALL_TRACE_I32("b5a0", customer_service_arrival_anim());
            CALL_TRACE_I32("b524", customer_service_b524());
            CALL_TRACE_I32("b544", customer_service_b544());
            /* standee POSE + reaction state (note #8/#9: Tear's manga-lines pose
             * + the out-of-sync standees).  poseL/poseR = the per-line sprite
             * slot b54c/b550 (FUN_0046602e draws g_scene_buy_sprites[b56c][b550]
             * for the right speaker); b53c = the '!'/flash timer; b5d0/b5d8 = the
             * pose/reaction state + want-index.  b1cc = the asset-load gate (2=
             * loading) — for the camera-transition frame-sync (when the cc08==4
             * scene starts relative to LOADING_END). */
            {
                struct cs_render_state cs;
                customer_service_get_render_state(&cs);
                CALL_TRACE_I32("poseL", cs.b54c);
                CALL_TRACE_I32("poseR", cs.b550);
                CALL_TRACE_I32("b53c",  cs.b53c);
                CALL_TRACE_I32("b5d0",  cs.b5d0);
                CALL_TRACE_I32("b5d8",  cs.b5d8);
                CALL_TRACE_I32("b1cc",  customer_service_b1cc());
                /* haggle-UI driver state (FUN_00466b7b sections 2-4): b598/b59c
                 * = the BARGAIN!! banner ramp, b58c = the choice-button row,
                 * b560/b540 = the price-digit cursor / Yes-No toggle.  These gate
                 * the price-info panel + BARGAIN number + Okay/Start-Again buttons
                 * the render now draws — probe them so the v3 state panel +
                 * flow_diff verify the new UI against retail. */
                CALL_TRACE_I32("b598",  cs.b598);
                CALL_TRACE_I32("b59c",  cs.b59c);
                CALL_TRACE_I32("b58c",  cs.b58c);
                CALL_TRACE_I32("b560",  cs.b560);
                CALL_TRACE_I32("b540",  cs.b540);
            }
            /* scripted-machine probes (RE §9): b51c (scripted gate), b608 (script
             * sub-state; ==4 = the BARGAIN price-confirm choice that should fire
             * PAUSE_OPEN), b5b0 (fileidx — always 0; tuto2/buy is PC-progression
             * not fileidx=1).  Mirror the extended retail 0x48670f probe. */
            CALL_TRACE_I32("b51c", customer_service_b51c());
            CALL_TRACE_I32("b608", customer_service_b608());
            CALL_TRACE_I32("b604", customer_service_b604());
            CALL_TRACE_I32("b5b0", customer_service_fileidx());
            /* in-shop chibi-NPC pump state (the cs-walker-rng-phase drill):
             * npcfr = s_cs_frame (DAT_073a8ba8, spawn cadence), npcsp =
             * spawned count (DAT_073a8bac), npcn = active slots, npcdr = LCG
             * draws the pump consumed THIS frame.  Joined to the retail
             * 0x48670f hook's DAT_073a8ba8/bac reads — flow_diff --field-timeline
             * pins the frame the spawn/retarget draws diverge. */
            CALL_TRACE_I32("npcfr", scene1_customer_npc_frame());
            CALL_TRACE_I32("npcsp", scene1_customer_npc_spawned());
            CALL_TRACE_I32("npcn",  scene1_customer_npc_active());
            CALL_TRACE_I32("npcdr", (int32_t)scene1_customer_npc_last_draws());
            /* shop-WINDOW bg-NPC drift positions (the 6 townsfolk, DAT_073a7f80):
             * x is the only animated axis; viewer note #11 (NPC at window diverges)
             * is a POSITION divergence (non-deterministic cad868 load drift) the
             * {bgnpcpin} corrects only at the f406 entry — this probe pins WHERE the
             * drift diverges so the pin point can be placed correctly.  Mirrored by
             * the retail 0x48670f hook (tools/flow/retail_fields.json bgx0..5). */
            {
                int bgvis = 0;
                for (int _b = 0; _b < SCENE1_BG_NPC_COUNT; _b++)
                    if (g_scene1_bg_npc[_b].visible == 0) bgvis |= (1 << _b);
                CALL_TRACE_I32("bgvis", bgvis);
                CALL_TRACE_F32("bgx0", g_scene1_bg_npc[0].x);
                CALL_TRACE_F32("bgx1", g_scene1_bg_npc[1].x);
                CALL_TRACE_F32("bgx2", g_scene1_bg_npc[2].x);
                CALL_TRACE_F32("bgx3", g_scene1_bg_npc[3].x);
                CALL_TRACE_F32("bgx4", g_scene1_bg_npc[4].x);
                CALL_TRACE_F32("bgx5", g_scene1_bg_npc[5].x);
            }
            /* foot-dust (records-A type-0xe) slot-state aggregate — the
             * RNG-pinned dust parity probe.  With RNG bit-exact + NPCs aligned,
             * a divergence here is dust LOGIC: dustvx/dustvz isolate the spawn
             * velocity-init (RNG values), dustsx/dustsz the emit pos + drift,
             * dustage the emit timing/lifetime, dustn the spawn/kill count.
             * Mirrored by the retail 0x48670f hook's src:'records_a_dust' fields
             * (tools/flow/retail_fields.json). */
            {
                int dn = 0, dage = 0;
                float dsx = 0.f, dsz = 0.f, dvx = 0.f, dvz = 0.f;
                int rcn = g_scene1_records_a_count;
                if (rcn > SCENE1_RECORDS_A_COUNT) rcn = SCENE1_RECORDS_A_COUNT;
                for (int s = 0; s < rcn; s++) {
                    const int32_t *rr =
                        &g_scene1_records_a[s * SCENE1_RECORDS_A_STRIDE];
                    if (rr[SCENE1_RECORDS_A_OFF_TYPE] != 0xe) continue;
                    dn++;
                    dage += rr[SCENE1_RECORDS_A_OFF_AGE];
                    dsx  += *(const float *)&rr[SCENE1_RECORDS_A_OFF_POS_X];
                    dsz  += *(const float *)&rr[SCENE1_RECORDS_A_OFF_POS_Z];
                    dvx  += *(const float *)&rr[SCENE1_RECORDS_A_OFF_VEL_X];
                    dvz  += *(const float *)&rr[SCENE1_RECORDS_A_OFF_VEL_Z];
                }
                CALL_TRACE_I32("dustn",   dn);
                CALL_TRACE_I32("dustage", dage);
                CALL_TRACE_F32("dustsx",  dsx);
                CALL_TRACE_F32("dustsz",  dsz);
                CALL_TRACE_F32("dustvx",  dvx);
                CALL_TRACE_F32("dustvz",  dvz);
            }
        }
        CALL_TRACE_END();
    }

    /* prologue guard FUN_00434d6a (all.c:86575): the save/load dialog gate.
     * Returns -1 only while that dialog pumps (DAT_0438b148 BSS-zero → returns
     * 0 in HOUSE); the engine early-returns on -1. */
    if (title_save_dialog_gate_tick() == -1)
        return;

    if (s_actor_char[0] == -1)        /* no live player actor (pre-HOUSE) */
        return;

    /* prologue (all.c:86580, BEFORE the transition arms): drop the periodic
     * "目玉商品" sparkles over the items on display.  No-op (no RNG) on an empty
     * display, so it leaves the foot-dust/wing RNG stream untouched there. */
    player_ctrl_display_sparkle_emit();

    /* the day-8 visitor's scene (work +0x2bcd8, all_ida FUN_0048670f): b924++;
     * from 121 the bg NPCs, the party (FUN_004897c6 once the visitor has spoken,
     * else FUN_0048b850) and the room clamp; +0x2bce8 / +0x2bce9 = 1, return */
    if (story_visitor_active()) {
        uint8_t *sb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
        scene1_beat_set_counter(scene1_beat_counter() + 1);
        if (scene1_beat_counter() > 120) {
            scene1_bg_npc_tick();                 /* FUN_0046f621 */
            if (sb[STORY_MET_OFF] != 0) {
                party_status_tick();              /* FUN_004897c6(&DAT_056da1b8) */
            } else {
                player_ctrl_walk();               /* FUN_0048b850 */
            }
            player_ctrl_house_room_clamp(&g_scene1_player_pos[0], &g_scene1_player_pos[2]);   /* FUN_00486435 */
        }
        sb[0x2bce8] = 1;
        sb[0x2bce9] = 1;
        return;
    }

    /* the day-15 scene (work +0x2bced, set by FUN_004852fb): b924++, the bg
     * NPCs, FUN_004897c6, the room clamp; the camera eases over the shop (class
     * 1: lookat x → −4, z → max(4 − b924·0.05, −9) at 0.2 a tick —
     * PORT-DEBT(render-coupled, FUN_0048670f): the eye / lookat writes); the
     * fade at b924 301, the flag clears once it is done (from 302) */
    {
        uint8_t *sb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
        if (sb != NULL && sb[0x2bced] != 0) {
            scene1_beat_set_counter(scene1_beat_counter() + 1);
            scene1_bg_npc_tick();                 /* FUN_0046f621 */
            party_status_tick();                  /* FUN_004897c6(&DAT_056da1b8) */
            player_ctrl_house_room_clamp(&g_scene1_player_pos[0], &g_scene1_player_pos[2]);   /* FUN_00486435 */
            if (scene1_beat_counter() == 301)
                fade_phase1_start(0, 0x11);       /* FUN_004526f5(0, 17) */
            if (scene1_beat_counter() >= 302 && fade_is_done()) {
                fade_phase_out_start(0, 0x11);    /* FUN_0045281c(0, 17) */
                sb[0x2bced] = 0;
            }
            return;
        }
    }

    /* prologue (all.c:86697-86703 / asm 486b7d-486bb2, gated SHOP_DAY ≥ 9 —
     * dormant through the tutorial traces): the news-break ticker pump.
     * While armed (g_news_ticker_timer = DAT_0438b92c > 0) it counts every
     * free-roam frame; at 0x1e with headlines present it requests the
     * news-jingle SE 0x2bd (FUN_00499519).
     * PORT-DEBT(stub, FUN_00436f97): the newspaper/ticker DRAW consuming b92c + the headline buffers is unported (also owns the ≤2f ticker-arm skew at the iv2 beat release). */
    {
        const uint32_t *nbank = save_work_dwords_at(save_work_active_slot());
        if (nbank != NULL &&
            (int32_t)nbank[SAVE_BANK_FIELD_SHOP_DAY] > 8 &&
            g_news_ticker_timer > 0) {
            if (g_news_ticker_timer == 0x1e &&
                (int32_t)nbank[SAVE_BANK_FIELD_NEWS_HL_COUNT] > 0)
                audio_play_se_by_id(0x2bd);
            g_news_ticker_timer++;
        }
    }
    scene1_beat_master_prologue();       /* the day-9+ morning block (b928 == 1) */

    /* prologue: per-frame background-window-NPC pump (FUN_0046f621 → the
     * spawn/drift sim FUN_0046f2a3, ported in scene1_bg_npc.c).  The first call
     * runs the 180× warmup that seeds the 6 NPCs; later calls tick once.  This
     * is a live consumer of the shared LCG (sporadic bound-cross respawns) —
     * its position in the call order, BEFORE FUN_0048b850's dust emit, is what
     * the foot-dust RNG-stream parity depends on.  (The periodic customer-spawn
     * refresh that also lives in this prologue stays inert — no customers.) */
    scene1_bg_npc_tick();

    /* prologue (all.c:86728, `if (DAT_0438b924==0) FUN_0048960d()`): rebuild the
     * furniture-LAYOUT grid (DAT_074b28e8) from the active record's shop-tier
     * template + the live placed-furniture footprints.  Runs every HOUSE frame
     * (b924==0 in steady free-roam), BEFORE the cc08 dispatch, so the grid is
     * fresh for the walk tail's cell-highlight detector and the open gate.  This
     * is the A0 prerequisite: the back-row display stands exist only as the
     * 1×4 furniture stamp the engine writes here (the base template has no
     * stand cells). */
    shop_display_grid_rebuild();

    /* DIAGNOSTIC (rng-survey §21.4 ROOT 2): one-shot grid+inputs dump the first
     * frame the f406 first-customer entry holds (cc08==4 && b51c==0 — the same
     * gate as the retail agent's bgnpc SoA dump), so the cs-walker retarget
     * grid divergence is diffable port↔retail.  Fires AFTER the rebuild so the
     * dumped grid is this frame's.  Compiled in; inert unless capturing. */
    {
        static int s_grid_dumped = 0;
        if (!s_grid_dumped && customer_service_active() &&
            customer_service_b51c() == 0) {
            const char *dir = openrecet_capture_dir();
            if (dir != NULL) {
                shop_display_grid_dump(dir, (uint32_t)g_sim_frame_count);
                s_grid_dumped = 1;
            }
        }
    }

    /* scene-transition fade handlers (DAT_0450f470/485/488/495): none fires in
     * steady HOUSE free-roam → fall through to the controller. */
    if (player_ctrl_scene_transition_tick())
        return;

    /* cc08 dispatch (DAT_0438cc08): cc08==1 = free-roam walk → the ported arm;
     * every other state is an unported event/menu/dialogue arm.  Reads the live
     * state id — a real dispatch, not the old unconditional route.
     *
     * Prologue guard: while the opening dialogue is running (iv1_1/iv1_2) or
     * loading its next script, the engine is in a cc08 != 1 (event) state, so
     * the free-roam WALK arm does NOT run and the player is immobile. The port
     * sets cc08=1 at HOUSE entry — *before* the prologue — so without this guard
     * the player walks during iv1_2 (the 2nd dialogue plays over the live
     * HOUSE), most visibly after an ESC skip of iv1_1 jumps onto it. Suppress
     * ONLY the walk arm: scene1_bg_npc_tick (above), records/particles, and the
     * bounds-clamp tail still run, so the load/scene path is untouched (gating
     * the whole controller broke the load — it skipped the NPC/RNG pump).
     * PORT-DEBT: the faithful fix is cc08 timing — it should flip to 1 only at
     * the real free-roam boundary (FUN_004850ec), after the prologue. */
    /* ALSO suppress while the conversation pose owns the player (_posing()).  The
     * D_TUT_LOAD->D_TUT lazy-load seam has a 1-frame window where _loading() just
     * dropped and _active() hasn't risen yet (only _posing() spans it).  Without
     * this the walk arm runs on that seam frame and resets the player's pose anim
     * (FUN_0048407f anim 6, the look-up-at-Tear blink) back to idle (anim 0); since
     * conv_pose_enter keys its restore on STATE (still 6) the anim never recovers,
     * so Recette plays her idle breathing loop (40-tick cycle) instead of the blink
     * (64) for the whole tutorial dialogue.  Retail has no such seam (its walk gate
     * tracks the cc08 event state = the talk flag), so suppressing the walk whenever
     * the pose holds the actor is the faithful fix (symptom: the iv1_5/iv1_6
     * shop-display tutorial CONV_POSE_BLINK cadence was port 40 vs retail 64). */
    /* camera stage-class: the cc08==4 master tick sets it to 1 (the cinematic
     * counter cam); every other state is the free-roam player-follow.  Reset it
     * here so the camera resumes tracking the moment customer service ends. */
    if (s_cc08 != 4)
        scene1_camera_set_freeroam_class();

    /* the free-roam period advance (asm 0x486c87, return from town) and the
     * night re-layout re-run while b4e0 == 1 (0x486d04) — both before the
     * slide ramp reads cc08. */
    shop_period_return_tick(s_cc08, s_cc04);
    day_close_relayout_tick();

    /* counter-menu slide ramp (all.c:96212-96225): after the grid rebuild,
     * before the cc08 dispatch, on this frame's dispatch value. */
    counter_menu_slide_tick(s_cc08);

    /* LABEL_104 (asm 0x486e2d): ++b924, the master-tick counter the beat holds
     * (b928 == 1 && b924 < 200) read from here on this frame. */
    scene1_beat_master_voices();
    scene1_beat_master_count();

    /* the day-close exchange (asm 0x486e27, after b924++): while b4e0 > 0 the
     * frame leaves through LABEL_132 — the player anim tick, FUN_0048a833 (no
     * FUN_0048b850, so db054 holds), then the LABEL_675 tail (FUN_00486435 +
     * FUN_00485861) — and skips the cc08 dispatch. */
    if (day_close_machine_tick()) {
        party_status_tick();                 /* LABEL_132: FUN_004897c6 */
        party_follow_tick();                 /* LABEL_133: FUN_0048a833 */
        player_ctrl_house_room_clamp(&g_scene1_player_pos[0],
                                     &g_scene1_player_pos[2]);
        actor_say_tick();
        return;
    }

    /* world-map exit stage 2 (all_ida 96366, the DAT_074b2ec4 == 1 block, after
     * the b4e0 machine and before the cc08 dispatch): the whole prologue — the
     * window-NPC pump, the period advance, the slides, ++b924 — still runs on
     * the door-exit frames (rhost multiday-tutorials-suggest f1606-1621: the
     * window NPCs keep drifting); then wait for the dissolve, mode 8 + the
     * world-map load, and LABEL_675 (FUN_00486435 + FUN_00485861) — no anim
     * tick, no FUN_0048a833. */
    if (player_ctrl_worldmap_exit_stage2()) {
        player_ctrl_house_room_clamp(&g_scene1_player_pos[0],
                                     &g_scene1_player_pos[2]);
        actor_say_tick();
        return;
    }

    if (s_cc08 == 1 &&
        !scene1_intro_dialogue_active() && !scene1_intro_dialogue_loading() &&
        !scene1_intro_dialogue_posing() &&
        !scene1_tutorial_dispatch_iv2_beat_active())
        player_ctrl_cc08_freeroam_arm();
    else if (s_cc08 == 1 && scene1_tutorial_dispatch_iv2_beat_active() &&
             !scene1_intro_dialogue_active() && !scene1_intro_dialogue_loading() &&
             !scene1_intro_dialogue_posing()) {
        /* the morning beat (b928 == 1 && b924 < 200): retail's cc08 == 1 arm
         * still runs its head — the rank arrivals (from b924 192) and the f406
         * entry — before LABEL_448 holds the pose (rhost multiday-tutorials-
         * suggest f27643: the rank-2 lesson at b924 192). */
        if (!player_ctrl_cc08_customer_escalate() && !player_ctrl_cc08_f406_entry() &&
            s_cc04 == 0) {
            player_ctrl_emote_label_448();   /* b928 == 1 && b924 < 200: goto LABEL_448 */
            /* LABEL_499: Recette faces Tear, anim 6; LABEL_416: FUN_0048a833 (Tear
             * beside her); LABEL_508: FUN_004897c6 (the status + 3 anim ticks) */
            PW(0x56db05cu).f = g_scene1_actor_pos[0][0] <= g_scene1_actor_pos[2][0]
                               ? 1.5707963705062866f : -1.5707963705062866f;
            party_anim_set(s_actor_record[0], 6);
            party_follow_tick();
            party_status_tick();
        }
    } else if (s_cc08 == 0 || s_cc08 == 10)
        player_ctrl_cc08_counter_menu_arm();
    else if ((s_cc08 >= 15 && s_cc08 <= 18) || s_cc08 == 30)
        player_ctrl_cc08_tear_menu_arm();
    else
        player_ctrl_cc08_unported_arm();

    /* tail LAB_004893ff: the room-bounds clamp (FUN_00486435) runs
     * UNCONDITIONALLY here — AFTER FUN_0048b850's mesh collision (§75).  The
     * mesh resolver gives the right wall (px 3.10) + counter (pz 8.94); this
     * clamp gives the front (pz≤9.5) + left (px≥-1.5 when pz>7) HOUSE bounds,
     * which aren't in the room mesh or any placed object (§67) — together they
     * reproduce retail's box px[-1.5,3.1] pz[8.94,9.5].  The clamp touches
     * position while b850's damp + the anim update touch velocity / the sprite
     * record, so the engine's split (damp inside b850, clamp in the tail) is
     * order-independent — moving it here from the old in-arm position is
     * bit-exact.  Then FUN_00485861 (screen-rumble; no-op in free-roam). */
    player_ctrl_house_room_clamp(&g_scene1_player_pos[0],
                                 &g_scene1_player_pos[2]);
    player_ctrl_tail_rumble();
}
