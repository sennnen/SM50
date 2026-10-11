/* SM50 renderer implementation. See gfx.h. */
#include "gfx.h"
#include <string.h>

GfxStats gGfxStats;

/* ------------------------------------------------------------------ state */
static u16 *sColor, *sDepth;
static int sW, sH;
static s32 sFocalX, sFocalY; /* Q4 pixels at depth 1 */
static s32 sCenterX, sCenterY; /* Q4 */
static s32 sNear = 64, sFar = 16000;
static const u8 *sSegments[GFX_MAX_SEGMENTS];

static Mat34 sView, sModel, sModelView;
#define MTX_STACK 10
static Mat34 sMtxStack[MTX_STACK];
static int sMtxSP;

static u32 sGeomMode;
static u32 sOtherL, sOtherH;
static u32 sPrimColor = 0xFFFFFFFF, sEnvColor = 0xFFFFFFFF;
static int sCombTex, sCombShade, sCombPrim, sCombEnv;

/* lights: colours 0..255, dirs Q14 in view space */
typedef struct { s32 r, g, b; s32 dx, dy, dz; } Light;
static Light sLights[8];
static int sNumLights = 1; /* number of directional lights (ambient is extra) */
static s32 sAmbR = 255, sAmbG = 255, sAmbB = 255;
static s32 sLightMdl[8][3]; /* light dirs transformed into model space */
static int sLightsDirty = 1;

/* textures / tiles */
typedef struct { u8 fmt, siz, cms, cmt, masks, maskt, shifts, shiftt, palette; u16 line, tmem; s32 uls, ult, lrs, lrt; } Tile;
static Tile sTiles[8];
static u32 sTimgAddr; static u8 sTimgFmt, sTimgSiz; static u16 sTimgWidth;
static u32 sTmemSrc[512]; /* tmem word address (64-bit units) -> source image addr */
static u32 sTlutSrc;
static u32 sTexScaleS = 0xFFFF, sTexScaleT = 0xFFFF;
static int sTexOn;

typedef struct {
    u32 key_addr, key_pal; u16 w, h; u8 fmt, siz;
    u16 *data; u32 lru; int has_alpha;
} TexEntry;
#define TEX_ENTRIES 128
static TexEntry sTexCache[TEX_ENTRIES];
static u8 *sTexMem; static u32 sTexMemSize, sTexMemUsed; static u32 sTexClock;

/* current resolved material */
#define MF_TEX 1
#define MF_SHADE 2
#define MF_ALPHA 4   /* texture has transparent texels: alpha test */
#define MF_XLU 8     /* blend 50% with the framebuffer */
#define MF_CLAMPS 16
#define MF_CLAMPT 32
#define MF_NOZW 64   /* don't write depth */
typedef struct {
    int flags; const u16 *tex; int tw_log, th_log, tw, th; u16 flat;
    s32 uoff, voff; /* tile origin in Q5 texels */
} Material;
static Material sMat;
static int sMatDirty = 1;

/* vertices */
typedef struct {
    s32 vx, vy, vz;   /* view space */
    s32 u, v;         /* Q5 texels */
    s32 r, g, b;      /* 0..255 */
    s32 sx, sy, q;    /* screen Q4, q = 2^28 / vz (Q8 of 2^20/vz) */
    u8 clip;          /* 1 = behind near plane */
} XVtx;
#define VTX_MAX 32
static XVtx sVtx[VTX_MAX];

/* -------------------------------------------------------------- utilities */
void gfx_init(u16 *colorbuf, u16 *zbuf, int width, int height, u8 *texcache, u32 texcache_size) {
    sColor = colorbuf; sDepth = zbuf; sW = width; sH = height;
    sCenterX = (width * 16) / 2; sCenterY = (height * 16) / 2;
    sTexMem = texcache; sTexMemSize = texcache_size; sTexMemUsed = 0;
    memset(sTexCache, 0, sizeof(sTexCache));
    memset(sSegments, 0, sizeof(sSegments));
    gfx_set_perspective(45, 64, 16000);
    mat_identity(&sView); mat_identity(&sModel); mat_identity(&sModelView);
    gfx_reset_state();
}

int gfx_width(void) { return sW; }
int gfx_height(void) { return sH; }
u16 *gfx_colorbuf(void) { return sColor; }

void gfx_set_segment(int seg, const void *base) {
    if (seg >= 0 && seg < GFX_MAX_SEGMENTS) sSegments[seg] = (const u8 *)base;
}

const u8 *gfx_seg_to_ptr(u32 addr) {
    u32 seg = (addr >> 24) & 0x1F;
    if (sSegments[seg] == NULL) return NULL;
    return sSegments[seg] + (addr & 0x00FFFFFF);
}

void gfx_set_perspective(int fov_deg, s32 near_z, s32 far_z) {
    /* focal = (H/2) / tan(fov/2); tan via sin/cos table */
    u16 half = (u16)((fov_deg * 65536) / 720);
    s32 s = fsin(half), c = fcos(half);
    s32 focal = (s32)(((s64)(sH * 16 / 2) * c) / (s ? s : 1));
    sFocalY = focal;
    sFocalX = focal; /* square pixels */
    sNear = near_z < 16 ? 16 : near_z; sFar = far_z;
}

void gfx_set_view(const Mat34 *view) { sView = *view; gfx_set_model(&sModel); }
const Mat34 *gfx_get_view(void) { return &sView; }

void gfx_set_model(const Mat34 *model) {
    sModel = *model;
    mat_mul(&sModelView, &sView, &sModel);
    sLightsDirty = 1;
}

