/* Adapt the ROM-derived SM50 collision query to an original SM64 Surface. */
#include "../../engine/world.h"

int sm50_native_rom_floor(s32 values[4]) {
    s32 nx = 0, nz = 0, ny = 0;
    s32 x = gLevel.spawn_x;
    s32 y = gLevel.spawn_y + 200;
    s32 z = gLevel.spawn_z;
    s32 floor_y_q8 = col_find_floor(x, y, z, &ny, 0);

    if (floor_y_q8 == INT32_MIN || !col_floor_normal(x, y, z, &nx, &nz)) {
        return 0;
    }
    values[0] = floor_y_q8;
    values[1] = nx;
    values[2] = ny;
    values[3] = nz;
    return 1;
}

int sm50_native_rom_data(s32 spawn[4], const u8 **collision, u32 *size) {
    const u8 *colv = pack_find(&gLevel.pack, "COLV", size);
    if (!colv) return 0;
    spawn[0] = gLevel.spawn_x;
    spawn[1] = gLevel.spawn_y;
    spawn[2] = gLevel.spawn_z;
    spawn[3] = gLevel.spawn_yaw;
    *collision = colv;
    return 1;
}

int sm50_native_find_sm64_spawn(u32 behavior, s16 transform[6], u32 *params) {
    for (int i = 0; i < gLevel.n_sm64_spawns; i++) {
        const Sm64Spawn *spawn = &gLevel.sm64_spawns[i];
        if (spawn->behavior_addr != behavior) continue;
        transform[0] = spawn->x;
        transform[1] = spawn->y;
        transform[2] = spawn->z;
        transform[3] = spawn->pitch;
        transform[4] = spawn->yaw;
        transform[5] = spawn->roll;
        *params = spawn->behavior_param;
        return 1;
    }
    return 0;
}

/* Expose the converter's ROM-decoded Battlefield yellow-coin placements to
 * the canonical object runtime. Rendering and display-list extraction remain
 * in SM50; coin hitboxes and collection are handled by original SM64 code. */
int sm50_native_yellow_coin_count(void) {
    int count = 0;
    for (int i = 0; i < gLevel.n_objs; i++)
        if (gLevel.objs[i].active && gLevel.objs[i].type == OBJ_COIN) count++;
    return count;
}

int sm50_native_yellow_coin_at(int ordinal, s32 values[5]) {
    int count = 0;
    for (int i = 0; i < gLevel.n_objs; i++) {
        Obj *obj = &gLevel.objs[i];
        if (!obj->active || obj->type != OBJ_COIN) continue;
        if (count++ != ordinal) continue;
        values[0] = i;
        values[1] = obj->x;
        values[2] = obj->y;
        values[3] = obj->z;
        values[4] = obj->yaw;
        return 1;
    }
    return 0;
}

void sm50_native_hide_coin(s32 index) {
    if (index >= 0 && index < gLevel.n_objs && gLevel.objs[index].type == OBJ_COIN)
        gLevel.objs[index].active = 0;
}
