/*
 * input.c — DirectInput 8 init / shutdown / poll.
 *
 * Two-layer split:
 *   - Top of file: pure-C decode + binding helpers + state globals.
 *     These compile on Linux for the ASan unit-test build.
 *   - Bottom (under `#ifdef _WIN32`): DI init/shutdown/poll wrapper that
 *     calls IDirectInputDevice8::Poll/GetDeviceState and feeds the
 *     decoded data into the helpers above.
 *
 * Original functions: FUN_0047af52 (init), FUN_0047b0ef (shutdown),
 * FUN_0047b73c (poll). RE writeup in
 * docs/findings/winmain-and-bootstrap.md §"Input poll".
 */

#include "input.h"
#include "recet_ini.h"
#include "call_trace.h"

#include <stdint.h>
#include <string.h>

/* ─── tables extracted from the unpacked binary ──────────────────────── */

/* DIK keyboard mapping at engine VA 0x005cbc2f. The original table is 41
 * bytes; index 0 is a `0x00` sentinel that the engine's `0 < sVar1`
 * guard skips. We drop the sentinel here and bias the lookup by one in
 * `input_apply_keyboard_block` below.
 *
 * Order (1-based) is the player-facing key-binding index. Defaults from
 * `recet_ini_pad_defaults[0]` map UP/RIGHT/DOWN/LEFT to indices 1..4
 * (entries DIK_UP/RIGHT/DOWN/LEFT) and the four face buttons to indices
 * 39/37/16/35 = Z/X/C/V. Extracted via
 *   `python3 tools/analyze/pe.py bytes 0x005cbc2f 41`. */
const uint8_t input_dik_table[INPUT_DIK_TABLE_SIZE] = {
    0xc8, 0xcd, 0xd0, 0xcb, 0x36, 0x9d, 0x39, 0x2a, 0x1d, 0x48,  /*  1.. 10 */
    0x4d, 0x50, 0x4b, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22,  /* 11.. 20 */
    0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10,  /* 21.. 30 */
    0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x00,  /* 31.. 40 */
    /* Slot 40 is `0x00` in the binary — last byte of the table is
     * unused. We keep it here so the [1..40] index range stays
     * symmetric with the engine's `sVar1 < 0x29` (= < 41) bound. */
};

/* Per-binding-slot output mask. The four D-pad slots (0..3) are in the
 * physical layout pad[N][0..3] which spells UP/RIGHT/DOWN/LEFT in the
 * vendor defaults; the output bits are 0x04/0x01/0x08/0x02 — the same
 * masks used everywhere downstream that reads DAT_073dddd0 (see e.g.
 * the camera-cursor code at lines 50410..50420 of all.c). */
const uint16_t input_binding_mask[INPUT_BINDING_SLOTS] = {
    0x0004,  /* pad[N][0] — D-pad UP    */
    0x0001,  /* pad[N][1] — D-pad RIGHT */
    0x0008,  /* pad[N][2] — D-pad DOWN  */
    0x0002,  /* pad[N][3] — D-pad LEFT  */
    0x0010,  /* pad[N][4] — button A    */
    0x0020,  /* pad[N][5] — button B    */
    0x0040,  /* pad[N][6] — button C    */
    0x0080,  /* pad[N][7] — button D    */
    0x0100,  /* pad[N][8] — button E (byte+1, bit 0)        */
    0x0200,  /* skill[N][0]              (byte+1, bit 1)    */
    0x0400,  /* skill[N][1]              (byte+1, bit 2)    */
    0x0800,  /* skill[N][2]              (byte+1, bit 3)    */
    0x1000,  /* skill[N][3]              (byte+1, bit 4)    */
    0x2000,  /* skill[N][4]              (byte+1, bit 5)    */
};

/* ─── globals (mirror engine BSS) ────────────────────────────────────── */

struct input_state g_input_state[INPUT_NUM_PLAYERS];
int16_t g_input_bindings[INPUT_BINDINGS_BLOCKS][INPUT_BINDING_SLOTS];

void input_bindings_load(const struct recet_ini *ini)
{
    /* Flatten pad+skill into the engine's interleaved per-controller
     * stride. Engine layout (DAT_0438ccea anchor): pad[0..8] skill[0..4]
     * pad[0..8] skill[0..4] — two contiguous 14-short blocks. The two
     * trailing blocks remain zero (mirrors the BSS bytes the engine's
     * dead iterations 2..3 walk; see quirk #41). */
    memset(g_input_bindings, 0, sizeof g_input_bindings);
    for (int c = 0; c < RECET_INI_CONTROLLERS && c < INPUT_BINDINGS_BLOCKS; c++) {
        for (int i = 0; i < RECET_INI_PAD_KEYS; i++) {
            g_input_bindings[c][i] = ini->pad[c][i];
        }
        for (int i = 0; i < RECET_INI_SKILL_KEYS; i++) {
            g_input_bindings[c][RECET_INI_PAD_KEYS + i] = ini->skill[c][i];
        }
    }
}

