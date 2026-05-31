/* probe_house_edges.c — find the floor edge + modeled vertical faces in each
 * cardinal direction from the HOUSE spawn, to compare against retail pins
 * (RIGHT 3.10, LEFT -1.5, DOWN/+z 9.5, UP/-z counter 8.941).
 *
 * Build (from tests/):
 *   gcc -I../src -o /tmp/edges probe_house_edges.c ../src/collision_mesh.c \
 *       ../src/collision_query.c ../src/xfile.c -lm && /tmp/edges
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collision_mesh.h"
#include "collision_query.h"
#include "xfile.h"

#define ROOT "/opt/src/openrecet"
#define XF   "/opt/src/openrecet/vendor/original/xfile/shop/shop_1st.x"
#define HEAD 1.5f

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(sz + 1); if (fread(b, 1, sz, f) != (size_t)sz){} b[sz]=0; fclose(f);
    if (n) *n = sz; return b;
}

int main(void) {
    size_t n; char *txt = slurp(XF, &n);
    xfile_t *xf = xfile_parse(txt, n, XF);
    if (!xf || xf->error[0]) { fprintf(stderr, "parse: %s\n", xf?xf->error:"null"); return 1; }
    collision_object obj; memset(&obj, 0, sizeof obj);
    if (collision_object_build(&obj, xf, COLLISION_PAD_SMALL) != 0) { fprintf(stderr,"build\n"); return 1; }
    collision_mesh m; memset(&m,0,sizeof m);
    m.objects=&obj; m.object_count=1;
    memcpy(m.aabb_min,obj.aabb_min,sizeof m.aabb_min);
    memcpy(m.aabb_max,obj.aabb_max,sizeof m.aabb_max);
    printf("mesh: %d tris  x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
        obj.tri_count, obj.aabb_min[0],obj.aabb_max[0],obj.aabb_min[1],obj.aabb_max[1],
        obj.aabb_min[2],obj.aabb_max[2]);

    const float sx=-0.30f, sz=9.35f;
    struct { const char*name; float dx,dz; float retail; } dir[] = {
        {"RIGHT(+x)",  1, 0,  3.10f},
        {"LEFT(-x)",  -1, 0, -1.50f},
        {"DOWN(+z)",   0, 1,  9.50f},
        {"UP(-z)",     0,-1,  8.941f},
    };
    for (int d=0; d<4; d++) {
        float x=sx, z=sz; collision_hit h;
        float edge_x=x, edge_z=z; int found=0;
        for (float t=0; t<60.0f; t+=0.05f) {
            float qx=sx+dir[d].dx*t, qz=sz+dir[d].dz*t;
            if (collision_query_ground(&m, qx, 0.0f+HEAD, qz, &h)) { edge_x=qx; edge_z=qz; found=1; }
            else break;
        }
        float pin = (dir[d].dx!=0)?edge_x:edge_z;
        printf("%-10s floor-edge at %s=%.3f  (retail pin %.3f, found=%d)\n",
            dir[d].name, (dir[d].dx!=0)?"px":"pz", pin, dir[d].retail, found);
    }

    /* List near-vertical faces (|n.y|<0.5) whose AABB overlaps the spawn row. */
    printf("\nNear-vertical faces near spawn row (|ny|<0.5, AABB overlaps "
           "x[-6,4] z[7,11]):\n");
    int cnt=0;
    for (int i=0;i<obj.tri_count;i++){
        const collision_tri *t=&obj.tris[i];
        if (fabsf(t->n[1])>=0.5f) continue;
        if (t->aabb_max[0]<-6||t->aabb_min[0]>4) continue;
        if (t->aabb_max[2]<7 ||t->aabb_min[2]>11) continue;
        printf("  tri%4d type=%d n=(%+.2f,%+.2f,%+.2f) x[%.2f,%.2f] z[%.2f,%.2f]\n",
            i,t->type,t->n[0],t->n[1],t->n[2],
            t->aabb_min[0],t->aabb_max[0],t->aabb_min[2],t->aabb_max[2]);
        if (++cnt>40){printf("  ...(more)\n");break;}
    }
    if(!cnt) printf("  (none)\n");
    return 0;
}
