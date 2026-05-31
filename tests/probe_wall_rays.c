/* probe_wall_rays.c — per-ray dump of the radial push at the retail pin.
 * Standalone: builds the room collision mesh from vendor shop_1st.x and logs
 * each of the 20 rays (hit? type, normal.y, push) at the retail equilibrium so
 * we can see why the port over-pushes (oscillates) where retail settles.
 *
 * Build (from tests/):
 *   gcc -I../src -DOPENRECET_ROOT='"/opt/src/openrecet"' -o /tmp/probe \
 *       probe_wall_rays.c ../src/collision_mesh.c ../src/collision_query.c \
 *       ../src/xfile.c -lm && /tmp/probe
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collision_mesh.h"
#include "collision_query.h"
#include "collision_resolve.h"
#include "xfile.h"

#define CR_PI 3.14159265358979323846f

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(sz + 1); fread(b, 1, sz, f); b[sz] = 0; fclose(f);
    if (n) *n = sz; return b;
}

/* Replica of collision_resolve_player's radial push, but logging each ray. */
static void push_dump(const collision_mesh *m, float pos[3], int palette_mode) {
    int nray = 8;
    if (palette_mode == 0 && pos[2] > 0.7f) nray = 20;
    printf("nray=%d  start pos=(%.4f,%.4f,%.4f)\n", nray, pos[0], pos[1], pos[2]);
    float net_x = 0, net_z = 0;
    for (int i = 0; i < nray; i++) {
        float ry = pos[1] + 0.7f;
        if (i > 7) ry = (float)(i / 8 + 1) * 0.08f + pos[1] + 0.1f;
        float ang = (float)i * (2.0f * CR_PI / (float)nray) + CR_PI;
        float dx = sinf(ang);
        float dz = cosf(ang) * 1.05f;
        if (palette_mode == 0 && dz > 0.1f) dz = cosf(ang) * 1.03f;
        float rp[3] = { pos[0], ry, pos[2] };
        float rd[3] = { dx, 0.0f, dz };
        float frac = 0, nrm[3] = {0,0,0}; int type = -1;
        int hit = collision_raycast(m, rp, rd, &frac, nrm, &type);
        int push = 0;
        if (hit) {
            if (type == 1 || type == 2 || type == 7) push = 1;
            else if (type == 0 && fabsf(nrm[1]) < 0.75f) push = 1;
        }
        printf("  ray%2d ang=%6.1f dir=(%+.3f,%+.3f) origin=(%.3f,%.3f,%.3f) "
               "hit=%d frac=%.3f type=%d ny=%+.3f push=%d",
               i, ang * 180.0f / CR_PI, dx, dz, rp[0], ry, rp[2],
               hit, frac, type, hit ? nrm[1] : 0.0f, push);
        if (push) {
            float s = 1.0f - frac;   /* penetration scale (asm 0x483bc3) */
            pos[0] -= s * dx; pos[2] -= s * dz; net_x -= s * dx; net_z -= s * dz;
            printf("  -> pos=(%.4f,%.4f)", pos[0], pos[2]);
        }
        printf("\n");
    }
    printf("NET push = (%+.4f, %+.4f)   end pos=(%.4f,%.4f)\n",
           net_x, net_z, pos[0], pos[2]);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/vendor/original/xfile/shop/shop_1st.x", OPENRECET_ROOT);
    size_t len = 0; char *buf = slurp(path, &len);
    if (!buf) { fprintf(stderr, "missing %s\n", path); return 1; }
    xfile_t *xf = xfile_parse(buf, len, path); free(buf);
    collision_object obj; collision_object_build(&obj, xf, COLLISION_PAD_SMALL);
    xfile_free(xf);
    collision_mesh m; m.objects = &obj; m.object_count = 1;

    /* Retail pin (runs/wall-retail): px=3.1019 pz~2.5, vx=0.1435 holding RIGHT.
     * Show one frame: integrate first (как resolver), then dump the push. */
    float pos[3] = { 3.1019f, 0.0f, 2.5f };
    float vx = 0.1435f;
    printf("=== frame at retail pin (px=3.1019, pz=2.5, vx=%.4f) ===\n", vx);
    pos[0] += vx;  /* integrate (resolver does pos += vel first) */
    printf("after integrate: px=%.4f\n", pos[0]);
    push_dump(&m, pos, 0);
    printf("\nretail net per frame: dpx=-0.1435 (cancels vx), dpz=-0.0422\n");
    collision_object_free(&obj);
    return 0;
}