void input_clear_states(void)
{
    memset(g_input_state, 0, sizeof g_input_state);
}

/* ─── pure-C decoders ────────────────────────────────────────────────── */

void input_joystick_decode(int32_t pov, int32_t lx, int32_t ly,
                           const uint8_t buttons[16],
                           uint8_t out[20])
{
    /* D-pad bits start clean. The engine's local_64[0..3] are zeroed
     * before any matching, then the eight POV cases below + the four
     * stick-axis checks below OR in via simple assignments to 1. */
    out[0] = out[1] = out[2] = out[3] = 0;

    /* POV-hat cardinals + diagonals. DInput reports angle×100 in degrees;
     * 0 / 9000 / 18000 / 27000 are N/E/S/W and the in-between values are
     * the 4 diagonals. Centered (no direction held) returns -1
     * (0xFFFFFFFF) and matches none of the cases below. */
    if (pov == 0)       out[0] = 1;
    if (pov == 0x1194) { out[0] = 1; out[1] = 1; }   /*  4500 — NE */
    if (pov == 9000)    out[1] = 1;
    if (pov == 0x34bc) { out[1] = 1; out[2] = 1; }   /* 13500 — SE */
    if (pov == 18000)   out[2] = 1;
    if (pov == 0x57e4) { out[2] = 1; out[3] = 1; }   /* 22500 — SW */
    if (pov == 27000)   out[3] = 1;
    if (pov == 0x7b0c) { out[3] = 1; out[0] = 1; }   /* 31500 — NW */

    /* Stick axes — DIPROP_RANGE was set to ±1000 in init, dead-zone is
     * a fixed ±500 here (no recet.ini key controls it). Engine writes
     * to local_64[0..3] directly, so we just clobber with 1 on hit. */
    if (ly < -500) out[0] = 1;
    if (lx >  500) out[1] = 1;
    if (ly >  500) out[2] = 1;
    if (lx < -500) out[3] = 1;

    /* Buttons — engine reads 16 of them (`do { ... } while (iVar3 !=
     * 0x10)`), tests the DI "pressed" high bit, leaves the lower 16
     * slots of the c_dfDIJoystick2 button array untouched. */
    for (int i = 0; i < 16; i++) {
        out[4 + i] = (buttons[i] & 0x80) ? 1 : 0;
    }
}

void input_apply_joystick_block(const int16_t bindings[INPUT_BINDING_SLOTS],
                                const uint8_t pressed[20],
                                int virtual_base,
                                uint16_t *out_mask)
{
    /* The engine's structure is i-outer, k-inner — we keep that order
     * because each binding slot can match at most one i, so once we set
     * a mask bit we don't need to keep scanning for it. (Compiler will
     * lift the inner loop's bound checks anyway.) */
    uint16_t acc = 0;
    for (int i = 0; i < 20; i++) {
        if (!pressed[i]) continue;
        int vcode = virtual_base + i;
        for (int k = 0; k < INPUT_BINDING_SLOTS; k++) {
            /* Engine compares `bindings[k] + -1 == iVar6`. Equivalent
             * to `bindings[k] == vcode + 1`. With bindings[k] = 0
             * (unbound / BSS), the comparison fails since vcode ≥ 0x27. */
            if (bindings[k] - 1 == vcode) {
                acc |= input_binding_mask[k];
            }
        }
    }
    *out_mask |= acc;
}

void input_apply_keyboard_block(const int16_t bindings[INPUT_BINDING_SLOTS],
                                const uint8_t kbd_state[256],
                                uint16_t *out_mask)
{
    /* The engine inverts this loop (key outer, binding inner) — same
     * result, but ours is O(14) instead of O(256·14). */
    uint16_t acc = 0;
    for (int k = 0; k < INPUT_BINDING_SLOTS; k++) {
        int16_t b = bindings[k];
        if (b <= 0 || b >= 41) continue;   /* engine: `0 < sVar1 && sVar1 < 0x29` */
        uint8_t dik = input_dik_table[b - 1];
        if (kbd_state[dik] & 0x80) {
            acc |= input_binding_mask[k];
        }
    }
    *out_mask |= acc;
}

