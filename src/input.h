/*
 * input.h — DirectInput 8 init/poll for keyboard + multi-joystick.
 *
 * Init + shutdown mirror FUN_0047af52 / FUN_0047b0ef + the Acquire dance
 * driven by WM_ACTIVATE; per-frame poll mirrors FUN_0047b73c (keyboard +
 * up to 4 joysticks with POV-hat decode, stick-axis dead-zone, button
 * scan, and a binding-table lookup against recet.ini pad/skill).
 *
 * The poll pure-C helpers (POV/axis decode, button decode, binding
 * application, keyboard DIK→bit mapping) are factored out so they run
 * under ASan in the unit tests without a real DI device.
 *
 * RE writeup: docs/findings/winmain-and-bootstrap.md §"Input poll".
 */
#ifndef OPENRECET_INPUT_H
#define OPENRECET_INPUT_H

#include <stdint.h>
#include <stddef.h>

struct recet_ini;

/* Up to 4 attached joysticks — matches the engine's `cmp dword 4; setl`
 * stop condition in the LAB_0047b167 enum callback. */
#define INPUT_MAX_JOYS    4

/* Engine reads 4 outer binding blocks but only writes 2 (one per
 * controller). Blocks 2..3 are uninitialized BSS bytes and effectively
 * always 0 — we mirror that by allocating 4 and only filling 2. */
#define INPUT_BINDINGS_BLOCKS 4
#define INPUT_BINDING_SLOTS   14   /* 9 pad + 5 skill */

/* Two player slots, 42-byte stride — see DAT_073dddd0 / DAT_073dddfa. The
 * poll only writes byte 0+1 (the 16-bit button mask); the remaining 40
 * bytes are filled by other engine modules we haven't ported yet. */
#define INPUT_NUM_PLAYERS     2
#define INPUT_PLAYER_STRIDE   0x2a

/* DIK keyboard table at engine VA 0x005cbc2f. The engine indexes it as
 * 1-based — index 0 is a sentinel `0x00` that `bindings[k] > 0` guards
 * against; valid binding values are 1..40, mapping to a DIK_* keycode. */
#define INPUT_DIK_TABLE_SIZE  40
extern const uint8_t input_dik_table[INPUT_DIK_TABLE_SIZE];

/* Output bit for each binding slot — see the 14 chained `psVar8[k] + -1
 * == iVar6` blocks in FUN_0047b73c. Slots 0..3 are D-pad (in the order
 * UP, RIGHT, DOWN, LEFT — note the ASCII binding indices in pad don't
 * line up with mask bit numbers); slots 4..7 are face buttons A..D;
 * slot 8 is button E (pad08); slots 9..13 are skill 0..4 in byte+1. */
extern const uint16_t input_binding_mask[INPUT_BINDING_SLOTS];

/* Player input record. `buttons` is the 14-bit accumulated mask the
 * poll writes each frame; remaining bytes are sim/render scratch we
 * mirror by padding so the struct stays at INPUT_PLAYER_STRIDE bytes
 * (matches engine layout for the few cross-module reads we've spotted
 * at offset 0 only — e.g. lines 49132/19292/60000 in all.c). */
struct input_state {
    uint16_t buttons;
    uint8_t  pad[INPUT_PLAYER_STRIDE - sizeof(uint16_t)];
};

extern struct input_state g_input_state[INPUT_NUM_PLAYERS];

/* Flat copy of recet.ini pad+skill bindings, in the engine's interleaved
 * layout (pad[N][0..8] then skill[N][0..4], stride 14 shorts per
 * controller). Index N in [0, INPUT_BINDINGS_BLOCKS-1]; blocks 2..3
 * stay zero — they exist so the outer poll loop can iterate 4× without
 * branching, matching the engine's `psVar8 != &DAT_0438cd5a` stop. */
extern int16_t g_input_bindings[INPUT_BINDINGS_BLOCKS][INPUT_BINDING_SLOTS];

/* Copy the live recet.ini pad/skill arrays into the flat block layout.
 * Call once after recet_ini_load (and again if bindings ever change). */
void input_bindings_load(const struct recet_ini *ini);

/* Zero the per-player input mask. The engine does this at the bottom of
 * each ticked frame, after FUN_004547ab (render) returns — so the next
 * `input_poll` starts from a clean slate. See FUN_0047be92 L78915-78916. */
void input_clear_states(void);

/* ─── pure-C decode helpers (testable without a DI device) ─────────── */

/* Decode the joystick "raw pressed" 20-bit array used by the engine's
 * binding loop. Output layout (mirrors FUN_0047b73c locals 0x40..0x90):
 *   out[0..3]: D-pad UP/RIGHT/DOWN/LEFT, OR of POV-hat and stick axes
 *   out[4..19]: rgbButtons[0..15] (the engine reads exactly 16 buttons)
 *
 * Each output byte is 0 or 1. The POV decoder recognises the 8 DInput
 * cardinal/diagonal values (0 / 4500 / 9000 / 13500 / 18000 / 22500 /
 * 27000 / 31500); "centered" (-1 / 0xFFFFFFFF) decodes to all zeros.
 * Stick dead-zone is ±500 on each axis — values past that latch the
 * corresponding D-pad bit. */
void input_joystick_decode(int32_t pov, int32_t lx, int32_t ly,
                           const uint8_t buttons[16],
                           uint8_t out[20]);

/* Apply one binding block to one joystick's pressed-bit array. For each
 * `i` in [0, 20) where `pressed[i] != 0`, the engine checks all 14
 * binding slots and ORs the corresponding bit into the output. The
 * `virtual_base` arg is `0x27 + joy_index * 0x14` — the start of the
 * virtual-button range this joystick occupies in the engine's flat
 * 80-button space (0x27..0x3a / 0x3b..0x4e / 0x4f..0x62 / 0x63..0x76).
 *
 * Updates `*out_mask` with `|=`; never clears. */
void input_apply_joystick_block(const int16_t bindings[INPUT_BINDING_SLOTS],
                                const uint8_t pressed[20],
                                int virtual_base,
                                uint16_t *out_mask);

/* Apply one binding block against the 256-byte keyboard state buffer.
 * For each binding slot whose value is in [1, 40], looks up the DIK
 * code in `input_dik_table[]` and ORs the slot's mask bit into
 * `*out_mask` if that key's high bit (DI's "pressed" flag) is set. */
void input_apply_keyboard_block(const int16_t bindings[INPUT_BINDING_SLOTS],
                                const uint8_t kbd_state[256],
                                uint16_t *out_mask);

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

int  input_init(HINSTANCE hInst, HWND hwnd);  /* returns 1 on success, 0 otherwise */
void input_shutdown(void);

/* WM_ACTIVATE pair: unacquire on deactivate, reacquire on activate. */
void input_unacquire_all(void);
void input_acquire_all(void);

/* Per-frame poll. Queries each acquired DI device, decodes raw button
 * state via the helpers above, and writes the accumulated 14-bit mask
 * into `g_input_state[0].buttons`. (Player-1 slot stays 0 — see the
 * "(local_8 / 2) * 0x2a" stride note in the RE writeup.) */
void input_poll(void);
#endif

#endif /* OPENRECET_INPUT_H */
