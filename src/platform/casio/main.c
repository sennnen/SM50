/* SM50 fx-CG50 platform layer: OS syscalls for display/timing, direct key
 * matrix reads for low-latency multi-key input, 2x upscaled blit to VRAM. */
#include "../../engine/game.h"
#include "../../engine/gfx.h"
#include "../../engine/world.h"
#include "display_layout.h"

#include <stdio.h>
#include <string.h>

void *GetVRAMAddress(void);
void Bdisp_PutDisp_DD(void);
void Bdisp_AllClr_VRAM(void);
void Bdisp_EnableColor(int n);
void EnableStatusArea(int opt);
int RTC_GetTicks(void);
int GetKey(int *key);
void emu_debug_print(const char *s);

#ifdef SM50_EMU_DEBUG
#define DBG(...) do { char _b[96]; snprintf(_b, sizeof _b, __VA_ARGS__); emu_debug_print(_b); } while (0)
#else
#define DBG(...) do { } while (0)
#endif

#define RW SM50_RENDER_2X_W
#define RH SM50_RENDER_2X_H
#define LCD_W SM50_OS_VRAM_W
#define LCD_H SM50_OS_VRAM_H

static u16 sFb[RW * RH] __attribute__((aligned(4)));
static u16 sZb[RW * RH] __attribute__((aligned(4)));
static u8 sTexCache[48 * 1024] __attribute__((aligned(4)));
static u8 sColWork[24 * 1024] __attribute__((aligned(4)));

#ifdef SM50_EMBED_PACK
extern const u8 sm50_pack_start[], sm50_pack_end[];
#endif
#ifdef SM50_NATIVE_PROBE
extern int native_sm64_movement_probe(void);
extern volatile s32 native_sm64_probe_slide_z_q12;
extern volatile s32 native_sm64_probe_walk_speed_q12;
extern volatile s32 native_sm64_probe_rom_floor_y_q8;
extern volatile s32 native_sm64_probe_rom_normal_y_q14;
extern volatile s32 native_sm64_probe_rom_walk_speed_q12;
#endif
#ifdef SM50_CANONICAL_PROBE
extern int native_sm64_canonical_probe(void);
extern volatile s32 native_sm64_canonical_floor_y_q8;
extern volatile s32 native_sm64_canonical_floor_ny_q14;
extern volatile s32 native_sm64_canonical_speed_q12;
extern volatile s32 native_sm64_canonical_ground_step;
extern volatile s32 native_sm64_canonical_moved_x_q8;
extern volatile s32 native_sm64_canonical_moved_z_q8;
extern volatile s32 native_sm64_canonical_pos_x_q8;
extern volatile s32 native_sm64_canonical_pos_y_q8;
extern volatile s32 native_sm64_canonical_pos_z_q8;
extern volatile u16 native_sm64_canonical_yaw;
extern volatile s32 native_sm64_canonical_anim_index;
extern volatile s32 native_sm64_canonical_speed_q12;
extern volatile s32 native_sm64_canonical_star_collected;
extern volatile s32 native_sm64_canonical_star_interaction_ok;
extern volatile s32 native_sm64_canonical_runtime_ready;
extern volatile s32 native_sm64_canonical_star_debug;
extern volatile s32 native_sm64_canonical_star_hitbox_debug;
extern volatile s32 native_sm64_canonical_coins;
extern volatile s32 native_sm64_canonical_yellow_coin_check;
extern volatile s32 native_sm64_canonical_red_coin_check;
extern volatile s32 native_sm64_canonical_red_coin_debug;
extern volatile s32 native_sm64_canonical_red_star_check;
#endif
extern const u8 rom_image_end[];

