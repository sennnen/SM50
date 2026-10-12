/* Endian-safe Q12 conversion bridge for the PS1 SM64 fixed-point code. */
#include <engine/math_util.h>

typedef union {
    f32 f;
    u32 bits;
} FloatBits;

f32 qtof(q32 value) {
    if (value == 0) return 0.0f;
    u32 magnitude = value < 0 ? 0u - (u32)value : (u32)value;
    u32 highest_bit = 31u - (u32)__builtin_clz(magnitude);
    u32 significand = highest_bit <= 23u
        ? magnitude << (23u - highest_bit)
        : magnitude >> (highest_bit - 23u);
    u32 exponent = 127u + highest_bit - FRACT_BITS;
    FloatBits result = { .bits = ((u32)(value < 0) << 31)
        | (exponent << 23) | (significand & 0x7fffffu) };
    return result.f;
}

q32 ftoq(f32 value) {
    FloatBits input = { .f = value };
    u32 exponent = (input.bits >> 23) & 0xffu;
    if (exponent == 0) return 0;
    s32 shift = 150 - FRACT_BITS - (s32)exponent;
    if (shift >= 32) return 0;
    if (shift < -8) return (q32)0x80000000u;
    u64 magnitude = (input.bits & 0x7fffffu) | 0x800000u;
    magnitude = shift >= 0 ? magnitude >> shift : magnitude << -shift;
    if (magnitude > 0x7fffffffu) return (q32)0x80000000u;
    return (input.bits >> 31) ? -(s32)magnitude : (s32)magnitude;
}
