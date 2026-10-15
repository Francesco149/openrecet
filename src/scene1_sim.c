/*
 * scene1_sim.c — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004536cb @ 0x4536cb state-1 sub-dispatch
 * (L50555-50568) — picks one of three arms based on flag globals,
 * then runs that arm's per-tick body.
 *
 * FUN_004427d3 (30 B transition/paused/EVENT wrapper — the arm retail runs on
 * every dialogue frame, see scene1_ingame_tick) call list:
 *
 *   FUN_0048407f  (  795 B) — scene1_conversation_pose_tick +
 *                             scene1_event_actor_tail_tick (bg-NPC pump,
 *                             companion follow + wing, db054++)
 *   FUN_00430c00  (  109 B) — stub (town-NPC anim pump; empty in HOUSE)
 *   FUN_0043ae20  (25750 B) — scene1_records_b_tick (skeleton)
 *   FUN_0043a5d9  ( 1429 B) — stub (slide-panel updater; no RNG)
 *   FUN_0040fb3a            — scene1_particles_tick()
 *   FUN_004426a7  (  300 B) — stub (cinematic camera ramp; inert, no RNG)
 *
 * C8j.3 ports FUN_00442cef (2490 B default-running arm) as a thin
 * wrapper.  The engine body is dominated by gameplay-logic gates
 * (pause counters, cinematic counters, equip-bag counter, player-state
 * transitions) that gate the actual ticks.  With BSS-zero gates (HOUSE
 * default) the per-tick effect collapses to:
 *
 *   FUN_0043ae20  table B tick  → scene1_records_b_tick() (skeleton —
 *                                  outer loop + preamble only;
 *                                  per-type dispatch deferred to
 *                                  sub-chip ladder C8j-tick.2+)
 *   FUN_0044284b  table C tick  → scene1_records_c_tick()
 *   FUN_0040fb3a  table A tick  → scene1_particles_tick()
 *
 * The 5/3 stubbed siblings are safe to drop because their outputs feed
 * `FUN_0043ae20`'s game-logic tick (player movement, NPC AI, item
 * pickups), which we don't run.  Nothing in our port reads what they
 * would have written — scene-1 render reads only from the per-record
 * tables, which the integrators + spawn API populate independently.
 *
 * See docs/findings/sim-step-a-dispatch.md §"Cs1" for the original
 * Cs1 survey, and docs/findings/scene1-record-populators.md §"C8j.3"
 * for the C8j ladder.
 */

#include "scene1_sim.h"

#include <math.h>
#include <string.h>

#include "actor_ai.h"
#include "audio.h"
#include "chara_equip.h"
#include "choice_box.h"
#include "dungeon_flow.h"
#include "dungeon_objects.h"
#include "dungeon_party.h"
#include "fade.h"
#include "floor_layout.h"
#include "map_collision.h"
#include "news_daily.h"
#include "party.h"
#include "save_work.h"
#include "scene1_camera.h"
#include "scene1_combat_sm.h"
#include "scene_pause.h"
#include "scene_adv_guild.h"
#include "stage_palette.h"
#include "sim.h"
#include "stage_gate.h"
#include "stage_post_load.h"
#include "title_save_dialog.h"
#include "x87.h"

#include "scene1_particles_tick.h"
#include "scene1_companion_ctrl.h"
#include "scene1_conversation_pose.h"
#include "scene1_intro_dialogue.h"
#include "scene1_player_ctrl.h"
#include "scene1_records_b_tick.h"
#include "scene1_records_c_tick.h"
#include "table_c.h"
#include "scene1_tutorial_dispatch.h"
#include "rng.h"
#include "call_trace.h"

/* Engine flag stand-ins.  All BSS-zero default → HOUSE selects the
 * default-running arm.  See header for engine-globals mapping.
 *
 * DAT_0438b1c8 (the "busy/paused" scene gate) has TWO surfaces in the port:
 * this explicit flag (for writers the engine has that we haven't ported —
 * none today) and the dialogue runtime's lifecycle, scene1_intro_dialogue_busy()
 * (retail's live writers: FUN_0044bd0d arms a dialogue with b1c8=2, the
 * LAB_00452aab load worker holds 1, the script end clears 0).  The dispatch
 * below ORs them — they are the SAME engine global. */
