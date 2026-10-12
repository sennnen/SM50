/* Executable probe against the CC0 canonical SM64 decompilation. */
#include <PR/ultratypes.h>
#include <string.h>
#include "sm64.h"
#include "game/mario.h"
#include "game/area.h"
#include "game/mario_step.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "surface_terrains.h"
#include "audio/external.h"
#include "game/camera.h"
#include "game/interaction.h"
#include "game/memory.h"
#include "game/sound_init.h"

extern s32 sm50_native_rom_data(s32 spawn[3], const u8 **collision, u32 *size);
extern void update_walking_speed(struct MarioState *m);
extern s32 act_jump(struct MarioState *m);
extern s32 act_jump_land(struct MarioState *m);
extern s32 act_freefall(struct MarioState *m);
extern s32 act_freefall_land(struct MarioState *m);
extern s32 act_walking(struct MarioState *m);
extern s16 sm50_set_mario_animation(struct MarioState *m, s32 targetAnimID);
extern s16 sm50_set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel);
extern void sm50_native_publish_mario(s32 x, s32 y, s32 z, u16 yaw, s32 speed,
                                      s32 vertical_speed, s32 floor_y, s32 grounded,
                                      s32 landing);
extern u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg);
extern f32 find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor);
extern s32 perform_ground_step(struct MarioState *m);
extern void *sys_malloc(u32 size);

SpatialPartitionCell gStaticSurfacePartition[NUM_CELLS][NUM_CELLS];
SpatialPartitionCell gDynamicSurfacePartition[NUM_CELLS][NUM_CELLS];
s32 gFindFloorIncludeSurfaceIntangible;
s32 gCheckingSurfaceCollisionsForCamera;
s32 gNumFindFloorMisses;
u32 gGlobalTimer;
TerrainData *gEnvironmentRegions;
u32 gAudioRandom;
s16 gCurrLevelNum;
s16 gCameraMovementFlags;
struct MarioState *gMarioState;
struct Object *gMarioObject;
struct Object *gCurrentObject;
struct NativeNumTimesCalled { s16 floor, ceil, wall; } gNumCalls;

void play_sound(s32 sound, f32 *pos) { (void)sound; (void)pos; }
void set_camera_shake_from_hit(s16 shake) { (void)shake; }
s32 load_patchable_table(struct DmaHandlerList *list, s32 index) { (void)list; (void)index; return 0; }
void raise_background_noise(s32 level) { (void)level; }
void set_camera_mode(struct Camera *camera, s16 mode, s16 frames) {
    (void)camera; (void)mode; (void)frames;
}
void mario_stop_riding_and_holding(struct MarioState *m) { (void)m; }
void mario_drop_held_object(struct MarioState *m) { (void)m; }

static struct Surface *sNativeSurfaces;
static struct SurfaceNode *sNativeNodes;
static struct MarioState sNativeMario;
static struct Object sNativeMarioObject;
static struct Area sNativeArea;
static struct Controller sNativeController;
static u32 sNativeNodeCount;
static u16 sNativePreviousButtons;

/* Animation decoding stays in the ROM-backed renderer; these hooks keep
 * action logic independent of the decompilation's N64 animation runtime. */
s16 sm50_set_mario_animation(struct MarioState *m, s32 targetAnimID) {
    (void)m; (void)targetAnimID;
    return 0;
}
s16 sm50_set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel) {
    (void)m; (void)targetAnimID; (void)accel;
    return 0;
}

static s16 read_s16(const u8 *p) {
    return (s16)(((u16)p[0] << 8) | p[1]);
}

static s16 lower_cell(s16 coordinate) {
    s32 value = coordinate + LEVEL_BOUNDARY_MAX;
    if (value < 0) value = 0;
    s32 index = value / CELL_SIZE;
    if (value % CELL_SIZE < 50) index--;
    return index < 0 ? 0 : (s16)index;
}

static s16 upper_cell(s16 coordinate) {
    s32 value = coordinate + LEVEL_BOUNDARY_MAX;
    if (value < 0) value = 0;
    s32 index = value / CELL_SIZE;
    if (value % CELL_SIZE > CELL_SIZE - 50) index++;
    return index >= NUM_CELLS ? NUM_CELLS_INDEX : (s16)index;
}

static void add_native_surface(struct Surface *surface, s16 cellX, s16 cellZ) {
    s32 listType = surface->normal.y > 0.01f ? SPATIAL_PARTITION_FLOORS
        : surface->normal.y < -0.01f ? SPATIAL_PARTITION_CEILS : SPATIAL_PARTITION_WALLS;
    s32 sortDir = listType == SPATIAL_PARTITION_FLOORS ? 1
        : listType == SPATIAL_PARTITION_CEILS ? -1 : 0;
    if (listType == SPATIAL_PARTITION_WALLS &&
        (surface->normal.x < -0.707f || surface->normal.x > 0.707f))
        surface->flags |= SURFACE_FLAG_X_PROJECTION;
    struct SurfaceNode *head = &gStaticSurfacePartition[cellZ][cellX][listType];
    struct SurfaceNode *node = &sNativeNodes[sNativeNodeCount++];
    node->surface = surface;
    while (head->next && sortDir != 0 &&
           surface->vertex1[1] * sortDir <= head->next->surface->vertex1[1] * sortDir) {
        head = head->next;
    }
    node->next = head->next;
    head->next = node;
}

