#ifndef SM50_WORLD_H
#define SM50_WORLD_H
#include "types.h"
#include "fixmath.h"

/* ---- content pack (big-endian, see tools/content/gbi.py) ---- */
typedef struct {
    const u8 *base, *data;
    u32 n_entries;
} Pack;

int pack_open(Pack *p, const void *mem, u32 size);
const u8 *pack_find(const Pack *p, const char tag[4], u32 *size);

/* ---- collision ---- */
#define SURF_FLOOR 0
#define SURF_WALL 1
#define SURF_CEIL 2

typedef struct {
    const u8 *raw; /* 9 s16 verts, 3 s16 normal (Q14), u16 type, u16 pad = 28 bytes */
} ColTri;

#define COL_TRI_SIZE 28
#define COL_GRID 16
#define COL_CELL 1024
#define COL_ORIGIN (-8192)

int col_init(const u8 *colv, u8 *work, u32 work_size);
/* All positions in world units. Floors: returns height in Q8, or INT32_MIN if none.
 * Searches for the highest floor at or below y + 100 (step-up tolerance). */
s32 col_find_floor(s32 x, s32 y, s32 z, s32 *out_ny, int *out_tri);
s32 col_find_ceil(s32 x, s32 y, s32 z);
/* Pushes (x,z) (Q8) out of walls at height y (world units) for the given radius.
 * Returns number of walls touched; wall normal yaw of the last one in *wall_yaw. */
int col_resolve_walls(s32 *xq8, s32 y, s32 *zq8, s32 radius, u16 *wall_yaw);
u32 col_tri_count(void);
s32 col_segment_hit(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz);
/* Horizontal components (Q14) of the normal of the floor under (x, y, z). */
int col_floor_normal(s32 x, s32 y, s32 z, s32 *nx, s32 *nz);

/* ---- level ---- */
#define OBJ_COIN 1
#define OBJ_STAR 2
#define OBJ_TREE 3
#define OBJ_SPAWN 4
#define OBJ_BLOB 5
#define OBJ_RED_COIN 6
#define OBJ_RED_STAR_TRIGGER 7
#define OBJ_KING_BOBOMB 8

typedef struct {
    u32 dl; s16 x, y, z, radius; u8 layer; s16 tx, ty, tz; /* layer: 0 opaque, 1 alpha-test, 2 translucent */
} Chunk;

typedef struct {
    u16 type, active; s32 x, y, z; /* world units */
    s32 hx, hy, hz; u16 yaw, param; s32 timer, vy, state;
} Obj;

/* Unmapped original SM64 placements retained for the native behavior runtime. */
typedef struct {
    u16 model;
    s16 x, y, z, pitch, yaw, roll;
    u16 reserved;
    u32 behavior_param, behavior_addr;
} Sm64Spawn;

typedef struct {
    u32 dl; s16 radius; u16 billboard;
    u16 scale14; /* optional per-model scale; zero uses the engine default */
} Model;

#define MAX_OBJS 96
#define MAX_SM64_SPAWNS 64
#define MAX_CHUNKS 64
#define MAX_MODELS 16
#define MAX_PARTS 24
#define MAX_ANIMS 16

typedef struct {
    s16 parent, px, py, pz; u32 dl;
} Part;

typedef struct {
    const u8 *raw; u16 nframes, nparts, loop;
} Anim;

typedef struct {
    Pack pack;
    s32 spawn_x, spawn_y, spawn_z; u16 spawn_yaw;
    s32 water_y, death_y;
    u16 player_scale, player_rot_z;
    s16 player_yoff;
    u16 sky_top, sky_bottom, fog;
    char name[17];
    Chunk chunks[MAX_CHUNKS]; int n_chunks;
    Obj objs[MAX_OBJS]; int n_objs;
    Sm64Spawn sm64_spawns[MAX_SM64_SPAWNS]; int n_sm64_spawns;
    Model models[MAX_MODELS]; int n_models;
    Part parts[MAX_PARTS]; int n_parts;
    Anim anims[MAX_ANIMS]; int n_anims;
    int coins_total;
    int red_coins_total;
} Level;

extern Level gLevel;
int level_load(const void *pack_mem, u32 size, u8 *col_work, u32 col_work_size);
#endif
