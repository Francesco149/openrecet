/*
 * layers.h — engine "render layer" objects + bootstrap init.
 *
 * Mirrors FUN_00454e69 ("init render ok") and its per-layer helper
 * FUN_004038e4. The engine has 24 layer objects total, in two arrays:
 *   - g_layers_a[4]   — original at DAT_073cba20 (unrolled in asm)
 *   - g_layers_b[20]  — original at DAT_073da2f0 (a stride-0x2f0 loop)
 *
 * Each object is 0x2f0 bytes. So far we know three fields (set during
 * init) and one pointer slot that gets zeroed. The rest is unknown
 * filler that later subsystems will reveal — keep the struct sized
 * exactly so neighbors don't shift when fields are named.
 */
#ifndef OPENRECET_LAYERS_H
#define OPENRECET_LAYERS_H

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <d3d8.h>
#include <stdint.h>

typedef struct render_layer {
    uint8_t            _pre[0x108];           /* 0x000..0x108 — TBD */
    IDirect3DDevice8  *device;                /* 0x108        — set by init */
    D3DSURFACE_DESC    backbuffer_desc;       /* 0x10c..0x12c — set by init */
    D3DCAPS8           caps;                  /* 0x12c..0x200 — set by init */
    void              *slot_200;              /* 0x200        — zeroed by init */
    uint8_t            _tail[0x2f0 - 0x204];  /* 0x204..0x2f0 — TBD */
} render_layer_t;

#define LAYERS_A_COUNT  4   /* original: 0x073cba20 + i*0x2f0 */
#define LAYERS_B_COUNT  20  /* original: 0x073da2f0 + i*0x2f0 */

extern render_layer_t g_layers_a[LAYERS_A_COUNT];
extern render_layer_t g_layers_b[LAYERS_B_COUNT];

/* FUN_00454e69 — call once after IDirect3DDevice8 is up.  Queries the
 * device caps from the factory, then stamps device/backbuffer-desc/caps
 * into every layer in both arrays. */
void layers_init(IDirect3D8 *d3d, IDirect3DDevice8 *dev);

#endif /* OPENRECET_LAYERS_H */
