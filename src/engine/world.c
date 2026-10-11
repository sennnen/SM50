/* Content pack parsing, collision grid and level loading. */
#include "world.h"
#include "gfx.h"
#include <string.h>

Level gLevel;

/* ------------------------------------------------------------------ pack */
int pack_open(Pack *p, const void *mem, u32 size) {
    const u8 *b = (const u8 *)mem;
    if (!b || size < 16 || memcmp(b, "S50P", 4) != 0) return 0;
    if (BE32(b + 4) != 1) return 0;
    p->base = b;
    p->n_entries = BE32(b + 8);
    u32 ds = BE32(b + 12);
    if (ds >= size) return 0;
    p->data = b + ds;
    return 1;
}

const u8 *pack_find(const Pack *p, const char tag[4], u32 *size) {
    const u8 *e = p->base + 16;
    for (u32 i = 0; i < p->n_entries; i++, e += 12)
        if (memcmp(e, tag, 4) == 0) {
            if (size) *size = BE32(e + 8);
            return p->data + BE32(e + 4);
        }
    return NULL;
}

/* ------------------------------------------------------------- collision */
static const u8 *sColTris;
static u32 sColCount;
/* per cell and class: offset/count into sCellList */
static u16 sCellOff[COL_GRID * COL_GRID][3], sCellCnt[COL_GRID * COL_GRID][3];
static u16 *sCellList;

#define TRI(i) (sColTris + (u32)(i) * COL_TRI_SIZE)
#define TV(t, k) BES16((t) + (k) * 2)

static int tri_class(const u8 *t) {
    s32 ny = TV(t, 10);
    if (ny > 164) return SURF_FLOOR;   /* > 0.01 */
    if (ny < -164) return SURF_CEIL;
    return SURF_WALL;
}

static void tri_bounds(const u8 *t, s32 *x0, s32 *z0, s32 *x1, s32 *z1) {
    s32 ax = TV(t, 0), az = TV(t, 2), bx = TV(t, 3), bz = TV(t, 5), cx = TV(t, 6), cz = TV(t, 8);
    *x0 = MIN(ax, MIN(bx, cx)); *x1 = MAX(ax, MAX(bx, cx));
    *z0 = MIN(az, MIN(bz, cz)); *z1 = MAX(az, MAX(bz, cz));
}

static int cell_of(s32 v) {
    int c = (v - COL_ORIGIN) / COL_CELL;
    return c < 0 ? 0 : c >= COL_GRID ? COL_GRID - 1 : c;
}

u32 col_tri_count(void) { return sColCount; }

int col_init(const u8 *colv, u8 *work, u32 work_size) {
    sColCount = BE32(colv);
    sColTris = colv + 4;
    memset(sCellCnt, 0, sizeof(sCellCnt));
    /* pass 1: count */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            u32 total = 0;
            for (int c = 0; c < COL_GRID * COL_GRID; c++)
                for (int k = 0; k < 3; k++) { sCellOff[c][k] = (u16)total; total += sCellCnt[c][k]; sCellCnt[c][k] = 0; }
            if (total * 2 > work_size || total > 65535) return 0;
            sCellList = (u16 *)work;
        }
        for (u32 i = 0; i < sColCount; i++) {
            const u8 *t = TRI(i);
            int cls = tri_class(t);
            s32 x0, z0, x1, z1;
            tri_bounds(t, &x0, &z0, &x1, &z1);
            s32 m = cls == SURF_WALL ? 100 : 0;
            int cx0 = cell_of(x0 - m), cx1 = cell_of(x1 + m), cz0 = cell_of(z0 - m), cz1 = cell_of(z1 + m);
            for (int cz = cz0; cz <= cz1; cz++)
                for (int cx = cx0; cx <= cx1; cx++) {
                    int c = cz * COL_GRID + cx;
                    if (pass == 1) sCellList[sCellOff[c][cls] + sCellCnt[c][cls]] = (u16)i;
                    sCellCnt[c][cls]++;
                }
        }
    }
    return 1;
}

/* point-in-triangle on the XZ plane (works for either winding) */
static int inside_xz(const u8 *t, s32 x, s32 z) {
    s32 ax = TV(t, 0), az = TV(t, 2), bx = TV(t, 3), bz = TV(t, 5), cx = TV(t, 6), cz = TV(t, 8);
    s64 c1 = (s64)(bx - ax) * (z - az) - (s64)(bz - az) * (x - ax);
    s64 c2 = (s64)(cx - bx) * (z - bz) - (s64)(cz - bz) * (x - bx);
    s64 c3 = (s64)(ax - cx) * (z - cz) - (s64)(az - cz) * (x - cx);
    return (c1 >= 0 && c2 >= 0 && c3 >= 0) || (c1 <= 0 && c2 <= 0 && c3 <= 0);
}

