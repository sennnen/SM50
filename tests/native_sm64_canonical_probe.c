/* Executable probe against the CC0 canonical SM64 decompilation. */
#include <PR/ultratypes.h>
#include <string.h>
#include "sm64.h"
#include "game/mario.h"
#include "mario_animation_ids.h"
#include "game/area.h"
#include "game/mario_step.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "surface_terrains.h"
#include "audio/external.h"

#define SM50_NATIVE_OBJECT_POOL_CAPACITY 64
#include "game/camera.h"
#include "game/interaction.h"
#include "game/memory.h"
#include "game/sound_init.h"
#include "game/mario_actions_object.h"
#include "game/object_list_processor.h"
#include "game/spawn_object.h"
#include "game/level_update.h"
#include "game/save_file.h"
#include "game/memory.h"
#include "engine/graph_node.h"
#include "behavior_data.h"

enum {
    SM50_ANIM_IDLE, SM50_ANIM_WALK, SM50_ANIM_RUN, SM50_ANIM_JUMP,
    SM50_ANIM_FALL, SM50_ANIM_LAND, SM50_ANIM_FLIP, SM50_ANIM_HURT
};

extern s32 sm50_native_rom_data(s32 spawn[4], const u8 **collision, u32 *size);
extern s32 sm50_native_find_sm64_spawn(u32 behavior, s16 transform[6], u32 *params);
extern s32 sm50_native_yellow_coin_count(void);
extern s32 sm50_native_yellow_coin_at(s32 ordinal, s32 values[5]);
extern void sm50_native_hide_coin(s32 index);
extern void update_walking_speed(struct MarioState *m);
extern void update_mario_inputs(struct MarioState *m);
extern s32 mario_execute_moving_action(struct MarioState *m);
extern s32 mario_execute_airborne_action(struct MarioState *m);
extern s32 mario_execute_stationary_action(struct MarioState *m);
extern s32 mario_execute_submerged_action(struct MarioState *m);
extern s32 mario_execute_automatic_action(struct MarioState *m);
extern s32 mario_execute_object_action(struct MarioState *m);
extern s16 sm50_set_mario_animation(struct MarioState *m, s32 targetAnimID);
extern s16 sm50_set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel);
extern void sm50_native_publish_mario(s32 x, s32 y, s32 z, u16 yaw, s32 speed,
                                      s32 vertical_speed, s32 floor_y, s32 grounded,
                                      s32 landing, s32 animation);
extern u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg);
extern f32 find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor);
extern s32 perform_ground_step(struct MarioState *m);
extern void *sys_malloc(u32 size);
extern struct Object *try_allocate_object(struct ObjectNode *destList,
                                          struct ObjectNode *freeList);
extern void detect_object_collisions(void);
extern void mario_process_interactions(struct MarioState *m);
extern s32 act_star_dance(struct MarioState *m);
extern void native_sm64_canonical_tick(u16 buttons, u16 cameraYaw);
extern volatile s32 native_sm64_canonical_pos_x_q8;
extern volatile s32 native_sm64_canonical_pos_y_q8;
extern volatile s32 native_sm64_canonical_pos_z_q8;
extern volatile u16 native_sm64_canonical_yaw;
extern volatile s32 native_sm64_canonical_anim_index;