static s32 load_rom_collision(const u8 *colv, u32 colvSize) {
    if (!colv || colvSize < 4) return 0;
    u32 count = ((u32)colv[0] << 24) | ((u32)colv[1] << 16) | ((u32)colv[2] << 8) | colv[3];
    if (colvSize != 4 + count * 28 || count != 1060) return 0;
    sNativeSurfaces = sys_malloc(count * sizeof(*sNativeSurfaces));
    sNativeNodes = sys_malloc(6000 * sizeof(*sNativeNodes));
    if (!sNativeSurfaces || !sNativeNodes) return 0;

    for (u32 i = 0; i < count; i++) {
        const u8 *record = colv + 4 + i * 28;
        struct Surface *s = &sNativeSurfaces[i];
        memset(s, 0, sizeof(*s));
        s16 x[3] = { read_s16(record), read_s16(record + 6), read_s16(record + 12) };
        s16 y[3] = { read_s16(record + 2), read_s16(record + 8), read_s16(record + 14) };
        s16 z[3] = { read_s16(record + 4), read_s16(record + 10), read_s16(record + 16) };
        s->vertex1[0] = x[0]; s->vertex1[1] = y[0]; s->vertex1[2] = z[0];
        s->vertex2[0] = x[1]; s->vertex2[1] = y[1]; s->vertex2[2] = z[1];
        s->vertex3[0] = x[2]; s->vertex3[1] = y[2]; s->vertex3[2] = z[2];
        s->normal.x = read_s16(record + 18) / 16384.0f;
        s->normal.y = read_s16(record + 20) / 16384.0f;
        s->normal.z = read_s16(record + 22) / 16384.0f;
        s->originOffset = -(s->normal.x * x[0] + s->normal.y * y[0] + s->normal.z * z[0]);
        s->type = (u16)read_s16(record + 24);
        s->lowerY = MIN(y[0], MIN(y[1], y[2]));
        s->upperY = MAX(y[0], MAX(y[1], y[2]));

        s16 x0 = lower_cell(MIN(x[0], MIN(x[1], x[2])));
        s16 x1 = upper_cell(MAX(x[0], MAX(x[1], x[2])));
        s16 z0 = lower_cell(MIN(z[0], MIN(z[1], z[2])));
        s16 z1 = upper_cell(MAX(z[0], MAX(z[1], z[2])));
        for (s16 cz = z0; cz <= z1; cz++)
            for (s16 cx = x0; cx <= x1; cx++) {
                if (sNativeNodeCount >= 6000) return 0;
                add_native_surface(s, cx, cz);
            }
    }
    return sNativeNodeCount == 5340;
}

volatile s32 native_sm64_canonical_floor_y_q8;
volatile s32 native_sm64_canonical_floor_ny_q14;
volatile s32 native_sm64_canonical_speed_q12;
volatile s32 native_sm64_canonical_ground_step;
volatile s32 native_sm64_canonical_moved_x_q8;
volatile s32 native_sm64_canonical_moved_z_q8;
volatile s32 native_sm64_canonical_pos_x_q8;
volatile s32 native_sm64_canonical_pos_y_q8;
volatile s32 native_sm64_canonical_pos_z_q8;

int native_sm64_canonical_probe(void) {
    s32 spawn[3];
    const u8 *collision = NULL;
    u32 collisionSize = 0;
    if (!sm50_native_rom_data(spawn, &collision, &collisionSize) ||
        !load_rom_collision(collision, collisionSize)) return 0;

    struct Surface *floor = NULL;
    f32 floorHeight = find_floor(spawn[0], spawn[1] + 200, spawn[2], &floor);
    if (!floor) return 0;

    memset(&sNativeMario, 0, sizeof(sNativeMario));
    memset(&sNativeMarioObject, 0, sizeof(sNativeMarioObject));
    memset(&sNativeArea, 0, sizeof(sNativeArea));
    memset(&sNativeController, 0, sizeof(sNativeController));
    sNativeArea.terrainType = TERRAIN_GRASS;
    struct MarioState *mario = &sNativeMario;
    struct Object *marioObject = &sNativeMarioObject;
    mario->floor = floor;
    mario->floorHeight = floorHeight;
    mario->pos[0] = spawn[0];
    mario->pos[1] = floorHeight;
    mario->pos[2] = spawn[2];
    mario->intendedMag = 32.0f;
    mario->action = ACT_WALKING;
    mario->area = &sNativeArea;
    mario->marioObj = marioObject;
    mario->controller = &sNativeController;
    gMarioState = mario;
    gMarioObject = marioObject;
    gCurrentObject = marioObject;
    s32 stepResult = GROUND_STEP_NONE;
    for (int frame = 0; frame < 30; frame++) {
        floorHeight = find_floor(mario->pos[0], mario->pos[1] + 200.0f,
                                 mario->pos[2], &floor);
        if (!floor) return 0;
        mario->floor = floor;
        mario->floorHeight = floorHeight;
        update_walking_speed(mario);
        stepResult = perform_ground_step(mario);
        if (stepResult != GROUND_STEP_NONE) break;
    }

    native_sm64_canonical_floor_y_q8 = (s32)(floorHeight * 256.0f);
    native_sm64_canonical_floor_ny_q14 = (s32)(floor->normal.y * 16384.0f);
    native_sm64_canonical_speed_q12 = (s32)(mario->forwardVel * 4096.0f);
    native_sm64_canonical_ground_step = stepResult;
    native_sm64_canonical_moved_x_q8 = (s32)((mario->pos[0] - spawn[0]) * 256.0f);
    native_sm64_canonical_moved_z_q8 = (s32)((mario->pos[2] - spawn[2]) * 256.0f);
    native_sm64_canonical_pos_x_q8 = (s32)(mario->pos[0] * 256.0f);
    native_sm64_canonical_pos_y_q8 = (s32)(mario->pos[1] * 256.0f);
    native_sm64_canonical_pos_z_q8 = (s32)(mario->pos[2] * 256.0f);
    return mario->forwardVel > 0.0f && mario->forwardVel < 48.0f
        && floor->normal.y > 0.95f && sNativeNodeCount == 5340
        && stepResult == GROUND_STEP_NONE;
}