void gfx_set_model_billboard(s32 x, s32 y, s32 z, s32 scale14) {
    s32 vx, vy, vz;
    mat_transform(&sView, x, y, z, &vx, &vy, &vz);
    Mat34 m;
    mat_identity(&m);
    for (int i = 0; i < 3; i++) m.m[i][i] = scale14;
    /* x right is the view-space +X, but our view basis maps world into a
     * left-handed view, so flip X to keep billboards un-mirrored */
    m.m[2][2] = -scale14; /* model +Z faces the camera */
    m.m[0][3] = vx; m.m[1][3] = vy; m.m[2][3] = vz;
    sModelView = m;
    sLightsDirty = 1;
}

void gfx_clear(u16 color) {
    u32 c2 = ((u32)color << 16) | color;
    u32 *p = (u32 *)sColor; int n = (sW * sH) >> 1;
    while (n--) *p++ = c2;
}

void gfx_clear_depth(void) { memset(sDepth, 0, (size_t)sW * sH * 2); }

int gfx_project_point(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *depth) {
    s32 vx, vy, vz;
    mat_transform(&sView, x, y, z, &vx, &vy, &vz);
    if (vz < sNear) return 0;
    *sx = (sCenterX + (s32)(((s64)vx * sFocalX) / vz)) >> 4;
    *sy = (sCenterY - (s32)(((s64)vy * sFocalY) / vz)) >> 4;
    if (depth) *depth = vz;
    return 1;
}

int gfx_sphere_visible(s32 x, s32 y, s32 z, s32 radius) {
    s32 vx, vy, vz;
    mat_transform(&sView, x, y, z, &vx, &vy, &vz);
    if (vz + radius < sNear || vz - radius > sFar) return 0;
    /* side planes: |vx| * focal / vz <= W/2 (+ radius slack) */
    s64 lim = (s64)(vz + radius * 2) * (sW * 8);
    if ((s64)(vx - radius) * sFocalX > lim) return 0;
    if ((s64)(vx + radius) * sFocalX < -lim) return 0;
    s64 limy = (s64)(vz + radius * 2) * (sH * 8);
    if ((s64)(vy - radius) * sFocalY > limy) return 0;
    if ((s64)(vy + radius) * sFocalY < -limy) return 0;
    return 1;
}

void gfx_reset_state(void) {
    sGeomMode = G_ZBUFFER | G_SHADE | G_CULL_BACK | G_SHADING_SMOOTH;
    sOtherL = sOtherH = 0;
    sPrimColor = sEnvColor = 0xFFFFFFFF;
    sCombTex = 0; sCombShade = 1; sCombPrim = 0; sCombEnv = 0;
    sNumLights = 1; sAmbR = sAmbG = sAmbB = 100;
    sLights[0].r = sLights[0].g = sLights[0].b = 155;
    sLights[0].dx = 0; sLights[0].dy = 0; sLights[0].dz = -ONE14;
    sLightsDirty = 1;
    sTexOn = 0; sTexScaleS = sTexScaleT = 0xFFFF;
    memset(sTiles, 0, sizeof(sTiles));
    sMtxSP = 0;
    sMatDirty = 1;
}

/* ------------------------------------------------------- texture decoding */
static u16 n64_to_565(u16 c, int *alpha) {
    *alpha = c & 1;
    return (u16)((c & 0xFFC0) | ((c >> 1) & 0x1F));
}

static u16 key_fix(u16 c) { return c == GFX_TRANSPARENT ? (u16)(GFX_TRANSPARENT ^ 0x20) : c; }

static void tex_evict_all(void) {
    for (int i = 0; i < TEX_ENTRIES; i++) sTexCache[i].data = NULL;
    sTexMemUsed = 0;
}

static TexEntry *tex_lookup(u32 addr, u32 pal, int fmt, int siz, int w, int h) {
    TexEntry *free_e = NULL, *old = NULL;
    for (int i = 0; i < TEX_ENTRIES; i++) {
        TexEntry *e = &sTexCache[i];
        if (e->data == NULL) { if (!free_e) free_e = e; continue; }
        if (e->key_addr == addr && e->key_pal == pal && e->fmt == fmt && e->siz == siz && e->w == w && e->h == h) {
            e->lru = ++sTexClock; return e;
        }
        if (!old || e->lru < old->lru) old = e;
    }
    u32 bytes = (u32)w * h * 2;
    if (bytes > sTexMemSize) return NULL;
    if (sTexMemUsed + bytes > sTexMemSize || free_e == NULL) { tex_evict_all(); free_e = &sTexCache[0]; }
    (void)old;
    const u8 *src = gfx_seg_to_ptr(addr);
    if (!src) return NULL;
    const u8 *psrc = pal ? gfx_seg_to_ptr(pal) : NULL;
    TexEntry *e = free_e;
    e->key_addr = addr; e->key_pal = pal; e->fmt = (u8)fmt; e->siz = (u8)siz; e->w = (u16)w; e->h = (u16)h;
    e->data = (u16 *)(sTexMem + sTexMemUsed); sTexMemUsed += (bytes + 3) & ~3u;
    e->lru = ++sTexClock; e->has_alpha = 0;
    gGfxStats.tex_converts++;
    int n = w * h;
    for (int i = 0; i < n; i++) {
        int a = 1; u16 c = 0;
        if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b) {
            c = n64_to_565(BE16(src + i * 2), &a);
        } else if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_32b) {
            const u8 *p = src + i * 4; c = rgb565(p[0], p[1], p[2]); a = p[3] >= 128;
        } else if (fmt == G_IM_FMT_CI) {
            int idx = siz == G_IM_SIZ_4b ? ((src[i >> 1] >> ((i & 1) ? 0 : 4)) & 0xF) : src[i];
            c = psrc ? n64_to_565(BE16(psrc + idx * 2), &a) : (u16)(idx * 0x0841);
        } else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_4b) {
            int b = (src[i >> 1] >> ((i & 1) ? 0 : 4)) & 0xF; int I = (b >> 1) * 255 / 7;
            c = rgb565(I, I, I); a = b & 1;
        } else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_8b) {
            int b = src[i]; int I = (b >> 4) * 17; c = rgb565(I, I, I); a = (b & 0xF) >= 8;
        } else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_16b) {
            int I = src[i * 2]; c = rgb565(I, I, I); a = src[i * 2 + 1] >= 128;
        } else if (fmt == G_IM_FMT_I && siz == G_IM_SIZ_4b) {
            int I = ((src[i >> 1] >> ((i & 1) ? 0 : 4)) & 0xF) * 17; c = rgb565(I, I, I);
        } else if (fmt == G_IM_FMT_I && siz == G_IM_SIZ_8b) {
            int I = src[i]; c = rgb565(I, I, I);
        }
        if (a) e->data[i] = key_fix(c);
        else { e->data[i] = GFX_TRANSPARENT; e->has_alpha = 1; }
    }
    return e;
}