#ifdef SM50_CANONICAL_PROBE
void sm50_native_publish_mario(s32 x, s32 y, s32 z, u16 yaw, s32 speed,
                               s32 vertical_speed, s32 floor_y, s32 grounded,
                               s32 landing, s32 animation) {
    int anim = animation >= 0 && animation < gLevel.n_anims
        ? animation : (landing ? ANIM_LAND
        : !grounded ? (vertical_speed > 0 ? ANIM_JUMP : ANIM_FALL)
        : (speed ? ANIM_WALK : ANIM_IDLE));
    gGame.p.x = x; gGame.p.y = y; gGame.p.z = z;
    gGame.p.coins = native_sm64_canonical_coins;
    gGame.p.floor_y = floor_y; gGame.p.floor_ny = 16384;
    gGame.p.face_yaw = yaw; gGame.p.fwd = speed;
    gGame.p.on_ground = grounded;
    gGame.p.action = landing ? ACT_LAND
        : !grounded ? ACT_JUMP : (speed ? ACT_WALK : ACT_IDLE);
    if (gGame.p.anim != anim) { gGame.p.anim = anim; gGame.p.anim_frame = 0; }
    else gGame.p.anim_frame += 16;
    gGame.p.safe_x = x; gGame.p.safe_y = y; gGame.p.safe_z = z;
#ifdef SM50_CANONICAL_PROBE
    static int sStarReported;
    if (native_sm64_canonical_runtime_ready &&
        native_sm64_canonical_star_collected && !sStarReported) {
        sStarReported = 1;
        for (int i = 0; i < gLevel.n_objs; i++) {
            Obj *o = &gLevel.objs[i];
            if (o->type == OBJ_STAR && o->x == 1550 && o->y == 1200 && o->z == 300)
                o->active = 0;
        }
        gGame.p.stars++;
        strncpy(gGame.msg, "STAR COLLECTED", sizeof(gGame.msg) - 1);
        gGame.msg_timer = 120;
    }
#endif
}
#endif

/* Locate the data pack: appended after the add-in image by tools/build-g3a.py
 * (16-byte aligned), or embedded at link time for test builds. */
static const u8 *find_pack(u32 *size) {
    const u8 *p = (const u8 *)(((u32)rom_image_end + 15) & ~15u);
    if (p[0] == 'S' && p[1] == '5' && p[2] == '0' && p[3] == 'P') {
        /* size: end of the last entry */
        u32 n = BE32(p + 8), ds = BE32(p + 12), end = ds;
        for (u32 i = 0; i < n; i++) { u32 e = ds + BE32(p + 16 + i * 12 + 4) + BE32(p + 16 + i * 12 + 8); if (e > end) end = e; }
        *size = end;
        return p;
    }
#ifdef SM50_EMBED_PACK
    *size = (u32)(sm50_pack_end - sm50_pack_start);
    return sm50_pack_start;
#else
    *size = 0;
    return NULL;
#endif
}

/* Key matrix (KEYSC data registers) */
static int keydown(int code) {
    const volatile unsigned short *kr = (const volatile unsigned short *)0xA44B0000;
    int row = code % 10, col = code / 10 - 1;
    int word = row >> 1, bit = col + 8 * (row & 1);
    return (kr[word] >> bit) & 1;
}

#define K_UP 28
#define K_DOWN 37
#define K_LEFT 38
#define K_RIGHT 27
#define K_SHIFT 78
#define K_ALPHA 77
#define K_F1 79
#define K_F2 69
#define K_F3 59
#define K_F4 49
#define K_F6 29
#define K_EXIT 47
#define K_MENU 48
#define K_EXE 31
#define K_OPTN 68

static u16 read_buttons(void) {
    u16 b = 0;
    if (keydown(K_UP)) b |= BTN_UP;
    if (keydown(K_DOWN)) b |= BTN_DOWN;
    if (keydown(K_LEFT)) b |= BTN_LEFT;
    if (keydown(K_RIGHT)) b |= BTN_RIGHT;
    if (keydown(K_SHIFT) || keydown(K_EXE)) b |= BTN_JUMP;
    if (keydown(K_ALPHA)) b |= BTN_ACTION;
    if (keydown(K_F1)) b |= BTN_CAM_L;
    if (keydown(K_F2)) b |= BTN_CAM_R;
    if (keydown(K_F3)) b |= BTN_CAM_ZOOM;
    if (keydown(K_EXIT)) b |= BTN_PAUSE;
    return b;
}