/* plane height in Q8 at (x, z) */
static s32 plane_y_q8(const u8 *t, s32 x, s32 z) {
    s32 nx = TV(t, 9), ny = TV(t, 10), nz = TV(t, 11);
    s32 ax = TV(t, 0), ay = TV(t, 1), az = TV(t, 2);
    s64 num = (s64)nx * (x - ax) + (s64)nz * (z - az);
    return ay * 256 - (s32)((num * 256) / ny);
}

s32 col_find_floor(s32 x, s32 y, s32 z, s32 *out_ny, int *out_tri) {
    int c = cell_of(z) * COL_GRID + cell_of(x);
    s32 best = INT32_MIN; int bt = -1;
    const u16 *l = sCellList + sCellOff[c][SURF_FLOOR];
    for (int i = 0; i < sCellCnt[c][SURF_FLOOR]; i++) {
        const u8 *t = TRI(l[i]);
        if (!inside_xz(t, x, z)) continue;
        s32 h = plane_y_q8(t, x, z);
        if (h <= (y + 100) * 256 && h > best) { best = h; bt = l[i]; }
    }
    if (out_ny) *out_ny = bt >= 0 ? TV(TRI(bt), 10) : ONE14;
    if (out_tri) *out_tri = bt;
    return best;
}

s32 col_find_ceil(s32 x, s32 y, s32 z) {
    int c = cell_of(z) * COL_GRID + cell_of(x);
    s32 best = INT32_MAX;
    const u16 *l = sCellList + sCellOff[c][SURF_CEIL];
    for (int i = 0; i < sCellCnt[c][SURF_CEIL]; i++) {
        const u8 *t = TRI(l[i]);
        if (!inside_xz(t, x, z)) continue;
        s32 h = plane_y_q8(t, x, z);
        if (h >= (y - 10) * 256 && h < best) best = h;
    }
    return best;
}

int col_resolve_walls(s32 *xq8, s32 y, s32 *zq8, s32 radius, u16 *wall_yaw) {
    int hits = 0;
    s32 x = *xq8 >> 8, z = *zq8 >> 8;
    int c = cell_of(z) * COL_GRID + cell_of(x);
    const u16 *l = sCellList + sCellOff[c][SURF_WALL];
    for (int i = 0; i < sCellCnt[c][SURF_WALL]; i++) {
        const u8 *t = TRI(l[i]);
        s32 ymin = MIN(TV(t, 1), MIN(TV(t, 4), TV(t, 7))), ymax = MAX(TV(t, 1), MAX(TV(t, 4), TV(t, 7)));
        if (y < ymin || y > ymax) continue;
        s32 nx = TV(t, 9), ny = TV(t, 10), nz = TV(t, 11);
        s32 ax = TV(t, 0), ay = TV(t, 1), az = TV(t, 2);
        /* signed distance (Q8) of the point from the plane */
        s64 d = ((s64)nx * (*xq8 - ax * 256) + (s64)ny * (y * 256 - ay * 256) + (s64)nz * (*zq8 - az * 256)) >> 14;
        if (d < -radius * 256 / 2 || d >= radius * 256) continue;
        /* projection must be within the triangle extents along the wall:
         * project onto the plane spanned by the horizontal tangent and Y */
        s32 px = (s32)(*xq8 - ((d * nx) >> 14)) >> 8, pz = (s32)(*zq8 - ((d * nz) >> 14)) >> 8;
        s32 ux, uz; /* use dominant horizontal axis */
        int use_x = (nx < 0 ? -nx : nx) < (nz < 0 ? -nz : nz);
        s32 bx = TV(t, 3), by = TV(t, 4), bz = TV(t, 5), cx2 = TV(t, 6), cy = TV(t, 7), cz2 = TV(t, 8);
        s32 p0 = use_x ? ax : az, p1 = use_x ? bx : bz, p2 = use_x ? cx2 : cz2, pp = use_x ? px : pz;
        s64 c1 = (s64)(p1 - p0) * (y - ay) - (s64)(by - ay) * (pp - p0);
        s64 c2 = (s64)(p2 - p1) * (y - by) - (s64)(cy - by) * (pp - p1);
        s64 c3 = (s64)(p0 - p2) * (y - cy) - (s64)(ay - cy) * (pp - p2);
        if (!((c1 >= 0 && c2 >= 0 && c3 >= 0) || (c1 <= 0 && c2 <= 0 && c3 <= 0))) continue;
        (void)ux; (void)uz;
        /* push out along the horizontal normal */
        s32 push = radius * 256 - (s32)d;
        s32 hl = (s32)isqrt32((u32)(nx * nx + nz * nz));
        if (hl == 0) continue;
        *xq8 += (s32)(((s64)push * nx) / hl);
        *zq8 += (s32)(((s64)push * nz) / hl);
        if (wall_yaw) *wall_yaw = fatan2(nx, nz);
        hits++;
    }
    return hits;
}

