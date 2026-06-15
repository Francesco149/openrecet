/*
 * scene_pause.h — engine pause + adventurer-status asset loader
 *                  (FUN_00473a3e @ 0x473a3e, 453 bytes) + the unnamed
 *                  pause-state FPU init at 0x435873 (86 bytes; Ghidra
 *                  missed it so the engine source notes call it "unnamed").
 *
 * Inner body for the C4E secondary worker thread (LAB_00452c4e), paired
 * with the FUN_00452e75 spawner. The thread proc calls TWO functions
 * before falling into the shared cleanup tail (objdump @ 0x452c4e..c53):
 *
 *     call 0x435873         ; pause-state FPU init (10 writes)
 *     call 0x473a3e         ; 20-asset BMP/TGA load
 *
 * Both are ported as one module. The body callback registered via
 * `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, ...)` does the
 * init first, then the asset load.
 *
 * ─── FPU init (engine unnamed @ 0x435873) ──────────────────────────────
 *
 *   Disassembled bytes (`objdump -d --start-address=0x435873`):
 *
 *     flds   [0x519474]            ; load 32.0f
 *     xor    eax, eax
 *     mov    [0x438b150], 1        ; DAT_0438b150 = 1   (int)
 *     fstps  [0x438ac00]           ; DAT_0438ac00 = 32.0f
 *     flds   [0x519440]            ; load 80.0f
 *     mov    [0x438ac20], eax      ; DAT_0438ac20 = 0   (int)
 *     mov    [0x438ac18], eax      ; DAT_0438ac18 = 0   (int)
 *     fstps  [0x438ac04]           ; DAT_0438ac04 = 80.0f
 *     flds   [0x519474]            ; load 32.0f
 *     mov    [0x438b158], eax      ; DAT_0438b158 = 0   (int)
 *     mov    [0x438b15c], eax      ; DAT_0438b15c = 0   (int)
 *     fstps  [0x438abf4]           ; DAT_0438abf4 = 32.0f
 *     flds   [0x519440]            ; load 80.0f
 *     mov    [0x438ac1c], eax      ; DAT_0438ac1c = 0   (int)
 *     fstps  [0x438abf8]           ; DAT_0438abf8 = 80.0f
 *     ret
 *
 *   Float constants extracted from .rdata via tools/analyze/pe.py:
 *     0x00519474: 00 00 00 42  → 32.0f
 *     0x00519440: 00 00 a0 42  → 80.0f
 *
 *   Net effect: 6 floats + 4 zero ints + 1 one int written. The (32,80)
 *   pairs look like (x,y) origins or (w,h) dimensions for pause-menu
 *   layout — no consumer is ported yet so the exact semantics will
 *   surface when the pause UI renderer lands.
 *
 * ─── asset load (engine FUN_00473a3e @ 0x473a3e) ──────────────────────
 *
 *   20 fixed sprite_load calls. Slot 0 has a selector:
 *     `bmp/pause_endless.tga` if  ((&DAT_045114fc)[stage_idx*0xb7f2] == 2
 *                              ||  (&DAT_045114fc)[stage_idx*0xb7f2] == 3)
 *     `bmp/pause.tga`         otherwise
 *
 *   The selector reads a 4-byte int at offset 0x14fc of the current
 *   stage's 0x2dfc8-byte stage-state record (engine's `int *` indexing
 *   makes 0xb7f2 elements = 0x2dfc8 bytes; same stage stride as the
 *   walls/floor/jutan selectors). Stage state isn't ported yet so
 *   `g_scene_pause_selector` is a standalone int32 (BSS-zero default;
 *   default load: "bmp/pause.tga").
 *
 *   The 20 slots, dest BSS region, dims:
 *     0  pause / pause_endless  DAT_073d86a8  0x400×0x200
 *     1  pause_bg_rete          DAT_073d86b8  0x400×0x200
 *     2  result_bord01          DAT_073d86c8  0x200×0x100
 *     3  dungeonbord            DAT_073a9b08  0x400×0x200
 *     4  sousa_lui              DAT_073d84d0  0x400×0x200  ┐
 *     5  sousa_sya              DAT_073d84e0  0x400×0x200  │ contiguous
 *     6  sousa_cai              DAT_073d84f0  0x400×0x200  │ stride 0x10:
 *     7  sousa_tel              DAT_073d8500  0x400×0x200  │   sprite_t[8]
 *     8  sousa_era              DAT_073d8510  0x400×0x200  │ cursor
 *     9  sousa_nag              DAT_073d8520  0x400×0x200  │ portraits
 *    10  sousa_grf              DAT_073d8530  0x400×0x200  │
 *    11  sousa_arm              DAT_073d8540  0x400×0x200  ┘
 *    12  st_ryui                DAT_073d8570  0x200×0x200  ┐
 *    13  st_sya                 DAT_073d8580  0x200×0x200  │ contiguous
 *    14  st_caillou             DAT_073d8590  0x200×0x200  │ stride 0x10:
 *    15  st_tiers               DAT_073d85a0  0x200×0x200  │   sprite_t[8]
 *    16  st_eran                DAT_073d85b0  0x200×0x200  │ status
 *    17  st_nagi                DAT_073d85c0  0x200×0x200  │ portraits
 *    18  st_griffe              DAT_073d85d0  0x200×0x200  │
 *    19  st_aruma               DAT_073d85e0  0x200×0x200  ┘
 *
 *   Engine sprite_load format flag is 0xc (vs 3 for walls/floor/jutan
 *   and 0x10 for buy-phase sprites). openrecet's sprite_load drops the
 *   format flag — same as all other call sites.
 *
 * Worker_load wiring:
 *
 *   `scene_pause_init(dev)` caches the D3D device and registers
 *   `scene_pause_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, …)`. The body
 *   calls `scene_pause_state_init()` first, then `scene_pause_load_with(
 *   …)` — matching LAB_00452c4e's two-call sequence.
 */

