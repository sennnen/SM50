/* SM50 game loop: camera, objects, rendering and HUD. */
#include "game.h"
#include "world.h"
#include "gfx.h"
#include <string.h>
#include <stdio.h>

Game gGame;

int game_init(const void *pack, u32 size, u8 *work, u32 work_size) {
    memset(&gGame, 0, sizeof(gGame));
    int r = level_load(pack, size, work, work_size);
    if (r) return r;
    Player *p = &gGame.p;
    p->health = 8; p->lives = 4;
    player_spawn(p);
    Camera *c = &gGame.cam;
    c->yaw = (u16)(gLevel.spawn_yaw);
    c->pitch = 0x0A00; c->dist = c->target_dist = 900;
    c->fx = p->x >> 8; c->fy = (p->y >> 8) + 100; c->fz = p->z >> 8;
    gGame.title = 1;
    for (int i = 0; i < gLevel.n_objs; i++) if (gLevel.objs[i].type == OBJ_STAR) gGame.stars_total++;
    return 0;
}

static void show_msg(const char *s) {
    strncpy(gGame.msg, s, sizeof(gGame.msg) - 1);
    gGame.msg_timer = 60;
}

/* ----------------------------------------------------------------- camera */
#define CAM_STEPS 8
/* number of clear eighths of the focus->camera segment for a camera at `yaw` */
static int cam_clear_steps(const Camera *c, u16 yaw) {
    s32 h = (s32)(((s64)c->dist * fcos((u16)c->pitch)) >> 14);
    s32 ex = c->fx - (s32)(((s64)h * fsin(yaw)) >> 14), ez = c->fz - (s32)(((s64)h * fcos(yaw)) >> 14);
    s32 ey = c->fy + (s32)(((s64)c->dist * fsin((u16)c->pitch)) >> 14) + 60;
    s32 t = col_segment_hit(c->fx, c->fy, c->fz, ex, ey, ez);
    if (t >= 65536) return CAM_STEPS;
    int k = (int)((t * CAM_STEPS) >> 16) - 1; /* stay a bit in front of the hit */
    return k < 0 ? 0 : k;
}

/* ---- camera tick ---- */
static void camera_tick(Camera *c, const Player *p, u16 b, u16 pressed);
static void camera_tick(Camera *c, const Player *p, u16 b, u16 pressed) {
    if (b & BTN_CAM_L) c->yaw = (u16)(c->yaw - 0x300);
    if (b & BTN_CAM_R) c->yaw = (u16)(c->yaw + 0x300);
    if (pressed & BTN_CAM_ZOOM) { c->zoom = (c->zoom + 1) % 3; }
    static const s32 dists[3] = { 900, 600, 1300 };
    c->target_dist = dists[c->zoom];
    c->dist += (c->target_dist - c->dist) / 6;
    /* lazily swing behind the player while running and not steering the camera */
    if (!(b & (BTN_CAM_L | BTN_CAM_R)) && p->fwd > 15 * 256 && (b & (BTN_LEFT | BTN_RIGHT)) == 0 &&
        p->action == ACT_WALK) {
        c->yaw = (u16)approach_angle((s16)c->yaw, (s16)p->face_yaw, 0x0080);
    }
    /* focus smoothing */
    s32 tx = p->x >> 8, ty = (p->y >> 8) + 110, tz = p->z >> 8;
    c->fx += (tx - c->fx) / 2;
    c->fz += (tz - c->fz) / 2;
    s32 dy = ty - c->fy;
    if (p->on_ground || dy < -200 || dy > 300) c->fy += dy / 4;
    /* camera position: behind focus along yaw */
    s32 h = (s32)(((s64)c->dist * fcos((u16)c->pitch)) >> 14);
    c->x = c->fx - (s32)(((s64)h * fsin(c->yaw)) >> 14);
    c->z = c->fz - (s32)(((s64)h * fcos(c->yaw)) >> 14);
    c->y = c->fy + (s32)(((s64)c->dist * fsin((u16)c->pitch)) >> 14) + 60;
    /* occlusion: march from the focus towards the camera; stop before the first wall.
     * If the view is blocked close to the focus, swing the yaw towards a clear side. */
    {
        int k = cam_clear_steps(c, c->yaw);
        /* blocked views raise the pitch (look down over the character), clear views relax it */
        s16 want_pitch = k < CAM_STEPS ? 0x2600 : 0x0A00;
        c->pitch = approach_angle(c->pitch, want_pitch, k < CAM_STEPS ? 0x0300 : 0x0080);
        if (k < CAM_STEPS) {
            if (!(b & (BTN_CAM_L | BTN_CAM_R))) {
                /* find the nearest yaw offset with a clear line of sight and turn towards it */
                static const s16 offs[6] = { 0x2000, -0x2000, 0x4000, -0x4000, 0x6000, -0x6000 };
                for (int i = 0; i < 6; i++)
                    if (cam_clear_steps(c, (u16)(c->yaw + offs[i])) == CAM_STEPS) {
                        c->yaw = (u16)(c->yaw + (offs[i] > 0 ? 0x0500 : -0x0500));
                        break;
                    }
            }
            if (k < 1) k = 1;
            { c->x = c->fx + (c->x - c->fx) * k / CAM_STEPS; c->y = c->fy + (c->y - c->fy) * k / CAM_STEPS;
              c->z = c->fz + (c->z - c->fz) * k / CAM_STEPS; }
        }
        s32 cy = col_find_ceil(c->x, c->fy, c->z);
        if (cy != INT32_MAX && (cy >> 8) < c->y + 20 && (cy >> 8) > c->fy) c->y = (cy >> 8) - 20;
    }
    /* keep the camera out of walls (sphere push at camera height) */
    {
        s32 cx = c->x * 256, cz = c->z * 256;
        if (col_resolve_walls(&cx, c->y, &cz, 60, NULL)) { c->x = cx >> 8; c->z = cz >> 8; }
    }
    /* keep above terrain */
    s32 fy = col_find_floor(c->x, c->y + 400, c->z, NULL, NULL);
    if (fy != INT32_MIN && c->y < (fy >> 8) + 80) c->y = (fy >> 8) + 80;
    if (c->y < gLevel.water_y + 40) c->y = gLevel.water_y + 40;
}