static int ilog2(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

static void resolve_material(void) {
    sMatDirty = 0;
    int flags = 0;
    if (sCombShade && (sGeomMode & G_SHADE)) flags |= MF_SHADE;
    /* render mode: FORCE_BL with no Z update -> translucent */
    if ((sOtherL & 0x4000) && !(sOtherL & 0x20)) flags |= MF_XLU | MF_NOZW;
    sMat.flat = 0xFFFF;
    if (sCombPrim) sMat.flat = rgb565(sPrimColor >> 24, (sPrimColor >> 16) & 0xFF, (sPrimColor >> 8) & 0xFF);
    else if (sCombEnv) sMat.flat = rgb565(sEnvColor >> 24, (sEnvColor >> 16) & 0xFF, (sEnvColor >> 8) & 0xFF);
    sMat.tex = NULL;
    if (sTexOn && sCombTex) {
        Tile *t = &sTiles[0];
        int w = ((t->lrs - t->uls) >> 2) + 1, h = ((t->lrt - t->ult) >> 2) + 1;
        if (t->masks) w = 1 << t->masks;
        if (t->maskt) h = 1 << t->maskt;
        if (w > 0 && h > 0 && w <= 256 && h <= 256) {
            u32 src = sTmemSrc[t->tmem & 511];
            u32 pal = t->fmt == G_IM_FMT_CI ? sTlutSrc : 0;
            if (pal && t->siz == G_IM_SIZ_4b) pal += t->palette * 32;
            TexEntry *e = src ? tex_lookup(src, pal, t->fmt, t->siz, w, h) : NULL;
            if (e) {
                flags |= MF_TEX;
                if (e->has_alpha) flags |= MF_ALPHA;
                sMat.tex = e->data; sMat.tw = w; sMat.th = h;
                sMat.tw_log = ilog2(w); sMat.th_log = ilog2(h);
                if ((1 << sMat.tw_log) != w || (t->cms & 2)) flags |= MF_CLAMPS;
                if ((1 << sMat.th_log) != h || (t->cmt & 2)) flags |= MF_CLAMPT;
                sMat.uoff = t->uls << 3; sMat.voff = t->ult << 3; /* 10.2 -> 10.5 */
            }
        }
    }
    sMat.flags = flags;
}

/* ------------------------------------------------------------ rasteriser */
typedef struct { s32 x, y, q, uq, vq, r, g, b; } RVtx; /* screen-space vertex */

#define SUBDIV_LOG 3

static void raster_tri(const RVtx *a, const RVtx *b, const RVtx *c) {
    const Material *m = &sMat;
    /* sort by y */
    const RVtx *t;
    if (a->y > b->y) { t = a; a = b; b = t; }
    if (b->y > c->y) { t = b; b = c; c = t; }
    if (a->y > b->y) { t = a; a = b; b = t; }
    s32 y0 = a->y, y1 = b->y, y2 = c->y;
    if (y2 - y0 <= 0) return;
    int ytop = (y0 + 7) >> 4, ybot = (y2 + 7) >> 4; /* rows whose centre (row*16+8) lies in [y0, y2) */
    if (ytop < 0) ytop = 0;
    if (ybot > sH) ybot = sH;
    if (ytop >= ybot) return;

    /* long edge a->c at y1 */
    s32 dy_ac = y2 - y0;
    s32 tq16 = (s32)(((s64)(y1 - y0) << 16) / dy_ac);
#define LERP(f) (a->f + (s32)(((s64)(c->f - a->f) * tq16) >> 16))
    s32 xl = LERP(x);
    s32 width = xl - b->x; /* Q4; >0 means long edge on the right */
    if (width == 0) return;
    s32 aw = width < 0 ? -width : width;
    if (aw < 2) return;
    s64 invw = ((s64)1 << 34) / aw; /* 2^34 / width(Q4) */
    if (width < 0) invw = -invw;
    /* gradients per pixel: (A_l - A_b) / (width/16) = diff * 16 / width = diff * invw >> 30 */
#define DX(f) ((s32)(((s64)(LERP(f) - b->f) * invw) >> 30))
    s32 dq = DX(q), duq = 0, dvq = 0, dr = 0, dg = 0, db = 0;
    int tex = m->flags & MF_TEX, shade = m->flags & MF_SHADE;
    if (tex) { duq = DX(uq); dvq = DX(vq); }
    if (shade) { dr = DX(r); dg = DX(g); db = DX(b); }
#undef DX
#undef LERP

    /* Edges: we walk the left edge attributes and both x positions. */
    const RVtx *lt, *lb; /* left edge current segment */
    int long_right = width > 0;
    /* x stepping in Q16 per scanline */
    s32 inv_ac = (s32)(((s64)1 << 34) / dy_ac);
    s32 dxdy_ac = (s32)(((s64)(c->x - a->x) * inv_ac) >> 18); /* Q16 x per scanline */

    for (int seg = 0; seg < 2; seg++) {
        const RVtx *s0 = seg == 0 ? a : b, *s1 = seg == 0 ? b : c;
        s32 sy0 = s0->y, sy1 = s1->y;
        int rs = (sy0 + 7) >> 4, re = (sy1 + 7) >> 4;
        if (rs < ytop) rs = ytop;
        if (re > ybot) re = ybot;
        if (rs >= re || sy1 - sy0 <= 0) continue;
        s32 inv_s = (s32)(((s64)1 << 34) / (sy1 - sy0));
        s32 dxdy_s = (s32)(((s64)(s1->x - s0->x) * inv_s) >> 18);
        /* positions at the first row centre (row*16 + 8 in Q4) */
        s32 ycen = rs * 16 + 8;
        s32 xs = (s0->x << 12) + (s32)(((s64)dxdy_s * (ycen - sy0)) >> 4);
        s32 xa = (a->x << 12) + (s32)(((s64)dxdy_ac * (ycen - y0)) >> 4);
        /* left edge is the short edge if long edge is on the right */
        const RVtx *e0, *e1; s32 xleft, dxl, xright, dxr;
        if (long_right) { e0 = s0; e1 = s1; xleft = xs; dxl = dxdy_s; xright = xa; dxr = dxdy_ac; }
        else { e0 = a; e1 = c; xleft = xa; dxl = dxdy_ac; xright = xs; dxr = dxdy_s; }
        lt = e0; lb = e1; (void)lb;
        /* attributes on the left edge: A(x,y) plane evaluation from vertex e0 using
         * gradient dA/dx and the edge: we compute dA/dy along the edge directly. */
        s32 edy = e1->y - e0->y;
        s32 inv_e = (s32)(((s64)1 << 34) / (edy > 0 ? edy : 1));
#define EDY(f) ((s32)(((s64)(e1->f - e0->f) * inv_e) >> 30)) /* per scanline */
        s32 et16 = (s32)(((s64)(ycen - e0->y) * inv_e) >> 18);
#define EAT(f) (e0->f + (s32)(((s64)(e1->f - e0->f) * et16) >> 16))
        s32 qL = EAT(q), dqL = EDY(q);
        s32 uL = 0, duL = 0, vL = 0, dvL = 0, rL = 0, drL = 0, gL = 0, dgL = 0, bL = 0, dbL = 0;
        if (tex) { uL = EAT(uq); duL = EDY(uq); vL = EAT(vq); dvL = EDY(vq); }
        if (shade) { rL = EAT(r); drL = EDY(r); gL = EAT(g); dgL = EDY(g); bL = EAT(b); dbL = EDY(b); }
#undef EDY
#undef EAT
        /* the edge point at row centre has x = xleft (Q16); attributes at x are on the edge */
        for (int y = rs; y < re; y++) {
            int x0 = (xleft + 0x7FFF) >> 16, x1 = (xright + 0x7FFF) >> 16;
            if (x0 < 0) x0 = 0;
            if (x1 > sW) x1 = sW;
            if (x0 < x1) {
                /* prestep from the exact left edge to pixel centre x0+0.5 */
                s32 pre = ((x0 << 16) + 0x8000) - xleft; /* Q16 pixels */
                s32 q = qL + (s32)(((s64)dq * pre) >> 16);
                u16 *cp = sColor + y * sW + x0, *zp = sDepth + y * sW + x0;
                int n = x1 - x0;
                gGfxStats.pixels += n;
                if (tex) {
                    s32 uq = uL + (s32)(((s64)duq * pre) >> 16);
                    s32 vq = vL + (s32)(((s64)dvq * pre) >> 16);
                    s32 rr = 0, dr2 = 0;
                    if (shade) {
                        /* textured+shade: modulate by intensity (green-weighted luma) */
                        s32 r0 = rL + (s32)(((s64)dr * pre) >> 16), g0 = gL + (s32)(((s64)dg * pre) >> 16), b0 = bL + (s32)(((s64)db * pre) >> 16);
                        rr = (r0 * 5 + g0 * 9 + b0 * 2) >> 4; dr2 = (dr * 5 + dg * 9 + db * 2) >> 4;
                    }
                    const u16 *tx = m->tex; int tws = m->tw_log, wm = m->tw - 1, hm = m->th - 1;
                    int clamp = m->flags & (MF_CLAMPS | MF_CLAMPT), alpha = m->flags & MF_ALPHA, xlu = m->flags & MF_XLU;
                    int zw = !(m->flags & MF_NOZW);
                    s32 uoff = m->uoff, voff = m->voff;
                    while (n > 0) {
                        int span = n > (1 << SUBDIV_LOG) ? (1 << SUBDIV_LOG) : n;
                        s32 qa = q >> 8; if (qa < 1) qa = 1; if (qa > 16384) qa = 16384;
                        s32 qb = (q + dq * span) >> 8; if (qb < 1) qb = 1; if (qb > 16384) qb = 16384;
                        s32 ua = (s32)(((s64)uq * gRecipTable[qa]) >> 30) - uoff;
                        s32 va = (s32)(((s64)vq * gRecipTable[qa]) >> 30) - voff;
                        s32 ub = (s32)(((s64)(uq + duq * span) * gRecipTable[qb]) >> 30) - uoff;
                        s32 vb = (s32)(((s64)(vq + dvq * span) * gRecipTable[qb]) >> 30) - voff;
                        s32 du, dv;
                        if (span == (1 << SUBDIV_LOG)) { du = (ub - ua) >> SUBDIV_LOG; dv = (vb - va) >> SUBDIV_LOG; }
                        else { du = (ub - ua) / span; dv = (vb - va) / span; }
                        s32 uu = ua << 0, vv = va;
                        for (int i = 0; i < span; i++) {
                            u16 z = (u16)(q >> 7);
                            if (z > *zp) {
                                int tu = uu >> 5, tv = vv >> 5;
                                if (clamp) {
                                    if (m->flags & MF_CLAMPS) tu = tu < 0 ? 0 : tu > wm ? wm : tu; else tu &= wm;
                                    if (m->flags & MF_CLAMPT) tv = tv < 0 ? 0 : tv > hm ? hm : tv; else tv &= hm;
                                } else { tu &= wm; tv &= hm; }
                                u16 c = tx[(tv << tws) + tu];
                                if (!(alpha && c == GFX_TRANSPARENT)) {
                                    if (shade) {
                                        s32 k = (rr + (rr >> 7)) >> 11; if (k > 32) k = 32; if (k < 0) k = 0;
                                        u32 c32 = ((u32)c | ((u32)c << 16)) & 0x07E0F81F;
                                        c32 = ((c32 * (u32)k) >> 5) & 0x07E0F81F;
                                        c = (u16)(c32 | (c32 >> 16));
                                    }
                                    if (xlu) c = (u16)((((c & 0xF7DE) + (*cp & 0xF7DE)) >> 1));
                                    *cp = c;
                                    if (zw) *zp = z;
                                }
                            }
                            cp++; zp++; q += dq; uu += du; vv += dv; rr += dr2;
                        }
                        uq += duq * span; vq += dvq * span; n -= span;
                    }
                } else {
                    int xlu = m->flags & MF_XLU, zw = !(m->flags & MF_NOZW);
                    if (shade) {
                        s32 r0 = rL + (s32)(((s64)dr * pre) >> 16), g0 = gL + (s32)(((s64)dg * pre) >> 16), b0 = bL + (s32)(((s64)db * pre) >> 16);
                        u16 flat = m->flat;
                        int fr = (flat >> 11) & 31, fg = (flat >> 5) & 63, fb = flat & 31; /* prim/env multiplier */
                        while (n--) {
                            u16 z = (u16)(q >> 7);
                            if (z > *zp) {
                                int R = r0 >> 8, G = g0 >> 8, B = b0 >> 8;
                                R = CLAMP(R, 0, 255); G = CLAMP(G, 0, 255); B = CLAMP(B, 0, 255);
                                u16 c = (u16)((((R * fr) >> 8) << 11) | (((G * fg) >> 8) << 5) | ((B * fb) >> 8));
                                if (xlu) c = (u16)((((c & 0xF7DE) + (*cp & 0xF7DE)) >> 1));
                                *cp = c; if (zw) *zp = z;
                            }
                            cp++; zp++; q += dq; r0 += dr; g0 += dg; b0 += db;
                        }
                    } else {
                        u16 c = m->flat;
                        while (n--) {
                            u16 z = (u16)(q >> 7);
                            if (z > *zp) {
                                *cp = xlu ? (u16)((((c & 0xF7DE) + (*cp & 0xF7DE)) >> 1)) : c;
                                if (zw) *zp = z;
                            }
                            cp++; zp++; q += dq;
                        }
                    }
                }
            }
            xleft += dxl; xright += dxr;
            qL += dqL;
            if (tex) { uL += duL; vL += dvL; }
            if (shade) { rL += drL; gL += dgL; bL += dbL; }
        }
    }
    (void)lt;
    gGfxStats.tris_drawn++;
}

/* Project a view-space vertex into an RVtx. */
static void project(RVtx *o, s32 vx, s32 vy, s32 vz, s32 u, s32 v, s32 r, s32 g, s32 b) {
    if (vz < sNear) vz = sNear;
    s32 inv = (s32)((1 << 28) / vz); /* Q8 of 2^20/vz */
    o->x = sCenterX + (s32)(((s64)vx * sFocalX) / vz);
    o->y = sCenterY - (s32)(((s64)vy * sFocalY) / vz);
    o->q = inv;
    o->uq = (s32)(((s64)u * inv) >> 8);
    o->vq = (s32)(((s64)v * inv) >> 8);
    o->r = r << 8; o->g = g << 8; o->b = b << 8;
}

static void xv_to_r(RVtx *o, const XVtx *v) {
    o->x = v->sx; o->y = v->sy; o->q = v->q;
    o->uq = (s32)(((s64)v->u * v->q) >> 8);
    o->vq = (s32)(((s64)v->v * v->q) >> 8);
    o->r = v->r << 8; o->g = v->g << 8; o->b = v->b << 8;
}

/* clip helpers in view space against z = near */
typedef struct { s32 x, y, z, u, v, r, g, b; } CV;

static void cv_lerp(CV *o, const CV *a, const CV *b, s32 t16) {
#define L(f) o->f = a->f + (s32)(((s64)(b->f - a->f) * t16) >> 16)
    L(x); L(y); L(z); L(u); L(v); L(r); L(g); L(b);
#undef L
}

static int guard_ok(const RVtx *v) {
    return v->x > -16 * 2048 && v->x < 16 * 4096 && v->y > -16 * 2048 && v->y < 16 * 4096;
}

/* 2D polygon clip against the viewport (attributes are screen-linear) */
static int clip2d(RVtx *in, int n, RVtx *out, int plane) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        RVtx *a = &in[i], *b = &in[(i + 1) % n];
        s32 da, db;
        switch (plane) {
            case 0: da = a->x + 16 * 64; db = b->x + 16 * 64; break;
            case 1: da = 16 * (sW + 64) - a->x; db = 16 * (sW + 64) - b->x; break;
            case 2: da = a->y + 16 * 64; db = b->y + 16 * 64; break;
            default: da = 16 * (sH + 64) - a->y; db = 16 * (sH + 64) - b->y; break;
        }
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) {
            s32 t = (s32)(((s64)da << 16) / (da - db));
            RVtx *o = &out[m++];
#define L(f) o->f = a->f + (s32)(((s64)(b->f - a->f) * t) >> 16)
            L(x); L(y); L(q); L(uq); L(vq); L(r); L(g); L(b);
#undef L
        }
    }
    return m;
}