SpatialPartitionCell gStaticSurfacePartition[NUM_CELLS][NUM_CELLS];
SpatialPartitionCell gDynamicSurfacePartition[NUM_CELLS][NUM_CELLS];
s16 gFindFloorIncludeSurfaceIntangible;
s16 gCheckingSurfaceCollisionsForCamera;
s32 gNumFindFloorMisses;
s8 gShowDebugText;
u32 gGlobalTimer;
TerrainData *gEnvironmentRegions;
u32 gAudioRandom;
s16 gCurrLevelNum;
s16 gCurrSaveFileNum;
s16 gCameraMovementFlags;
struct MarioState *gMarioState;
struct Object *gMarioObject;
struct Object *gCurrentObject;
const BehaviorScript *gCurBhvCommand;
struct GraphNode *sLoadedGraphNodes[128];
struct GraphNode **gLoadedGraphNodes = sLoadedGraphNodes;
s16 gCurrCourseNum;
u32 gTimeStopState;
s16 gMarioCurrentRoom;
s16 gNumRoomedObjectsInMarioRoom;
s16 gNumRoomedObjectsNotInMarioRoom;
RoomData gDoorAdjacentRooms[60][2];
Vec3s gVec3sZero = { 0, 0, 0 };
struct Object gObjectPool[SM50_NATIVE_OBJECT_POOL_CAPACITY];
struct ObjectNode gObjectListArray[16];
struct ObjectNode *gObjectLists;
struct ObjectNode gFreeObjectList;
struct GraphNode gObjParentGraphNode;
struct Area *gCurrentArea;
s16 gCurrAreaIndex;
struct NumTimesCalled gNumCalls;
Vec3f gVec3fZero = { 0.0f, 0.0f, 0.0f };
struct MarioBodyState gBodyStates[2];
u8 gLastCompletedStarNum;
s32 gDialogResponse;
static u32 sNativeSavedStars;
volatile s32 native_sm64_canonical_star_collected;
volatile s32 native_sm64_canonical_star_interaction_ok;
volatile s32 native_sm64_canonical_runtime_ready;
volatile s32 native_sm64_canonical_star_debug;
volatile s32 native_sm64_canonical_star_hitbox_debug;
static s32 sNativeStarExitRequested;

void save_file_collect_star_or_key(s16 coinScore, s16 starIndex) {
    (void)coinScore;
    if (starIndex >= 0 && starIndex < 32) sNativeSavedStars |= 1u << starIndex;
    native_sm64_canonical_star_collected = 1;
}
void bhv_spawn_star_no_level_exit(u32 starIndex) { (void)starIndex; }

extern const BehaviorScript bhvStar[];
extern const BehaviorScript bhvYellowCoin[];
extern void cur_obj_update(void);

u32 save_file_get_star_flags(s32 fileIndex, s32 courseIndex) {
    (void)fileIndex;
    (void)courseIndex;
    return sNativeSavedStars;
}
void stop_sounds_from_source(f32 *pos) { (void)pos; }
void drop_queued_background_music(void) {}
void fadeout_level_music(s16 fadeTimer) { (void)fadeTimer; }
void disable_background_sound(void) {}
void enable_background_sound(void) {}
void play_course_clear(void) {}
void play_cap_music(u16 seqArgs) { (void)seqArgs; }
u32 save_file_get_flags(void) { return 0; }
const u8 warp_pipe_seg3_collision_03009AC8[] = { 0 };
void abort(void) { for (;;) {} }
void play_music(u8 player, u16 seqArgs, u16 fadeTimer) {
    (void)player; (void)seqArgs; (void)fadeTimer;
}
void create_dialog_box_with_response(s16 dialog) { (void)dialog; }
void save_file_do_save(s32 fileIndex) { (void)fileIndex; }

void play_sound(s32 sound, f32 *pos) { (void)sound; (void)pos; }
void stop_sound(u32 sound, f32 *pos) { (void)sound; (void)pos; }
void set_camera_shake_from_hit(s16 shake) { (void)shake; }
s32 load_patchable_table(struct DmaHandlerList *list, s32 index) { (void)list; (void)index; return 0; }
void raise_background_noise(s32 level) { (void)level; }
void lower_background_noise(s32 level) { (void)level; }
void play_shell_music(void) {}
void stop_shell_music(void) {}
s32 save_file_get_total_star_count(s32 fileIndex, s32 minCourse, s32 maxCourse) {
    (void)fileIndex; (void)minCourse; (void)maxCourse;
    u32 bits = sNativeSavedStars;
    s32 count = 0;
    while (bits) { count += bits & 1u; bits >>= 1; }
    return count;
}
void set_camera_mode(struct Camera *camera, s16 mode, s16 frames) {
    (void)camera; (void)mode; (void)frames;
}
void save_file_set_cap_pos(s16 x, s16 y, s16 z) { (void)x; (void)y; (void)z; }
void *segmented_to_virtual(const void *address) { return (void *)address; }
const BehaviorScript bhvJumpingBox[] = { 0 };
const BehaviorScript bhvKoopaShellUnderwater[] = { 0 };
const BehaviorScript bhvTree[] = { 0 };
const BehaviorScript bhvGiantPole[] = { 0 };
/* Placeholder behavior pointers used by unrelated interaction/action paths;
   the active Battlefield star uses the generated canonical bhvStar script. */
