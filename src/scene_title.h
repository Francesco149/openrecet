/*
 * scene_title.h — title-screen scene module.
 *
 * Today: just the texture loader (FUN_004733d5). Holds 7 sprite_t
 * slots — 4 title-specific BG/menu textures + 3 cross-scene ones
 * (pause/result/dungeon) that the engine batches into the same
 * load call.
 *
 * Later commits add: menu items init (FUN_0049a324 + FUN_0049a43d)
 * and the bare-path render (FUN_0049c644).
 *
 * Two-layer split, same as render_quad.{c,h}: a pure-C constant
 * asset table + accessors at the top; D3D-touching loader under
 * `#ifdef _WIN32` at the bottom.
 */
#ifndef OPENRECET_SCENE_TITLE_H
#define OPENRECET_SCENE_TITLE_H

#include <stdint.h>

/* Slot indices for the texture array (same order as FUN_004733d5). */
enum {
    SCENE_TITLE_TEX_BG2       = 0,  /* bmp/title_bg2.bmp     1024x1024 */
    SCENE_TITLE_TEX_01        = 1,  /* bmp/title01.tga        512x256  */
    SCENE_TITLE_TEX_FUKI      = 2,  /* bmp/title_fuki.tga     512x1024 */
    SCENE_TITLE_TEX_WAKU      = 3,  /* bmp/title_waku.tga    1024x512  */
    SCENE_TITLE_TEX_PAUSE     = 4,  /* bmp/pause.tga         1024x512  */
    SCENE_TITLE_TEX_RESULT    = 5,  /* bmp/result_bord01.tga  512x256  */
    SCENE_TITLE_TEX_DUNGEON   = 6,  /* bmp/dungeonbord.tga   1024x512  */
    SCENE_TITLE_TEX_COUNT     = 7,
};

/* One entry of the asset table at PE 0x005c8688..0x005c86fc, paired
 * with the (expected_w, expected_h) arguments FUN_0047193c was
 * called with. Used by the loader; exposed for tests. */
typedef struct {
    const char *path;
    uint32_t    expected_w;
    uint32_t    expected_h;
} scene_title_asset_t;

extern const scene_title_asset_t scene_title_assets[SCENE_TITLE_TEX_COUNT];

/* ─── animation state ────────────────────────────────────────────────── */

/* Mirrors the engine's title-scene counters at DAT_096435.. region.
 *
 * `frame_counter`, `cursor_pos`, `select_phase`, `pulse_phase` are read
 * by the render. `cursor_anim` drives the menu slide-in tween;
 * `menu_folding_out` is the direction flag — both internal to the sim
 * but kept here so the struct stays the single carrier of title state. */
typedef struct {
    uint32_t frame_counter;     /* DAT_09643518 — increments per sim tick */
    uint32_t cursor_pos;        /* DAT_09643540 — current selected menu index */
    uint32_t cursor_anim;       /* DAT_09643520 — 0..10 menu-fold-in tween */
    uint32_t select_phase;      /* DAT_09643544 — fast pulse after A-press */
    uint32_t pulse_phase;       /* DAT_0964352c — slow steady-state pulse */
    int      menu_folding_out;  /* DAT_09643528 — 1: cursor_anim ↓ (main menu
                                 *                visible); 0: cursor_anim ↑
                                 *                (main menu sliding off for
                                 *                a submenu). FUN_0049a3a3
                                 *                ("bootstrap done") seeds 1. */
} scene_title_anim_t;

/* Initialize `out` to the state FUN_0049a3a3 leaves the engine in at
 * end-of-bootstrap: every counter zero except `menu_folding_out = 1`. */
void scene_title_anim_init_fresh(scene_title_anim_t *out);

/* ─── menu init (FUN_0049a324 + FUN_0049a43d) ────────────────────────── */

/* Engine menu-item codes. The same byte ends up at &DAT_09643358 + N
 * and gets fanned out by FUN_0049a59e (sim) and FUN_0049c644 (render).
 *
 * The engine's vocabulary is wider than what a fresh boot ever picks:
 *   - 0 / 4 / 5 are three flavours of "New Game" / "Continue", picked
 *     based on save-bank state (see FUN_0049a324's bitmask).
 *   - 1 is the in-progress-save quick-start (added when any bank's
 *     score > 0).
 *   - 6 is Survival mode, gated on the "adventure 2 cleared" save bit.
 *   - 7 is the Ranking screen.
 *   - 8 is the hidden-character entry, gated on DAT_056e5788. */
enum {
    SCENE_TITLE_MENU_NEW_GAME      = 0,
    SCENE_TITLE_MENU_CONTINUE_ANY  = 1,
    SCENE_TITLE_MENU_OPTIONS       = 2,
    SCENE_TITLE_MENU_EXIT          = 3,
    SCENE_TITLE_MENU_NEW_HAS_SAVE  = 4,
    SCENE_TITLE_MENU_CONT_HAS_SAVE = 5,
    SCENE_TITLE_MENU_SURVIVAL      = 6,
    SCENE_TITLE_MENU_RANKING       = 7,
    SCENE_TITLE_MENU_HIDDEN_CHAR   = 8,
};

/* Inputs to `scene_title_menu_init`. Mirrors the two reads that
 * FUN_0049a324 and FUN_0049a43d make against the save banks. */