static void draw_poly(RVtx *p, int n) {
    /* backface / frontface culling on the screen-space winding */
    s64 area = 0;
    for (int i = 1; i + 1 < n; i++)
        area += (s64)(p[i].x - p[0].x) * (p[i + 1].y - p[0].y) - (s64)(p[i + 1].x - p[0].x) * (p[i].y - p[0].y);
    if (area == 0) return;
    /* N64 front faces are counter-clockwise on screen (y up); with y down they are negative area */
    if ((sGeomMode & G_CULL_BACK) && area > 0) { gGfxStats.tris_culled++; return; }
    if ((sGeomMode & G_CULL_FRONT) && area < 0) { gGfxStats.tris_culled++; return; }
    /* trivial reject */
    int out_l = 1, out_r = 1, out_t = 1, out_b = 1, guard = 1;
    for (int i = 0; i < n; i++) {
        if (p[i].x >= 0) out_l = 0;
        if (p[i].x < sW * 16) out_r = 0;
        if (p[i].y >= 0) out_t = 0;
        if (p[i].y < sH * 16) out_b = 0;
        if (!guard_ok(&p[i])) guard = 0;
    }
    if (out_l || out_r || out_t || out_b) { gGfxStats.tris_culled++; return; }
    if (!guard) {
        RVtx b1[16], b2[16];
        n = clip2d(p, n, b1, 0); if (n < 3) return;
        n = clip2d(b1, n, b2, 1); if (n < 3) return;
        n = clip2d(b2, n, b1, 2); if (n < 3) return;
        n = clip2d(b1, n, b2, 3); if (n < 3) return;
        for (int i = 1; i + 1 < n; i++) raster_tri(&b2[0], &b2[i], &b2[i + 1]);
        return;
    }
    for (int i = 1; i + 1 < n; i++) raster_tri(&p[0], &p[i], &p[i + 1]);
}

