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
 * Hand dinput8 the host module instead; retail state never sees the value.
 * Keyboard device (FUN_0047af52: CreateDevice → SetDataFormat → SetCooperativeLevel
 * (DAT_073dfc7c, NONEXCLUSIVE|FOREGROUND) → SetProperty(BUFFERSIZE 100) → Acquire)
 * gets a logging vtable copy: every setup HRESULT, and each change of the poll's
 * GetDeviceState result (FUN_0047b73c @47bac5). Retail re-acquires only on
 * DIERR_INPUTLOST or WM_ACTIVATE; in --record the wrapper also re-acquires on
 * NOTACQUIRED (device state is not game state — the recorded mask is).
 * Hooks patch dinput8's OWN vtables in place: Windows dinput8 validates
 * this->lpVtbl, so swapping an object onto a vtable copy made every call on it
 * fail E_INVALIDARG (CreateDevice(keyboard) = 80070057; wine doesn't check). */
#include "rhost.h"

#define DI8DEVCLASS_GAMECTRL_ 4
typedef HRESULT (WINAPI *create8_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *enumdev_t)(void *self, DWORD type, void *cb, void *ref, DWORD flags);
static create8_t real_create;
static enumdev_t real_enum;
static long g_filtered;

/* vt[i] := fn in place (dinput8's vtable lives in its read-only .rdata); returns the original */
static void *patch_vt(void **vt, int i, void *fn)
{
    void *orig = vt[i];
    if (orig == fn) return NULL;            /* already patched (second object of the class) */
    DWORD old;
    VirtualProtect(&vt[i], sizeof(void *), PAGE_READWRITE, &old);
    vt[i] = fn;
    VirtualProtect(&vt[i], sizeof(void *), old, &old);
    return orig;
}

static HRESULT WINAPI my_EnumDevices(void *self, DWORD type, void *cb, void *ref, DWORD flags)
{
    if ((type & 0xff) == DI8DEVCLASS_GAMECTRL_) { g_filtered++; return S_OK; }   /* no pads, ever */
    return real_enum(self, type, cb, ref, flags);
}
/* ---- keyboard device: logging vtable (IDirectInputDevice8A, 32 slots) ---- */
#define DIERR_NOTACQUIRED_ ((HRESULT)0x8007000CL)
#define DIERR_INPUTLOST_   ((HRESULT)0x8007001EL)
static const GUID kGUID_SysKeyboard = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
typedef HRESULT (WINAPI *createdev_t)(void *, REFGUID, void **, void *);
typedef HRESULT (WINAPI *dev0_t)(void *);
typedef HRESULT (WINAPI *setprop_t)(void *, REFGUID, const void *);
typedef HRESULT (WINAPI *getstate_t)(void *, DWORD, void *);
typedef HRESULT (WINAPI *setfmt_t)(void *, const void *);
typedef HRESULT (WINAPI *setcoop_t)(void *, HWND, DWORD);
static createdev_t real_createdev;
static void *g_kreal[32];                   /* original IDirectInputDevice8A entries */
static int g_kpatched;
static HRESULT g_last_acq = 1, g_last_gds = 1;
static long g_heals;
#define HR(r) ((unsigned long)(r))

static HRESULT WINAPI k_SetProperty(void *d, REFGUID p, const void *h)
{ HRESULT r = ((setprop_t)g_kreal[6])(d, p, h); rh_log("dinput: kbd SetProperty = %08lx\n", HR(r)); return r; }
static HRESULT WINAPI k_Acquire(void *d)
{
    HRESULT r = ((dev0_t)g_kreal[7])(d);
    if (r != g_last_acq) { rh_log("dinput: kbd Acquire = %08lx (frame %u, foreground=%p)\n", HR(r), g_frame, (void *)GetForegroundWindow()); g_last_acq = r; }
    return r;
}
static HRESULT WINAPI k_Unacquire(void *d)
{ HRESULT r = ((dev0_t)g_kreal[8])(d); rh_log("dinput: kbd Unacquire = %08lx (frame %u)\n", HR(r), g_frame); g_last_acq = 1; return r; }
static HRESULT WINAPI k_GetDeviceState(void *d, DWORD n, void *buf)
{
    HRESULT r = ((getstate_t)g_kreal[9])(d, n, buf);
    if (O.record && (r == DIERR_NOTACQUIRED_ || r == DIERR_INPUTLOST_) && ((dev0_t)g_kreal[7])(d) >= 0) {
        g_heals++;
        r = ((getstate_t)g_kreal[9])(d, n, buf);
    }
    if (r != g_last_gds) {
        rh_log("dinput: kbd GetDeviceState = %08lx (frame %u, foreground=%p, record re-acquires=%ld)\n",
               HR(r), g_frame, (void *)GetForegroundWindow(), g_heals);
        g_last_gds = r;
    }
    return r;
}
static HRESULT WINAPI k_SetDataFormat(void *d, const void *f)
{ HRESULT r = ((setfmt_t)g_kreal[11])(d, f); rh_log("dinput: kbd SetDataFormat = %08lx\n", HR(r)); return r; }
static HRESULT WINAPI k_SetCooperativeLevel(void *d, HWND h, DWORD fl)
{
    HRESULT r = ((setcoop_t)g_kreal[13])(d, h, fl);
    rh_log("dinput: kbd SetCooperativeLevel(hwnd=%p flags=%lx) = %08lx (foreground=%p)\n",
           (void *)h, (unsigned long)fl, HR(r), (void *)GetForegroundWindow());
    return r;
}
static HRESULT WINAPI my_CreateDevice(void *self, REFGUID g, void **dev, void *outer)
{
    HRESULT r = real_createdev(self, g, dev, outer);
    int kbd = g && !memcmp(g, &kGUID_SysKeyboard, sizeof(GUID));
    rh_log("dinput: CreateDevice(%s) = %08lx\n", kbd ? "keyboard" : "other", HR(r));
    if (r >= 0 && kbd && dev && *dev && !g_kpatched) {
        void **vt = *(void ***)*dev;
        memcpy(g_kreal, vt, sizeof g_kreal);
        patch_vt(vt, 6, (void *)k_SetProperty);   patch_vt(vt, 7, (void *)k_Acquire);
        patch_vt(vt, 8, (void *)k_Unacquire);     patch_vt(vt, 9, (void *)k_GetDeviceState);
        patch_vt(vt, 11, (void *)k_SetDataFormat); patch_vt(vt, 13, (void *)k_SetCooperativeLevel);
        g_kpatched = 1;
    }
    return r;
}

static HRESULT WINAPI my_DirectInput8Create(HINSTANCE h, DWORD ver, REFIID iid, LPVOID *out, LPUNKNOWN outer)
{
    if (h == (HINSTANCE)0x400000) h = GetModuleHandleA(NULL);
    HRESULT r = real_create(h, ver, iid, out, outer);
    if (r < 0) rh_log("dinput: DirectInput8Create failed hr=%08lx\n", (unsigned long)r);
    if (r >= 0 && out && *out && !real_enum) {
        void **vt = *(void ***)*out;                /* IDirectInput8A: QI AddRef Release CreateDevice EnumDevices ... */
        real_createdev = (createdev_t)patch_vt(vt, 3, (void *)my_CreateDevice);
        real_enum = (enumdev_t)patch_vt(vt, 4, (void *)my_EnumDevices);
        rh_log("dinput: game-controller enumeration hidden (hermetic)\n");
    }
    return r;
}
void rh_dinput_install(void)
{
    real_create = (create8_t)iat_lookup("DirectInput8Create");
    iat_override("DirectInput8Create", my_DirectInput8Create);
}
