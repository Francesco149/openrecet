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