#ifndef OPENRECET_SCENE_PAUSE_H
#define OPENRECET_SCENE_PAUSE_H

#include <stdint.h>

#define SCENE_PAUSE_SOUSA_COUNT   8
#define SCENE_PAUSE_STATUS_COUNT  8

/* Total dispatch slots: 4 singletons (pause, pause_bg_rete,
 * result_bord01, dungeonbord) + 8 sousa + 8 status = 20. */
#define SCENE_PAUSE_LOAD_COUNT    20

/* Per-stage pause-variant selector. Engine: 4-byte int at offset 0x14fc
 * of the current stage's 0x2dfc8-byte stage-state record. Values 2 or
 * 3 → "bmp/pause_endless.tga"; everything else → "bmp/pause.tga". Zero
 * by default (BSS) until the stage system writes it. */
extern int32_t g_scene_pause_selector;

/* Pause-state globals written by the FPU init (engine unnamed @
 * 0x435873). Exposed for tests + future pause-UI consumers. Zero-init
 * via BSS until scene_pause_state_init() runs. */
extern int32_t g_scene_pause_state_b150;  /* DAT_0438b150, set to 1 */
extern int32_t g_scene_pause_state_b158;  /* DAT_0438b158, zero */
extern int32_t g_scene_pause_state_b15c;  /* DAT_0438b15c, zero */
extern int32_t g_scene_pause_state_ac18;  /* DAT_0438ac18, zero */
extern int32_t g_scene_pause_state_ac1c;  /* DAT_0438ac1c, zero */
extern int32_t g_scene_pause_state_ac20;  /* DAT_0438ac20, zero */
extern float   g_scene_pause_state_abf4;  /* DAT_0438abf4, 32.0f */
extern float   g_scene_pause_state_abf8;  /* DAT_0438abf8, 80.0f */
extern float   g_scene_pause_state_ac00;  /* DAT_0438ac00, 32.0f */
extern float   g_scene_pause_state_ac04;  /* DAT_0438ac04, 80.0f */

/* Port of unnamed FUN @ 0x435873 — writes 10 pause-state constants
 * (6 floats + 4 zeroes + 1 one) in the engine's exact write order.
 * Idempotent; safe to call multiple times. */
void scene_pause_state_init(void);

/* Optional injected loader for tests. Receives the asset path, slot
 * index, and expected dims (engine's sprite_load `expected_w` /
 * `expected_h`). Return value is ignored — tests use it to record
 * dispatches. */
typedef int (*scene_pause_load_fn)(const char *path, int slot,
                                    int w, int h, void *userdata);

/* Pure-C body — engine FUN_00473a3e end-to-end. Resolves slot 0's
 * filename via `g_scene_pause_selector`, dispatches `load_fn` for all
 * 20 slots in slot order. Returns the number of dispatches (always
 * 20, unconditionally — there's no per-slot predicate). NULL `load_fn`
 * is a counting-only dry run. */
int  scene_pause_load_with(scene_pause_load_fn load_fn, void *userdata);

/* Inspection helpers — exposed for tests.
 *
 * For slot 0 (pause variant), returns "bmp/pause_endless.tga" if
 * `g_scene_pause_selector` is 2 or 3, else "bmp/pause.tga". Other
 * slots return their fixed asset name. NULL if out of range. */
const char *scene_pause_filename(int slot);

/* Per-slot expected dimensions. Returns 1 on success (w/h written),
 * 0 if `slot` is out of range. */
