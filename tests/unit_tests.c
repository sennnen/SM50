/* Host unit tests for the SM50 engine: fixed-point maths, the F3D interpreter
 * (texture formats, combiner, culling, depth), and collision queries. */
#include "../src/engine/gfx.h"
#include "../src/engine/world.h"
#include "../src/engine/fixmath.h"
#include "../src/platform/casio/display_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static u8 mem[65536];
static u32 memp;
static u32 put(const void *d, u32 n) { memp = (memp + 7) & ~7u; memcpy(mem + memp, d, n); u32 a = memp; memp += n; return 0x07000000 | a; }
static void be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = (u8)v; }

typedef struct { u32 w[512][2]; int n; } DLB;
static void cmd(DLB *d, u32 w0, u32 w1) { d->w[d->n][0] = w0; d->w[d->n][1] = w1; d->n++; }
static u32 dl_put(DLB *d) {
    u8 buf[4096];
    for (int i = 0; i < d->n; i++) { be32(buf + i * 8, d->w[i][0]); be32(buf + i * 8 + 4, d->w[i][1]); }
    return put(buf, d->n * 8);
}
static void vtx(u8 *p, int x, int y, int z, int s, int t, int r, int g, int b) {
    be16(p, (u16)x); be16(p + 2, (u16)y); be16(p + 4, (u16)z); be16(p + 6, 0); be16(p + 8, (u16)s); be16(p + 10, (u16)t);
    p[12] = (u8)r; p[13] = (u8)g; p[14] = (u8)b; p[15] = 255;
}

#define W 64
#define H 48
static u16 fb[W * H], zb[W * H];
static u8 tc[32768];

static void setup_view(void) {
    Mat34 v; Vec3i eye = { 0, 0, -1000 }, tgt = { 0, 0, 0 };
    mat_lookat(&v, eye, tgt);
    gfx_set_view(&v);
    Mat34 id; mat_identity(&id); gfx_set_model(&id);
}

/* A camera-facing quad (CCW as seen from the camera at -Z looking +Z). */
static u32 quad_dl(int fmt, int siz, u32 timg, u32 pal, int tw, int th, int z, int r, int g, int b, int textured) {
    u8 v[64];
    int S = tw * 32, T = th * 32;
    /* camera at z=-1000 looking toward +z: screen right = world -x */
    vtx(v + 0, 300, -300, z, 0, T, r, g, b);
    vtx(v + 16, -300, -300, z, S, T, r, g, b);
    vtx(v + 32, -300, 300, z, S, 0, r, g, b);
    vtx(v + 48, 300, 300, z, 0, 0, r, g, b);
    u32 va = put(v, 64);
    DLB d; d.n = 0;
    cmd(&d, 0xB6000000, G_LIGHTING | G_CULL_FRONT);
    cmd(&d, 0xB7000000, G_SHADE | G_CULL_BACK);
    if (textured) {
        cmd(&d, 0xBB000001, 0xFFFFFFFF);
        /* combine TEXEL0 * SHADE */
        cmd(&d, 0xFC000000 | (1 << 20) | (4 << 15) | (1 << 5) | 4, (8u << 28) | (8u << 24) | (7 << 15) | (7 << 6));
        if (pal) { cmd(&d, 0xFD100000, pal); cmd(&d, 0xF5000100, 0x07000000); cmd(&d, 0xF0000000, 0x07000000 | (15 << 14)); }
        cmd(&d, 0xFD000000 | (fmt << 21) | (siz << 19), timg);
        cmd(&d, 0xF5000000 | (fmt << 21) | (siz << 19), 0x07000000);
        cmd(&d, 0xF3000000, 0x07000000);
        int ml = 0; while ((1 << ml) < tw) ml++;
        int mt = 0; while ((1 << mt) < th) mt++;
        cmd(&d, 0xF5000000 | (fmt << 21) | (siz << 19), (mt << 14) | (ml << 4));
        cmd(&d, 0xF2000000, ((tw - 1) << 2) << 12 | ((th - 1) << 2));
    } else {
        cmd(&d, 0xBB000000, 0);
        cmd(&d, 0xFC000000 | (15 << 20) | (31 << 15), (15u << 28) | (4 << 15));
    }
    cmd(&d, 0x04000000 | (3 << 20) | 64, va);
    cmd(&d, 0xBF000000, (0 << 16) | (10 << 8) | 20);
    cmd(&d, 0xBF000000, (0 << 16) | (20 << 8) | 30);
    cmd(&d, 0xB8000000, 0);
    return dl_put(&d);
}