const BehaviorScript bhvStarKeyCollectionPuffSpawner[] = { 0 };
const BehaviorScript bhvCelebrationStar[] = { 0 };
const BehaviorScript bhvCarrySomething3[] = { 0 };
const BehaviorScript bhvCarrySomething4[] = { 0 };
const BehaviorScript bhvCarrySomething5[] = { 0 };
const BehaviorScript bhvNormalCap[] = { 0 };
const BehaviorScript bhvMetalCap[] = { 0 };
const BehaviorScript bhvWingCap[] = { 0 };
const BehaviorScript bhvVanishCap[] = { 0 };
const BehaviorScript bhvBowser[] = { 0 };
const BehaviorScript bhvHauntedChair[] = { 0 };
const BehaviorScript bhvMadPiano[] = { 0 };
const BehaviorScript bhvMessagePanel[] = { 0 };
void geo_obj_init(struct GraphNodeObject *node, void *model, Vec3f pos, Vec3s angle) {
    (void)node; (void)model; (void)pos; (void)angle;
}
void geo_obj_init_animation(struct GraphNodeObject *node, struct Animation **anim) {
    (void)node; (void)anim;
}
u8 gSpecialTripleJump;
s16 level_trigger_warp(struct MarioState *m, s32 operation) {
    (void)m;
    if (operation == WARP_OP_STAR_EXIT) sNativeStarExitRequested = 1;
    return 0;
}
void load_level_init_text(u32 arg) { (void)arg; }
void set_sound_moving_speed(u8 bank, u8 speed) { (void)bank; (void)speed; }
void *virtual_to_segmented(u32 segment, const void *address) {
    (void)segment;
    return (void *)address;
}
s32 retrieve_animation_index(s32 frame, u16 **attributes) {
    (void)frame; (void)attributes;
    return 0;
}
void print_text_fmt_int(s32 x, s32 y, const char *text, s32 value) {
    (void)x; (void)y; (void)text; (void)value;
}
s16 geo_update_animation_frame(struct AnimInfo *info, s32 *accelAssist) {
    (void)info; (void)accelAssist;
    return 0;
}

static struct Surface *sNativeSurfaces;
static struct SurfaceNode *sNativeNodes;
static struct MarioState sNativeMario;
static struct Object *sNativeMarioObject;
static struct Object *sNativeStarObject;
static struct Object *sNativeYellowCoins[48];
static s32 sNativeYellowCoinSource[48];
static s32 sNativeYellowCoinCount;
static s32 sNativeStarProbeOK;
static struct Area sNativeArea;
static struct Camera sNativeCamera;
static struct Controller sNativeController;
static u32 sNativeNodeCount;
static u16 sNativePreviousButtons;
static s32 sNativeObjectPoolInitialized;

volatile s32 native_sm64_canonical_coins;
volatile s32 native_sm64_canonical_yellow_coin_check;

struct GraphNode *geo_remove_child(struct GraphNode *node) {
    return node;
}
struct GraphNode *geo_add_child(struct GraphNode *parent, struct GraphNode *child) {
    (void)parent;
    return child;
}

static void initialize_native_object_pool(void) {
    if (sNativeObjectPoolInitialized) return;
    clear_object_lists(gObjectListArray);
    gObjectLists = gObjectListArray;
    /* The stock initializer assumes 240 slots. Seed the canonical free list
       using the calculator's measured 64-object pool instead. */
    gFreeObjectList.next = (struct ObjectNode *)&gObjectPool[0];
    for (int i = 0; i < SM50_NATIVE_OBJECT_POOL_CAPACITY - 1; i++)
        gObjectPool[i].header.next = (struct ObjectNode *)&gObjectPool[i + 1];
    gObjectPool[SM50_NATIVE_OBJECT_POOL_CAPACITY - 1].header.next = NULL;
    sNativeMarioObject = try_allocate_object(&gObjectLists[OBJ_LIST_PLAYER], &gFreeObjectList);
    if (!sNativeMarioObject) return;
    sNativeMarioObject->activeFlags = ACTIVE_FLAG_ACTIVE | ACTIVE_FLAG_UNK8;
    sNativeMarioObject->parentObj = sNativeMarioObject;
    sNativeMarioObject->hitboxRadius = 50.0f;
    sNativeMarioObject->hitboxHeight = 160.0f;
    sNativeMarioObject->oIntangibleTimer = 0;
    gMarioObject = sNativeMarioObject;
    sNativeObjectPoolInitialized = 1;
}

