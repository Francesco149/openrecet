/*
 * state_va.c — see state_va.h.  Add a row when a new port mirror of a retail
 * global is needed for a frame-by-frame diff; keep the mapping exact (a row is a
 * claim that the port variable IS that engine global, value for value).
 */
#include "state_va.h"

#include <stdlib.h>
#include <string.h>

#include "customer_service.h"
#include "rng.h"
#include "scene.h"
#include "scene1_player_ctrl.h"
#include "sim.h"

static int32_t get_rng(void)   { return (int32_t)g_rng_seed; }
static int32_t get_scene(void) { return g_scene_state; }
static int32_t get_cc08(void)  { return player_ctrl_cc08(); }
static int32_t get_b1cc(void)  { return customer_service_b1cc(); }
static int32_t get_b524(void)  { return customer_service_b524(); }
static int32_t get_gsim(void)  { return (int32_t)g_sim_frame_count; }

typedef struct { uint32_t va; const char *name; int32_t (*get)(void); } state_va_t;
static const state_va_t k_map[] = {
    { 0x6023a0u,  "rng (DAT_006023a0)",               get_rng   },
    { 0x438b1c0u, "scene state (DAT_0438b1c0)",       get_scene },
    { 0x438cc08u, "house mode cc08 (DAT_0438cc08)",   get_cc08  },
    { 0x438b1ccu, "cs load phase b1cc (DAT_0438b1cc)", get_b1cc },
    { 0x730b524u, "cs idle counter b524 (DAT_0730b524)", get_b524 },
    { 0x438b8ccu, "sim frame counter gsim (DAT_0438b8cc)", get_gsim },
};
#define K_N ((int)(sizeof k_map / sizeof k_map[0]))

static const state_va_t *s_sel[16];
static int s_nsel;

int state_va_select(const char *list, uint32_t *bad)
{
    /* no strtok: the caller (main.c parse_cmdline) is mid-strtok over argv */
    const char *t = list ? list : "";
    s_nsel = 0;
    for (; *t; t += (*t == ',')) {
        char *end;
        uint32_t va = (uint32_t)strtoul(t, &end, 16);
        if (end == t) break;
        t = end;
        int k = 0;
        while (k < K_N && k_map[k].va != va) k++;
        if (k == K_N) {
            if (bad) *bad = va;
            s_nsel = 0;
            return -1;
        }
        if (s_nsel < (int)(sizeof s_sel / sizeof s_sel[0]))
            s_sel[s_nsel++] = &k_map[k];
    }
    return s_nsel;
}

void state_va_log(FILE *fp, uint32_t frame)
{
    if (!fp || s_nsel == 0) return;
    fprintf(fp, "post %u", (unsigned)frame);
    for (int i = 0; i < s_nsel; i++)
        fprintf(fp, " %x=%d", (unsigned)s_sel[i]->va, (int)s_sel[i]->get());
    fputc('\n', fp);
}

const char *state_va_known(void)
{
    static char out[512];
    size_t o = 0;
    out[0] = '\0';
    for (int k = 0; k < K_N && o < sizeof out; k++)
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%x %s", k ? ", " : "",
                              (unsigned)k_map[k].va, k_map[k].name);
    return out;
}
