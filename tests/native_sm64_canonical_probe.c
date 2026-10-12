/* Executable probe against the CC0 canonical SM64 decompilation. */
#include <PR/ultratypes.h>
#include "sm64.h"
#include "game/mario.h"

extern s32 sm50_native_rom_floor(s32 values[4]);
extern void update_walking_speed(struct MarioState *m);

u32 mario_update_moving_sand(struct MarioState *m) { (void)m; return 0; }
u32 mario_update_windy_ground(struct MarioState *m) { (void)m; return 0; }
s32 mario_floor_is_slope(struct MarioState *m) { (void)m; return 0; }
s32 mario_get_floor_class(struct MarioState *m) { (void)m; return 0; }

volatile s32 native_sm64_canonical_floor_y_q8;
volatile s32 native_sm64_canonical_floor_ny_q14;
volatile s32 native_sm64_canonical_speed_q12;

int native_sm64_canonical_probe(void) {
    s32 floor_data[4] = { 0 };
    if (!sm50_native_rom_floor(floor_data)) return 0;

    struct Surface floor = { 0 };
    floor.normal.x = floor_data[1] / 16384.0f;
    floor.normal.y = floor_data[2] / 16384.0f;
    floor.normal.z = floor_data[3] / 16384.0f;

    struct MarioState mario = { 0 };
    mario.floor = &floor;
    mario.intendedMag = 32.0f;
    for (int frame = 0; frame < 30; frame++) update_walking_speed(&mario);

    native_sm64_canonical_floor_y_q8 = floor_data[0];
    native_sm64_canonical_floor_ny_q14 = floor_data[2];
    native_sm64_canonical_speed_q12 = (s32)(mario.forwardVel * 4096.0f);
    return mario.forwardVel > 0.0f && mario.forwardVel < 48.0f
        && floor.normal.y > 0.95f;
}
