/* P1b — more offline-oracle targets: retail vs port, 3 x87 PC modes.
 *   boss_id_allowed  FUN_00431990 cdecl(int)            vs stage_gate_boss_id_allowed
 *   floor_checkpoint FUN_0043195d () reads 0x438b4c8/cc vs stage_gate_floor_is_checkpoint
 *   audio_fade       FUN_00499583 () slider 0x56e5778, fake IDirectMusicAudioPath @0x9643108
 *                    (vtable slot 5 SetVolume records lVolume)  vs audio_fade_compute(slider,0) */
#include "retail_map.h"
#include <float.h>
#include "stage_gate.h"
#include "audio_fade.h"
RETAIL_HOLE_DEFINE
#define G32(va) (*(volatile int32_t *)(uintptr_t)(va))
extern int32_t g_scene1_combat_stage_id;

static int32_t g_vol_seen; static int g_vol_calls;
static HRESULT __stdcall fake_setvolume(void *self, LONG vol, DWORD dur) { (void)self; (void)dur; g_vol_seen = vol; g_vol_calls++; return 0; }
static HRESULT __stdcall fake_trap(void *self) { (void)self; fprintf(stderr, "unexpected COM slot\n"); return 0x80004001; }
static void *g_fake_vtbl[8]; static void *g_fake_obj[1] = { g_fake_vtbl };

int main(int argc, char **argv)
{
    retail_image_t img;
    if (retail_map(argc > 1 ? argv[1] : "recettear.unpacked.exe", &img, 1)) return 1;
    for (int i = 0; i < 8; i++) g_fake_vtbl[i] = (void *)fake_trap;
    g_fake_vtbl[5] = (void *)fake_setvolume;
    unsigned cw_modes[] = {_PC_53, _PC_24, _PC_64}; const char *cwn[] = {"PC53", "PC24", "PC64"};
    for (int m = 0; m < 3; m++) {
        unsigned old; _controlfp_s(&old, cw_modes[m], _MCW_PC);
        long n = 0, mis = 0;
        for (int32_t id = -2000; id <= 2000; id++) {
            int r = ((int (__cdecl *)(int32_t))0x431990)(id), p = stage_gate_boss_id_allowed(id);
            n++; if (r != p && mis++ < 3) printf("  boss_id mismatch id=%d retail=%d port=%d\n", id, r, p);
        }
        printf("boss_id_allowed  [%s]: %ld/%ld\n", cwn[m], n - mis, n);
        n = mis = 0;
        for (int32_t d = -3; d <= 12; d++) for (int32_t f = -50; f <= 60; f++) {
            G32(0x438b4c8) = d; G32(0x438b4cc) = f;
            g_scene1_combat_stage_id = d; stage_gate_set_next(f);
            int r = ((int (__cdecl *)(void))0x43195d)(), p = stage_gate_floor_is_checkpoint();
            n++; if (r != p && mis++ < 3) printf("  checkpoint mismatch d=%d f=%d retail=%d port=%d\n", d, f, r, p);
        }
        printf("floor_checkpoint [%s]: %ld/%ld\n", cwn[m], n - mis, n);
        n = mis = 0;
        *(float *)0x5d1580 = 1.0f;                     /* full target volume */
        *(void **)0x9643108 = g_fake_obj; G32(0x438ccb4) = 0;
        for (int32_t s = 0; s <= 9; s++) {
            G32(0x56e5778) = s; g_vol_calls = 0;
            ((void (__cdecl *)(void))0x499583)();
            int32_t p = audio_fade_compute(s, 0);
            n++;
            if (g_vol_calls != 1 || g_vol_seen != p) { mis++; printf("  [%s] fade slider=%d retail=%d (calls=%d) port=%d\n", cwn[m], s, g_vol_seen, g_vol_calls, p); }
        }
        printf("audio_fade       [%s]: %ld/%ld exact\n", cwn[m], n - mis, n);
        _controlfp_s(&old, _PC_53, _MCW_PC);
    }
    return 0;
}