int  scene_pause_slot_dims(int slot, int *out_w, int *out_h);

/* Reset module state — clears the selector + pause-state globals (FPU
 * init values reset to zero) and (on Win32) zeroes the destination
 * sprite_t handles. Tests only. */
void scene_pause_reset(void);

/* ─── pause state machine (engine mode 9) ──────────────────────────────
 *
 * The in-game ESC pause: FUN_00453384 (trigger) + FUN_0047f2f6 (menu
 * build) + FUN_0047fa76 (update) + FUN_00480614 (nav). Pure C — the
 * render half (FUN_004820ba) is Win32. See docs/plans/pause-menu.md.
 *
 * Lifecycle: ESC → pause_dispatch(0) starts the ramp (g_sim_counter_998,
 * dir g_sim_mode_9a0=opening); at ramp==3 the integration layer flips
 * g_scene_state=9, calls pause_menu_setup, and spawns the primary worker
 * (case-9 → the 20-asset pause load); the mode-9 update runs
 * pause_menu_update → pause_menu_nav. */

/* DAT_06a4997c — the pause action: 0 = ESC menu, 1/2 = other entries
 * (PORT-DEBT). Latched by pause_dispatch. */
extern int32_t g_pause_action;

/* DAT_06a499a8 — the scene mode to restore on unpause (saved by the
 * enter-pause path from g_scene_state). */
extern int32_t g_pause_saved_mode;

/* The menu entry-type list + cursor (engine DAT_074b2844[] / 073e154c /
 * 074b2878). Entry types: 0=Status 1=Items 2=Options 3=Save 4=Exit
 * 5=dungeon-only 6=Encyclopedia. */
#define SCENE_PAUSE_MAX_ENTRIES 8
extern int32_t g_pause_entries[SCENE_PAUSE_MAX_ENTRIES]; /* DAT_074b2844 */
extern int32_t g_pause_count;        /* DAT_073e154c — live entry count   */
extern int32_t g_pause_sel;          /* DAT_074b2878 — selected index      */
extern int32_t g_pause_sel_anim;     /* DAT_074b2870 — select-press anim    */
extern int32_t g_pause_sub_anim;     /* DAT_074b2880 — submenu open anim 0..10 */
extern int32_t g_pause_sub_dir;      /* DAT_074b2884 — 1 opening / 0 closing */
extern int32_t g_pause_row_spacing;  /* DAT_005cc678 — derived row pitch    */
extern int32_t g_pause_exit_confirm; /* DAT_074b2830 — return-to-title flow */
extern int32_t g_pause_exit_phase;   /* DAT_073e1550 — the quit-to-title counter */
extern int32_t g_pause_frame;        /* _DAT_074b2874 — pause frame counter */

/* Save submenu (type 3) picker state — seeded by the sel_anim==0xf commit,
 * read by the render wrapper FUN_004812e4. `cur` is always 0 for Save so the
 * engine's 4-wide val[]/val2[] collapse to scalars. */
extern int32_t g_pause_save_cursor;  /* DAT_074b2834 (val[0])  — selected slot */
extern int32_t g_pause_save_scroll;  /* DAT_074b2820 (val2[0]) — window top    */
extern int32_t g_pause_save_vscroll; /* DAT_074b2898 (c898)    — column slide  */
extern int32_t g_pause_save_hscroll; /* DAT_074b2894 (c894)    — row slide     */
extern int32_t g_pause_save_phase;   /* DAT_074b289c (c89c)    — save anim     */
extern int32_t g_pause_save_overwrite; /* DAT_074b28a4 — "Overwriting file." up */

/* Options submenu (type 2) state. */
extern int32_t g_pause_options_row;    /* DAT_074b2834[0] — config cursor row 0..4 */
extern int32_t g_pause_options_phase;  /* DAT_074b2890 — 0 clean/1 dirty/2 save/3 nosave */

/* Menu-build inputs (engine DAT_0741bed8 = adventurer/party count and
 * *DAT_068dd2f0 = stage type, 0=HOUSE). The integration layer captures
 * these from the live game state just before pause_menu_setup; tests set
 * them directly. Default 0 (the early-tutorial house). */
void pause_set_menu_inputs(int status_count, int stage_type);

/* FUN_00453384 — the ESC trigger / pause toggle. action 0 = ESC menu.
 * Starts the open ramp on a pausable resting scene; toggles off (begins
 * the unpause) when already fully paused. */
void pause_dispatch(int action);

/* FUN_0047f2f6 — build the menu entry-type list from the captured
 * inputs + g_pause_saved_mode, reset the cursor/anim, run the FPU
 * layout init. Called at ramp==3 (mode just flipped to 9). */
void pause_menu_setup(void);