static void draw_triangle(int i0, int i1, int i2) {
    gGfxStats.tris_in++;
    if (sMatDirty) resolve_material();
    XVtx *v[3] = { &sVtx[i0], &sVtx[i1], &sVtx[i2] };
    int behind = v[0]->clip + v[1]->clip + v[2]->clip;
    if (behind == 3) { gGfxStats.tris_culled++; return; }
    if (v[0]->vz > sFar && v[1]->vz > sFar && v[2]->vz > sFar) { gGfxStats.tris_culled++; return; }
    RVtx p[4];
    if (behind == 0) {
        for (int i = 0; i < 3; i++) xv_to_r(&p[i], v[i]);
        draw_poly(p, 3);
        return;
    }
    CV c[3], o[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        c[i].x = v[i]->vx; c[i].y = v[i]->vy; c[i].z = v[i]->vz; c[i].u = v[i]->u; c[i].v = v[i]->v;
        c[i].r = v[i]->r; c[i].g = v[i]->g; c[i].b = v[i]->b;
    }
    for (int i = 0; i < 3; i++) {
        CV *a = &c[i], *b = &c[(i + 1) % 3];
        int ain = a->z >= sNear, bin = b->z >= sNear;
        if (ain) o[n++] = *a;
        if (ain != bin) {
            s32 t = (s32)(((s64)(sNear - a->z) * 65536) / (b->z - a->z));
            cv_lerp(&o[n], a, b, t); o[n].z = sNear; n++;
        }
    }
    if (n < 3) return;
    for (int i = 0; i < n; i++) project(&p[i], o[i].x, o[i].y, o[i].z, o[i].u, o[i].v, o[i].r, o[i].g, o[i].b);
    draw_poly(p, n);
}

