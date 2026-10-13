/*
 * math3d.h — minimal vec3/mat4 helpers backing the engine's D3DX usage.
 *
 * The engine's D3DX8 math dispatches through a pointer table (0x5fda48) that
 * D3DXCpuOptimizations (FUN_004cdd9f) fills on the first math call; on every
 * SSE2 CPU without 3DNow! (all Intel, AMD since Bulldozer) that is the SSE2
 * table.  math3d.c mirrors the dispatched variants bit-exactly (SSE parts via
 * intrinsics, x87 parts via x87.h): see math3d.c header and
 * docs/findings/x87-float-semantics.md "D3DX8 math".  rsqrtss/rcpss results are
 * CPU-specific, so retail (and the port) differ between Intel and AMD in
 * vec3_normalize / mat4_lookat_rh / mat4_inverse / mat4_shadow.
 *
 * Storage convention: row-major 4×4 matrices in a flat float[16].
 *   m[i*4 + j] is row i, column j.
 *
 * Matches D3DXMATRIX semantics for `world_view_proj = world * view * proj`
 * applied to a row-vector via `v' = v * M`. Both helpers below produce
 * right-handed matrices (zaxis = normalize(eye - target)).
 */

#ifndef OPENRECET_MATH3D_H
#define OPENRECET_MATH3D_H

/* D3DXVec3Normalize (slot 7 → SSE2 0x4ecb60: rsqrtss + 1 Newton step).
 * in/out may alias.  |v|² < 2^-46 → (0,0,0) (no NaN for a zero vector). */
void vec3_normalize(float out[3], const float in[3]);

/* D3DXMatrixLookAtRH(out, eye, target, up) @0x4a3b52 (x87; normalizes via
 * the dispatched D3DXVec3Normalize).
 *   zaxis = normalize(eye - target)
 *   xaxis = normalize(up × zaxis)
 *   yaxis = zaxis × xaxis
 * Stored with axes in the first three columns; translation in row 3
 * (m[12..14]) so that row-vector × M places eye at the origin.
 * eye == target (the boot call): normalize gives (0,0,0) → all-zero matrix
 * with m[15] = 1 (translation −0), finite — not NaN. */
void mat4_lookat_rh(float out[16], const float eye[3],
                    const float target[3], const float up[3]);

/* D3DXMatrixPerspectiveFovRH(out, fov_y, aspect, z_near, z_far) @0x4a3ee8
 * (x87 fsincos). Result has m[2*4+3] = -1 (right-handed). */
void mat4_perspective_fov_rh(float out[16], float fov_y, float aspect,
                             float z_near, float z_far);

/* Row-major matrix multiply: out = a * b (D3DXMatrixMultiply, slot 3 → SSE2
 * 0x4eca10).  Safe with out==a or out==b (all loads precede the stores). */
void mat4_mul(float out[16], const float a[16], const float b[16]);

/* Identity matrix. Matches D3DXMatrixIdentity. */
void mat4_identity(float out[16]);

/* Translation matrix.  Matches D3DXMatrixTranslation (slot 26, c_ 0x4a34b6).
 * Translation goes in row 3 (m[12..14]) so row-vector × M shifts by (tx, ty, tz). */
void mat4_translation(float out[16], float tx, float ty, float tz);

/* Scaling matrix.  Matches D3DXMatrixScaling (slot 33, c_ 0x4a3406).
 * Diagonal (sx, sy, sz, 1). */
void mat4_scaling(float out[16], float sx, float sy, float sz);

/* Axis rotations: D3DXMatrixRotationX/Y (slots 30/31 → SSE2 0x4ed280/0x4ed300,
 * polynomial sse2_sincos) and RotationZ (slot 32, c_ 0x4a3692, x87 fsincos).
 * Right-handed (positive angle rotates +Y toward +Z for X-axis, etc.). */
void mat4_rotation_x(float out[16], float radians);
void mat4_rotation_y(float out[16], float radians);
void mat4_rotation_z(float out[16], float radians);

/* D3DXPlaneFromPointNormal(pOut, pPoint, pNormal).  pOut = (n.x, n.y, n.z,
 * -dot(point, normal)) — the plane through `point` with normal `normal`.
 * NOT normalised (the engine normalises later, inside mat4_shadow).  Slot 12,
 * c_ @ 0x4a4f65 (the SSE2 table keeps it). */
void plane_from_point_normal(float out[4], const float point[3],
                             const float normal[3]);

/* D3DXMatrixShadow(pOut, pLight, pPlane), slot 27 → SSE2 0x4ed550.  Builds the
 * row-major projection matrix that flattens geometry onto `plane` along the
 * light direction `light` (light[3] == 0 → directional, == 1 → point).  The
 * plane is normalised inline (rsqrt; a zero normal leaves P = 0):
 *   dot = P·L (4-component);  out._ii = dot - L_i*P_i, off-diagonal -L_j*P_i. */
void mat4_shadow(float out[16], const float light[4], const float plane[4]);

/* D3DXMatrixInverse(out, NULL, in), slot 18 → SSE2 0x4ecdb0 (Cramer, rcpss +
 * 1 Newton step).  Returns 0 on success; non-zero iff retail returns NULL
 * (det == 0 or NaN), in which case `out` is left untouched.  Safe with
 * out == in (all loads precede the stores). */
int mat4_inverse(float out[16], const float in[16]);

#endif /* OPENRECET_MATH3D_H */
