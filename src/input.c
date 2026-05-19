/*
 * input.c — DirectInput 8 init / shutdown.
 *
 * Port of FUN_0047af52 ("init dinput ok") + FUN_0047b0ef (cleanup) + the
 * joystick enum cb at LAB_0047b167 + the per-object axis-range cb at
 * FUN_0047b1f2. RE writeup: docs/findings/winmain-and-bootstrap.md.
 *
 * Sequence the original runs (and we match here):
 *
 *   DirectInput8Create(hInst, 0x0800, IID_IDirectInput8A, &g_di, NULL)
 *   g_di->CreateDevice(GUID_SysKeyboard, &g_kbd, NULL)
 *   g_kbd->SetDataFormat(&c_dfDIKeyboard)
 *   g_kbd->SetCooperativeLevel(hwnd, DISCL_FOREGROUND|DISCL_NONEXCLUSIVE)
 *   g_kbd->SetProperty(DIPROP_BUFFERSIZE, .data = 100)
 *   g_kbd->Acquire()
 *
 *   g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, &enum_joy_cb, NULL,
 *                     DIEDFL_ATTACHEDONLY)
 *     for each joystick (capped at 4):
 *       g_di->CreateDevice(lpddi->guidInstance, &g_joys[i], NULL)
 *       g_joys[i]->GetCapabilities(&caps)        ← on failure: release+zero+continue
 *
 *   for each joystick that survived enumeration:
 *     g_joys[i]->SetDataFormat(&c_dfDIJoystick)
 *     g_joys[i]->SetCooperativeLevel(hwnd, FOREGROUND|NONEXCLUSIVE)
 *     g_joys[i]->EnumObjects(&enum_obj_cb, NULL, DIDFT_AXIS|DIDFT_POV)
 *         (cb sets DIPROP_RANGE = ±1000 per object, DIPH_BYID)
 *     g_joys[i]->SetProperty(DIPROP_AXISMODE, .data = DIPROPAXISMODE_ABS)
 *     g_joys[i]->SetProperty(DIPROP_BUFFERSIZE, .data = 100)
 *     g_joys[i]->Acquire()
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "input.h"

/* Names mirror the original's DAT_073dfcc0… globals so cross-referencing the
 * Ghidra dump stays mechanical. Layout-wise, the engine packs g_joys[4]
 * immediately after g_kbd (joy_count lives right after the array). */
static IDirectInput8A       *g_di;                              /* DAT_073dfcc0 */
static IDirectInputDevice8A *g_kbd;                             /* DAT_073dfcc4 */
static IDirectInputDevice8A *g_joys[INPUT_MAX_JOYS];            /* DAT_073dfcc8…ccd */
static DWORD                 g_joy_count;                       /* DAT_073dfcd8 */
static DWORD                 g_joy_cur;                         /* DAT_073dfcdc */
static DIDEVCAPS             g_joy_caps_scratch;                /* DAT_073de3e8 */

/* ─── DI helper: write a single-DWORD property to a device ─────────────── */
static HRESULT di_set_dword_prop(IDirectInputDevice8A *dev, REFGUID prop, DWORD val)
{
    DIPROPDWORD p = {0};
    p.diph.dwSize       = sizeof(p);          /* 0x14 in the original */
    p.diph.dwHeaderSize = sizeof(p.diph);     /* 0x10 */
    p.diph.dwObj        = 0;
    p.diph.dwHow        = DIPH_DEVICE;        /* 0 */
    p.dwData            = val;
    return IDirectInputDevice8_SetProperty(dev, prop, &p.diph);
}

/* ─── per-axis/POV callback — port of FUN_0047b1f2 ─────────────────────────
 * Sets DIPROP_RANGE = ±1000 on the enumerated object via DIPH_BYID. The
 * "current joystick" is selected by g_joy_cur, which the outer init loop
 * updates before each EnumObjects call (matches the original's pattern of
 * stashing the index in DAT_073dfcdc). */
