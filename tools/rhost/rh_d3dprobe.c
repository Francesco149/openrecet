/* rh_d3dprobe.c — --d3d-probe: record what the REAL Direct3D 8 of this machine answers.
 *
 * The --headless device (rh_dev.c) answers adapter questions from a table recorded on the
 * reference machine, so D3DX and the game take the same decisions (texture formats, caps
 * paths) as under the real device. This file makes that recording: retail's
 * Direct3DCreate8 goes through here, the real object is probed, and the answers are
 * logged as `d3dref ...` lines (rhost.log → tools/rhost/gen_dev_ref.py → rh_dev_ref.h).
 * Retail then continues with the real object, untouched. */
#define CINTERFACE
#define COBJMACROS
#include "rhost.h"
#include <d3d8.h>

typedef IDirect3D8 *(WINAPI *create8_t)(UINT);
static create8_t real_create8;

static const uint32_t FMTS[] = {
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 34, 40, 41, 50, 51, 52, 60, 61, 62, 63, 64, 65, 67,
    MAKEFOURCC('U', 'Y', 'V', 'Y'), MAKEFOURCC('Y', 'U', 'Y', '2'),
    MAKEFOURCC('D', 'X', 'T', '1'), MAKEFOURCC('D', 'X', 'T', '2'), MAKEFOURCC('D', 'X', 'T', '3'),
    MAKEFOURCC('D', 'X', 'T', '4'), MAKEFOURCC('D', 'X', 'T', '5'),
    70, 71, 73, 75, 77, 79, 80, 100, 101, 102 };
#define NFMT (sizeof FMTS / sizeof FMTS[0])
static const uint32_t ADFMT[] = { 22, 23 }, USAGES[] = { 0, 1, 2, 0x200 }, RTYPES[] = { 1, 3, 4, 5 };
static const uint32_t DSFMT[] = { 70, 71, 73, 75, 77, 79, 80 }, RTFMT[] = { 21, 22, 23, 24, 25 };

static void probe(IDirect3D8 *d)
{
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    D3DDISPLAYMODE m; memset(&m, 0, sizeof m);
    HRESULT hr = IDirect3D8_GetAdapterDisplayMode(d, 0, &m);
    rh_log("d3dref mode %08lx %u %u %u %u cw=%04x\n", (unsigned long)hr, m.Width, m.Height, m.RefreshRate, (unsigned)m.Format, cw);
    UINT nm = IDirect3D8_GetAdapterModeCount(d, 0);
    rh_log("d3dref adapters %u modes %u\n", IDirect3D8_GetAdapterCount(d), nm);
    for (UINT i = 0; i < nm; i++)
        if (IDirect3D8_EnumAdapterModes(d, 0, i, &m) >= 0)
            rh_log("d3dref enum %u %u %u %u %u\n", i, m.Width, m.Height, m.RefreshRate, (unsigned)m.Format);
    for (int dt = 1; dt <= 2; dt++) {
        D3DCAPS8 c; memset(&c, 0, sizeof c);
        hr = IDirect3D8_GetDeviceCaps(d, 0, (D3DDEVTYPE)dt, &c);
        char hex[sizeof c * 2 + 1];
        for (size_t i = 0; i < sizeof c; i++) snprintf(hex + i * 2, 3, "%02x", ((uint8_t *)&c)[i]);
        rh_log("d3dref caps %d %08lx %u %s\n", dt, (unsigned long)hr, (unsigned)sizeof c, hex);
    }
    for (size_t a = 0; a < 2; a++)
        for (size_t u = 0; u < 4; u++)
            for (size_t r = 0; r < 4; r++)
                for (size_t f = 0; f < NFMT; f++) {
                    hr = IDirect3D8_CheckDeviceFormat(d, 0, D3DDEVTYPE_HAL, (D3DFORMAT)ADFMT[a], USAGES[u], (D3DRESOURCETYPE)RTYPES[r], (D3DFORMAT)FMTS[f]);
                    rh_log("d3dref cdf %u %x %u %08x %08lx\n", ADFMT[a], USAGES[u], RTYPES[r], FMTS[f], (unsigned long)hr);
                }
    for (size_t a = 0; a < 2; a++)
        for (size_t r = 0; r < sizeof RTFMT / 4; r++)
            for (size_t s = 0; s < sizeof DSFMT / 4; s++) {
                hr = IDirect3D8_CheckDepthStencilMatch(d, 0, D3DDEVTYPE_HAL, (D3DFORMAT)ADFMT[a], (D3DFORMAT)RTFMT[r], (D3DFORMAT)DSFMT[s]);
                rh_log("d3dref cdsm %u %u %u %08lx\n", ADFMT[a], RTFMT[r], DSFMT[s], (unsigned long)hr);
            }
    for (size_t a = 0; a < 2; a++)
        for (int w = 0; w < 2; w++) {
            hr = IDirect3D8_CheckDeviceType(d, 0, D3DDEVTYPE_HAL, (D3DFORMAT)ADFMT[a], (D3DFORMAT)ADFMT[a], w);
            rh_log("d3dref cdt %u %d %08lx\n", ADFMT[a], w, (unsigned long)hr);
            for (int ms = 0; ms <= 16; ms++) {
                hr = IDirect3D8_CheckDeviceMultiSampleType(d, 0, D3DDEVTYPE_HAL, (D3DFORMAT)ADFMT[a], w, (D3DMULTISAMPLE_TYPE)ms);
                rh_log("d3dref cdms %u %d %d %08lx\n", ADFMT[a], w, ms, (unsigned long)hr);
            }
        }
    rh_log("d3dref end\n");
    rh_log_flush();
}