/* ---------------------------------------------------------------- objects */
static s32 dist2(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz) {
    s32 dx = ax - bx, dy = ay - by, dz = az - bz;
    if (dx > 2000 || dx < -2000 || dy > 2000 || dy < -2000 || dz > 2000 || dz < -2000) return 0x7FFFFFFF;
    return dx * dx + dy * dy + dz * dz;
}

static void objects_tick(Player *p) {
    s32 px = p->x >> 8, py = p->y >> 8, pz = p->z >> 8;
    for (int i = 0; i < gLevel.n_objs; i++) {
        Obj *o = &gLevel.objs[i];
        if (!o->active) continue;
        o->timer++;
        switch (o->type) {
            case OBJ_COIN:
                o->yaw = (u16)(o->yaw + 0x0800);
                if (dist2(px, py + 60, pz, o->x, o->y, o->z) < 110 * 110) {
                    o->active = 0; p->coins++;
                    if (p->health < 8) p->health++;
                }
                break;
            case OBJ_STAR:
                o->yaw = (u16)(o->yaw + 0x0600);
                if (dist2(px, py + 60, pz, o->x, o->y, o->z) < 140 * 140 && !gGame.won) {
                    o->active = 0; p->stars++;
                    if (p->stars >= gGame.stars_total) {
                        gGame.won = 1; gGame.win_timer = 0;
                        p->action = ACT_WIN; p->action_timer = 0;
                    }
                    show_msg("STAR GET!");
                }
                break;
            case OBJ_BLOB: {
                if (o->state == 0) {
                    /* patrol in a circle around home; chase when close */
                    s32 d = dist2(px, py, pz, o->x, o->y, o->z);
                    u16 dir;
                    if (d < 700 * 700) dir = fatan2(px - o->x, pz - o->z);
                    else dir = (u16)(fatan2(o->hx - o->x, o->hz - o->z) + ((o->timer & 128) ? 0x3000 : -0x3000));
                    o->yaw = (u16)approach_angle((s16)o->yaw, (s16)dir, 0x0300);
                    s32 spd = d < 700 * 700 ? 9 : 5;
                    s32 nx = o->x + ((spd * fsin(o->yaw)) >> 14), nz = o->z + ((spd * fcos(o->yaw)) >> 14);
                    s32 fy = col_find_floor(nx, o->y + 60, nz, NULL, NULL);
                    if (fy != INT32_MIN && (fy >> 8) > gLevel.water_y + 10 && (fy >> 8) - o->y < 60 && o->y - (fy >> 8) < 100) {
                        o->x = nx; o->z = nz; o->y = fy >> 8;
                    }
                    if (d < 120 * 120 + 0 && py < o->y + 140) {
                        if (p->vy < 0 && py > o->y + 50) {
                            /* stomped */
                            o->state = 1; o->timer = 0;
                            p->vy = 40 * 256; p->on_ground = 0; p->action = ACT_DOUBLE_JUMP; p->action_timer = 0;
                            p->coins++;
                        } else if (p->invuln == 0) {
                            p->health -= 2; p->invuln = 60;
                            p->action = ACT_KNOCKBACK; p->action_timer = 0; p->on_ground = 0;
                            p->vy = 30 * 256; p->fwd = 0;
                            u16 away = fatan2(px - o->x, pz - o->z);
                            p->sx = (24 * fsin(away)) >> 6; p->sz = (24 * fcos(away)) >> 6;
                        }
                    }
                } else {
                    if (o->timer > 20) o->active = 0; /* squash then vanish */
                }
                break;
            }
            default: break;
        }
    }
    if (p->health <= 0) {
        p->lives--; p->health = 8; player_spawn(p);
        show_msg(p->lives > 0 ? "OUCH! TRY AGAIN" : "GAME OVER");
        if (p->lives <= 0) p->lives = 4;
    }
}