int g_scene1_ingame_transition_flag = 0;  /* DAT_0438b1d0 */
int g_scene1_ingame_skip_flag       = 0;  /* DAT_0438b1d8 */
int g_scene1_ingame_paused_flag     = 0;  /* DAT_0438b1c8 (explicit-writer mirror) */

void scene1_ingame_transition_arm_tick(void)
{
    /* E.2 probe — FUN_004427d3 @ 0x4427d3.  STUB→partial: 4 of the engine's 6
     * calls run; FUN_00430c00 / FUN_0043a5d9 / FUN_004426a7 stay stubbed (see
     * below) so the row keeps its ≈ marker.
     *
     * This is the arm retail dispatches EVERY dialogue/event frame to
     * (FUN_004536cb routes b1c8 != 0 here INSTEAD of FUN_00442cef — verified on
     * item-display-2: during the two tutorial dialogues the retail call-trace
     * has 0x4427d3 + 0x48407f + 0x46f621 once per frame and NO 0x442cef /
     * 0x48670f rows).  Until 2026-06-09 the port kept running the DEFAULT arm
     * during dialogues, so the dev-overlay LCG step (+1/frame) and the
     * 目玉-sparkle emitter (+6 per 8 frames) kept consuming shared LCG that
     * retail does not — the +3245-call rngcalls DESYNC across the dialogue
     * window (the "bg-NPC desync after dialogue start" of
     * findings/shop-display-menu-RE.md follow-up #2). */
    CALL_TRACE_ENTER_STUB(0x4427d3u);

    /* FUN_004427d3 L8 — FUN_0048407f, the event-actor driver: the pose/release
     * branch (lean tick, shared with the default arm's release frame) + the
     * tail the lean tick deliberately omits (bg-NPC pump, companion
     * spring-follow + wing emit, the unconditional db054++). */
    if (mapcol_maptype() >= 1) {
        dungeon_event_actor_tick();          /* FUN_0048407f, *DAT_068dd2f0 >= 1 */
    } else {
        scene1_conversation_pose_tick();
        scene1_event_actor_tail_tick();
    }

    /* FUN_004427d3 L9 — FUN_00430c00: town-NPC anim pump (iterates the
     * 0x2e9-dword townfolk records; chr_anim_tick per live slot, then
     * FUN_0042ea35).  PORT-DEBT(focused, FUN_00430c00): no town NPCs exist in
     * the HOUSE scenes we verify; the loop body is a no-op there and consumes
     * no RNG. */

    /* FUN_004427d3 L10 — FUN_0043ae20 (table B tick; the same skeleton the
     * default arm runs). */
    scene1_records_b_tick();

    /* FUN_004427d3 L11 — FUN_0043a5d9: slide-panel/banner offset updater
     * (0x2a0-stride array at DAT_0695ef78, ±0.1 ramps, clamp −13).
     * PORT-DEBT(focused, FUN_0043a5d9): no consumer wired; no RNG. */

    /* FUN_004427d3 L12 — FUN_0040fb3a (table A particle integrator). */
    scene1_particles_tick();

    /* FUN_004427d3 L13 — FUN_004426a7: cinematic camera ramp, gated on
     * DAT_06a46fa0 > 0 (a cutscene counter, 0 in our windows); sin/cos only,
     * no RNG.  PORT-DEBT(focused, FUN_004426a7). */
}

static void stage_event_tick(void);

/* FUN_0048670f's slot of FUN_00442cef on a house stage: the port's house
 * controller (the conversation-pose release, scene1_player_ctrl_tick = the
 * FUN_0048670f reconstruction, the companion fallback).
 * PORT-DEBT(structure, FUN_0048670f): one retail-shaped master tick. */
