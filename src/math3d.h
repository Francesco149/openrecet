/*
 * math3d.h — minimal vec3/mat4 helpers backing the engine's D3DX usage.
 *
 * The engine pulls in D3DX8/9-style matrix utilities through an indirect
 * dispatch (FUN_004cdd9f picks an x87 / MMX / SSE backend at startup and
 * stores callable pointers in _DAT_005fda4c..). For our purposes a single
 * portable implementation is enough — the algebraic outputs are what
 * matter, not the SIMD path.
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

/* in/out may alias. Caller guarantees in[] is non-zero. */
void vec3_normalize(float out[3], const float in[3]);

/* D3DXMatrixLookAtRH(out, eye, target, up).
 *   zaxis = normalize(eye - target)
 *   xaxis = normalize(up × zaxis)
 *   yaxis = zaxis × xaxis
 * Stored with axes in the first three columns; translation in row 3
 * (m[12..14]) so that row-vector × M places eye at the origin.
 *
 * Matches FUN_004a3b52 at 0x4a3b52.  If eye == target the matrix is
 * mathematically degenerate (zaxis normalises (0,0,0) → ±inf/NaN); we
 * reproduce that faithfully — the engine itself feeds the degenerate
 * inputs at boot and never reads the result. */
void mat4_lookat_rh(float out[16], const float eye[3],
                    const float target[3], const float up[3]);

/* D3DXMatrixPerspectiveFovRH(out, fov_y, aspect, z_near, z_far). Matches
 * FUN_004a3ee8 at 0x4a3ee8. Result has m[2*4+3] = -1 (right-handed). */
void mat4_perspective_fov_rh(float out[16], float fov_y, float aspect,
                             float z_near, float z_far);

/* Row-major matrix multiply: out = a * b. Safe with out==a or out==b
 * (uses an internal temporary). Matches D3DXMatrixMultiply semantics —
 * the engine's thunk_FUN_004a2a03 calls the dispatcher pointer that lands
 * in the same per-element formula. */
void mat4_mul(float out[16], const float a[16], const float b[16]);

/* Identity matrix. Matches D3DXMatrixIdentity. */
void mat4_identity(float out[16]);

/* Translation matrix.  Matches D3DXMatrixTranslation — the engine's
 * thunk_FUN_004a3462 lands here.  Translation goes in row 3
 * (m[12..14]) so row-vector × M shifts by (tx, ty, tz). */
void mat4_translation(float out[16], float tx, float ty, float tz);

/* Scaling matrix.  Matches D3DXMatrixScaling — the engine's
 * thunk_FUN_004a33d2 lands here.  Diagonal (sx, sy, sz, 1). */
void mat4_scaling(float out[16], float sx, float sy, float sz);

/* Axis rotations.  Match D3DXMatrixRotationX/Y/Z — the engine's thunks
 * 35d3 / 3537 / 3670 respectively.  Right-handed (positive angle
 * rotates +Y toward +Z for X-axis, etc.). */
void mat4_rotation_x(float out[16], float radians);
void mat4_rotation_y(float out[16], float radians);
void mat4_rotation_z(float out[16], float radians);

/* D3DXPlaneFromPointNormal(pOut, pPoint, pNormal).  pOut = (n.x, n.y, n.z,
 * -dot(point, normal)) — the plane through `point` with normal `normal`.
 * NOT normalised (the engine normalises later, inside mat4_shadow).  Matches
 * the engine's thunk_FUN_004a4f52 (PSGP slot 12, default impl @ 0x4a4f65). */
void plane_from_point_normal(float out[4], const float point[3],
                             const float normal[3]);

/* D3DXMatrixShadow(pOut, pLight, pPlane).  Builds the row-major projection
 * matrix that flattens geometry onto `plane` along the light direction
 * `light` (light[3] == 0 → directional, == 1 → point).  The plane is
 * normalised internally (all four components scaled by 1/|n|).  Matches the
 * engine's thunk_FUN_004a4454 (PSGP slot 27, default impl @ 0x4a5c86):
 *   dot = P·L (4-component);  out._ii = dot - L_i*P_i, off-diagonal -L_j*P_i. */
void mat4_shadow(float out[16], const float light[4], const float plane[4]);

/* General 4×4 inverse via cofactor expansion.  Matches
 * D3DXMatrixInverse(out, NULL, in) — the engine's thunk_FUN_004a2f35
 * lands here (the second arg is `pDeterminant`, always NULL at the
 * call sites we've seen).  Returns 0 on success, non-zero if the
 * matrix is singular (|det| <= ~1e-7); the caller is responsible for
 * handling singular cases (the engine itself does not check).  Safe
 * with out == in (uses an internal temporary). */
int mat4_inverse(float out[16], const float in[16]);

#endif /* OPENRECET_MATH3D_H */