void game_tick(u16 buttons) {
    Game *g = &gGame;
    g->pressed = buttons & ~g->prev_buttons;
    g->prev_buttons = buttons;
    g->buttons = buttons;
    if (g->title) {
        /* title screen: slow orbit around the spawn point until jump is pressed */
        g->cam.yaw = (u16)(g->cam.yaw + 0x0060);
        camera_tick(&g->cam, &g->p, 0, 0);
        if (g->pressed & BTN_JUMP) { g->title = 0; g->prev_buttons |= BTN_JUMP; }
        return;
    }
    if (g->pressed & BTN_PAUSE) g->paused = !g->paused;
    if (g->paused) return;
    g->ticks++;
    player_tick(&g->p, buttons, g->pressed, g->cam.yaw);
    objects_tick(&g->p);
    camera_tick(&g->cam, &g->p, buttons, g->pressed);
    if (g->msg_timer > 0) g->msg_timer--;
    if (g->won) g->win_timer++;
}

/* ---------------------------------------------------------------- render */
static void draw_sky(void) {
    u16 *fb = gfx_colorbuf();
    int w = gfx_width(), h = gfx_height();
    u16 top = gLevel.sky_top, bot = gLevel.sky_bottom;
    int tr = top >> 11, tg = (top >> 5) & 63, tb = top & 31, br = bot >> 11, bg = (bot >> 5) & 63, bb = bot & 31;
    for (int y = 0; y < h; y++) {
        int k = y * 256 / h;
        u16 c = (u16)((((tr * (256 - k) + br * k) >> 8) << 11) | (((tg * (256 - k) + bg * k) >> 8) << 5) | ((tb * (256 - k) + bb * k) >> 8));
        u32 c2 = ((u32)c << 16) | c;
        u32 *row = (u32 *)(fb + y * w);
        for (int x = 0; x < (w >> 1); x++) row[x] = c2;
    }
}

static void draw_skeleton(const Player *p) {
    const Level *L = &gLevel;
    const Anim *a = &L->anims[p->anim < L->n_anims ? p->anim : 0];
    int f = (p->anim_frame >> 4);
    if (a->loop) f %= a->nframes; else if (f >= a->nframes) f = a->nframes - 1;
    int np = MIN(a->nparts, L->n_parts);
    const u8 *fr = a->raw + 8 + (u32)f * (2 + 6 * a->nparts);
    s32 rooty = BES16(fr);
    Mat34 world[MAX_PARTS], root, tmp;
    mat_from_trs(&root, p->x >> 8, p->y >> 8, p->z >> 8, 0, p->face_yaw, 0, ONE14);
    if (p->roll) {
        Mat34 r; mat_from_trs(&r, 0, 80, 0, p->roll, 0, 0, ONE14);
        Mat34 back; mat_identity(&back); back.m[1][3] = -80;
        mat_mul(&tmp, &r, &back); mat_mul(&root, &root, &tmp);
    }
    for (int i = 0; i < np; i++) {
        const Part *pt = &L->parts[i];
        const u8 *rot = fr + 2 + i * 6;
        Mat34 local;
        s32 ty = pt->py + (pt->parent < 0 ? rooty : 0);
        mat_from_trs(&local, pt->px, ty, pt->pz, BE16(rot), BE16(rot + 2), BE16(rot + 4), ONE14);
        const Mat34 *par = pt->parent >= 0 && pt->parent < i ? &world[pt->parent] : &root;
        mat_mul(&world[i], par, &local);
        gfx_set_model(&world[i]);
        gfx_run_dl(pt->dl);
    }
}