void native_sm64_canonical_tick(u16 buttons, u16 cameraYaw) {
    s32 ix = ((buttons & 8) != 0) - ((buttons & 4) != 0);
    s32 iz = ((buttons & 1) != 0) - ((buttons & 2) != 0);
    if (ix || iz) {
        sNativeMario.intendedYaw = cameraYaw + atan2s(-ix, iz);
        sNativeMario.intendedMag = 32.0f;
    } else {
        sNativeMario.intendedYaw = sNativeMario.faceAngle[1];
        sNativeMario.intendedMag = 0.0f;
    }

    s32 jumpPressed = (buttons & 0x10) && !(sNativePreviousButtons & 0x10);
    s32 actionPressed = (buttons & 0x20) && !(sNativePreviousButtons & 0x20);
    sNativePreviousButtons = buttons;
    sNativeController.buttonDown = ((buttons & 0x10) ? A_BUTTON : 0)
        | ((buttons & 0x20) ? B_BUTTON : 0);
    sNativeController.buttonPressed = (jumpPressed ? A_BUTTON : 0)
        | (actionPressed ? B_BUTTON : 0);
    sNativeController.stickX = (f32)ix * 64.0f;
    sNativeController.stickY = (f32)iz * 64.0f;
    sNativeController.stickMag = (ix || iz) ? 64.0f : 0.0f;
    sNativeMario.input = (ix || iz ? INPUT_NONZERO_ANALOG : 0)
        | (jumpPressed ? INPUT_A_PRESSED : 0)
        | (actionPressed ? INPUT_B_PRESSED : 0);
    struct Surface *floor = NULL;
    if (sNativeMario.action == ACT_JUMP) {
        act_jump(&sNativeMario);
        floor = sNativeMario.floor;
    } else if (sNativeMario.action == ACT_FREEFALL) {
        act_freefall(&sNativeMario);
        floor = sNativeMario.floor;
    } else if (sNativeMario.action == ACT_JUMP_LAND) {
        act_jump_land(&sNativeMario);
    } else if (sNativeMario.action == ACT_FREEFALL_LAND) {
        act_freefall_land(&sNativeMario);
    } else {
        f32 height = find_floor(sNativeMario.pos[0], sNativeMario.pos[1] + 200.0f,
                                sNativeMario.pos[2], &floor);
        if (floor) {
            sNativeMario.floor = floor;
            sNativeMario.floorHeight = height;
            if (sNativeMario.action == ACT_WALKING) {
                act_walking(&sNativeMario);
            } else {
                update_walking_speed(&sNativeMario);
                perform_ground_step(&sNativeMario);
            }
        }
    }

    native_sm64_canonical_pos_x_q8 = (s32)(sNativeMario.pos[0] * 256.0f);
    native_sm64_canonical_pos_y_q8 = (s32)(sNativeMario.pos[1] * 256.0f);
    native_sm64_canonical_pos_z_q8 = (s32)(sNativeMario.pos[2] * 256.0f);
    sm50_native_publish_mario(native_sm64_canonical_pos_x_q8,
                              native_sm64_canonical_pos_y_q8,
                              native_sm64_canonical_pos_z_q8,
                              sNativeMario.faceAngle[1],
                              (s32)(sNativeMario.forwardVel * 256.0f),
                              (s32)(sNativeMario.vel[1] * 256.0f),
                              (s32)(sNativeMario.floorHeight * 256.0f),
                              !(sNativeMario.action & ACT_FLAG_AIR),
                              sNativeMario.action == ACT_JUMP_LAND ||
                                  sNativeMario.action == ACT_FREEFALL_LAND);
}