/* FUN_0047fa76 — the mode-9 per-frame update. Runs the nav when no
 * submenu is open; ticks the submenu open/close anim otherwise (the
 * submenu updaters + the exit-confirm flow are PORT-DEBT). */
void pause_menu_update(void);

/* FUN_00480614 — the menu nav: U/D wrap the cursor, B closes (re-enters
 * pause_dispatch to unpause), A starts the select anim (the commit →
 * submenu/exit is PORT-DEBT). Reads g_sim_buttons[0]. */
void pause_menu_nav(void);

/* FUN_0047f5bc (resting/nav path) — the Save submenu slot-picker nav,
 * dispatched by pause_menu_update at sub_anim==10 when Save (type 3) is
 * selected. U/D ±1 + L/R ±3 over the 100-slot list with the c894/c898
 * slide anims; B cancels. The A-confirm + the commit (FUN_004905a8) are
 * M4c PORT-DEBT(save-picker-commit). Reads g_sim_buttons[0]; mutates the
 * g_pause_save_* picker state. */
void pause_save_submenu_update(void);

/* FUN_0047fc44 — the Options (type 2) config-slider nav (sub_anim==10, Options
 * selected). U/D move the cursor row, L/R adjust the row's value (Music/Sound/
 * Voice volumes + Message Speed + Unread Text Skip), A/B exit (saving if dirty).
 * Reads g_sim_buttons[0]; mutates g_pause_options_row/phase + the config model. */
void pause_options_submenu_update(void);

/* FUN_0047ff40 — the Items (type 1) inventory-grid nav (sub_anim==10, Items
 * selected). HOUSE path: one frame of display_menu_update + B-close; the place /
 * use-item paths are dungeon-only PORT-DEBT(pause-items-dungeon). Reads
 * g_sim_buttons[0] (via display_menu_update); mutates the display-menu cursor +
 * the submenu close state. */
void pause_items_submenu_update(void);

/* Reset the state machine globals (tests + pause_menu_setup share it). */
void pause_sm_reset(void);

/* 1 when the Save submenu (type 3) is fully open + navigable — scene mode 9,
 * sub_anim==10, Save selected (the picker nav pause_save_submenu_update runs).
 * The anchor layer uses this for SAVE_PICKER_READY (re-syncing the nav past the
 * per-side-variable pause-open phase). Takes the scene mode (engine
 * DAT_0438b1c0) so the host build needn't link the Win32 scene global. */
int pause_save_picker_navigable(int scene_mode);

/* Encyclopedia submenu (type 6) open + navigable — the ENCYCLOPEDIA_READY
 * anchor predicate (scene 9, sub_anim==10, Encyclopedia selected). */
int pause_encyclopedia_navigable(int scene_mode);

/* Options submenu (type 2) open + navigable — the OPTIONS_READY anchor
 * predicate (scene 9, sub_anim==10, Options selected). */
int pause_options_navigable(int scene_mode);

/* Items submenu (type 1) open + navigable — the ITEMS_READY anchor predicate
 * (scene 9, sub_anim==10, Items selected). */
int pause_items_navigable(int scene_mode);

#ifdef _WIN32

#include "sprite.h"

/* Destination sprite slots, named to match the engine's BSS layout.
 * The "sousa" and "status" arrays are contiguous engine-side (stride
 * 0x10 = sizeof openrecet's sprite_t + 4 trailing engine bytes); the
 * four singletons live in distinct named BSS spots. */
extern sprite_t g_scene_pause_pause;          /* DAT_073d86a8 */
extern sprite_t g_scene_pause_bg_rete;        /* DAT_073d86b8 */
extern sprite_t g_scene_pause_result_bord01;  /* DAT_073d86c8 */
extern sprite_t g_scene_pause_dungeonbord;    /* DAT_073a9b08 */
extern sprite_t g_scene_pause_sousa[SCENE_PAUSE_SOUSA_COUNT];     /* DAT_073d84d0 */
extern sprite_t g_scene_pause_status[SCENE_PAUSE_STATUS_COUNT];   /* DAT_073d8570 */

struct IDirect3DDevice8;

/* Cache the D3D device and register the body via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, ...). Call once
 * at boot, after the device is created. Idempotent. */
void scene_pause_init(struct IDirect3DDevice8 *dev);

/* FUN_004820ba — render the pause menu (mode 9). Called from the render
 * dispatcher while the slide ramp is in (3, 0xd). Currently draws the
 * pause_bg_rete backdrop (the menu's full-screen background); the option
 * list, calendar/gold, portrait, and cursor are PORT-DEBT (see
 * docs/plans/pause-menu.md M2b/M3). */
void pause_menu_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_PAUSE_H */