/* ─── Win32: DirectInput init / shutdown / poll ──────────────────────── */
#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

/* Mirrors of the engine's DAT_073dfcc0… globals. The poll function reads
 * these directly; the WM_ACTIVATE Acquire/Unacquire pair too. */
static IDirectInput8A       *g_di;                              /* DAT_073dfcc0 */
static IDirectInputDevice8A *g_kbd;                             /* DAT_073dfcc4 */
static IDirectInputDevice8A *g_joys[INPUT_MAX_JOYS];            /* DAT_073dfcc8…ccd */
static DWORD                 g_joy_count;                       /* DAT_073dfcd8 */
static DWORD                 g_joy_cur;                         /* DAT_073dfcdc */
static DIDEVCAPS             g_joy_caps_scratch;                /* DAT_073de3e8 */

/* `DAT_005cbc24` in the engine — a one-shot "input enabled" flag set to
 * 1 by an early-init routine and (as far as we've found) never cleared.
 * Gates the Poll-failure / GetDeviceState-failure retry inner loops. */
static int g_input_retry_on_unacquired = 1;

static HRESULT di_set_dword_prop(IDirectInputDevice8A *dev, REFGUID prop, DWORD val)
{
    DIPROPDWORD p = {0};
    p.diph.dwSize       = sizeof(p);
    p.diph.dwHeaderSize = sizeof(p.diph);
    p.diph.dwObj        = 0;
    p.diph.dwHow        = DIPH_DEVICE;
    p.dwData            = val;
    return IDirectInputDevice8_SetProperty(dev, prop, &p.diph);
}

/* Port of FUN_0047b1f2 — sets DIPROP_RANGE = ±1000 on the enumerated
 * axis/POV via DIPH_BYID. Reads g_joy_cur which the init loop sets
 * before each EnumObjects call. */
static BOOL CALLBACK enum_obj_cb(LPCDIDEVICEOBJECTINSTANCEA lpdoi, LPVOID pvRef)
{
    (void)pvRef;
    DIPROPRANGE r = {0};
    r.diph.dwSize       = sizeof(r);
    r.diph.dwHeaderSize = sizeof(r.diph);
    r.diph.dwObj        = lpdoi->dwType;
    r.diph.dwHow        = DIPH_BYID;
    r.lMin              = -1000;
    r.lMax              =  1000;
    IDirectInputDevice8_SetProperty(g_joys[g_joy_cur], DIPROP_RANGE, &r.diph);
    return DIENUM_CONTINUE;
}

/* Port of LAB_0047b167. Creates the device, queries caps (if caps fail
 * the slot is released + zeroed before continuing), bumps g_joy_count,
 * stops once we hit INPUT_MAX_JOYS. */
static BOOL CALLBACK enum_joy_cb(LPCDIDEVICEINSTANCEA lpddi, LPVOID pvRef)
{
    (void)pvRef;
    HRESULT hr = IDirectInput8_CreateDevice(
        g_di, &lpddi->guidInstance, &g_joys[g_joy_count], NULL);
    if (FAILED(hr)) return DIENUM_CONTINUE;

    g_joy_caps_scratch.dwSize = sizeof(g_joy_caps_scratch);
    hr = IDirectInputDevice8_GetCapabilities(
        g_joys[g_joy_count], &g_joy_caps_scratch);
    if (FAILED(hr)) {
        IDirectInputDevice8A *dev = g_joys[g_joy_count];
        if (dev) {
            IDirectInputDevice8_Release(dev);
            g_joys[g_joy_count] = NULL;
        }
        return DIENUM_CONTINUE;
    }

    g_joy_count++;
    return (g_joy_count < INPUT_MAX_JOYS) ? DIENUM_CONTINUE : DIENUM_STOP;
}

/* Keyboard/joystick cooperative level.  Retail (FUN_0047af52) uses
 * DISCL_FOREGROUND — input arrives only while the game window is the FOREGROUND
 * window.  The port is launched from WSL (tools/run-openrecet.sh starts the
 * Windows exe from the Linux side): the window appears but never becomes the
 * foreground window and cannot be focused even by clicking, so a FOREGROUND
 * device delivers NOTHING — interactive input is dead, while TAS traces (which
 * bypass DirectInput and write g_input_state directly) work fine.  Use
 * DISCL_BACKGROUND so input arrives regardless of focus.  Harness/dev divergence
 * only — it does NOT affect parity (traces never touch DirectInput); revisit if
 * the port ever ships as a native foreground app.
 *
 * Regression guard (interactive input has died from this being FOREGROUND twice):
 * the _Static_assert below fails the build if the BACKGROUND bit is ever dropped. */