static void test_math(void) {
    CHECK(fsin(0) == 0 && fsin(0x4000) == 16384 && fcos(0) == 16384, "sin/cos basics");
    CHECK(abs(fsin(0x2000) - 11585) < 8, "sin 45 = %d", fsin(0x2000));
    CHECK(fatan2(0, 1) == 0, "atan2 0");
    CHECK(abs((s16)(fatan2(1, 0) - 0x4000)) < 4, "atan2 90 = %x", fatan2(1, 0));
    CHECK(abs((s16)(fatan2(-100, -100) - (s16)0xA000)) < 16, "atan2 -135 = %x", fatan2(-100, -100));
    CHECK(isqrt32(1000000) == 1000 && isqrt64(10000000000ull) == 100000, "isqrt");
    Mat34 a, b, c;
    mat_from_trs(&a, 10, 20, 30, 0, 0x4000, 0, ONE14);
    mat_identity(&b);
    mat_mul(&c, &a, &b);
    CHECK(memcmp(&a, &c, sizeof a) == 0, "identity multiply");
    s32 x, y, z;
    mat_transform(&a, 100, 0, 0, &x, &y, &z);
    CHECK(abs(x - 10) <= 1 && abs(z - (30 - 100)) <= 1, "Ry(90) maps +x to -z: %d %d %d", x, y, z);
}

