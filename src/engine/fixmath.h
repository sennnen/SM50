/* Fixed-point maths. The fx-CG50 CPU (SH4AL-DSP) has no FPU, so the engine
 * uses integers everywhere: angles are u16 (65536 = full turn), unit vectors
 * and rotation matrices are Q14, world positions are Q8 (1/256 unit). */
#ifndef SM50_FIXMATH_H
#define SM50_FIXMATH_H
#include "types.h"

#define ONE14 16384
#define RECIP_TABLE_SIZE 16385
extern const s16 gSinTable[4096 + 1024];
extern const u16 gAtanTable[1025];
extern const u32 gRecipTable[RECIP_TABLE_SIZE];

static inline s32 fsin(u16 a) { return gSinTable[a >> 4]; }
static inline s32 fcos(u16 a) { return gSinTable[(a >> 4) + 1024]; }
static inline s32 mul14(s32 a, s32 b) { return (s32)(((s64)a * b) >> 14); }

u16 fatan2(s32 y, s32 x);           /* angle of vector (x, y) */
u32 isqrt32(u32 x);
u32 isqrt64(u64 x);
s32 approach_s32(s32 cur, s32 target, s32 inc, s32 dec);
s16 approach_angle(s16 cur, s16 target, s16 step);

typedef struct { s32 x, y, z; } Vec3i;

/* 3x4 affine matrix. m[r][0..2] are Q14 (rotation/scale), m[r][3] is the
 * translation in world units (integer). Row r produces output coordinate r. */
typedef struct { s32 m[3][4]; } Mat34;

void mat_identity(Mat34 *m);
void mat_mul(Mat34 *dst, const Mat34 *a, const Mat34 *b); /* dst = a * b */
/* Build T * Ry(yaw) * Rx(pitch) * Rz(roll) * S */
void mat_from_trs(Mat34 *m, s32 tx, s32 ty, s32 tz, u16 rx, u16 ry, u16 rz, s32 scale14);
/* SM64-style Euler order used by animation channels: R = Ry * Rx * Rz */
void mat_rotate_xyz(Mat34 *m, u16 rx, u16 ry, u16 rz);
void mat_lookat(Mat34 *m, Vec3i eye, Vec3i target);
void mat_transform(const Mat34 *m, s32 x, s32 y, s32 z, s32 *ox, s32 *oy, s32 *oz);
#endif
