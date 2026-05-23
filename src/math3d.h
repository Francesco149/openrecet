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

#endif /* OPENRECET_MATH3D_H */