volatile s32 native_sm64_canonical_anim_index;

/* Map canonical animation ids onto the ROM-derived subset packed for SM50. */
static void publish_native_animation(s32 targetAnimID) {
    switch (targetAnimID) {
        case MARIO_ANIM_IDLE_HEAD_CENTER:
            native_sm64_canonical_anim_index = SM50_ANIM_IDLE; break;
        case MARIO_ANIM_WALKING:
            native_sm64_canonical_anim_index = SM50_ANIM_WALK; break;
        case MARIO_ANIM_RUNNING:
            native_sm64_canonical_anim_index = SM50_ANIM_RUN; break;
        case MARIO_ANIM_SINGLE_JUMP:
            native_sm64_canonical_anim_index = SM50_ANIM_JUMP; break;
        case MARIO_ANIM_GENERAL_FALL:
            native_sm64_canonical_anim_index = SM50_ANIM_FALL; break;
        case MARIO_ANIM_LAND_FROM_SINGLE_JUMP:
            native_sm64_canonical_anim_index = SM50_ANIM_LAND; break;
        case MARIO_ANIM_BACKFLIP:
            native_sm64_canonical_anim_index = SM50_ANIM_FLIP; break;
        case MARIO_ANIM_AIR_FORWARD_KB:
            native_sm64_canonical_anim_index = SM50_ANIM_HURT; break;
        default:
            if (sNativeMario.action & ACT_FLAG_AIR) {
                native_sm64_canonical_anim_index = sNativeMario.vel[1] > 0.0f
                    ? SM50_ANIM_JUMP : SM50_ANIM_FALL;
            } else {
                native_sm64_canonical_anim_index = SM50_ANIM_IDLE;
            }
            break;
    }
}

/* Animation decoding stays in the ROM-backed renderer; these hooks keep
 * action logic independent of the decompilation's N64 animation runtime. */
s16 sm50_set_mario_animation(struct MarioState *m, s32 targetAnimID) {
    (void)m;
    publish_native_animation(targetAnimID);
    return 0;
}
s16 sm50_set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel) {
    (void)m; (void)accel;
    publish_native_animation(targetAnimID);
    return 0;
}

