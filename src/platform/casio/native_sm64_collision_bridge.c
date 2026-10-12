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