static void house_master_tick(void)
{
    /* DAY-2 actor re-place one-shot (retail FUN_0048526d scene-entry re-seat).
     * FIRST, before the conversation-pose tick derives the face-each-other
     * octants and before render — the default arm runs only on non-dialogue
     * free-roam frames, so this fires on the first post-iv2_5 free-roam frame
     * (the beat's first counting frame, retail day2 @15470).  Inert (pending
     * flag 0) on every other frame. */
    scene1_tutorial_dispatch_consume_day2_replace();

    /* cc04 (the display-stand menu sub-state) SAMPLED AT DISPATCH: the engine's
     * db054 increment sits at the FUN_0048b850 tail, reached only when the
     * frame's cc08/cc04 dispatch took a non-menu path.  On the menu-CLOSE frame
     * the menu arm clears cc04 MID-frame and b850 never runs → retail does NOT
     * advance db054 that frame; the port's fallback below used to re-read cc04
     * after the clear and advanced anyway — +1 db054 per pause vs retail, which
     * skewed every db054-aligned diff after the first menu (the phantom
     * house_update.px/py/dust "DRIFT @202" on item-display-2: the walk values
     * are bit-identical under a 1-frame shift). */
    int cc04_at_dispatch = player_ctrl_cc04();

    /* cc08 SAMPLED AT DISPATCH — the cc04 story above, for the cs mode boundary
     * (RE §21.23).  db054 advances ONLY when the engine reaches the FUN_0048b850
     * tail (the free-roam MOVE), and retail's FUN_0048670f skips that move on
     * BOTH cc08==4 boundary frames — so its db054 stays FROZEN across the whole
     * cc08==4 span AND its two edge frames:
     *   - the 4→1 LEAVE (frame 631): retail's `if (cc08==4){…master tick sets
     *     cc08=1 when the b520 dissolve completes…} else {…walk→FUN_0048b850…}`
     *     is an if/else on the FRAME-START cc08 — the leave frame takes the
     *     cc08==4 branch and never reaches the walk/db054 path, even though cc08
     *     flipped to 1 mid-frame.  ⇒ gate on cc08_at_dispatch (the dispatch value
     *     was still 4).
     *   - the 1→4 ENTRY (frame 304): the walk arm (cc08==1 branch) detects the
     *     customer-service trigger, sets cc08=4, and `goto`s the tail SKIPPING
     *     FUN_0048b850 (all.c:87485-89) ⇒ no db054++.  ⇒ gate on the LIVE cc08
     *     (now 4), since the dispatch value was 1.
     * The port splits that single if/else across scene1_player_ctrl_tick (the
     * cc08==4 arm / freeroam arm flip cc08) and THIS fallback.  Gating on the
     * LIVE cc08 alone (the pre-§21.23 code) caught the ENTRY but re-read the
     * flipped-to-1 LEAVE value and advanced db054 a frame early — the +1 drift
     * from CONV_POSE_START (632); gating on the DISPATCH value alone catches the
     * leave but wrongly advances on the entry (dispatch still 1).  Require BOTH
     * == 1 = "free-roam at dispatch and after" = FUN_0048b850 actually ran (the
     * counter menu cc08 0/10 skips it too: switch cases, not the walk).
     * (The +1 drift cascaded into the late 目玉 sparkle / chibi walk phase via
     * the shared LCG — viewer notes #20/#22.) */
    int cc08_at_dispatch = player_ctrl_cc08();

    /* FUN_0048407f conversation branch (the event-actor tick): hold the iv1_2
     * face-to-face pose on the freeroam chibis (Recette look-up/blink anim 6,
     * Tear talk anim 4) and advance their sprite anim.  Runs FIRST, before the
     * freeroam controllers, mirroring the branch's position ahead of the
     * per-actor anim step + the spring-follow.  Inert outside the conversation;
     * the companion controller reads scene1_conversation_pose_active() to yield
     * its anim/facing while the pose is held.
     *
     * NOTE the engine's FUN_00442cef does NOT call FUN_0048407f — that driver
     * belongs to the EVENT arm (FUN_004427d3).  This default-arm call is the
     * port's release-frame compensation: our dialogue runtime drops busy() and
     * posing() on the SAME frame the script completes, so the release branch
     * (anim 6→0) must run from the first post-busy default frame.  Inert on
     * every other default frame (the !posing && !s_pose_active early-out). */
    scene1_conversation_pose_tick();

    /* FUN_00442cef L40595-40598 — the player controller runs FIRST, before
     * the records-B tick, gated on DAT_0438be94 < 0x78 and a dispatcher that
     * selects FUN_0048670f (the live HOUSE branch) over the FUN_0048b3f6
     * variant.  Both gates open in HOUSE; W1 wires the entry (stub body). */
    scene1_player_ctrl_tick();

    /* Companion controller (FUN_0048a833).  The engine nests it INSIDE the player
     * driver's FUN_0048b850, right before the foot-dust emit, so the fairy's
     * wing-sparkle RNG precedes the dust's.  On a free-roam walk frame
     * player_ctrl_b850_move() already ran it there (and advanced db054 after the
     * dust); this fallback ticks+advances it on every OTHER frame — the
     * display-menu / pre-move paths the engine reaches through FUN_0048b850's
     * other callers but the port's stubbed arms do not.  (Dialogue frames no
     * longer come through here at all — scene1_ingame_tick routes busy frames
     * to the event arm, whose tail owns the companion + db054.)  Gated on the
     * latch so the companion ticks exactly once per frame (engine-quirks §114);
     * the db054 advance additionally requires the frame to have DISPATCHED
     * outside the menu (cc04_at_dispatch — see above; advance_phase itself
     * still re-checks the live cc04 for the menu-OPEN frame, set mid-frame). */
    if (!player_ctrl_companion_ticked() && !player_ctrl_companion_skip_this_frame()) {
        scene1_companion_ctrl_tick();
        /* db054 advances ONLY in free-roam: the engine bumps it at the
         * FUN_0048b850 tail, which does NOT run in cc08==4 (customer service) —
         * so retail's db054 FREEZES there (verified: frozen at 156).  The
         * companion wing-glow sparkle gate is `db054 % 4 == 0`, so a frozen db054
         * (156 % 4 == 0) emits the type-0x1f particle EVERY frame, vs every 4th
         * while it keeps incrementing — this is the cc08==4 ambient-particle rng
         * the port was under-drawing (~5.5/f vs retail ~10/f).  Freeze it here to
         * match (RE §8.8 — the resolved §8.5 rng-rate gap). */
        /* … and not while the b928 beat holds (b924 < 200): FUN_0048670f's
         * LABEL_448 → LABEL_416 path runs FUN_0048a833 but not FUN_0048b850. */
        if (cc04_at_dispatch == 0 && cc08_at_dispatch == 1 &&
            player_ctrl_cc08() == 1 && !scene1_tutorial_dispatch_iv2_beat_active())
            scene1_companion_ctrl_advance_phase();
    }

}