static IDirect3D8 *WINAPI my_Direct3DCreate8(UINT ver)
{
    IDirect3D8 *d = real_create8(ver);
    if (d) probe(d);
    return d;
}

void rh_d3dprobe_install(void)
{
    real_create8 = (create8_t)iat_lookup("Direct3DCreate8");
    iat_override("Direct3DCreate8", my_Direct3DCreate8);
}

/* the device's own view, once it exists (tick 1): caps under the chosen behaviour flags,
 * the texture memory figure, the control word D3D left behind */
void rh_d3dprobe_device(void)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)(uintptr_t)G32(0x73dfcbcu);
    if (!dev) return;
    D3DCAPS8 c; memset(&c, 0, sizeof c);
    HRESULT hr = IDirect3DDevice8_GetDeviceCaps(dev, &c);
    char hex[sizeof c * 2 + 1];
    for (size_t i = 0; i < sizeof c; i++) snprintf(hex + i * 2, 3, "%02x", ((uint8_t *)&c)[i]);
    rh_log("d3dref devcaps %08lx %u %s\n", (unsigned long)hr, (unsigned)sizeof c, hex);
    D3DDEVICE_CREATION_PARAMETERS cp; memset(&cp, 0, sizeof cp);
    IDirect3DDevice8_GetCreationParameters(dev, &cp);
    rh_log("d3dref devcreate adapter=%u type=%u flags=%lx texmem=%u\n", cp.AdapterOrdinal, (unsigned)cp.DeviceType,
           (unsigned long)cp.BehaviorFlags, IDirect3DDevice8_GetAvailableTextureMem(dev));
    const uint32_t *pp = (const uint32_t *)(uintptr_t)0x73de268u;   /* retail's D3DPRESENT_PARAMETERS after CreateDevice */
    rh_log("d3dref pp %u %u %u %u %u %u %08x %u %u %u %u %u %u\n", pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[6], pp[7],
           pp[8], pp[9], pp[10], pp[11], pp[12]);
    IDirect3DSurface8 *bb = NULL;
    if (IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bb) >= 0 && bb) {
        D3DSURFACE_DESC sd; memset(&sd, 0, sizeof sd);
        IDirect3DSurface8_GetDesc(bb, &sd);
        rh_log("d3dref backbuffer fmt=%u type=%u usage=%lx pool=%u size=%u ms=%u %ux%u\n", (unsigned)sd.Format, (unsigned)sd.Type,
               (unsigned long)sd.Usage, (unsigned)sd.Pool, sd.Size, (unsigned)sd.MultiSampleType, sd.Width, sd.Height);
        IDirect3DSurface8_Release(bb);
    }
    D3DVIEWPORT8 vp; memset(&vp, 0, sizeof vp);
    IDirect3DDevice8_GetViewport(dev, &vp);
    rh_log("d3dref viewport %lu %lu %lu %lu %g %g\n", vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ);
    rh_log_flush();
}
