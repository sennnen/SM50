#include "fixmath.h"

u16 fatan2(s32 y, s32 x) {
    if (x == 0 && y == 0) return 0;
    s32 ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    u16 a;
    if (ax >= ay) a = gAtanTable[(u32)(((s64)ay << 10) / ax)];
    else a = 0x4000 - gAtanTable[(u32)(((s64)ax << 10) / ay)];
    if (x < 0) a = 0x8000 - a;
    if (y < 0) a = (u16)(-(s32)a);
    return a;
}

u32 isqrt32(u32 x) {
    u32 r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

u32 isqrt64(u64 x) {
    if (x < 0xffffffffull) return isqrt32((u32)x);
    u64 r = 0, b = 1ull << 62;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (u32)r;
}

s32 approach_s32(s32 cur, s32 target, s32 inc, s32 dec) {
    if (cur < target) { cur += inc; if (cur > target) cur = target; }
    else { cur -= dec; if (cur < target) cur = target; }
    return cur;
}

s16 approach_angle(s16 cur, s16 target, s16 step) {
    s16 d = (s16)(target - cur);
    if (d > step) d = step;
    else if (d < -step) d = -step;
    return (s16)(cur + d);
}

void mat_identity(Mat34 *m) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) m->m[r][c] = (r == c) ? ONE14 : 0;
}

void mat_mul(Mat34 *dst, const Mat34 *a, const Mat34 *b) {
    Mat34 t;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++)
            t.m[r][c] = (s32)(((s64)a->m[r][0] * b->m[0][c] + (s64)a->m[r][1] * b->m[1][c] +
                               (s64)a->m[r][2] * b->m[2][c]) >> 14);
        t.m[r][3] = (s32)(((s64)a->m[r][0] * b->m[0][3] + (s64)a->m[r][1] * b->m[1][3] +
                           (s64)a->m[r][2] * b->m[2][3]) >> 14) + a->m[r][3];
    }
    *dst = t;
}

void mat_rotate_xyz(Mat34 *m, u16 rx, u16 ry, u16 rz) {
    s32 sx = fsin(rx), cx = fcos(rx), sy = fsin(ry), cy = fcos(ry), sz = fsin(rz), cz = fcos(rz);
    /* R = Ry * Rx * Rz */
    m->m[0][0] = mul14(cy, cz) + mul14(mul14(sy, sx), sz);
    m->m[0][1] = -mul14(cy, sz) + mul14(mul14(sy, sx), cz);
    m->m[0][2] = mul14(sy, cx);
    m->m[1][0] = mul14(cx, sz);
    m->m[1][1] = mul14(cx, cz);
    m->m[1][2] = -sx;
    m->m[2][0] = -mul14(sy, cz) + mul14(mul14(cy, sx), sz);
    m->m[2][1] = mul14(sy, sz) + mul14(mul14(cy, sx), cz);
    m->m[2][2] = mul14(cy, cx);
    m->m[0][3] = m->m[1][3] = m->m[2][3] = 0;
}

void mat_from_trs(Mat34 *m, s32 tx, s32 ty, s32 tz, u16 rx, u16 ry, u16 rz, s32 scale14) {
    mat_rotate_xyz(m, rx, ry, rz);
    if (scale14 != ONE14)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) m->m[r][c] = mul14(m->m[r][c], scale14);
    m->m[0][3] = tx; m->m[1][3] = ty; m->m[2][3] = tz;
}

/* View matrix: camera looks along +Z in view space, +Y up, +X right.
 * The world is right-handed (Y up), so right = forward x up. */
void mat_lookat(Mat34 *m, Vec3i eye, Vec3i target) {
    s32 fx = target.x - eye.x, fy = target.y - eye.y, fz = target.z - eye.z;
    u32 len = isqrt64((u64)((s64)fx * fx + (s64)fy * fy + (s64)fz * fz));
    if (len == 0) { mat_identity(m); return; }
    fx = (s32)(((s64)fx * 16384) / len); fy = (s32)(((s64)fy * 16384) / len); fz = (s32)(((s64)fz * 16384) / len);
    s32 rx = -fz, rz = fx;
    u32 rl = isqrt32((u32)(rx * rx + rz * rz));
    if (rl == 0) { rx = ONE14; rz = 0; } else { rx = rx * 16384 / (s32)rl; rz = rz * 16384 / (s32)rl; }
    s32 ux = -mul14(rz, fy), uy = mul14(rz, fx) - mul14(rx, fz), uz = mul14(rx, fy);
    m->m[0][0] = rx; m->m[0][1] = 0;  m->m[0][2] = rz;
    m->m[1][0] = ux; m->m[1][1] = uy; m->m[1][2] = uz;
    m->m[2][0] = fx; m->m[2][1] = fy; m->m[2][2] = fz;
    for (int r = 0; r < 3; r++)
        m->m[r][3] = -(s32)(((s64)m->m[r][0] * eye.x + (s64)m->m[r][1] * eye.y + (s64)m->m[r][2] * eye.z) >> 14);
}

void mat_transform(const Mat34 *m, s32 x, s32 y, s32 z, s32 *ox, s32 *oy, s32 *oz) {
    *ox = ((m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z) >> 14) + m->m[0][3];
    *oy = ((m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z) >> 14) + m->m[1][3];
    *oz = ((m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z) >> 14) + m->m[2][3];
}