/* the portal check at the top of FUN_00442cef: standing within 1 of a portal
 * (kind 8, or kind 6 once its state is 1) arms the Yes/No (be9c = 1) unless
 * the cooldown runs; walking 1 away from a declined one re-arms it (-1 → 0) */
static void stage_portal_scan(void)
{
    static const float k3 = 3.0f, k1 = 1.0f;
    for (int n = 0; n < DOBJ_COUNT; n++) {
        const dobj_t *o = &g_dobj[n];
        const int32_t kind = o->i[DOBJ_KIND];
        if (kind == -1) continue;
        if (kind == 6) { if (o->i[DOBJ_STATE] != 1) continue; }
        else if (kind != 8) continue;
        if (o->i[DOBJ_STATE] == 3) continue;
        const float dx = spill32((long double)g_party.pos[0][0] - o->f[DOBJ_POS + 0]);
        const float dz = spill32((long double)g_party.pos[0][2] - o->f[DOBJ_POS + 2]);
        const float d = spill32((long double)sqrt(spill64((long double)dz * dz + (long double)dx * dx)));
        if (!(d < k3)) continue;
        g_portal_return = kind == 6;
        if (g_portal_prompt != 0) {
            if (g_portal_prompt == -1 && d > k1) g_portal_prompt = 0;
        } else if (g_portal_cooldown <= 0 && d < k1) {
            g_portal_prompt = 1;
            g_portal_cooldown = 30;
        }
    }
}