static void draw_shadow(const Player *p) {
    if (p->floor_y == INT32_MIN) return;
    s32 sx, sy, d;
    if (!gfx_project_point(p->x >> 8, (p->floor_y >> 8) + 2, p->z >> 8, &sx, &sy, &d)) return;
    s32 r = (40 * gfx_height() * 2) / (d ? d : 1);
    if (r < 2) r = 2;
    if (r > 40) r = 40;
    gfx_blend_rect(sx - r, sy - r / 3, r * 2, (r * 2) / 3 + 1, 0x0000);
}

static void draw_model(int idx, s32 x, s32 y, s32 z, u16 yaw, s32 scale) {
    if (idx >= gLevel.n_models) return;
    const Model *m = &gLevel.models[idx];
    if (!gfx_sphere_visible(x, y, z, m->radius)) return;
    if (m->billboard) gfx_set_model_billboard(x, y, z, scale);
    else { Mat34 w; mat_from_trs(&w, x, y, z, 0, yaw, 0, scale); gfx_set_model(&w); }
    gfx_run_dl(m->dl);
}

static void draw_title(void) {
    int w = gfx_width(), s = w >= 300 ? 2 : 1;
    int big = w >= 180 ? 3 : 2;
    gfx_blend_rect(0, 0, w, gfx_height(), 0x0000);
    gfx_draw_text((w - gfx_text_width("SM50", big * s)) / 2, 6 * s, "SM50", 0xFFE0, big * s);
    gfx_draw_text((w - gfx_text_width(gLevel.name, s)) / 2, 6 * s + 24 * s, gLevel.name, 0x07FF, s);
    static const char *lines[] = { "ARROWS  MOVE", "SHIFT   JUMP x3", "ALPHA   POUND", "F1 F2   CAMERA", "F4 SPEED F6 FPS" };
    int y = 6 * s + 36 * s;
    if (gfx_height() < 100) y -= 6;
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++, y += 9 * s)
        if (y + 8 * s < gfx_height() - 10 * s) gfx_draw_text((w - gfx_text_width(lines[i], s)) / 2, y, lines[i], 0xFFFF, s);
    if ((gGame.frame / 15) & 1)
        gfx_draw_text((w - gfx_text_width("PRESS SHIFT", s)) / 2, gfx_height() - 10 * s, "PRESS SHIFT", 0xFFE0, s);
}