/* ---------------------------------------------------------- vertex stage */
static void update_lights(void) {
    sLightsDirty = 0;
    /* transform view-space light directions into model space using the
     * transpose of the modelview rotation, then renormalise */
    for (int i = 0; i < sNumLights; i++) {
        s32 x = sLights[i].dx, y = sLights[i].dy, z = sLights[i].dz;
        s32 mx = (s32)(((s64)sModelView.m[0][0] * x + (s64)sModelView.m[1][0] * y + (s64)sModelView.m[2][0] * z) >> 14);
        s32 my = (s32)(((s64)sModelView.m[0][1] * x + (s64)sModelView.m[1][1] * y + (s64)sModelView.m[2][1] * z) >> 14);
        s32 mz = (s32)(((s64)sModelView.m[0][2] * x + (s64)sModelView.m[1][2] * y + (s64)sModelView.m[2][2] * z) >> 14);
        u32 len = isqrt64((u64)((s64)mx * mx + (s64)my * my + (s64)mz * mz));
        if (len == 0) len = 1;
        sLightMdl[i][0] = (s32)(((s64)mx << 14) / len);
        sLightMdl[i][1] = (s32)(((s64)my << 14) / len);
        sLightMdl[i][2] = (s32)(((s64)mz << 14) / len);
    }
}