/* the "Equip items in bag?" prompt (bea0) at a run's first floor */
static void stage_equip_prompt(void)
{
    if (!fade_in_settled()) return;          /* FUN_004528e2 */
    if (g_equip_prompt < 118) ++g_equip_prompt;
    if (g_equip_prompt == 2) {
        audio_play_se_by_id(371);
        choice_box_open("Equip items in bag?", 1, 0);   /* FUN_00434def(…, 1, 0) */
    }
    if (g_equip_prompt <= 2) return;
    const int r = choice_box_poll(g_sim_buttons[0].pressed, 1);   /* FUN_00434ed2(1) */
    g_ctrl_lock = 0;
    if (r == CB_OPT0) {
        /* PORT-DEBT(stub, FUN_0047f46a): equip the bag's items */
        chara_equip_recompute_aggregate();   /* FUN_004844ef */
    } else if (r != CB_OPT1) {
        return;
    }
    g_equip_prompt = 0;
    choice_box_reset();                      /* FUN_00434dbf */
    title_save_dialog_cursor_set_visible(0); /* FUN_00435612 */
    g_ctrl_lock = -1;
}

/* the portal prompt's best-time records (b1e8 into the work record): a boss
 * run's return portal → +0x2c42c[b4d0][count/5]; a regular dungeon's every 5th
 * floor → +0x2c42c[b4c8][b4cc/5]; dungeon 5 every 10th below 30 → +0x2c5bc;
 * dungeon 5 from 30 on, the return portal → +0x2c5c8 */
static void portal_time_record(uint8_t *rec)
{
    const int32_t fl = stage_gate_get_next();
    uint8_t *slot = NULL;
    if (g_dat_0438b4d4 >= 0) {
        if (g_portal_return == 0) return;
        const int32_t d = stage_post_load_get_mode_b4d0();
        slot = rec + 0x2c42c + 80 * d + 4 * (floor_layout_floor_count(d) / 5);
    } else if (g_scene1_combat_stage_id != 5) {
        if (fl % 5 != 4) return;
        slot = rec + 0x2c42c + 80 * g_scene1_combat_stage_id + 4 * (fl / 5);
    } else if (fl < 30) {
        if (fl % 10 != 9) return;
        slot = rec + 0x2c5bc + 4 * (fl / 10);
    } else if (g_portal_return != 0) {
        slot = rec + 0x2c5c8;
    } else {
        return;
    }
    int32_t t; memcpy(&t, slot, 4);
    if (t == 0 || t > g_floor_time) memcpy(slot, &g_floor_time, 4);
}

/* the portal Yes/No (be9c 1..3), then the floor change (120: the next floor's
 * load request, or the return pause), the run's end (121+) */