static void draw_hud(int show_fps, int fps10) {
    char buf[40];
    if (gGame.title) { draw_title(); return; }
    const Player *p = &gGame.p;
    int s = gfx_width() >= 300 ? 2 : 1;
    if (gLevel.n_parts == 0 && gLevel.n_objs == 0) {
        /* scene pack (geometry only): show the scene name and position */
        gfx_draw_text(4, 4, gLevel.name, 0xFFFF, s);
        snprintf(buf, sizeof(buf), "%d %d %d", p->x >> 8, p->y >> 8, p->z >> 8);
        gfx_draw_text(4, 4 + 9 * s, buf, 0xFFE0, s);
        if (show_fps) {
            snprintf(buf, sizeof(buf), "%d.%d FPS", fps10 / 10, fps10 % 10);
            gfx_draw_text(gfx_width() - gfx_text_width(buf, s) - 4, 4, buf, 0x07FF, s);
        }
        return;
    }
    for (int i = 0; i < 8; i++) gfx_fill_rect(4 + i * 5 * s, 4, 4 * s, 6 * s, i < p->health ? 0xF800 : 0x4208);
    snprintf(buf, sizeof(buf), "COIN %d/%d", p->coins, gLevel.coins_total);
    gfx_draw_text(4, 4 + 8 * s, buf, 0xFFE0, s);
    snprintf(buf, sizeof(buf), "STAR %d/%d LIFE %d", p->stars, gGame.stars_total, p->lives);
    gfx_draw_text(4, 4 + 17 * s, buf, 0xFFFF, s);
    if (show_fps) {
        snprintf(buf, sizeof(buf), "%d.%d FPS", fps10 / 10, fps10 % 10);
        gfx_draw_text(gfx_width() - gfx_text_width(buf, s) - 4, 4, buf, 0x07FF, s);
    }
    if (gGame.msg_timer > 0 || gGame.won) {
        const char *m = gGame.won ? "ALL STARS! YOU WIN" : gGame.msg;
        int big = s + 1;
        while (big > 1 && gfx_text_width(m, big) > gfx_width() - 4) big--;
        gfx_draw_text((gfx_width() - gfx_text_width(m, big)) / 2, gfx_height() / 3, m, 0xFFE0, big);
    }
    if (gGame.paused) gfx_draw_text((gfx_width() - gfx_text_width("PAUSED", s + 1)) / 2, gfx_height() / 2, "PAUSED", 0xFFFF, s + 1);
}

void game_render(int show_fps, int fps10) {
    Game *g = &gGame;
    Camera *c = &g->cam;
    draw_sky();
    gfx_clear_depth();
    Mat34 view;
    Vec3i eye = { c->x, c->y, c->z }, tgt = { c->fx, c->fy, c->fz };
    mat_lookat(&view, eye, tgt);
    gfx_set_view(&view);
    Mat34 id; mat_identity(&id);
    gfx_reset_state();
    /* opaque level chunks */
    for (int layer = 0; layer < 3; layer += 2) {
        if (layer == 2) {
            /* objects + player before translucent water */
            if (gLevel.n_parts > 0) draw_skeleton(&g->p);
            for (int i = 0; i < gLevel.n_objs; i++) {
                const Obj *o = &gLevel.objs[i];
                if (!o->active) continue;
                switch (o->type) {
                    case OBJ_COIN: draw_model(0, o->x, o->y, o->z, o->yaw, ONE14); break;
                    case OBJ_STAR: draw_model(1, o->x, o->y, o->z, o->yaw, ONE14 * 2); break;
                    case OBJ_TREE: draw_model(2, o->x, o->y, o->z, 0, ONE14); break;
                    case OBJ_BLOB: draw_model(3, o->x, o->y, o->z, o->yaw, o->state ? ONE14 / 2 : ONE14); break;
                }
            }
            draw_shadow(&g->p);
        }
        gfx_set_model(&id);
        /* visible chunks of this layer, nearest first (opaque) to maximise early z rejection */
        int order[MAX_CHUNKS]; s32 key[MAX_CHUNKS]; int n = 0;
        for (int i = 0; i < gLevel.n_chunks; i++) {
            const Chunk *ch = &gLevel.chunks[i];
            if ((layer == 2) != (ch->layer == 2)) continue;
            if (!gfx_sphere_visible(ch->x, ch->y, ch->z, ch->radius)) continue;
            s32 dx = (ch->x - c->x) >> 4, dy = (ch->y - c->y) >> 4, dz = (ch->z - c->z) >> 4;
            s32 k = dx * dx + dy * dy + dz * dz;
            int j = n++;
            while (j > 0 && key[j - 1] > k) { key[j] = key[j - 1]; order[j] = order[j - 1]; j--; }
            key[j] = k; order[j] = i;
        }
        gfx_force_xlu(layer == 2);
        for (int i = 0; i < n; i++) {
            const Chunk *ch = &gLevel.chunks[order[i]];
            if (ch->tx | ch->ty | ch->tz) {
                Mat34 t; mat_identity(&t); t.m[0][3] = ch->tx; t.m[1][3] = ch->ty; t.m[2][3] = ch->tz;
                gfx_set_model(&t);
            } else gfx_set_model(&id);
            gfx_run_dl(ch->dl);
        }
        gfx_force_xlu(0);
    }
    draw_hud(show_fps, fps10);
    g->frame++;
}