static void test_render(void) {
    gfx_init(fb, zb, W, H, tc, sizeof tc);
    gfx_set_segment(7, mem);
    gfx_set_perspective(60, 64, 16000);
    setup_view();
    /* 1: flat shaded quad, colour from vertices */
    gfx_clear(0); gfx_clear_depth(); gfx_reset_state();
    u32 dl = quad_dl(0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0);
    gfx_run_dl(dl);
    u16 c = fb[(H / 2) * W + W / 2];
    CHECK((c >> 11) > 26 && ((c >> 5) & 63) < 4 && (c & 31) < 4, "red quad centre = %04x", c);
    CHECK(fb[0] == 0, "corner untouched");
    /* 2: back face culled (reverse winding by viewing from behind) */
    Mat34 v; Vec3i eye = { 0, 0, 1000 }, tgt = { 0, 0, 0 };
    mat_lookat(&v, eye, tgt); gfx_set_view(&v);
    gfx_clear(0); gfx_clear_depth(); gfx_reset_state();
    gfx_run_dl(dl);
    CHECK(fb[(H / 2) * W + W / 2] == 0, "back face culled");
    setup_view();
    /* 3: depth - near green quad over far red quad, drawn far-then-near and near-then-far */
    u32 dl_near = quad_dl(0, 0, 0, 0, 0, 0, -200, 0, 255, 0, 0);
    for (int order = 0; order < 2; order++) {
        gfx_clear(0); gfx_clear_depth(); gfx_reset_state();
        if (order == 0) { gfx_run_dl(dl); gfx_run_dl(dl_near); } else { gfx_run_dl(dl_near); gfx_run_dl(dl); }
        c = fb[(H / 2) * W + W / 2];
        CHECK(((c >> 5) & 63) > 55 && (c >> 11) < 4, "depth order %d: centre %04x should be green", order, c);
    }
    /* 4: texture formats. 8x8 texture: left half colour A, right half colour B */
    struct { int fmt, siz; const char *name; } fmts[] = { {0, 2, "RGBA16"}, {0, 3, "RGBA32"}, {2, 0, "CI4"}, {2, 1, "CI8"},
                                                          {3, 1, "IA8"}, {3, 2, "IA16"}, {4, 0, "I4"}, {4, 1, "I8"}, {3, 0, "IA4"} };
    for (unsigned f = 0; f < sizeof fmts / sizeof fmts[0]; f++) {
        u8 tex[256]; memset(tex, 0, sizeof tex);
        u8 pal[32]; memset(pal, 0, sizeof pal);
        int fmt = fmts[f].fmt, siz = fmts[f].siz;
        u32 palad = 0;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int right = x >= 4, i = y * 8 + x;
                if (fmt == 0 && siz == 2) be16(tex + i * 2, right ? 0x07C1 : 0xF801);         /* green : red */
                else if (fmt == 0 && siz == 3) { u8 *p = tex + i * 4; p[0] = right ? 0 : 255; p[1] = right ? 255 : 0; p[2] = 0; p[3] = 255; }
                else if (fmt == 2 && siz == 0) tex[i / 2] |= (u8)((right ? 1 : 2) << ((i & 1) ? 0 : 4));
                else if (fmt == 2 && siz == 1) tex[i] = right ? 1 : 2;
                else if (fmt == 3 && siz == 1) tex[i] = right ? 0xFF : 0x0F;                   /* white : black opaque */
                else if (fmt == 3 && siz == 2) { tex[i * 2] = right ? 255 : 0; tex[i * 2 + 1] = 255; }
                else if (fmt == 4 && siz == 0) tex[i / 2] |= (u8)((right ? 15 : 0) << ((i & 1) ? 0 : 4));
                else if (fmt == 4 && siz == 1) tex[i] = right ? 255 : 0;
                else if (fmt == 3 && siz == 0) tex[i / 2] |= (u8)((right ? 15 : 1) << ((i & 1) ? 0 : 4));
            }
        if (fmt == 2) { be16(pal + 2, 0x07C1); be16(pal + 4, 0xF801); palad = put(pal, 32); }
        u32 ta = put(tex, sizeof tex);
        u32 d2 = quad_dl(fmt, siz, ta, palad, 8, 8, 0, 255, 255, 255, 1);
        gfx_clear(0x001F); gfx_clear_depth(); gfx_reset_state();
        gfx_run_dl(d2);
        /* camera looks +z, world -x is screen right; texture s increases toward screen right */
        u16 L = fb[(H / 2) * W + W / 2 - 6], R = fb[(H / 2) * W + W / 2 + 6];
        int lr = L >> 11, lg = (L >> 5) & 63, rr = R >> 11, rg = (R >> 5) & 63;
        if (fmt == 0 || fmt == 2)
            CHECK(lr > 25 && lg < 6 && rg > 50 && rr < 4, "%s: left %04x should be red, right %04x green", fmts[f].name, L, R);
        else
            CHECK(lg < 12 && rg > 50 && rr > 25, "%s: left %04x should be dark, right %04x white", fmts[f].name, L, R);
    }
    /* 5: near-plane clipping: a quad that straddles the camera must not crash and must fill the screen bottom */
    gfx_clear(0); gfx_clear_depth(); gfx_reset_state();
    u8 v4[64];
    vtx(v4 + 0, 3000, -200, -3000, 0, 0, 0, 0, 255); vtx(v4 + 16, -3000, -200, -3000, 0, 0, 0, 0, 255);
    vtx(v4 + 32, -3000, -200, 3000, 0, 0, 0, 0, 255); vtx(v4 + 48, 3000, -200, 3000, 0, 0, 0, 0, 255);
    u32 va = put(v4, 64);
    DLB d; d.n = 0;
    cmd(&d, 0xB6000000, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT);
    cmd(&d, 0xFC000000 | (15 << 20) | (31 << 15), (15u << 28) | (4 << 15));
    cmd(&d, 0x04000000 | (3 << 20) | 64, va);
    cmd(&d, 0xBF000000, (0 << 16) | (10 << 8) | 20);
    cmd(&d, 0xBF000000, (0 << 16) | (20 << 8) | 30);
    cmd(&d, 0xB8000000, 0);
    gfx_run_dl(dl_put(&d));
    int bottom = 0, top = 0;
    for (int x = 0; x < W; x++) { bottom += fb[(H - 1) * W + x] != 0; top += fb[x] != 0; }
    CHECK(bottom == W && top == 0, "ground plane clip: bottom row %d/%d filled, top row %d", bottom, W, top);
    /* A list without ENDDL must stop at the segment boundary. */
    DLB truncated; truncated.n = 0;
    cmd(&truncated, 0, 0);
    u32 truncated_dl = dl_put(&truncated);
    gfx_set_segment_sized(7, mem, memp);
    u32 commands_before = gGfxStats.dl_cmds;
    gfx_run_dl(truncated_dl);
    CHECK(gGfxStats.dl_cmds == commands_before + 1 && gfx_seg_to_ptr(0x07000000 | memp) == NULL,
          "display list and address stop at segment boundary");
    printf("render tests: tris_in=%u drawn=%u culled=%u\n", gGfxStats.tris_in, gGfxStats.tris_drawn, gGfxStats.tris_culled);
}

