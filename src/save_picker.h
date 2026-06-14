/*
 * save_picker.{c,h} — the shared save-file selection card list.
 *
 * Engine sources:
 *   FUN_0049b537 @ 0x49b537 (31 B)    — slot-perm + count init
 *   FUN_0049b556 @ 0x49b556 (2810 B)  — the card-list render (THE big one)
 *
 * This render is SHARED between two callers (both pass the same 6-arg
 * shape to FUN_0049b556):
 *   - the PAUSE Save submenu (entry type 3), via the wrapper FUN_004812e4
 *     → pause `(0, val[cur], val2[cur], c898, c894, save-phase)`;
 *   - the TITLE-screen Continue/load picker, via FUN_0049c644
 *     → title `(x, cursor, scroll, vscroll, hscroll, confirm-countdown)`.
 * The pause Save submenu is the first consumer ported (docs/plans/pause-menu.md
 * M4); the title picker render is the deferred PORT-DEBT(render) in
 * title_continue_picker.c and can adopt save_picker_render() later.
 *
 * ── What FUN_0049b556 draws (per visible card) ──
 *   The picker shows a vertical list of save-slot cards. The window is a
 *   3-page horizontal carousel (left wing / center / right wing); only the
 *   CENTER renders unless `9 < g_save_picker_hpage_anim` (engine DAT_09643520,
 *   the title Continue picker's open-ramp — left at 10 post-Continue, so a
 *   pause Save submenu reached via Continue draws all 3 pages; the wings sit at
 *   dst_x ±640 = off-screen, zero pixel impact). The title render
 *   (scene_title.c) drives that shared global; it is never reset. Each page
 *   draws 5 rows; for row r the slot index is `r - 1 + scroll` (so the window
 *   of slots scrolls under a fixed 5-row grid).
 *
 *   Pass 1 (one item_win.tga quad per card, COLOROP=ADDSIGNED): the card
 *   background box, src(0,320)-(640,480) → dst((x-(vscroll<<7)-640)+page,
 *   r*140-92-hscroll*28, 640,160). The selected row's diffuse breathes
 *   (sin(frame*0.1)*32+159); rows 0/1 fade during the row slide.
 *
 *   Pass 2 (per-card content): file number "%03d" (perm[slot]+1); then if the
 *   slot is OCCUPIED (bank[2] != 0) — an inner box, a survival badge (mode 3),
 *   the rotating clock-hand portrait, the big day number, the gold number, the
 *   "Merchant Level" plaque + level digits, and the SCORE / LOOP / TIME rows;
 *   else "NO-DATA". Pass 2 ends (page==3) with the COLOROP=MODULATE reset +
 *   the up/down scroll arrows (drawn when scroll>0 / scroll<count-3).
 *
 * Per-card bank fields (read via save_bank_dwords_at(perm[slot])):
 *   [2]      occupied / PLAYTIME frames@60     (SAVE_BANK_FIELD_OCCUPIED)
 *   [3]      gold "pix"                         (SAVE_BANK_FIELD_GOLD)
 *   [0xb0f7] SCORE                              (SAVE_BANK_FIELD_SCORE)
 *   [0xb0f9] LOOP (+1)                          (SAVE_BANK_FIELD_LOOP)
 *   [0xb0fb] big day number (+1)               (SAVE_BANK_FIELD_CARD_DAY)
 *   [0xb0fc] portrait/clock-hand rotation      (SAVE_BANK_FIELD_PORTRAIT_ROT)
 *   [0xb100] merchant level (drawn as +1)      (SAVE_BANK_FIELD_CHAR_LEVEL)
 *   [0xb759] game mode 0/1/2/3                  (SAVE_BANK_FIELD_GAME_MODE)
 *   [0xb78d] survival sub-mode flag (mode 2)    (= byte +0x2de34)
 *
 * Constants (geometry, the four NO-DATA/SCORE/LOOP/TIME format strings, the
 * portrait UVs, scale 0.8/0.65) are recovered from objdump @0x49b556..0x49c050
 * — Ghidra dropped the FP .rdata consts, the SetTexture texture args, the
 * sprintf format args, and the TIME seconds vararg. See pause-menu.md M4.
 *
 * The render is Win32 (D3D); the perm-init + globals are pure C (host-tested).
 *
 * The picker NAV (FUN_0047f5bc — U/D ±1 / L/R ±3 + the c894/c898 slide anims +
 * B-cancel) is ported in scene_pause.c as pause_save_submenu_update (M4b).
 * M4c PORT-DEBT(save-picker-commit / -overwrite): the A-confirm branch (empty
 * slot → commit / occupied → the "Overwriting file?" dialog), the COMMIT
 * itself (FUN_004905a8 disk write), and the dungeon-save warning — all behind
 * an A press the nav trace never makes, and behind the disk write.
 */

#ifndef OPENRECET_SAVE_PICKER_H
#define OPENRECET_SAVE_PICKER_H

#include <stdint.h>

#define SAVE_PICKER_SLOTS 100

/* Shared picker globals (engine .bss). Pure C so pause_menu_nav's type-3
 * commit (which calls save_picker_perm_init) is host-testable. */
extern int32_t g_save_picker_perm[SAVE_PICKER_SLOTS]; /* DAT_09643380 */
extern int32_t g_save_picker_count;                   /* DAT_005d1bbc — 100 */
extern int32_t g_save_picker_frame;                   /* _DAT_09643574 — selected-card
                                                       * breathe phase; incremented by BOTH
                                                       * renders (pause + title), never reset */
extern int32_t g_save_picker_restricted;              /* DAT_09643564 — dim-unavailable flag */
extern int32_t g_save_picker_hpage_anim;              /* DAT_09643520 — wing-render gate (0 at rest) */

/* DAT_096432f4 — per-slot "available for this op" byte (only consulted when
 * g_save_picker_restricted != 0, i.e. the title-screen overwrite/new-game
 * pick). The pause Save submenu clears restricted, so this is dead there;
 * what POPULATES it (the title restricted mode) is PORT-DEBT. BSS-zero. */
extern uint8_t g_save_picker_avail[SAVE_PICKER_SLOTS];

/* FUN_0049b537 — fill the slot-perm array with the identity 0..99 and set the
 * count to 100. Called by the pause type-3 commit (and the title picker open).
 * Idempotent. */
void save_picker_perm_init(void);

/* Reset the shared globals to BSS-zero (tests). */
void save_picker_reset(void);

#ifdef _WIN32

struct IDirect3DDevice8;

/* FUN_0049b556 — render the save-slot card list.
 *   x        — horizontal origin (engine param_1; 0 for the pause submenu)
 *   cursor   — selected slot index (param_2)
 *   scroll   — top-of-window slot index (param_3)
 *   vscroll  — between-column page slide (param_4; dst shift = vscroll<<7)
 *   hscroll  — within-column row slide (param_5; row shift = hscroll*28)
 *   phase    — save-animation / confirm-countdown (param_6; pulses the cursor)
 * Binds item_win.tga + g_scene_pause_pause internally; leaves COLOROP=MODULATE. */
void save_picker_render(struct IDirect3DDevice8 *dev,
                        float x, int cursor, int scroll,
                        int vscroll, int hscroll, int phase);

#endif /* _WIN32 */

#endif /* OPENRECET_SAVE_PICKER_H */
