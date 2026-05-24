#include "math3d.h"

#include <math.h>
#include <string.h>

void vec3_normalize(float out[3], const float in[3])
{
    float x = in[0], y = in[1], z = in[2];
    float len = sqrtf(x * x + y * y + z * z);
    out[0] = x / len;
    out[1] = y / len;
    out[2] = z / len;
}

void mat4_lookat_rh(float out[16], const float eye[3],
                    const float target[3], const float up[3])
{
    float z[3] = {
        eye[0] - target[0],
        eye[1] - target[1],
        eye[2] - target[2],
    };
    vec3_normalize(z, z);

    float x[3] = {
        up[1] * z[2] - up[2] * z[1],
        up[2] * z[0] - up[0] * z[2],
        up[0] * z[1] - up[1] * z[0],
    };
    vec3_normalize(x, x);

    float y[3] = {
        z[1] * x[2] - z[2] * x[1],
        z[2] * x[0] - z[0] * x[2],
        z[0] * x[1] - z[1] * x[0],
    };

    /* Engine writes the translation as -(axis.z*eye.z + axis.y*eye.y +
     * axis.x*eye.x) — i.e. negated dot product, summed in z/y/x order.
     * We match that summation order so the (rare) lossy float case lines
     * up. */
    out[ 0] = x[0]; out[ 1] = y[0]; out[ 2] = z[0]; out[ 3] = 0.0f;
    out[ 4] = x[1]; out[ 5] = y[1]; out[ 6] = z[1]; out[ 7] = 0.0f;
    out[ 8] = x[2]; out[ 9] = y[2]; out[10] = z[2]; out[11] = 0.0f;
    out[12] = -(x[2] * eye[2] + x[1] * eye[1] + x[0] * eye[0]);
    out[13] = -(y[2] * eye[2] + y[1] * eye[1] + y[0] * eye[0]);
    out[14] = -(z[2] * eye[2] + z[1] * eye[1] + z[0] * eye[0]);
    out[15] = 1.0f;
}

void mat4_perspective_fov_rh(float out[16], float fov_y, float aspect,
                             float z_near, float z_far)
{
    float h = cosf(fov_y * 0.5f) / sinf(fov_y * 0.5f);   /* cot(fov/2) */
    float w = h / aspect;
    float z_range = z_far / (z_near - z_far);

    out[ 0] = w;    out[ 1] = 0.0f; out[ 2] = 0.0f;    out[ 3] = 0.0f;
    out[ 4] = 0.0f; out[ 5] = h;    out[ 6] = 0.0f;    out[ 7] = 0.0f;
    out[ 8] = 0.0f; out[ 9] = 0.0f; out[10] = z_range; out[11] = -1.0f;
    out[12] = 0.0f; out[13] = 0.0f; out[14] = z_range * z_near;
    out[15] = 0.0f;
}

void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i * 4 + j] =
                a[i * 4 + 0] * b[0 * 4 + j] +
                a[i * 4 + 1] * b[1 * 4 + j] +
                a[i * 4 + 2] * b[2 * 4 + j] +
                a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    memcpy(out, tmp, sizeof tmp);
}

void mat4_identity(float out[16])
{
    static const float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    memcpy(out, ident, sizeof ident);
}

void mat4_translation(float out[16], float tx, float ty, float tz)
{
    mat4_identity(out);
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
}

void mat4_scaling(float out[16], float sx, float sy, float sz)
{
    mat4_identity(out);
    out[ 0] = sx;
    out[ 5] = sy;
    out[10] = sz;
}

void mat4_rotation_x(float out[16], float radians)
{
    float c = cosf(radians), s = sinf(radians);
    mat4_identity(out);
    out[ 5] =  c;  out[ 6] =  s;
    out[ 9] = -s;  out[10] =  c;
}

void mat4_rotation_y(float out[16], float radians)
{
    float c = cosf(radians), s = sinf(radians);
    mat4_identity(out);
    out[ 0] =  c;  out[ 2] = -s;
    out[ 8] =  s;  out[10] =  c;
}

void mat4_rotation_z(float out[16], float radians)
{
    float c = cosf(radians), s = sinf(radians);
    mat4_identity(out);
    out[ 0] =  c;  out[ 1] =  s;
    out[ 4] = -s;  out[ 5] =  c;
}

int mat4_inverse(float out[16], const float in[16])
{
    /* Cofactor-based inverse.  Computes the adjugate row-by-row using the
     * 12 unique 2×2 minors of the bottom two rows (and symmetric pairings
     * for the other rows), divides by the determinant, then writes to out.
     * Operates on a local buffer so `out == in` is safe.
     *
     * Same formula as D3DXMatrixInverse's portable backend (the engine
     * dispatches through _DAT_005fda90 to one of three SIMD variants; the
     * algebraic output is identical).  */

    const float *m = in;
    float inv[16];

    inv[ 0] =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
             + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[ 4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
             - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[ 8] =  m[4]*m[ 9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
             + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[ 9];
    inv[12] = -m[4]*m[ 9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
             - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[ 9];

    inv[ 1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
             - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[ 5] =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
             + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[ 9] = -m[0]*m[ 9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
             - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[ 9];
    inv[13] =  m[0]*m[ 9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
             + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[ 9];

    inv[ 2] =  m[1]*m[ 6]*m[15] - m[1]*m[ 7]*m[14] - m[5]*m[2]*m[15]
             + m[5]*m[3]*m[14] + m[13]*m[2]*m[ 7] - m[13]*m[3]*m[ 6];
    inv[ 6] = -m[0]*m[ 6]*m[15] + m[0]*m[ 7]*m[14] + m[4]*m[2]*m[15]
             - m[4]*m[3]*m[14] - m[12]*m[2]*m[ 7] + m[12]*m[3]*m[ 6];
    inv[10] =  m[0]*m[ 5]*m[15] - m[0]*m[ 7]*m[13] - m[4]*m[1]*m[15]
             + m[4]*m[3]*m[13] + m[12]*m[1]*m[ 7] - m[12]*m[3]*m[ 5];
    inv[14] = -m[0]*m[ 5]*m[14] + m[0]*m[ 6]*m[13] + m[4]*m[1]*m[14]
             - m[4]*m[2]*m[13] - m[12]*m[1]*m[ 6] + m[12]*m[2]*m[ 5];

    inv[ 3] = -m[1]*m[ 6]*m[11] + m[1]*m[ 7]*m[10] + m[5]*m[2]*m[11]
             - m[5]*m[3]*m[10] - m[ 9]*m[2]*m[ 7] + m[ 9]*m[3]*m[ 6];
    inv[ 7] =  m[0]*m[ 6]*m[11] - m[0]*m[ 7]*m[10] - m[4]*m[2]*m[11]
             + m[4]*m[3]*m[10] + m[ 8]*m[2]*m[ 7] - m[ 8]*m[3]*m[ 6];
    inv[11] = -m[0]*m[ 5]*m[11] + m[0]*m[ 7]*m[ 9] + m[4]*m[1]*m[11]
             - m[4]*m[3]*m[ 9] - m[ 8]*m[1]*m[ 7] + m[ 8]*m[3]*m[ 5];
    inv[15] =  m[0]*m[ 5]*m[10] - m[0]*m[ 6]*m[ 9] - m[4]*m[1]*m[10]
             + m[4]*m[2]*m[ 9] + m[ 8]*m[1]*m[ 6] - m[ 8]*m[2]*m[ 5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];

    if (det >= -1e-7f && det <= 1e-7f) return 1;

    float inv_det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * inv_det;
    return 0;
}
