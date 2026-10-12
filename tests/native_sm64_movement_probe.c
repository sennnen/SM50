/*
 * Experimental SH4 bridge to the PS1 port's Q12-adapted SM64 slide math.
 * The file is intentionally built only by `make calc-native-probe`; it uses
 * local PS1-port source and does not alter the regular ROM-derived preview.
 */
#include <types.h>
#include <engine/math_util.h>

extern void update_sliding_angleq(struct MarioState *m, q32 accelq, q32 lossFactorq);
extern void update_walking_speed(struct MarioState *m);

/* The isolated probe does not model these environmental surface callbacks. */
u32 mario_update_moving_sand(struct MarioState *m) { (void)m; return 0; }
u32 mario_update_windy_ground(struct MarioState *m) { (void)m; return 0; }
s32 mario_floor_is_slope(struct MarioState *m) { (void)m; return 0; }
s32 mario_get_floor_class(struct MarioState *m) { (void)m; return 0; }

volatile s32 native_sm64_probe_slide_z_q12;
volatile s32 native_sm64_probe_slide_x_q12;
volatile s32 native_sm64_probe_math[11];
volatile s32 native_sm64_probe_walk_speed_q12;

int native_sm64_movement_probe(void) {
    struct MarioState mario = { 0 };
    struct Surface ramp = { 0 };

    /* A 30-degree, Z-axis ramp in the original compressed-normal format. */
    ramp.compressed_normal.x = 0;
    ramp.compressed_normal.y = 104;
    ramp.compressed_normal.z = 60;
    mario.floor = &ramp;

    native_sm64_probe_math[0] = q(0.5f);
    native_sm64_probe_math[1] = q(0.99f);
    native_sm64_probe_math[2] = sqrtq(30);
    native_sm64_probe_math[3] = cosqs(0);
    native_sm64_probe_math[4] = qtof(2048) == 0.5f;
    native_sm64_probe_math[5] = qmul(qmul(q(0.5f), sqrtq(30)), cosqs(0));
    native_sm64_probe_math[6] = ftoq(qtof(158));
    native_sm64_probe_math[7] = qtof(158) > 0.0f;
    native_sm64_probe_math[9] = qtof(4096) == 1.0f;
    native_sm64_probe_math[10] = qtof(-4096) == -1.0f;

    for (int frame = 0; frame < 30; frame++) {
        update_sliding_angleq(&mario, q(0.5f), q(0.99f));
        if (frame == 0) native_sm64_probe_math[8] = ftoq(mario.slideVelZ);
    }

    native_sm64_probe_slide_x_q12 = q(mario.slideVelX);
    native_sm64_probe_slide_z_q12 = q(mario.slideVelZ);

    struct MarioState walker = { 0 };
    struct Surface flat = { 0 };
    flat.compressed_normal.y = 120;
    walker.floor = &flat;
    walker.intendedMag = 32.0f;
    for (int frame = 0; frame < 30; frame++) update_walking_speed(&walker);
    native_sm64_probe_walk_speed_q12 = q(walker.forwardVel);

    /* Positive slope acceleration and bounded, nonzero output are expected. */
    return mario.slideVelZ > 1.0f && mario.slideVelZ < 32.0f
        && walker.forwardVel > 0.0f && walker.forwardVel < 48.0f
        && native_sm64_probe_math[4] == 1
        && native_sm64_probe_math[6] == 158
        && native_sm64_probe_math[7] == 1
        && native_sm64_probe_math[9] == 1
        && native_sm64_probe_math[10] == 1
        && mario.slideYaw >= -32768 && mario.slideYaw <= 32767;
}