static BOOL CALLBACK enum_obj_cb(LPCDIDEVICEOBJECTINSTANCEA lpdoi, LPVOID pvRef)
{
    (void)pvRef;
    DIPROPRANGE r = {0};
    r.diph.dwSize       = sizeof(r);          /* 0x18 */
    r.diph.dwHeaderSize = sizeof(r.diph);     /* 0x10 */
    r.diph.dwObj        = lpdoi->dwType;      /* identify by full dwType */
    r.diph.dwHow        = DIPH_BYID;          /* 2 */
    r.lMin              = -1000;
    r.lMax              =  1000;
    IDirectInputDevice8_SetProperty(g_joys[g_joy_cur], DIPROP_RANGE, &r.diph);
    return DIENUM_CONTINUE;
}

/* ─── joystick enumeration callback — port of LAB_0047b167 ─────────────────
 * Creates the device into g_joys[g_joy_count]. On success calls
 * GetCapabilities — if THAT fails, the original Releases the freshly-created
 * device and zeroes the slot before continuing. After a clean add it bumps
 * the count and returns DIENUM_STOP once we hit INPUT_MAX_JOYS. */
static BOOL CALLBACK enum_joy_cb(LPCDIDEVICEINSTANCEA lpddi, LPVOID pvRef)
{
    (void)pvRef;
    HRESULT hr = IDirectInput8_CreateDevice(
        g_di, &lpddi->guidInstance, &g_joys[g_joy_count], NULL);
    if (FAILED(hr)) return DIENUM_CONTINUE;

    g_joy_caps_scratch.dwSize = sizeof(g_joy_caps_scratch);   /* 0x2C */
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

/* ─── public init — replays the full bootstrap order ──────────────────── */
BOOL input_init(HINSTANCE hInst, HWND hwnd)
{
    const DWORD coop = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE;  /* 6 in the original */

    HRESULT hr = DirectInput8Create(
        hInst, DIRECTINPUT_VERSION, &IID_IDirectInput8A, (LPVOID *)&g_di, NULL);
    if (FAILED(hr)) return FALSE;

    /* Keyboard. The original returns success even if any keyboard step fails
     * past CreateDevice — we'd rather know about it, but stick to the original
     * behavior so a missing kbd device doesn't kill boot. */
    hr = IDirectInput8_CreateDevice(g_di, &GUID_SysKeyboard, &g_kbd, NULL);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(IDirectInputDevice8_SetDataFormat(g_kbd, &c_dfDIKeyboard)) &&
            SUCCEEDED(IDirectInputDevice8_SetCooperativeLevel(g_kbd, hwnd, coop)) &&
            SUCCEEDED(di_set_dword_prop(g_kbd, DIPROP_BUFFERSIZE, 100))) {
            IDirectInputDevice8_Acquire(g_kbd);
        }
    }

    /* Joystick discovery — populates g_joys[] + g_joy_count. */
    IDirectInput8_EnumDevices(
        g_di, DI8DEVCLASS_GAMECTRL, enum_joy_cb, NULL, DIEDFL_ATTACHEDONLY);

    /* Per-joystick configuration. enum_obj_cb reads g_joy_cur, so we MUST
     * set it before each EnumObjects call (original does the same). */
    for (DWORD i = 0; i < g_joy_count; i++) {
        g_joy_cur = i;
        IDirectInputDevice8A *dev = g_joys[i];
        if (!dev) continue;
        if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick)))   continue;
        if (FAILED(IDirectInputDevice8_SetCooperativeLevel(dev, hwnd, coop))) continue;
        IDirectInputDevice8_EnumObjects(dev, enum_obj_cb, NULL,
                                        DIDFT_AXIS | DIDFT_POV);
        if (FAILED(di_set_dword_prop(dev, DIPROP_AXISMODE,   DIPROPAXISMODE_ABS))) continue;
        if (FAILED(di_set_dword_prop(dev, DIPROP_BUFFERSIZE, 100)))                continue;
        IDirectInputDevice8_Acquire(dev);
    }
    return TRUE;
}

/* ─── shutdown — port of FUN_0047b0ef ───────────────────────────────────── */
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

/* ─── WM_ACTIVATE companions ───────────────────────────────────────────── */
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
