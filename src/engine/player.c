/* Original third-person platformer controller for Volt (SM50's own character).
 * 30 Hz fixed step; positions/velocities in Q8 world units. */
#include "game.h"
#include "world.h"

#define Q(x) ((s32)((x) * 256))
#define GRAVITY Q(4)
#define TERMINAL Q(-72)
#define MAX_WALK Q(30)
#define RADIUS 48
#define HEIGHT 150
#define STEP_UP 70

static void set_action(Player *p, int a) {
    p->prev_action = p->action;
    p->action = a;
    p->action_timer = 0;
}

void player_spawn(Player *p) {
    p->x = gLevel.spawn_x * 256; p->y = gLevel.spawn_y * 256; p->z = gLevel.spawn_z * 256;
    p->vy = 0; p->fwd = 0; p->sx = p->sz = 0;
    p->face_yaw = gLevel.spawn_yaw; p->roll = 0;
    p->action = ACT_FREEFALL; p->action_timer = 0;
    p->jump_chain = 0; p->chain_timer = 0; p->wall_timer = 0;
    p->safe_x = p->x; p->safe_y = p->y; p->safe_z = p->z;
    p->invuln = 30;
}

static int input_dir(u16 b, u16 cam_yaw, u16 *yaw) {
    s32 ix = 0, iz = 0;
    if (b & BTN_UP) iz += 1;
    if (b & BTN_DOWN) iz -= 1;
    if (b & BTN_LEFT) ix -= 1;
    if (b & BTN_RIGHT) ix += 1;
    if (!ix && !iz) return 0;
    /* camera looks along cam_yaw; "up" moves away from the camera */
    u16 rel = fatan2(-ix, iz); /* 0 = forward */
    *yaw = (u16)(cam_yaw + rel);
    return 1;
}

static void begin_jump(Player *p, int kind) {
    s32 base[3] = { Q(42), Q(52), Q(66) };
    p->vy = base[kind] + p->fwd / 4;
    set_action(p, kind == 0 ? ACT_JUMP : kind == 1 ? ACT_DOUBLE_JUMP : ACT_TRIPLE_JUMP);
    p->on_ground = 0;
    p->roll = 0;
}

static void play_anim(Player *p, int anim) {
    if (p->anim != anim) { p->anim = anim; p->anim_frame = 0; }
}