static void load_vertices(const u8 *src, int n, int v0) {
    if (!src) return;
    if (sLightsDirty && (sGeomMode & G_LIGHTING)) update_lights();
    const Mat34 *m = &sModelView;
    s32 ss = (s32)sTexScaleS + 1, st = (s32)sTexScaleT + 1; /* /65536 */
    for (int i = 0; i < n && v0 + i < VTX_MAX; i++, src += 16) {
        XVtx *o = &sVtx[v0 + i];
        s32 x = BES16(src), y = BES16(src + 2), z = BES16(src + 4);
        o->vx = ((m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z) >> 14) + m->m[0][3];
        o->vy = ((m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z) >> 14) + m->m[1][3];
        o->vz = ((m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z) >> 14) + m->m[2][3];
        s32 s = BES16(src + 8), t = BES16(src + 10);
        o->u = (s32)(((s64)s * ss) >> 16);
        o->v = (s32)(((s64)t * st) >> 16);
        if (sGeomMode & G_LIGHTING) {
            s32 nx = (s8)src[12], ny = (s8)src[13], nz = (s8)src[14];
            s32 r = sAmbR, g = sAmbG, b = sAmbB;
            for (int l = 0; l < sNumLights; l++) {
                s32 d = (nx * sLightMdl[l][0] + ny * sLightMdl[l][1] + nz * sLightMdl[l][2]) >> 7; /* Q14 */
                if (d > 0) {
                    r += (sLights[l].r * d) >> 14; g += (sLights[l].g * d) >> 14; b += (sLights[l].b * d) >> 14;
                }
            }
            o->r = r > 255 ? 255 : r; o->g = g > 255 ? 255 : g; o->b = b > 255 ? 255 : b;
            if (sGeomMode & G_TEXTURE_GEN) {
                /* spherical environment mapping from the view-space normal */
                s32 vnx = (m->m[0][0] * nx + m->m[0][1] * ny + m->m[0][2] * nz) >> 14;
                s32 vny = (m->m[1][0] * nx + m->m[1][1] * ny + m->m[1][2] * nz) >> 14;
                o->u = (s32)((((s64)(vnx + 128) * (sTexScaleS >> 6)) >> 8) * 32 >> 5);
                o->v = (s32)((((s64)(128 - vny) * (sTexScaleT >> 6)) >> 8) * 32 >> 5);
            }
        } else {
            o->r = src[12]; o->g = src[13]; o->b = src[14];
        }
        o->clip = o->vz < sNear;
        if (!o->clip) {
            s32 inv = (s32)((1 << 28) / o->vz);
            o->q = inv;
            o->sx = sCenterX + (s32)(((s64)o->vx * sFocalX * inv) >> 28);
            o->sy = sCenterY - (s32)(((s64)o->vy * sFocalY * inv) >> 28);
        }
        gGfxStats.vtx_xform++;
    }
}

/* N64 Mtx: 4x4 s15.16 split into integer halves then fraction halves,
 * row-vector convention (v' = v * M). Convert to our column-vector Mat34. */
static void load_n64_mtx(Mat34 *o, const u8 *p) {
    s32 f[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            s32 hi = BES16(p + (i * 4 + j) * 2);
            u32 lo = BE16(p + 32 + (i * 4 + j) * 2);
            f[i][j] = (s32)(((u32)hi << 16) | lo);
        }
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) o->m[r][c] = f[c][r] >> 2; /* Q16 -> Q14 */
        o->m[r][3] = f[3][r] >> 16;
    }
}

static void decode_combine(u32 w0, u32 w1) {
    int a = (w0 >> 20) & 0xF, c = (w0 >> 15) & 0x1F, b = (w1 >> 28) & 0xF, d = (w1 >> 15) & 0x7;
    sCombTex = (a == 1 || b == 1 || c == 1 || d == 1);
    sCombShade = (a == 4 || b == 4 || c == 4 || d == 4);
    sCombPrim = (a == 3 || b == 3 || c == 3 || d == 3);
    sCombEnv = (a == 5 || b == 5 || c == 5 || d == 5);
    if (!sCombTex && !sCombShade && !sCombPrim && !sCombEnv) sCombShade = 1;
}