static void test_collision(void) {
    /* floor square (2 tris) at y=100 spanning +-500, a wall at x=300 facing -x, a ceiling at y=400 */
    static u8 col[4 + 28 * 6];
    be32(col, 6);
    s16 tris[6][12] = {
        { -500, 100, -500, -500, 100, 500, 500, 100, 500, 0, 16384, 0 },
        { -500, 100, -500, 500, 100, 500, 500, 100, -500, 0, 16384, 0 },
        { 300, 0, -500, 300, 0, 500, 300, 600, 500, -16384, 0, 0 },
        { 300, 0, -500, 300, 600, 500, 300, 600, -500, -16384, 0, 0 },
        { -500, 400, -500, 500, 400, 500, -500, 400, 500, 0, -16384, 0 },
        { -500, 400, -500, 500, 400, -500, 500, 400, 500, 0, -16384, 0 },
    };
    for (int t = 0; t < 6; t++) {
        u8 *p = col + 4 + t * 28;
        for (int k = 0; k < 12; k++) be16(p + k * 2, (u16)tris[t][k]);
        be16(p + 24, 0); be16(p + 26, 0);
    }
    static u8 work[8192];
    CHECK(col_init(col, work, sizeof work), "col_init");
    s32 ny;
    s32 f = col_find_floor(0, 150, 0, &ny, NULL);
    CHECK(f == 100 * 256 && ny == 16384, "floor at 100: %d", f >> 8);
    CHECK(col_find_floor(0, -50, 0, NULL, NULL) == INT32_MIN, "no floor far below");
    CHECK(col_find_floor(800, 150, 0, NULL, NULL) == INT32_MIN, "no floor outside");
    CHECK(col_find_ceil(0, 200, 0) == 400 * 256, "ceiling 400");
    s32 x = 280 * 256, z = 0;
    int hits = col_resolve_walls(&x, 150, &z, 50, NULL);
    CHECK(hits == 1 && abs((x >> 8) - 250) <= 1, "wall push: hits=%d x=%d", hits, x >> 8);
    x = 100 * 256; z = 0;
    CHECK(col_resolve_walls(&x, 150, &z, 50, NULL) == 0, "no wall push when far");
}

static void test_display_bounds(void) {
    CHECK(SM50_OS_VRAM_BYTES == 162 * 1024, "OS VRAM is 384x216 RGB565 (%u bytes)",
          (unsigned)SM50_OS_VRAM_BYTES);
    CHECK(SM50_VRAM_2X_LAST_PIXEL == SM50_OS_VRAM_PIXELS - 1,
          "2x final write lands on final VRAM pixel (%u)",
          (unsigned)SM50_VRAM_2X_LAST_PIXEL);
    CHECK(SM50_VRAM_3X_LAST_PIXEL == SM50_OS_VRAM_PIXELS - 1,
          "3x final write lands on final VRAM pixel (%u)",
          (unsigned)SM50_VRAM_3X_LAST_PIXEL);
    for (int y = 0; y < SM50_RENDER_2X_H; y++) {
        for (int x = 0; x < SM50_RENDER_2X_W; x++) {
            u32 last = ((u32)y * 2 + 1) * SM50_OS_VRAM_W + (u32)x * 2 + 1;
            CHECK(last < SM50_OS_VRAM_PIXELS, "2x blit pixel (%d,%d) is in VRAM", x, y);
        }
    }
    for (int y = 0; y < SM50_RENDER_3X_H; y++) {
        for (int x = 0; x < SM50_RENDER_3X_W; x++) {
            u32 last = ((u32)y * 3 + 2) * SM50_OS_VRAM_W + (u32)x * 3 + 2;
            CHECK(last < SM50_OS_VRAM_PIXELS, "3x blit pixel (%d,%d) is in VRAM", x, y);
        }
    }
}

int main(void) {
    test_math();
    test_render();
    test_collision();
    test_display_bounds();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