static int sRes = 0; /* 0: 192x108 upscaled 2x, 1: 128x72 upscaled 3x */

static void blit3x(void) {
    u16 *vram = (u16 *)GetVRAMAddress();
    const int w = SM50_RENDER_3X_W, h = SM50_RENDER_3X_H;
    const u16 *src = sFb;
    for (int y = 0; y < h; y++) {
        u16 *r0 = vram + (y * 3) * LCD_W;
        for (int x = 0; x < w; x++) {
            u16 c = *src++;
            r0[x * 3] = r0[x * 3 + 1] = r0[x * 3 + 2] = c;
        }
        memcpy(r0 + LCD_W, r0, LCD_W * 2);
        memcpy(r0 + LCD_W * 2, r0, LCD_W * 2);
    }
}

static void set_res(int r) {
    sRes = r;
    if (r) { gfx_resize(SM50_RENDER_3X_W, SM50_RENDER_3X_H); memset(GetVRAMAddress(), 0, SM50_OS_VRAM_BYTES); }
    else gfx_resize(RW, RH);
    gfx_set_perspective(50, 64, 16000);
}

static void blit2x(void) {
    if (sRes) { blit3x(); return; }
    u32 *vram = (u32 *)GetVRAMAddress();
    const u16 *src = sFb;
    for (int y = 0; y < RH; y++) {
        u32 *r0 = vram + y * 2 * (LCD_W / 2), *r1 = r0 + LCD_W / 2;
        for (int x = 0; x < RW; x++) {
            u32 c = *src++;
            c |= c << 16;
            r0[x] = c; r1[x] = c;
        }
    }
}

static void message(const char *a, const char *b) {
    gfx_clear(0x0010);
    gfx_draw_text(4, 30, a, 0xFFFF, 1);
    gfx_draw_text(4, 50, b, 0xFFE0, 1);
    blit2x();
    Bdisp_PutDisp_DD();
}