static s32 initialize_native_mario(const s32 spawn[3]) {
    struct Surface *floor = NULL;
    f32 floorHeight = find_floor(spawn[0], spawn[1] + 200.0f,
                                 spawn[2], &floor);
    if (!floor) return 0;

    memset(&sNativeMario, 0, sizeof(sNativeMario));
    memset(gBodyStates, 0, sizeof(gBodyStates));
    initialize_native_object_pool();
    if (!sNativeMarioObject ||
        gObjectLists[OBJ_LIST_PLAYER].next != (struct ObjectNode *)sNativeMarioObject ||
        gObjectLists[OBJ_LIST_PLAYER].prev != (struct ObjectNode *)sNativeMarioObject) return 0;
    memset(&sNativeMarioObject->header.gfx, 0, sizeof(sNativeMarioObject->header.gfx));
    memset((u8 *)sNativeMarioObject + sizeof(struct ObjectNode), 0,
           sizeof(*sNativeMarioObject) - sizeof(struct ObjectNode));
    sNativeMarioObject->activeFlags = ACTIVE_FLAG_ACTIVE | ACTIVE_FLAG_UNK8;
    sNativeMarioObject->parentObj = sNativeMarioObject;
    sNativeMarioObject->hitboxRadius = 50.0f;
    sNativeMarioObject->hitboxHeight = 160.0f;
    sNativeMarioObject->hitboxDownOffset = 0.0f;
    sNativeMarioObject->oIntangibleTimer = 0;
    memset(&sNativeArea, 0, sizeof(sNativeArea));
    memset(&sNativeCamera, 0, sizeof(sNativeCamera));
    memset(&sNativeController, 0, sizeof(sNativeController));
    sNativeArea.terrainType = TERRAIN_GRASS;
    sNativeArea.camera = &sNativeCamera;
    sNativeMario.floor = floor;
    sNativeMario.floorHeight = floorHeight;
    sNativeMario.pos[0] = spawn[0];
    sNativeMario.pos[1] = floorHeight;
    sNativeMario.pos[2] = spawn[2];
    sNativeMario.faceAngle[1] = (s16)spawn[3];
    sNativeMario.intendedYaw = (s16)spawn[3];
    sNativeMario.action = ACT_IDLE;
    sNativeMario.health = 0x880;
    sNativeMario.waterLevel = -11000;
    sNativeMario.area = &sNativeArea;
    sNativeMario.marioObj = sNativeMarioObject;
    sNativeMario.marioBodyState = &gBodyStates[0];
    sNativeMario.controller = &sNativeController;
    sNativePreviousButtons = 0;
    gMarioState = &sNativeMario;
    gMarioObject = sNativeMarioObject;
    gCurrentObject = sNativeMarioObject;
    gCurrentArea = &sNativeArea;
    gCurrAreaIndex = 1;
    if (!sNativeStarObject) {
        s16 transform[6];
        u32 params;
        if (!sm50_native_find_sm64_spawn(0x13003E3C, transform, &params)) return 0;
        sNativeStarObject = try_allocate_object(&gObjectLists[OBJ_LIST_LEVEL], &gFreeObjectList);
        if (!sNativeStarObject) return 0;
        sNativeStarObject->activeFlags = ACTIVE_FLAG_ACTIVE;
        sNativeStarObject->parentObj = sNativeMarioObject;
        sNativeStarObject->oPosX = transform[0];
        sNativeStarObject->oPosY = transform[1];
        sNativeStarObject->oPosZ = transform[2];
        sNativeStarObject->oHomeX = transform[0];
        sNativeStarObject->oHomeY = transform[1];
        sNativeStarObject->oHomeZ = transform[2];
        sNativeStarObject->oFaceAnglePitch = transform[3];
        sNativeStarObject->oFaceAngleYaw = transform[4];
        sNativeStarObject->oFaceAngleRoll = transform[5];
        sNativeStarObject->header.gfx.scale[0] = 1.0f;
        sNativeStarObject->header.gfx.scale[1] = 1.0f;
        sNativeStarObject->header.gfx.scale[2] = 1.0f;
        sNativeStarObject->oBhvParams = params;
        sNativeStarObject->curBhvCommand = bhvStar;
        gCurrentObject = sNativeStarObject;
        cur_obj_update();
        sNativeStarProbeOK = sNativeStarObject->oInteractType == INTERACT_STAR_OR_KEY
            && sNativeStarObject->oFaceAngleYaw == (s16)(transform[4] + 0x800)
            && sNativeStarObject->oRoom == -1
            && gObjectLists[OBJ_LIST_LEVEL].next == (struct ObjectNode *)sNativeStarObject;
    }
    gCurrentObject = sNativeMarioObject;
    if (!sNativeStarProbeOK) return 0;
    native_sm64_canonical_pos_x_q8 = (s32)(spawn[0] * 256);
    native_sm64_canonical_pos_y_q8 = (s32)(floorHeight * 256.0f);
    native_sm64_canonical_pos_z_q8 = (s32)(spawn[2] * 256);
    native_sm64_canonical_yaw = (u16)spawn[3];
    native_sm64_canonical_anim_index = SM50_ANIM_IDLE;
    return 1;
}