#define INPUT_COOP_LEVEL (DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)
_Static_assert((INPUT_COOP_LEVEL & DISCL_BACKGROUND) != 0,
               "interactive input needs DISCL_BACKGROUND: a WSL-launched window "
               "never gets foreground focus, so DISCL_FOREGROUND => dead input");

int input_init(HINSTANCE hInst, HWND hwnd)
{
    const DWORD coop = INPUT_COOP_LEVEL;

    HRESULT hr = DirectInput8Create(
        hInst, DIRECTINPUT_VERSION, &IID_IDirectInput8A, (LPVOID *)&g_di, NULL);
    if (FAILED(hr)) return 0;

    hr = IDirectInput8_CreateDevice(g_di, &GUID_SysKeyboard, &g_kbd, NULL);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(IDirectInputDevice8_SetDataFormat(g_kbd, &c_dfDIKeyboard)) &&
            SUCCEEDED(IDirectInputDevice8_SetCooperativeLevel(g_kbd, hwnd, coop)) &&
            SUCCEEDED(di_set_dword_prop(g_kbd, DIPROP_BUFFERSIZE, 100))) {
            IDirectInputDevice8_Acquire(g_kbd);
        }
    }

    IDirectInput8_EnumDevices(
        g_di, DI8DEVCLASS_GAMECTRL, enum_joy_cb, NULL, DIEDFL_ATTACHEDONLY);

    /* The engine's init uses c_dfDIJoystick2 (272-byte format, see
     * dwDataSize=0x110 at 0x0051c4cc). We mirror that so GetDeviceState
     * with cbData=sizeof(DIJOYSTATE2) below matches the registered
     * format — using c_dfDIJoystick (80-byte) with a 272-byte buffer
     * gets DIERR_INVALIDPARAM. */
    for (DWORD i = 0; i < g_joy_count; i++) {
        g_joy_cur = i;
        IDirectInputDevice8A *dev = g_joys[i];
        if (!dev) continue;
        if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2))) continue;
        if (FAILED(IDirectInputDevice8_SetCooperativeLevel(dev, hwnd, coop))) continue;
        IDirectInputDevice8_EnumObjects(dev, enum_obj_cb, NULL,
                                        DIDFT_AXIS | DIDFT_POV);
        if (FAILED(di_set_dword_prop(dev, DIPROP_AXISMODE,   DIPROPAXISMODE_ABS))) continue;
        if (FAILED(di_set_dword_prop(dev, DIPROP_BUFFERSIZE, 100)))                continue;
        IDirectInputDevice8_Acquire(dev);
    }
    return 1;
}

void input_shutdown(void)
{
    if (g_kbd) {
        IDirectInputDevice8_Unacquire(g_kbd);
        IDirectInputDevice8_Release(g_kbd);
        g_kbd = NULL;
    }
    for (DWORD i = 0; i < g_joy_count; i++) {
        if (g_joys[i]) {
            IDirectInputDevice8_Unacquire(g_joys[i]);
            IDirectInputDevice8_Release(g_joys[i]);
            g_joys[i] = NULL;
        }
    }
    g_joy_count = 0;
    if (g_di) {
        IDirectInput8_Release(g_di);
        g_di = NULL;
    }
}

void input_unacquire_all(void)
{
    if (g_kbd) IDirectInputDevice8_Unacquire(g_kbd);
    for (DWORD i = 0; i < g_joy_count; i++) {
        if (g_joys[i]) IDirectInputDevice8_Unacquire(g_joys[i]);
    }
}

void input_acquire_all(void)
{
    if (g_kbd) IDirectInputDevice8_Acquire(g_kbd);
    for (DWORD i = 0; i < g_joy_count; i++) {
        if (g_joys[i]) IDirectInputDevice8_Acquire(g_joys[i]);
    }
}

/* ─── Win32 input_poll — port of FUN_0047b73c ─────────────────────────── */