static void stage_portal_prompt(uint8_t *rec)
{
    int32_t v = g_portal_prompt;
    if (v < 118) v = ++g_portal_prompt;
    if (rec[0x2bc82] != 0) {
        /* dungeon 0 floor 4's tutorial portal */
        g_ctrl_lock = 0;
        if (v == 2) scene1_intro_dialogue_start_single(2, 12);   /* FUN_0044ba2c(2, 12, 0) */
        if (g_portal_prompt == 3) {
            g_portal_prompt = 0;
            rec[0x2bc81] = 1;
            rec[0x2bc82] = 0;
            g_ctrl_lock = -1;
            rec[0x2bc80] = 1;
        }
    } else {
        if (v == 2) {
            audio_play_se_by_id(371);
            choice_box_open(g_portal_return ? "Return to town now?" : "Go to the next floor?", 1, 0);
            portal_time_record(rec);
        }
        if (g_portal_prompt > 2) {
            const int r = choice_box_poll(g_sim_buttons[0].pressed, 1);
            g_ctrl_lock = 0;
            if (r == CB_OPT0) {
                g_party.vel[0][0] = 0.0f;
                g_party.vel[0][1] = 0.0f;
                g_party.vel[0][2] = 0.0f;
                if (g_portal_return) {
                    /* the last floor's return (be98 3) or a mid-run one (2) */
                    if (stage_gate_get_next() == floor_layout_floor_count(g_scene1_combat_stage_id) - 1) {
                        g_floor_end_kind = 3;
                        audio_play_se_file("bin/se/00re/shop/re_yata.bin");
                    } else {
                        g_floor_end_kind = 2;
                        audio_play_se_file("bin/se/01ti/action/01ti_btl03.bin");
                    }
                    g_portal_prompt = 0;
                } else {
                    g_portal_prompt = 120;
                    if (stage_gate_get_next() == 98 && rec[0x2bd01] == 0) {
                        scene1_intro_dialogue_start_single(12, 21);
                        rec[0x2bd01] = 1;
                        g_portal_prompt = 119;
                    }
                }
                g_ctrl_lock = -1;
            } else if (r == CB_OPT1) {
                g_portal_prompt = -1;
                choice_box_reset();
                title_save_dialog_cursor_set_visible(0);
                g_ctrl_lock = -1;
            }
        }
    }
    if (g_portal_prompt == 119) {
        g_portal_prompt = 120;
    } else {
        if (g_portal_prompt == 120) {
            if (g_portal_return) {
                pause_dispatch(2);           /* FUN_004536c2 = FUN_00453384(2) */
            } else {
                /* the next floor's stage (FUN_0045bcea on b4cc + 1; b4d4 + 1 stays),
                 * requested with b4cc + 1 (FUN_00452f16) */
                const int32_t fl = stage_gate_get_next();
                const int32_t cur = g_stage_b4dc;
                if (g_dat_0438b4d4 >= 0) ++g_dat_0438b4d4;
                stage_gate_set_next(fl + 1);
                adv_guild_set_stage(rec);    /* FUN_0045bcea */
                stage_gate_set_next(fl);
                const int32_t next = g_stage_b4dc;
                g_stage_b4dc = cur;
                sim_stage_load_request(next, 0, 0, fl + 1);
            }
            choice_box_reset();
            title_save_dialog_cursor_set_visible(0);
        }
        if (g_portal_prompt > 120 && fade_is_done()) {
            /* PORT-DEBT(stub, FUN_00442cef): the run's end (FUN_00442c86,
             * FUN_0048f7f0, FUN_00474d92, the deepest-floor / trips records,
             * FUN_0045e028, the world map or FUN_0048526d) */
        }
    }
}

/* the floor end (be98 > 0): the defeat voice at be94 60, the result pause at 119 */
static void stage_floor_end(uint8_t *rec)
{
    static const char *const k_down_voice[8] = {
        "bin/se/00re/action/00re_btl11.bin", "bin/se/00re/action/00re_btl12.bin",
        "bin/se/00re/action/00re_btl13.bin", "bin/se/00re/action/00re_btl14.bin",
        "bin/se/00re/action/00re_btl16.bin", "bin/se/00re/action/00re_btl15.bin",
        "bin/se/00re/action/00re_btl17.bin", "bin/se/00re/action/00re_btl18.bin",
    };
    g_ctrl_lock = 0;
    g_cut_follow = 0;
    if (g_floor_end_timer == 60) {
        const int32_t c = chara_equip_get_current_chara();
        if (g_floor_end_kind == 1 && c >= 0 && c < 8)
            audio_play_se_file(k_down_voice[c]);
        else {
            ++g_floor_end_timer;
            goto check;
        }
    }
    if (g_floor_end_timer < 118) {
        ++g_floor_end_timer;
    } else if (g_floor_end_kind != 1) {
        g_floor_end_timer = 119;
    } else if (rec[0x2bc88] == 0) {
        rec[0x2bc88] = 1;
        int32_t story; memcpy(&story, rec + 0x2dd64, 4);
        if (story != 2) scene1_intro_dialogue_start_single(2, 13);
    } else if (g_sim_buttons[0].pressed & 0x10) {
        g_floor_end_timer = 119;
    }
check:
    if (g_floor_end_timer == 119) {
        pause_dispatch(2);                   /* FUN_004536c2 */
        uint8_t *cr = rec + 0x2ceb0 + 0x6c * chara_equip_get_current_chara();
        int32_t *cnt = (int32_t *)(void *)(cr + (g_floor_end_kind == 1 ? 0x54 : g_floor_end_kind == 2 ? 0x58 : 0x5c));
        ++*cnt;
    }
}