static s32 initialize_native_yellow_coins(void) {
    sNativeYellowCoinCount = sm50_native_yellow_coin_count();
    if (sNativeYellowCoinCount > (s32)(sizeof(sNativeYellowCoins) /
                                        sizeof(sNativeYellowCoins[0]))) return 0;
    for (s32 i = 0; i < sNativeYellowCoinCount; i++) {
        s32 values[5];
        if (!sm50_native_yellow_coin_at(i, values)) return 0;
        struct Object *coin = try_allocate_object(&gObjectLists[OBJ_LIST_LEVEL],
                                                   &gFreeObjectList);
        if (!coin) return 0;
        memset((u8 *)coin + sizeof(struct ObjectNode), 0,
               sizeof(*coin) - sizeof(struct ObjectNode));
        coin->activeFlags = ACTIVE_FLAG_ACTIVE;
        coin->parentObj = gMarioObject;
        coin->oPosX = coin->oHomeX = (f32)values[1];
        coin->oPosY = coin->oHomeY = (f32)values[2];
        coin->oPosZ = coin->oHomeZ = (f32)values[3];
        coin->oFaceAngleYaw = (s16)values[4];
        coin->oMoveAngleYaw = (s16)values[4];
        coin->header.gfx.scale[0] = 1.0f;
        coin->header.gfx.scale[1] = 1.0f;
        coin->header.gfx.scale[2] = 1.0f;
        coin->behavior = bhvYellowCoin;
        coin->curBhvCommand = bhvYellowCoin;
        sNativeYellowCoins[i] = coin;
        sNativeYellowCoinSource[i] = values[0];
        gCurrentObject = coin;
        cur_obj_update(); /* original SM64 behavior VM and coin init callback */
    }
    gCurrentObject = gMarioObject;
    return 1;
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
volatile u16 native_sm64_canonical_yaw;

int native_sm64_canonical_probe(void) {
    s32 spawn[4];
    const u8 *collision = NULL;
    u32 collisionSize = 0;
    if (!sm50_native_rom_data(spawn, &collision, &collisionSize) ||
        !load_rom_collision(collision, collisionSize)) return 0;

    if (!initialize_native_mario(spawn)) return 0;
    struct MarioState *mario = &sNativeMario;
    /* Keep the standalone movement regression on its original flat +Z path;
     * runtime initialization below restores the ROM's actual spawn yaw. */
    mario->faceAngle[1] = 0;
    mario->intendedYaw = 0;
    mario->intendedMag = 32.0f;
    struct Surface *floor = mario->floor;
    f32 floorHeight = mario->floorHeight;
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
    s32 pass = mario->forwardVel > 0.0f && mario->forwardVel < 48.0f
        && floor->normal.y > 0.95f && sNativeNodeCount == 5340
        && stepResult == GROUND_STEP_NONE;
    /* The 30-tick diagnostic is not the player's start state. Restore the
     * actual ROM spawn so gameplay begins with canonical zero velocity. */
    if (!initialize_native_mario(spawn)) return 0;

    /* Exercise the full ROM-star collision path once at startup: canonical
       behavior setup -> canonical hitbox detection -> original star handler
       -> canonical star-deletion behavior. Then rebuild the object pool so
       this self-check cannot award a star or consume a gameplay object slot. */
    native_sm64_canonical_runtime_ready = 0;
    sNativeSavedStars = 0;
    native_sm64_canonical_star_collected = 0;
    native_sm64_canonical_star_debug = 0;
    native_sm64_canonical_star_hitbox_debug = 0;
    sNativeMario.pos[0] = sNativeStarObject->oPosX;
    sNativeMario.pos[1] = sNativeStarObject->oPosY;
    sNativeMario.pos[2] = sNativeStarObject->oPosZ;
    sNativeMario.action = ACT_IDLE;
    sNativeMario.floor = NULL;
    sNativeMario.floorHeight = find_floor(sNativeMario.pos[0],
        sNativeMario.pos[1] + 200.0f, sNativeMario.pos[2], &sNativeMario.floor);
    native_sm64_canonical_tick(0, 0);
    s32 starAction = sNativeMario.action == ACT_STAR_DANCE_EXIT ||
                     sNativeMario.action == ACT_STAR_DANCE_NO_EXIT;
    s32 starSaved = (sNativeSavedStars & (1u << 5)) != 0;
    native_sm64_canonical_tick(0, 0);
    native_sm64_canonical_star_interaction_ok = starAction && starSaved
        && !(sNativeStarObject->activeFlags & ACTIVE_FLAG_ACTIVE);

    /* Run the ROM-derived yellow-coin placement through canonical behavior
       setup, hitbox collision, Mario interaction and the original coin loop. */
    native_sm64_canonical_yellow_coin_check = 0;
    if (initialize_native_yellow_coins() && sNativeYellowCoinCount > 0) {
        s32 coinData[5];
        if (sm50_native_yellow_coin_at(0, coinData)) {
            sNativeMario.pos[0] = (f32)coinData[1];
            sNativeMario.pos[1] = (f32)coinData[2];
            sNativeMario.pos[2] = (f32)coinData[3];
            sNativeMario.forwardVel = 0.0f;
            sNativeMario.numCoins = 0;
            sNativeMario.action = ACT_IDLE;
            sNativeMario.floor = NULL;
            sNativeMario.floorHeight = find_floor(sNativeMario.pos[0],
                sNativeMario.pos[1] + 200.0f, sNativeMario.pos[2], &sNativeMario.floor);
            native_sm64_canonical_tick(0, 0);
            native_sm64_canonical_tick(0, 0);
            native_sm64_canonical_yellow_coin_check = sNativeMario.numCoins == 1
                && !(sNativeYellowCoins[0]->activeFlags & ACTIVE_FLAG_ACTIVE);
        }
    }

    sNativeObjectPoolInitialized = 0;
    sNativeMarioObject = NULL;
    sNativeStarObject = NULL;
    sNativeYellowCoinCount = 0;
    native_sm64_canonical_coins = 0;
    memset(sNativeYellowCoins, 0, sizeof(sNativeYellowCoins));
    memset(gObjectPool, 0, sizeof(gObjectPool));
    clear_object_lists(gObjectListArray);
    sNativeSavedStars = 0;
    native_sm64_canonical_star_collected = 0;
    sNativePreviousButtons = 0;
    gTimeStopState = 0;
    sNativeStarExitRequested = 0;
    if (!initialize_native_mario(spawn)) return 0;
    if (!initialize_native_yellow_coins()) return 0;
    native_sm64_canonical_runtime_ready = 1;
    pass = pass && native_sm64_canonical_star_interaction_ok
        && native_sm64_canonical_yellow_coin_check;
    return pass;
}

void native_sm64_canonical_tick(u16 buttons, u16 cameraYaw) {
    s32 ix = ((buttons & 8) != 0) - ((buttons & 4) != 0);
    s32 iz = ((buttons & 1) != 0) - ((buttons & 2) != 0);

    s32 jumpPressed = (buttons & 0x10) && !(sNativePreviousButtons & 0x10);
    s32 actionPressed = (buttons & 0x20) && !(sNativePreviousButtons & 0x20);
    sNativePreviousButtons = buttons;
    sNativeCamera.yaw = (s16)cameraYaw;
    sNativeController.buttonDown = ((buttons & 0x10) ? A_BUTTON : 0)
        | ((buttons & 0x20) ? B_BUTTON : 0);
    sNativeController.buttonPressed = (jumpPressed ? A_BUTTON : 0)
        | (actionPressed ? B_BUTTON : 0);
    /* Map calculator up/right into the original N64 stick axes. */
    sNativeController.stickX = (f32)iz * 64.0f;
    sNativeController.stickY = (f32)ix * 64.0f;
    sNativeController.stickMag = (ix || iz) ? 64.0f : 0.0f;

    /* Run the original ROM object's behavior and hitbox pass before Mario
       consumes the resulting interaction flags. */
    sNativeMarioObject->oPosX = sNativeMario.pos[0];
    sNativeMarioObject->oPosY = sNativeMario.pos[1];
    sNativeMarioObject->oPosZ = sNativeMario.pos[2];
    sNativeMarioObject->oFaceAngleYaw = sNativeMario.faceAngle[1];
    if (sNativeStarObject && (sNativeStarObject->activeFlags & ACTIVE_FLAG_ACTIVE)) {
        gCurrentObject = sNativeStarObject;
        cur_obj_update();
    }
    for (s32 i = 0; i < sNativeYellowCoinCount; i++) {
        struct Object *coin = sNativeYellowCoins[i];
        if (coin && (coin->activeFlags & ACTIVE_FLAG_ACTIVE)) {
            gCurrentObject = coin;
            cur_obj_update();
        }
        if (native_sm64_canonical_runtime_ready && coin &&
            !(coin->activeFlags & ACTIVE_FLAG_ACTIVE))
            sm50_native_hide_coin(sNativeYellowCoinSource[i]);
    }
    gCurrentObject = sNativeMarioObject;
    detect_object_collisions();
    if (!native_sm64_canonical_runtime_ready && sNativeStarObject) {
        native_sm64_canonical_star_debug =
            (sNativeMarioObject->numCollidedObjs & 0xff)
            | ((sNativeMarioObject->collidedObjInteractTypes & 0xff) << 8)
            | ((sNativeStarObject->oInteractType & 0xff) << 16);
        native_sm64_canonical_star_hitbox_debug =
            ((sNativeMarioObject->hitboxRadius > 0.0f) ? 1 : 0)
            | ((sNativeMarioObject->hitboxHeight > 0.0f) ? 2 : 0)
            | ((sNativeStarObject->hitboxRadius > 0.0f) ? 4 : 0)
            | ((sNativeStarObject->hitboxHeight > 0.0f) ? 8 : 0)
            | ((sNativeStarObject->activeFlags & ACTIVE_FLAG_ACTIVE) ? 16 : 0);
    }

    update_mario_inputs(&sNativeMario);
    mario_process_interactions(&sNativeMario);
    if (sNativeMario.floor) {
        s32 inLoop = TRUE;
        while (inLoop) {
            if (sNativeMario.action == ACT_STAR_DANCE_EXIT ||
                sNativeMario.action == ACT_STAR_DANCE_NO_EXIT) {
                inLoop = act_star_dance(&sNativeMario);
                if (sNativeStarExitRequested) {
                    sNativeStarExitRequested = 0;
                    set_mario_action(&sNativeMario, ACT_IDLE, 0);
                }
                continue;
            }
            switch (sNativeMario.action & ACT_GROUP_MASK) {
                case ACT_GROUP_STATIONARY:
                    inLoop = mario_execute_stationary_action(&sNativeMario); break;
                case ACT_GROUP_MOVING:
                    inLoop = mario_execute_moving_action(&sNativeMario); break;
                case ACT_GROUP_AIRBORNE:
                    inLoop = mario_execute_airborne_action(&sNativeMario); break;
                case ACT_GROUP_SUBMERGED:
                    inLoop = mario_execute_submerged_action(&sNativeMario); break;
                case ACT_GROUP_AUTOMATIC:
                    inLoop = mario_execute_automatic_action(&sNativeMario); break;
                case ACT_GROUP_OBJECT:
                    inLoop = mario_execute_object_action(&sNativeMario); break;
                default:
                    inLoop = FALSE; break;
            }
        }
    }

    native_sm64_canonical_pos_x_q8 = (s32)(sNativeMario.pos[0] * 256.0f);
    native_sm64_canonical_pos_y_q8 = (s32)(sNativeMario.pos[1] * 256.0f);
    native_sm64_canonical_pos_z_q8 = (s32)(sNativeMario.pos[2] * 256.0f);
    native_sm64_canonical_yaw = (u16)sNativeMario.faceAngle[1];
    native_sm64_canonical_coins = sNativeMario.numCoins;
    sm50_native_publish_mario(native_sm64_canonical_pos_x_q8,
                              native_sm64_canonical_pos_y_q8,
                              native_sm64_canonical_pos_z_q8,
                              native_sm64_canonical_yaw,
                              (s32)(sNativeMario.forwardVel * 256.0f),
                              (s32)(sNativeMario.vel[1] * 256.0f),
                              (s32)(sNativeMario.floorHeight * 256.0f),
                              !(sNativeMario.action & ACT_FLAG_AIR),
                              sNativeMario.action == ACT_JUMP_LAND ||
                                  sNativeMario.action == ACT_FREEFALL_LAND,
                              native_sm64_canonical_anim_index);
}