typedef struct {
    int has_any_adv_cleared;     /* FUN_0049a324: any bank[0x244c] == 3
                                  *               (cleared Adventure 2) */
    int has_any_adv8_cleared;    /* FUN_0049a324: any bank entry has
                                  *               (cleared >> 6) ∈ [0xd49..0xd50] */
    int has_any_score;           /* FUN_0049a43d: any bank's score > 0 */
    int hidden_char_unlocked;    /* DAT_056e5788 */
} scene_title_save_t;

/* Output of `scene_title_menu_init`. Slot count is bounded by the
 * engine — at most 8 entries are ever written. */
#define SCENE_TITLE_MENU_MAX  8

typedef struct {
    int   items[SCENE_TITLE_MENU_MAX];  /* DAT_09643358[N] menu codes */
    int   count;                        /* DAT_09643510 */
    int   default_cursor;               /* DAT_09643540 — starting cursor pos */
    float y_stride;                     /* DAT_005d1bb4 — pixel y-stride per row */
    float y_origin;                     /* DAT_005d1bb8 — y-origin offset */
} scene_title_menu_t;

/* Pure-C menu builder. Mirrors FUN_0049a43d (consuming the bitmask
 * that FUN_0049a324 produces). Deterministic; no globals. */
void scene_title_menu_init(const scene_title_save_t *save,
                           scene_title_menu_t *out);

/* Default-save query — all-zero, equivalent to a fresh boot with no
 * save files loaded. Convenience wrapper around the above. */
void scene_title_menu_init_fresh(scene_title_menu_t *out);

/* ─── sim (FUN_0049a59e — bare path) ────────────────────────────────── */

/* Pure-C title sim. Mirrors the bare path of FUN_0049a59e — the path
 * reached at end of `FUN_0049a3a3` ("bootstrap done", scene state 0,
 * no scene transitions pending). Inputs:
 *
 *   pressed: this-frame rising-edge button mask (the engine's
 *            DAT_073dddd4). Bit 0x10 = A.
 *   held:    held-with-auto-repeat mask (DAT_073dddd6). Bits 0x04 = UP,
 *            0x08 = DOWN, used to step the menu cursor.
 *
 * What runs:
 *   - `cursor_anim` is decremented when `menu_folding_out == 1`
 *     (clamps at 0) and incremented when 0 (clamps at 10).
 *   - When `cursor_anim == 0`: `frame_counter++`; while
 *     `frame_counter < 0x1bc6` (7110), A starts the select pulse
 *     (`select_phase = 1`), UP/DOWN wrap the cursor mod menu->count.
 *   - When `select_phase > 0`: it increments to 0xf and snaps back
 *     to 0 (engine dispatches a scene transition there, which we
 *     stub — no scene yet exists to receive control).
 *   - Tail: `pulse_phase++`.
 *
 * Side-effect callouts the engine makes here (sound effects 0x143/
 * 0x146 via FUN_00499519, intro-movie attempt at frame == 0x1be4)
 * are *not* invoked; they wait for the audio + video subsystems. */
void scene_title_sim(scene_title_anim_t *anim,
                     const scene_title_menu_t *menu,
                     uint16_t pressed,
                     uint16_t held);

/* ─── module globals (engine memory mapping) ─────────────────────────── */

/* Module-level state — the engine carries one of each at globals
 * DAT_09643358.. (menu) and DAT_09643518.. (anim). Exposed so sim.c
 * and the Win32 render dispatcher can both reach them without
 * threading pointers through every function. */
extern scene_title_menu_t  g_scene_title_menu;
extern scene_title_anim_t  g_scene_title_anim;

/* Sim entry that uses the module globals + the sim's button ring
 * (g_sim_buttons[0].pressed / .held). The dispatcher in sim_step_a
 * calls this when scene state == 0. */
void scene_title_sim_default(void);

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "sprite.h"

/* Set to non-zero by `scene_title_load_assets` on full success — the
 * render dispatcher gates `scene_title_render_default` on it. */
extern int g_scene_title_assets_loaded;

/* Load the 7 title-scene textures via sprite_load (FUN_0047193c).
 * Returns the number of slots that loaded successfully (== 7 on
 * full success). On partial failure the unloaded slots are left
 * zeroed; render code must NULL-guard before drawing. */
int scene_title_load_assets(IDirect3DDevice8 *dev);

/* Read access to one loaded texture by slot index. Returns a
 * pointer to the static slot — never NULL — but slot.tex may be
 * NULL if the load failed or it hasn't run yet. */
const sprite_t *scene_title_get(int slot);

/* Free all 7 textures + zero the slots. Idempotent. */
void scene_title_unload_assets(void);

/* Bare-title render — FUN_0049c644's BG + menu path. Skips the
 * sub-menu, sub-screen, and fade-in branches (all gated on engine
 * counters that stay at BSS-zero until the sim port lands).
 *
 * Caller is responsible for BeginScene/EndScene and for clearing
 * the back buffer. This function only emits quads.
 *
 * Requires `scene_title_load_assets` to have been called once and
 * `render_quad_init` (for the static vbuf) to have been called at
 * D3D startup. Both invariants are checked at runtime — the func
 * is a no-op if either is missing.
 */
void scene_title_render(IDirect3DDevice8 *dev,
                        const scene_title_menu_t *menu,
                        const scene_title_anim_t *anim);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_TITLE_H */