int main(void) {
    Bdisp_EnableColor(1);
    EnableStatusArea(3);
    Bdisp_AllClr_VRAM();
    gfx_init(sFb, sZb, RW, RH, sTexCache, sizeof sTexCache);
    gfx_set_perspective(50, 64, 16000);
    u32 pack_size = 0;
    const u8 *pack = find_pack(&pack_size);
    DBG("SM50 start, pack %u bytes", (unsigned)pack_size);
    if (!pack) {
        message("SM50: no game data in this add-in", "run install-sm50.command");
        int k; for (;;) GetKey(&k);
    }
    int r = game_init(pack, pack_size, sColWork, sizeof sColWork);
    if (r) {
        char buf[32]; snprintf(buf, sizeof buf, "error %d", r);
        message("SM50: game data pack failed", buf);
        int k; for (;;) GetKey(&k);
    }
#ifdef SM50_NATIVE_PROBE
    int native_probe_ok = native_sm64_movement_probe();
    DBG("SM64 Q12 %s slide=%d walk=%d",
        native_probe_ok ? "PASS" : "FAIL", native_sm64_probe_slide_z_q12,
        native_sm64_probe_walk_speed_q12);
    DBG("SM64 ROM floor y_q8=%d ny_q14=%d walk=%d",
        native_sm64_probe_rom_floor_y_q8,
        native_sm64_probe_rom_normal_y_q14,
        native_sm64_probe_rom_walk_speed_q12);
#endif
#ifdef SM50_CANONICAL_PROBE
    int canonical_probe_ok = native_sm64_canonical_probe();
    DBG("SM64 canonical %s floor_y_q8=%d ny_q14=%d walk=%d",
        canonical_probe_ok ? "PASS" : "FAIL",
        native_sm64_canonical_floor_y_q8,
        native_sm64_canonical_floor_ny_q14,
        native_sm64_canonical_speed_q12);
    DBG("SM64 ground step=%d dx_q8=%d dz_q8=%d",
        native_sm64_canonical_ground_step,
        native_sm64_canonical_moved_x_q8,
        native_sm64_canonical_moved_z_q8);
    DBG("SM64 canonical star interaction %s",
        native_sm64_canonical_star_interaction_ok ? "PASS" : "FAIL");
    DBG("SM64 canonical ROM yellow coin %s",
        native_sm64_canonical_yellow_coin_check ? "PASS" : "FAIL");
    DBG("SM64 canonical ROM red coin %s",
        native_sm64_canonical_red_coin_check ? "PASS" : "FAIL");
    DBG("SM64 canonical eight red coins spawn star %s",
        native_sm64_canonical_red_star_check ? "PASS" : "FAIL");
    DBG("red coin state=0x%08x", native_sm64_canonical_red_coin_debug);
    DBG("star collision debug=%d hitboxes=%d saved=%d",
        native_sm64_canonical_star_debug,
        native_sm64_canonical_star_hitbox_debug,
        native_sm64_canonical_star_collected);
    if (canonical_probe_ok) {
        /* Publish canonical movement state to the existing renderer. */
        sm50_native_publish_mario(native_sm64_canonical_pos_x_q8,
                                  native_sm64_canonical_pos_y_q8,
                                  native_sm64_canonical_pos_z_q8,
                                  native_sm64_canonical_yaw,
                                  0, 0,
                                  native_sm64_canonical_pos_y_q8, 1, 0,
                                  native_sm64_canonical_anim_index);
    }
#endif
    DBG("level loaded: %d chunks %d objs %d SM64 placements", gLevel.n_chunks, gLevel.n_objs,
        gLevel.n_sm64_spawns);
    int last = RTC_GetTicks();
    int acc = 0; /* in 1/128 s * 30 units */
    int fps10 = 0, frames = 0, fps_t0 = last;
    int show_fps = 1;
    u32 frame_no = 0;
    for (;;) {
        if (keydown(K_MENU)) {
            /* hand control to the OS main menu; resumes when the user returns */
            int k; GetKey(&k);
            EnableStatusArea(3);
            last = RTC_GetTicks(); acc = 0;
        }
        {
            static int f6, f4;
            int k6 = keydown(K_F6), k4 = keydown(K_F4);
            if (k6 && !f6) show_fps = !show_fps;
            if (k4 && !f4) set_res(!sRes);
            f6 = k6; f4 = k4;
        }
        int now = RTC_GetTicks();
        int dt = now - last; last = now;
        if (dt < 0 || dt > 32) dt = 32;
        acc += dt * 30;
        int steps = 0;
        u16 b = read_buttons();
        /* fixed 30 Hz logic; at least one step per rendered frame, at most 2 to
         * avoid a spiral of death on slow frames (the game then slows down). */
        do { game_tick(b); acc -= 128; steps++; } while (acc >= 128 && steps < 2);
        if (acc > 128) acc = 0;
        if (acc < -128) acc = -128;
        memset(&gGfxStats, 0, sizeof gGfxStats);
        game_render(show_fps, fps10);
        blit2x();
        Bdisp_PutDisp_DD();
        frames++; frame_no++;
        if (now - fps_t0 >= 128) { fps10 = frames * 1280 / (now - fps_t0); frames = 0; fps_t0 = now; }
        DBG("F %u tris=%u px=%u x=%d y=%d z=%d act=%d", (unsigned)frame_no, (unsigned)gGfxStats.tris_drawn,
            (unsigned)gGfxStats.pixels, gGame.p.x >> 8, gGame.p.y >> 8, gGame.p.z >> 8, gGame.p.action);
    }
    return 0;
}
