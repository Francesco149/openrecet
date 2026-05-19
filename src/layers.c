/*
 * layers.c — render-layer bootstrap.  Ports FUN_00454e69 + FUN_004038e4.
 *
 * Per-layer init writes happen even when dev is NULL (the original
 * stores the user pointer and the device into the struct before the
 * NULL check); the back-buffer-desc + caps copy are gated on dev.
 * In practice the bootstrap call site always passes a live device, so
 * the NULL branch is paranoia parity with the original.
 */

#include "layers.h"
#include <stddef.h>
#include <string.h>

render_layer_t g_layers_a[LAYERS_A_COUNT];
render_layer_t g_layers_b[LAYERS_B_COUNT];

/* Trust-but-verify: the offsets above are derived from the unpacked exe
 * (see docs/findings/winmain-and-bootstrap.md §"render layers"). If
 * mingw's d3d8.h ever drifts on D3DCAPS8 / D3DSURFACE_DESC sizing, these
 * assertions fail at build time and we revisit the layout. */
_Static_assert(offsetof(render_layer_t, device)          == 0x108, "device offset");
_Static_assert(offsetof(render_layer_t, backbuffer_desc) == 0x10c, "bb desc offset");
_Static_assert(offsetof(render_layer_t, caps)            == 0x12c, "caps offset");
_Static_assert(offsetof(render_layer_t, slot_200)        == 0x200, "slot_200 offset");
_Static_assert(sizeof(render_layer_t)                    == 0x2f0, "render_layer size");

/* FUN_004038e4 — thiscall in the original; plain function here. */
static void layer_init_one(render_layer_t *layer,
                           void *user_ptr,
                           IDirect3DDevice8 *dev,
                           const D3DCAPS8 *caps)
{
    layer->slot_200 = user_ptr;
    layer->device   = dev;
    if (!dev) return;

    IDirect3DSurface8 *bb = NULL;
    IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    IDirect3DSurface8_GetDesc(bb, &layer->backbuffer_desc);
    IDirect3DSurface8_Release(bb);

    memcpy(&layer->caps, caps, sizeof(D3DCAPS8));
}

void layers_init(IDirect3D8 *d3d, IDirect3DDevice8 *dev)
{
    D3DCAPS8 caps = {0};
    IDirect3D8_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);

    /* Same order as the original: 20-element loop first, then the 4
     * unrolled calls. The user_ptr arg is always 0 here. */
    for (size_t i = 0; i < LAYERS_B_COUNT; i++) {
        layer_init_one(&g_layers_b[i], NULL, dev, &caps);
    }
    for (size_t i = 0; i < LAYERS_A_COUNT; i++) {
        layer_init_one(&g_layers_a[i], NULL, dev, &caps);
    }
}
