#ifndef SM50_TYPES_H
#define SM50_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef int8_t s8;   typedef uint8_t u8;
typedef int16_t s16; typedef uint16_t u16;
typedef int32_t s32; typedef uint32_t u32;
typedef int64_t s64; typedef uint64_t u64;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* Big-endian data access (asset data is stored big-endian, native on SuperH). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BE16(p) (*(const u16 *)(p))
#define BE32(p) (*(const u32 *)(p))
#else
#define BE16(p) ((u16)((((const u8 *)(p))[0] << 8) | ((const u8 *)(p))[1]))
#define BE32(p) ((u32)(((u32)((const u8 *)(p))[0] << 24) | ((u32)((const u8 *)(p))[1] << 16) | \
                       (((const u8 *)(p))[2] << 8) | ((const u8 *)(p))[3]))
#endif
#define BES16(p) ((s16)BE16(p))

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#endif