void player_tick(Player *p, u16 b, u16 pressed, u16 cam_yaw) {
    u16 want_yaw = p->face_yaw;
    int moving = (p->action != ACT_WIN && p->action != ACT_KNOCKBACK) && input_dir(b, cam_yaw, &want_yaw);
    p->action_timer++;
    if (p->invuln > 0) p->invuln--;
    if (p->chain_timer > 0 && --p->chain_timer == 0) p->jump_chain = 0;
    if (p->wall_timer > 0) p->wall_timer--;

    /* ------------------------------------------------ action logic */
    switch (p->action) {
        case ACT_IDLE:
        case ACT_WALK:
        case ACT_LAND:
            if (moving) {
                s16 diff = (s16)(want_yaw - p->face_yaw);
                int turn = p->fwd > Q(16) ? 0x0A00 : 0x2000;
                if ((diff > 0x6000 || diff < -0x6000) && p->fwd > Q(16)) {
                    p->fwd -= Q(6); /* skid on sharp reversal */
                    if (p->fwd < 0) p->fwd = 0;
                } else {
                    p->face_yaw = (u16)approach_angle((s16)p->face_yaw, (s16)want_yaw, (s16)turn);
                    s32 accel = p->fwd < Q(8) ? Q(2.4) : Q(1.4);
                    p->fwd = approach_s32(p->fwd, MAX_WALK, accel, Q(1));
                }
                if (p->action != ACT_WALK && !(p->action == ACT_LAND && p->action_timer < 3)) set_action(p, ACT_WALK);
            } else {
                p->fwd = approach_s32(p->fwd, 0, 0, Q(3));
                if (p->action == ACT_WALK && p->fwd == 0) set_action(p, ACT_IDLE);
                if (p->action == ACT_LAND && p->action_timer > 6) set_action(p, ACT_IDLE);
            }
            if (pressed & BTN_JUMP) {
                int kind = 0;
                if (p->chain_timer > 0 && p->jump_chain == 1) kind = 1;
                else if (p->chain_timer > 0 && p->jump_chain == 2 && p->fwd > Q(20)) kind = 2;
                p->jump_chain = kind + 1;
                begin_jump(p, kind);
            }
            /* steep ground: slide */
            if (p->floor_ny < 9000 && p->on_ground) set_action(p, ACT_SLIDE);
            break;
        case ACT_SLIDE:
            p->fwd = approach_s32(p->fwd, 0, 0, Q(1));
            if (p->floor_ny >= 9000) set_action(p, ACT_IDLE);
            if (pressed & BTN_JUMP) begin_jump(p, 0);
            break;
        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
        case ACT_TRIPLE_JUMP:
        case ACT_FREEFALL:
        case ACT_WALL_KICK:
            if (moving) {
                p->face_yaw = (u16)approach_angle((s16)p->face_yaw, (s16)want_yaw, 0x0500);
                p->fwd = approach_s32(p->fwd, MAX_WALK, Q(0.6), Q(0.6));
            } else {
                p->fwd = approach_s32(p->fwd, 0, 0, Q(0.35));
            }
            /* variable jump height */
            if (!(b & BTN_JUMP) && p->vy > Q(16) && p->action != ACT_WALL_KICK && p->action != ACT_FREEFALL) p->vy = Q(16);
            if (p->action == ACT_TRIPLE_JUMP) p->roll = (u16)(p->roll + 0x0E00);
            if (pressed & BTN_ACTION) { set_action(p, ACT_GROUND_POUND); p->vy = 0; p->fwd = 0; }
            if ((pressed & BTN_JUMP) && p->wall_timer > 0) {
                p->face_yaw = p->wall_yaw;
                p->fwd = Q(24);
                p->vy = Q(54);
                set_action(p, ACT_WALL_KICK);
                p->wall_timer = 0;
            }
            break;
        case ACT_GROUND_POUND:
            p->roll = (u16)(p->roll + 0x2000);
            if (p->action_timer < 10) p->vy = 0; else p->vy = Q(-60);
            break;
        case ACT_KNOCKBACK:
            if (p->on_ground && p->action_timer > 10) set_action(p, ACT_IDLE);
            break;
        case ACT_WIN:
            p->fwd = 0;
            p->face_yaw = (u16)(p->face_yaw + 0x400);
            break;
    }

    /* ------------------------------------------------ integrate */
    s32 vx = (s32)(((s64)p->fwd * fsin(p->face_yaw)) >> 14) + p->sx;
    s32 vz = (s32)(((s64)p->fwd * fcos(p->face_yaw)) >> 14) + p->sz;
    p->sx = approach_s32(p->sx, 0, Q(1), Q(1));
    p->sz = approach_s32(p->sz, 0, Q(1), Q(1));
    if (p->action == ACT_SLIDE) {
        /* slide down the slope: accelerate along the floor normal's horizontal part */
        s32 nx, nz;
        if (col_floor_normal(p->x >> 8, p->y >> 8, p->z >> 8, &nx, &nz)) {
            p->sx += (nx * 6) >> 6; p->sz += (nz * 6) >> 6;
            s32 lim = Q(40);
            p->sx = CLAMP(p->sx, -lim, lim); p->sz = CLAMP(p->sz, -lim, lim);
            vx += p->sx; vz += p->sz;
        }
    }

    /* move in up to 4 substeps so fast motion doesn't tunnel through walls */
    int steps = 1 + (MAX(vx < 0 ? -vx : vx, vz < 0 ? -vz : vz) >> 13);
    if (steps > 4) steps = 4;
    int touched_wall = 0;
    for (int s = 0; s < steps; s++) {
        s32 nx = p->x + vx / steps, nz = p->z + vz / steps;
        s32 yw = p->y >> 8;
        u16 wy = 0;
        int h = col_resolve_walls(&nx, yw + STEP_UP + 10, &nz, RADIUS, &wy); /* low walls are steps: handled by the ledge check */
        h += col_resolve_walls(&nx, yw + 130, &nz, RADIUS, &wy);
        if (h) { touched_wall = 1; p->wall_yaw = wy; }
        /* ledge / step check */
        s32 ny;
        s32 fy = col_find_floor(nx >> 8, (p->y >> 8) + STEP_UP - 100, nz >> 8, &ny, NULL);
        if (fy == INT32_MIN) {
            /* out of the world: block */
            continue;
        }
        if (p->on_ground && fy > p->y + STEP_UP * 256) {
            touched_wall = 1; /* too high: treat as wall */
            continue;
        }
        p->x = nx; p->z = nz;
    }
    if (touched_wall && !p->on_ground) p->wall_timer = 5;
    if (touched_wall && p->on_ground && p->fwd > Q(20)) p->fwd = Q(20);

    /* vertical */
    s32 floor_ny = ONE14;
    s32 fy = col_find_floor(p->x >> 8, (p->y >> 8) + 30, p->z >> 8, &floor_ny, NULL);
    p->floor_y = fy; p->floor_ny = floor_ny;
    if (p->on_ground) {
        if (fy != INT32_MIN && fy >= p->y - Q(40)) {
            p->y = fy; p->vy = 0;
        } else {
            p->on_ground = 0;
            if (p->action != ACT_JUMP && p->action != ACT_DOUBLE_JUMP && p->action != ACT_TRIPLE_JUMP)
                set_action(p, ACT_FREEFALL);
        }
    }
    if (!p->on_ground) {
        p->vy -= GRAVITY;
        if (p->vy < TERMINAL) p->vy = TERMINAL;
        p->y += p->vy;
        s32 cy = col_find_ceil(p->x >> 8, (p->y >> 8) + HEIGHT / 2, p->z >> 8);
        if (cy != INT32_MAX && p->y + Q(HEIGHT) > cy && p->vy > 0) { p->y = cy - Q(HEIGHT); p->vy = 0; }
        if (fy != INT32_MIN && p->y <= fy) {
            p->y = fy; p->vy = 0; p->on_ground = 1;
            if (p->action == ACT_GROUND_POUND) { set_action(p, ACT_LAND); p->action_timer = -6; }
            else if (p->action == ACT_KNOCKBACK) { }
            else if (p->action != ACT_WIN) {
                set_action(p, ACT_LAND);
                p->chain_timer = 5;
                if (p->prev_action == ACT_TRIPLE_JUMP) p->jump_chain = 0;
            }
            p->roll = 0;
        }
        if (p->action == ACT_JUMP && p->vy < 0 && p->action_timer > 30) set_action(p, ACT_FREEFALL);
    }
    /* safe position for respawn */
    if (p->on_ground && p->floor_ny > 12000 && (p->y >> 8) > gLevel.water_y + 20) {
        p->safe_x = p->x; p->safe_y = p->y; p->safe_z = p->z;
    }
    /* fell into the water / out of the world */
    if ((p->y >> 8) < gLevel.water_y - 40) {
        p->x = p->safe_x; p->y = p->safe_y + Q(200); p->z = p->safe_z;
        p->vy = 0; p->fwd = 0; p->on_ground = 0;
        if (p->health > 0) p->health -= 2;
        set_action(p, ACT_FREEFALL);
        p->invuln = 45;
    }

    /* ------------------------------------------------ animation */
    switch (p->action) {
        case ACT_IDLE: play_anim(p, ANIM_IDLE); p->anim_frame += 16; break;
        case ACT_WALK:
            play_anim(p, p->fwd > Q(18) ? ANIM_RUN : ANIM_WALK);
            p->anim_frame += 8 + (p->fwd >> 8) * (p->anim == ANIM_RUN ? 1 : 2) / 2;
            break;
        case ACT_LAND: play_anim(p, ANIM_LAND); p->anim_frame += 16; break;
        case ACT_JUMP: case ACT_DOUBLE_JUMP: case ACT_WALL_KICK:
            play_anim(p, p->vy > 0 ? ANIM_JUMP : ANIM_FALL); p->anim_frame += 16; break;
        case ACT_TRIPLE_JUMP: case ACT_GROUND_POUND: play_anim(p, ANIM_FLIP); break;
        case ACT_FREEFALL: case ACT_SLIDE: play_anim(p, ANIM_FALL); p->anim_frame += 16; break;
        case ACT_KNOCKBACK: play_anim(p, ANIM_HURT); break;
        case ACT_WIN: play_anim(p, ANIM_JUMP); break;
    }
}
