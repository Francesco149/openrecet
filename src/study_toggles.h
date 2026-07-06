/*
 * study_toggles.h — six runtime kill-switches for the HOUSE lighting tricks.
 *
 * NOT engine code — a study/recording tool layered on the port (the
 * light_debug.c pattern).  For filming the @GemmaExplains Recettear
 * shop-lighting video: each toggle disables ONE of the six visual tricks
 * the retail engine stacks on the scene-1 3D room pass, so the owner can
 * film the actual game with any combination off.  All six default ON
 * (= retail behaviour) and every hook site is a read of a default-1 int,
 * so traces/TAS/parity are bit-unaffected until a toggle is flipped.
 *
 * Hotkeys (main.c WM_KEYDOWN, repeat-guarded): SHIFT+1..SHIFT+6.
 * Digits 1..6 are NOT in the engine's bindable DIK table (input.c
 * input_dik_table has no 0x02..0x07) so they can never collide with
 * gameplay input; SHIFT is bindable but unbound in the vendor defaults.
 * Each press logs the new state to stdout — NO on-screen indicator
 * (overlays would ruin the footage).
 * CLI: `--study-off mod2x,hikari,...` pre-flips toggles at boot
 * (headless verification + hands-free filming setup).
 *
 * The six tricks + where the hook lives:
 *   SHIFT+1 mod2x    — ×2 room brightness: palette drawcode 2 →
 *                      COLOROP=MODULATE2X on the room/map+furniture draws
 *                      (scene1_render.c palette-combiner sites; off →
 *                      demoted to MODULATE).  Scope = the scene-1 room
 *                      pass only (base + pre + alpha_pre, all drawcode/
 *                      +0x1a40-driven); UI/title MODULATE2X and the
 *                      hikaridrawcode (+0x1a54) pass are untouched.
 *   SHIFT+2 keylight — the single directional key light: maplight
 *                      DIFFUSE zeroed (scene1_maplight.c).  Component-
 *                      zeroed rather than LightEnable(0,FALSE) because
 *                      the ambient fill lives in the SAME light 0 —
 *                      a full disable would kill both tricks at once.
 *   SHIFT+3 ambient  — the ambient fill: maplight AMBIENT zeroed
 *                      (scene1_maplight.c).  NB the render state
 *                      D3DRS_AMBIENT is already black (0xff000000)
 *                      through the scene-1 pass (scene1_render.c L188-
 *                      L198); the visible fill is light 0's Ambient.
 *   SHIFT+4 fog      — the (near-invisible) scene fog: forces the
 *                      palette fog gate off (scene1_apply_fog_state).
 *   SHIFT+5 hikari   — the five hand-modeled god-ray planes of
 *                      shop_1st.x: the pass-3 draws draw_loop_b_mesh
 *                      emits (scene1_walker_pass_init.c) are skipped
 *                      entirely — the same draws light_debug wraps.
 *   SHIFT+6 blob     — character blob shadows: Block A + the bg-NPC /
 *                      customer shadow blobs (scene1_chr_shadow.c)
 *                      skipped; the display-cell glow (Block G, not a
 *                      shadow) still draws.
 */
#ifndef STUDY_TOGGLES_H
#define STUDY_TOGGLES_H

enum {
    STUDY_T_MOD2X = 0,
    STUDY_T_KEYLIGHT,
    STUDY_T_AMBIENT,
    STUDY_T_FOG,
    STUDY_T_HIKARI,
    STUDY_T_BLOB,
    STUDY_T_COUNT
};

/* Seeded all-1 (retail).  Read directly via study_toggle_on at the draw
 * sites — a single int load, nothing else on the hot path. */
extern int g_study_toggles[STUDY_T_COUNT];

static inline int study_toggle_on(int t) { return g_study_toggles[t]; }

/* Flip one toggle + log the new state to stdout (hotkey arm). */
void study_toggle_flip(int t);

/* Parse a comma list of trick names ("mod2x,hikari") and pre-flip them
 * OFF (--study-off).  Unknown names log a warning and are skipped. */
void study_toggles_parse_off_list(const char *list);

/* mod2x helper for the room-pass palette-combiner sites: the palette
 * mode int maps (mode%7)==2 → D3DTOP_MODULATE2X (scene1_apply_palette_
 * combiner_mode's table).  With mod2x off, demote such a mode to 1
 * (→ D3DTOP_MODULATE); every other mode passes through untouched. */
static inline int study_room_combiner_mode(int mode)
{
    int m = mode % 7;
    if (m < 0) m += 7;
    return (g_study_toggles[STUDY_T_MOD2X] || m != 2) ? mode : 1;
}

#endif /* STUDY_TOGGLES_H */