static void poll_one_joystick(IDirectInputDevice8A *dev, uint8_t pressed[20])
{
    DIJOYSTATE2 st = {0};
    memset(pressed, 0, 20);

    /* Poll() (vtable+100) primes the device; on failure the engine
     * Acquires until either Acquire returns something other than
     * DIERR_NOTACQUIRED or the global gate clears. The condition checks
     * Acquire's return against the *GetDeviceState* error code — that's
     * a quirk (#42); Acquire never returns DIERR_NOTACQUIRED in
     * practice, so this always exits on the first iteration. */
    HRESULT hr = IDirectInputDevice8_Poll(dev);
    if (FAILED(hr)) {
        do {
            hr = IDirectInputDevice8_Acquire(dev);
            if (g_input_retry_on_unacquired == 0) break;
        } while ((HRESULT)hr == (HRESULT)0x8007001E /* DIERR_NOTACQUIRED */);
    }

    hr = IDirectInputDevice8_GetDeviceState(dev, sizeof st, &st);
    if (FAILED(hr)) return;

    input_joystick_decode((int32_t)st.rgdwPOV[0], st.lX, st.lY,
                          st.rgbButtons, pressed);
}

static int poll_keyboard_state(uint8_t kbd_state[256])
{
    /* GetDeviceState into a 256-byte buffer (one byte per DIK code, MSB
     * = pressed). Engine retries via Acquire on DIERR_INPUTLOST or
     * DIERR_NOTACQUIRED; on success we hand the buffer to the decoder. */
    memset(kbd_state, 0, 256);
    HRESULT hr = IDirectInputDevice8_GetDeviceState(g_kbd, 256, kbd_state);
    if (SUCCEEDED(hr)) return 1;

    if (g_input_retry_on_unacquired == 0) return 0;
    if ((HRESULT)hr != (HRESULT)0x8007001E) return 0;

    /* DIERR_NOTACQUIRED — single retry mirror. The engine then runs a
     * GetDeviceData drain loop; for our binding-only consumer that
     * drain is irrelevant (we don't keep a buffered queue), so we just
     * Acquire and treat this frame as "no input" rather than spinning. */
    IDirectInputDevice8_Acquire(g_kbd);
    return 0;
}

void input_poll(void)
{
    /* E.2 probe — FUN_0047b73c @ 0x47b73c. */
    CALL_TRACE_ENTER(0x47b73cu);

    /* Pre-poll clear of the button accumulator. The engine clears at the
     * BOTTOM of each ticked frame (FUN_0047be92 L78915-78916, after the
     * render callback returns) — that lets it accumulate input across
     * the multiple polls-per-frame that fire when `speed > 0` (e.g.
     * 30FPS sim where input polls at 60FPS).
     *
     * Our pre-clear here matches the engine exactly at the default
     * speed=0 / 60FPS path (one poll per render, so cleared either way),
     * but diverges from the engine for higher `fps` settings — at
     * speed=1 the engine would OR two polls' worth of bits into the
     * same frame and our port would only see the latest. Revisit when
     * the tick scheduler grows a post-render clear hook (likely lands
     * with the FUN_004547ab render port). */
    g_input_state[0].buttons = 0;
    g_input_state[1].buttons = 0;

    /* Per-joystick raw "pressed" bits (20 = 4 D-pad + 16 buttons). */
    uint8_t pressed[INPUT_MAX_JOYS][20] = {{0}};

    /* Poll every joystick once; the engine re-polls per binding block,
     * which is a quirk (#43) — same data four times in a row. */
    if (g_di) {
        for (DWORD i = 0; i < g_joy_count; i++) {
            if (g_joys[i]) poll_one_joystick(g_joys[i], pressed[i]);
        }
    }

    /* Joystick path. The engine iterates 4 outer binding blocks but
     * only 2 are populated; blocks 2..3 OR-into player-1's slot (which
     * stays zero because their bindings are zero). We still iterate 4×
     * so the player-slot indexing matches the original. */
    for (int b = 0; b < INPUT_BINDINGS_BLOCKS; b++) {
        int player_slot = b / 2;
        for (DWORD j = 0; j < g_joy_count; j++) {
            if (!g_joys[j]) continue;
            int virtual_base = 0x27 + (int)j * 0x14;
            input_apply_joystick_block(g_input_bindings[b], pressed[j],
                                       virtual_base,
                                       &g_input_state[player_slot].buttons);
        }
    }

    /* Keyboard path — engine walks 2 blocks (psVar8 != &DAT_0438cd22).
     * Both feed into player 0 via the same (iVar3/2)*0x2a stride. */
    if (g_kbd) {
        uint8_t kbd_state[256];
        if (poll_keyboard_state(kbd_state)) {
            for (int b = 0; b < 2; b++) {
                int player_slot = b / 2;   /* both 0 — see quirk #40 */
                input_apply_keyboard_block(g_input_bindings[b], kbd_state,
                                           &g_input_state[player_slot].buttons);
            }
        }
    }
}

#endif /* _WIN32 */
