/* rh_dinput.c — hermetic DirectInput device enumeration.
 * Retail enumerates game controllers at boot (DI8DEVCLASS_GAMECTRL → callbacks
 * FUN_0047b167/FUN_0047b1f2 store device + axis state). Whatever is plugged into
 * the host therefore changed retail state (wine saw a pad, Windows none: the two
 * diverged at new game, 2026-10-13). Retail's IDirectInput8A gets a vtable copy
 * whose EnumDevices reports NO game controllers; keyboard/mouse are untouched.
 * Input itself never comes from devices in deterministic runs (rhost scripts the
 * mask); in --record mode the keyboard is the recorded device.
 * hinst: retail passes WinMain's hInstance (DAT_073de628 → FUN_0047af52), which
 * rhost fakes as 0x400000 — not a loader-known module, so Windows dinput8 rejected
 * it and every Windows run before 2026-10-13 had NO DInput (wine accepts it).
 * Hand dinput8 the host module instead; retail state never sees the value. */
#include "rhost.h"

#define DI8DEVCLASS_GAMECTRL_ 4
typedef HRESULT (WINAPI *create8_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *enumdev_t)(void *self, DWORD type, void *cb, void *ref, DWORD flags);
static create8_t real_create;
static enumdev_t real_enum;
static void *g_vt[16];
static long g_filtered;

static HRESULT WINAPI my_EnumDevices(void *self, DWORD type, void *cb, void *ref, DWORD flags)
{
    if ((type & 0xff) == DI8DEVCLASS_GAMECTRL_) { g_filtered++; return S_OK; }   /* no pads, ever */
    return real_enum(self, type, cb, ref, flags);
}
static HRESULT WINAPI my_DirectInput8Create(HINSTANCE h, DWORD ver, REFIID iid, LPVOID *out, LPUNKNOWN outer)
{
    if (h == (HINSTANCE)0x400000) h = GetModuleHandleA(NULL);
    HRESULT r = real_create(h, ver, iid, out, outer);
    if (r < 0) rh_log("dinput: DirectInput8Create failed hr=%08lx\n", (unsigned long)r);
    if (r >= 0 && out && *out) {
        void ***obj = (void ***)*out;
        memcpy(g_vt, *obj, 11 * sizeof(void *));    /* IDirectInput8A: QI AddRef Release CreateDevice EnumDevices ... */
        real_enum = (enumdev_t)g_vt[4];
        g_vt[4] = (void *)my_EnumDevices;
        *obj = g_vt;
        rh_log("dinput: game-controller enumeration hidden (hermetic)\n");
    }
    return r;
}
void rh_dinput_install(void)
{
    real_create = (create8_t)iat_lookup("DirectInput8Create");
    iat_override("DirectInput8Create", my_DirectInput8Create);
}
