/*
 * test_scene1_render.c — world→screen projection (FUN_00490c78 → FUN_00490d29).
 *
 * Exercises the pure scene1_project_world_mat: the perspective math, the
 * 320−fx·vx/vz / 240+fy·vy/vz screen mapping, and — crucially — that the view
 * transform is an affine COORD transform (includes the camera translation in
 * matrix row 3), not a NORMAL transform.  That sign/translation behaviour is
 * what the C3b item tooltip's bubble position depends on.
 */
#include "t.h"

#include <math.h>

#include "scene1_render.h"
#include "math3d.h"

static int near_(float a, float b, float tol) { return fabsf(a - b) <= tol; }
#define NEAR(a, b, tol) do { \
    if (!near_((a), (b), (tol))) \
        T_FAIL("expected %s≈%g, got %g", #a, (double)(b), (double)(a)); \
} while (0)

static const float IDENT[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

/* A symmetric RH perspective with proj[0]=proj[5]=1 (fov 90°, aspect 1):
 * fx = 1·320 = 320, fy = 1·240 = 240. */
static void proj90(float out[16])
{
    mat4_perspective_fov_rh(out, 1.5707963f /* π/2 */, 1.0f, 1.0f, 100.0f);
}

/* Centred point in front of the camera (RH: -z forward) → screen centre. */
int test_project_world_centered_point(void)
{
    float proj[16];
    proj90(proj);
    float sx, sy, vz;
    scene1_project_world_mat(IDENT, proj, 0.0f, 0.0f, -5.0f, &sx, &sy, &vz);
    NEAR(sx, 320.0f, 1e-3f);
    NEAR(sy, 240.0f, 1e-3f);
    NEAR(vz, -5.0f, 1e-3f);   /* view-space depth = raw z under identity view */
    return 0;
}

/* Off-centre points pin the sign of both axes: with proj[0]=proj[5]=1 at z=-5,
 * a +1 world-x lands at 384 (right of centre), +1 world-y lands at 192 (above
 * centre — screen y grows downward, the 240+fy·vy/vz term is negative for
 * negative vz). */
int test_project_world_offcenter_signs(void)
{
    float proj[16];
    proj90(proj);
    float sx, sy;
    scene1_project_world_mat(IDENT, proj, 1.0f, 0.0f, -5.0f, &sx, &sy, NULL);
    NEAR(sx, 384.0f, 1e-3f);   /* 320 - 320·1/(-5) = 320 + 64 */
    NEAR(sy, 240.0f, 1e-3f);
    scene1_project_world_mat(IDENT, proj, 0.0f, 1.0f, -5.0f, &sx, &sy, NULL);
    NEAR(sx, 320.0f, 1e-3f);
    NEAR(sy, 192.0f, 1e-3f);   /* 240 + 240·1/(-5) = 240 - 48 */
    return 0;
}

/* The view transform MUST include the row-3 translation (COORD, not NORMAL).
 * Camera pushed -10 in z + a +x point: only the translated (vz=-5) reading
 * gives the in-front projection (sx=448); a translation-dropping NORMAL
 * transform would read vz=+5 and mirror it to 192. */
int test_project_world_view_translation(void)
{
    float proj[16];
    proj90(proj);
    /* identity rotation, translate (3, 0, -10) in row 3 (D3D row-vector). */
    float view[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        3, 0, -10, 1,
    };
    float sx, sy, vz;
    /* world (2,0,5): vx = 2 + 3 = 5, vz = 5 - 10 = -5. */
    scene1_project_world_mat(view, proj, 2.0f, 0.0f, 5.0f, &sx, &sy, &vz);
    NEAR(vz, -5.0f, 1e-3f);
    NEAR(sx, 320.0f - 320.0f * 5.0f / -5.0f, 1e-3f);   /* = 640 */
    NEAR(sy, 240.0f, 1e-3f);
    return 0;
}

/* All three out-pointers are optional. */
int test_project_world_null_outputs(void)
{
    float proj[16];
    proj90(proj);
    float sy;
    scene1_project_world_mat(IDENT, proj, 1.0f, 1.0f, -5.0f, NULL, &sy, NULL);
    NEAR(sy, 192.0f, 1e-3f);
    return 0;
}