void scene1_ingame_default_arm_tick(void)
{
    /* E.2 probe — FUN_00442cef @ 0x442cef. */
    CALL_TRACE_ENTER(0x442cefu);
    uint8_t *rec = save_work_bank_at(save_work_active_slot());

    /* DAT_0438b8bc = 0 (the light-pass gate, never set in the port); DAT_06a46fa4 = 0 */
    g_dat_06a46fa4 = 0;
    if (g_portal_cooldown > 0) --g_portal_cooldown;
    if (g_scene1_camera_stage_class == 0 && g_floor_end_kind == 0 && g_dat_0438becc == 0 && g_ctrl_lock == -1)
        stage_portal_scan();
    if (g_dat_0438beb4 > 0) {
        --g_dat_0438beb4;
        /* PORT-DEBT(stub, FUN_0044b38c) */
        goto tail;
    }
    if (g_dat_0438bebc <= 0) {
        if (g_scene1_combat_dat_0438bed8 <= 0) {
            g_sim_buttons[0].pressed |= (uint16_t)g_input_hold;
            g_input_hold = 0;
        } else {
            g_input_hold |= g_sim_buttons[0].pressed;
        }
    }
    if (g_dat_0438bebc > 0) {
        --g_dat_0438bebc;
        /* PORT-DEBT(stub, FUN_0044b635) */
        goto tail;
    }
    if (g_news_ticker_timer != 0 && mapcol_maptype() != 0) {
        /* the floor event's ticker: the player waits (FUN_0048b844 = the status
         * tick only); SE 701 at 30; A/B/C fast-forwards (b950 = 2, +7/tick).
         * The ticker's end (b92c = 0) is decided by the HUD render FUN_0040a765
         * from the text width — PORT-DEBT(render-coupled, FUN_0040a765). */
        if (g_ctrl_lock == -1 && g_news_ticker_timer > 0) {
            int32_t v = g_news_ticker_timer;
            if (g_dat_0438b950 == 2) v += 7;
            ++v;
            g_news_ticker_timer = v;
            if (v > 30 && (g_sim_buttons[0].pressed & 0x70)) g_dat_0438b950 = 2;
            if (v == 30 && !stage_gate_floor_is_checkpoint() && g_floor_event_n > 0)
                audio_play_se_by_id(701);
        }
        party_status_tick();                 /* FUN_0048b844 */
        goto tail;
    }
    if (g_scene1_combat_dat_0438bed8 > 0) {
        --g_scene1_combat_dat_0438bed8;
        g_dat_06a46fa4 = 1;
        goto tail;
    }
    if (g_equip_prompt > 0 && rec != NULL && (rec[0x2bce2] == 0 || rec[0x2bcf8] != 0)) {
        stage_equip_prompt();
        goto tail;
    }
    if (g_portal_prompt > 0 && rec != NULL) {
        stage_portal_prompt(rec);
        goto tail;
    }
    if (g_dat_0438beb0 > 0) --g_dat_0438beb0;
    if (g_floor_intro == 0) ++g_floor_frames;
    if (g_floor_end_kind == 0) ++g_floor_time;
    if (g_floor_end_kind > 0 && rec != NULL) stage_floor_end(rec);
    {
        int32_t v = g_dat_0438becc;
        if (g_dat_0438becc > 0) v = --g_dat_0438becc;
        if (v % 3 != 0)
            return;                          /* the off-frames end here, no tail */
    }
    if (g_floor_intro > 0) {
        --g_floor_intro;
        g_ctrl_lock = 0;
        if (g_floor_intro == 1) {
            if (rec != NULL && rec[0x2bc83] == 0) {
                rec[0x2bc83] = 1;
                int32_t story; memcpy(&story, rec + 0x2dd64, 4);
                if (story != 2)
                    scene1_intro_dialogue_start_single(2, 10);   /* FUN_0044ba2c(2, 10, 0) */
            }
        } else if (g_floor_intro == 0) {
            g_equip_prompt = 1;
        }
    }
    if (g_floor_end_timer < 120) {
        dungeon_fall_tick();                 /* FUN_0044bb1a */
        if (dungeon_flow_is_dungeon())
            dungeon_player_tick();           /* FUN_0048b3f6 */
        else
            house_master_tick();             /* FUN_0048670f */
        if (g_floor_intro == 0) {
            /* PORT-DEBT(stub, FUN_00441aab): the "?" spots / item radar */
            actor_pass();                    /* FUN_00430c6d */
            scene1_records_b_tick();         /* FUN_0043ae20 */
            /* PORT-DEBT(stub, FUN_0042ea35) */
            dobj_tick();                     /* FUN_0043a5d9 */
        }
    }
    if (g_scene1_combat_dat_0438b908 > 0) --g_scene1_combat_dat_0438b908;
    table_c_tick();                          /* FUN_0044284b */
    for (int k = 0; k < 32; k++)
        if (g_popup[k][3] > 0) --g_popup[k][3];
    g_dat_0064e818 = 1;
tail:
    /* FUN_004427f1 (the house's story cascade / the dungeon's events) */
    if ((stage_post_load_get_mode_b4d0() >= 0 && g_dat_0438b4d4 == 0) ||
        (g_scene1_combat_stage_id == 5 && stage_gate_get_next() == 30)) {
        if (g_equip_prompt <= 1) stage_event_tick();
    } else if (g_scene1_combat_dat_0438bed8 == 0) {
        stage_event_tick();
    }
    scene1_particles_tick();                 /* FUN_0040fb3a */
    /* PORT-DEBT(stub, FUN_004426a7): the cinematic camera ramp */
    scene1_debug_overlay_consume_rng();      /* the overlay rand + X/Y/Z text */
}