static void run_dl(const u8 *dl, int depth) {
    if (!dl || depth > 10) return;
    for (int guard = 0; guard < 100000; guard++, dl += 8) {
        u32 w0 = BE32(dl), w1 = BE32(dl + 4);
        u8 op = w0 >> 24;
        gGfxStats.dl_cmds++;
        switch (op) {
            case G_VTX: {
                int n = ((w0 >> 20) & 0xF) + 1, v0 = (w0 >> 16) & 0xF;
                load_vertices(gfx_seg_to_ptr(w1), n, v0);
                break;
            }
            case G_TRI1:
                draw_triangle(((w1 >> 16) & 0xFF) / 10, ((w1 >> 8) & 0xFF) / 10, (w1 & 0xFF) / 10);
                break;
            case G_DL: {
                const u8 *sub = gfx_seg_to_ptr(w1);
                if ((w0 >> 16) & 0xFF) { dl = sub - 8; if (!sub) return; continue; } /* branch, no push */
                run_dl(sub, depth + 1);
                break;
            }
            case G_ENDDL: return;
            case G_MTX: {
                int params = (w0 >> 16) & 0xFF;
                const u8 *p = gfx_seg_to_ptr(w1);
                if (!p || (params & 1)) break; /* projection matrices are owned by the engine */
                Mat34 m; load_n64_mtx(&m, p);
                if ((params & 4) && sMtxSP < MTX_STACK) sMtxStack[sMtxSP++] = sModelView;
                if (params & 2) mat_mul(&sModelView, &sView, &m);
                else mat_mul(&sModelView, &sModelView, &m);
                sLightsDirty = 1;
                break;
            }
            case G_POPMTX:
                if (sMtxSP > 0) { sModelView = sMtxStack[--sMtxSP]; sLightsDirty = 1; }
                break;
            case G_MOVEMEM: {
                int idx = (w0 >> 16) & 0xFF;
                const u8 *p = gfx_seg_to_ptr(w1);
                if (!p) break;
                if (idx >= G_MV_L0 && idx <= G_MV_L0 + 14) {
                    int l = (idx - G_MV_L0) / 2;
                    if (l == sNumLights) { sAmbR = p[0]; sAmbG = p[1]; sAmbB = p[2]; }
                    else if (l < 8) {
                        Light *L = &sLights[l];
                        L->r = p[0]; L->g = p[1]; L->b = p[2];
                        L->dx = (s8)p[8] * 128; L->dy = (s8)p[9] * 128; L->dz = (s8)p[10] * 128;
                    }
                    sLightsDirty = 1;
                }
                break;
            }
            case G_MOVEWORD: {
                int idx = w0 & 0xFF, off = (w0 >> 8) & 0xFFFF;
                if (idx == G_MW_NUMLIGHT) { sNumLights = (int)(((w1 & 0x7FFFFFFF) / 32) - 1); if (sNumLights < 0) sNumLights = 0; if (sNumLights > 7) sNumLights = 7; }
                else if (idx == G_MW_SEGMENT) { /* segments set by the engine are kept */ (void)off; }
                break;
            }
            case G_TEXTURE:
                sTexOn = w0 & 0xFF; sTexScaleS = w1 >> 16; sTexScaleT = w1 & 0xFFFF; sMatDirty = 1;
                break;
            case G_SETGEOMETRYMODE: sGeomMode |= w1; sMatDirty = 1; sLightsDirty = 1; break;
            case G_CLEARGEOMETRYMODE: sGeomMode &= ~w1; sMatDirty = 1; break;
            case G_SETOTHERMODE_L: {
                int sh = (w0 >> 8) & 0xFF, len = w0 & 0xFF;
                u32 mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << sh;
                sOtherL = (sOtherL & ~mask) | (w1 & mask); sMatDirty = 1;
                break;
            }
            case G_SETOTHERMODE_H: {
                int sh = (w0 >> 8) & 0xFF, len = w0 & 0xFF;
                u32 mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << sh;
                sOtherH = (sOtherH & ~mask) | (w1 & mask);
                break;
            }
            case G_SETCOMBINE: decode_combine(w0, w1); sMatDirty = 1; break;
            case G_SETPRIMCOLOR: sPrimColor = w1; sMatDirty = 1; break;
            case G_SETENVCOLOR: sEnvColor = w1; sMatDirty = 1; break;
            case G_SETTIMG:
                sTimgAddr = w1; sTimgFmt = (w0 >> 21) & 7; sTimgSiz = (w0 >> 19) & 3; sTimgWidth = (w0 & 0xFFF) + 1;
                break;
            case G_SETTILE: {
                Tile *t = &sTiles[(w1 >> 24) & 7];
                t->fmt = (w0 >> 21) & 7; t->siz = (w0 >> 19) & 3; t->line = (w0 >> 9) & 0x1FF; t->tmem = w0 & 0x1FF;
                t->palette = (w1 >> 20) & 0xF; t->cmt = (w1 >> 18) & 3; t->maskt = (w1 >> 14) & 0xF; t->shiftt = (w1 >> 10) & 0xF;
                t->cms = (w1 >> 8) & 3; t->masks = (w1 >> 4) & 0xF; t->shifts = w1 & 0xF;
                sMatDirty = 1;
                break;
            }
            case G_SETTILESIZE: {
                Tile *t = &sTiles[(w1 >> 24) & 7];
                t->uls = (w0 >> 12) & 0xFFF; t->ult = w0 & 0xFFF; t->lrs = (w1 >> 12) & 0xFFF; t->lrt = w1 & 0xFFF;
                sMatDirty = 1;
                break;
            }
            case G_LOADBLOCK: case G_LOADTILE: {
                Tile *t = &sTiles[(w1 >> 24) & 7];
                sTmemSrc[t->tmem & 511] = sTimgAddr; sMatDirty = 1;
                break;
            }
            case G_LOADTLUT: sTlutSrc = sTimgAddr; sMatDirty = 1; break;
            case G_RDPHALF_1: case G_RDPHALF_2: case G_RDPHALF_CONT: case G_TEXRECT:
            case G_RDPLOADSYNC: case G_RDPPIPESYNC: case G_RDPTILESYNC: case G_RDPFULLSYNC:
            case G_SETSCISSOR: case G_SETZIMG: case G_SETCIMG: case G_SETFOGCOLOR: case G_SETBLENDCOLOR:
            case G_SETFILLCOLOR: case G_FILLRECT: case G_SPNOOP: case G_NOOP: case G_CULLDL:
                break;
            default:
                break;
        }
    }
}

void gfx_run_dl_ptr(const u8 *dl) { run_dl(dl, 0); }
void gfx_run_dl(u32 seg_addr) { run_dl(gfx_seg_to_ptr(seg_addr), 0); }

/* -------------------------------------------------------------- 2D / HUD */
void gfx_fill_rect(int x, int y, int w, int h, u16 color) {
    int x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, sW), y1 = MIN(y + h, sH);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) sColor[yy * sW + xx] = color;
}

void gfx_blend_rect(int x, int y, int w, int h, u16 color) {
    int x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, sW), y1 = MIN(y + h, sH);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            u16 *p = &sColor[yy * sW + xx];
            *p = (u16)(((*p & 0xF7DE) + (color & 0xF7DE)) >> 1);
        }
}

#include "font.h"

void gfx_draw_text(int x, int y, const char *s, u16 color, int scale) {
    for (; *s; s++, x += 6 * scale) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) continue;
        const u8 *g = gFont5x7[ch - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row)) {
                    /* 1px dark outline for readability */
                    gfx_fill_rect(x + col * scale + 1, y + row * scale + 1, scale, scale, 0x0000);
                }
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row)) gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

int gfx_text_width(const char *s, int scale) { int n = 0; while (*s++) n++; return n * 6 * scale; }