/* ----------------------------------------------------------------- level */
int level_load(const void *pack_mem, u32 size, u8 *col_work, u32 col_work_size) {
    Level *L = &gLevel;
    memset(L, 0, sizeof(*L));
    if (!pack_open(&L->pack, pack_mem, size)) return -1;
    gfx_set_segment(7, L->pack.data);
    const u8 *p;
    if (!(p = pack_find(&L->pack, "INFO", NULL))) return -2;
    L->spawn_x = BES16(p); L->spawn_y = BES16(p + 2); L->spawn_z = BES16(p + 4); L->spawn_yaw = BE16(p + 6);
    L->water_y = BES16(p + 8); L->death_y = BES16(p + 10);
    L->sky_top = BE16(p + 12); L->sky_bottom = BE16(p + 14); L->fog = BE16(p + 16);
    memcpy(L->name, p + 20, 16); L->name[16] = 0;

    if (!(p = pack_find(&L->pack, "LVLM", NULL))) return -3;
    L->n_chunks = (int)MIN(BE32(p), MAX_CHUNKS);
    for (int i = 0; i < L->n_chunks; i++) {
        const u8 *c = p + 4 + i * 16;
        Chunk *ch = &L->chunks[i];
        ch->dl = BE32(c); ch->x = BES16(c + 4); ch->y = BES16(c + 6); ch->z = BES16(c + 8);
        ch->radius = BES16(c + 10); ch->layer = c[12];
    }
    if (!(p = pack_find(&L->pack, "COLV", NULL))) return -4;
    if (!col_init(p, col_work, col_work_size)) return -5;

    if (!(p = pack_find(&L->pack, "MDLS", NULL))) return -6;
    L->n_models = (int)MIN(BE32(p), MAX_MODELS);
    for (int i = 0; i < L->n_models; i++) {
        const u8 *m = p + 4 + i * 8;
        L->models[i].dl = BE32(m); L->models[i].radius = BES16(m + 4); L->models[i].billboard = BE16(m + 6);
    }
    if (!(p = pack_find(&L->pack, "SKEL", NULL))) return -7;
    L->n_parts = (int)MIN(BE32(p), MAX_PARTS);
    for (int i = 0; i < L->n_parts; i++) {
        const u8 *s = p + 4 + i * 12;
        Part *pt = &L->parts[i];
        pt->parent = BES16(s); pt->px = BES16(s + 2); pt->py = BES16(s + 4); pt->pz = BES16(s + 6); pt->dl = BE32(s + 8);
    }
    if (!(p = pack_find(&L->pack, "ANIM", NULL))) return -8;
    L->n_anims = (int)MIN(BE32(p), MAX_ANIMS);
    for (int i = 0; i < L->n_anims; i++) {
        const u8 *a = p + BE32(p + 4 + i * 4);
        L->anims[i].raw = a; L->anims[i].nframes = BE16(a); L->anims[i].nparts = BE16(a + 2); L->anims[i].loop = BE16(a + 4);
    }
    if (!(p = pack_find(&L->pack, "OBJS", NULL))) return -9;
    u32 n = BE32(p);
    for (u32 i = 0; i < n; i++) {
        const u8 *o = p + 4 + i * 12;
        u16 type = BE16(o);
        if (type == OBJ_SPAWN) continue;
        if (L->n_objs >= MAX_OBJS) break;
        Obj *ob = &L->objs[L->n_objs++];
        memset(ob, 0, sizeof(*ob));
        ob->type = type; ob->active = 1;
        ob->x = ob->hx = BES16(o + 2); ob->y = BES16(o + 4); ob->z = ob->hz = BES16(o + 6);
        ob->yaw = BE16(o + 8); ob->param = BE16(o + 10);
        if (type == OBJ_COIN) L->coins_total++;
    }
    return 0;
}
