/* P1 — offline oracle smoke test: retail image mapped in-process, retail
 * functions called directly and compared against the openrecet port's C.
 *   rng_next15     (port src/rng.c)            vs retail FUN_005041f6
 *   haggle_decide  (port src/customer_haggle.c) vs retail FUN_00460672
 *     retail reads globals: accept_ref @0x730b588, player_ask @0x5c6bb8. */
#include "retail_map.h"
#include <float.h>
#include "rng.h"
#include "customer_haggle.h"

RETAIL_HOLE_DEFINE
#define G32(va) (*(volatile int32_t *)(uintptr_t)(va))

int main(int argc, char **argv)
{
    retail_image_t img;
    printf("hole %p..%p\n", g_retail_hole, g_retail_hole + sizeof g_retail_hole);
    const char *exe = argc > 1 ? argv[1] : "recettear.unpacked.exe";
    if (retail_map(exe, &img, 1)) return 1;
    printf("mapped %s @%p size %08x imports resolved=%d stubbed=%d\n",
           exe, img.base, img.size, img.n_imports_resolved, img.n_imports_stubbed);

    /* rng: 2M steps from several seeds */
    long rng_mis = 0, rng_n = 0;
    uint32_t seeds[] = {1, 19937, 0xdeadbeef, 0x7fffffff};
    for (int s = 0; s < 4; s++) {
        G32(0x6023a0) = (int32_t)seeds[s];
        rng_seed(seeds[s]);
        for (int i = 0; i < 500000; i++, rng_n++) {
            uint32_t r = retail_call0(0x5041f6) & 0xffff;
            uint32_t p = rng_next15();
            if (r != p || (uint32_t)G32(0x6023a0) != g_rng_seed) { if (rng_mis++ < 3) printf("rng mismatch s=%u i=%d r=%u p=%u\n", seeds[s], i, r, p); }
        }
    }
    printf("rng_next15     : %ld/%ld match\n", rng_n - rng_mis, rng_n);

    /* haggle_decide: grid over accept_ref x ask. Two FPU precision modes:
     * host default and 24-bit (D3D8 CreateDevice w/o FPU_PRESERVE). */
    unsigned int cw_modes[] = {_PC_53, _PC_24, _PC_64};
    const char *cw_names[] = {"PC53", "PC24", "PC64"};
    for (int m = 0; m < 3; m++) {
        unsigned int old; _controlfp_s(&old, cw_modes[m], _MCW_PC);
        long n = 0, mis = 0;
        for (int32_t ref = 0; ref < 2000000; ref = ref < 2000 ? ref + 1 : ref + 997) {
            for (int d = -60; d <= 60; d++) {
                int32_t ask = ref + d * (ref / 100 + 1);
                G32(0x730b588) = ref; G32(0x5c6bb8) = ask;
                int32_t r = (int32_t)retail_call0(0x460672);
                int32_t p = haggle_decide(ask, ref);
                n++;
                if (r != p) { if (mis++ < 5) printf("  [%s] haggle mismatch ref=%d ask=%d retail=%d port=%d\n", cw_names[m], ref, ask, r, p); }
            }
        }
        printf("haggle_decide  [%s]: %ld/%ld match\n", cw_names[m], n - mis, n);
        _controlfp_s(&old, _PC_53, _MCW_PC);
    }
    return 0;
}