/* FUN_004427f1 */
static void stage_event_tick(void)
{
    if (dungeon_flow_is_dungeon())
        dungeon_event_tick();
    else
        scene1_tutorial_dispatch_tick();     /* FUN_0044bd0d */
}

/* §95 dev-overlay LCG step (442cef.c L421 / ret 0x443606), UNCONDITIONAL: the
 * engine consumes exactly one raw LCG step at the FUN_00442cef tail every render
 * frame, idle or moving (the call sits at LAB_004435f7, past every gate).
 * Extracted so it is unit-testable in isolation. */
void scene1_debug_overlay_consume_rng(void)
{
    (void)rng_next15();
}

void scene1_ingame_tick(void)
{
    /* RE §21.28 cc08 boundary markers live for exactly ONE frame — clear at
     * the frame top, before any arm can set or read them (the player tick
     * can't own this: it does not run on event frames). */
    player_ctrl_cc08_markers_frame_clear();

    /* Engine FUN_004536cb L50555-50568 — state-1 sub-dispatch:
     *
     *   if (transition_flag != 0)  → transition arm
     *   else if (skip_flag != 0)   → skip (no sim call)
     *   else if (paused_flag == 0) → default-running arm
     *   else                       → paused arm (= transition arm body)
     *
     * paused_flag is DAT_0438b1c8, the dialogue/event "busy" gate: retail sets
     * it 2 when FUN_0044bd0d arms a dialogue, 1 through the load worker, and
     * clears it when the script ends — so EVERY dialogue frame runs the
     * event/paused arm, never FUN_00442cef (verified on the item-display-2
     * retail call-trace: zero 0x442cef/0x48670f rows during both tutorial
     * dialogues, 0x4427d3 once per frame).  The port's live writer of that
     * lifecycle is the dialogue runtime — read its busy() alongside the
     * explicit flag (same engine global, see the definitions above). */
    if (g_scene1_ingame_transition_flag != 0) {
        scene1_ingame_transition_arm_tick();
    } else if (g_scene1_ingame_skip_flag != 0) {
        /* skip — no sim call */
    } else if (g_scene1_ingame_paused_flag == 0 &&
               !scene1_intro_dialogue_busy()) {
        scene1_ingame_default_arm_tick();
    } else {
        scene1_ingame_transition_arm_tick();
    }
}
