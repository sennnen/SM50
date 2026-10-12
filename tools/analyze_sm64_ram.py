#!/usr/bin/env python3
"""Report bounded native-SM64 object and collision memory for an SM50 pack."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path


GRID = 16
CELL = 1024
ORIGIN = -8192
OBJECT_BYTES_SH4 = 548
SURFACE_BYTES_SH4 = 40
NODE_BYTES_SH4 = 8
DEFAULT_OBJECTS = 200
CALCULATOR_OBJECTS = 64


def be16s(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def pack_entry(pack: bytes, tag: bytes) -> bytes:
    count, data_offset = struct.unpack_from(">II", pack, 8)
    for index in range(count):
        entry = 16 + index * 12
        if pack[entry:entry + 4] == tag:
            offset, size = struct.unpack_from(">II", pack, entry + 4)
            return pack[data_offset + offset:data_offset + offset + size]
    raise ValueError(f"pack entry {tag!r} not found")


def lower_cell(value: int) -> int:
    coord = max(0, value - ORIGIN)
    index = coord // CELL
    if coord % CELL < 50:
        index -= 1
    return max(0, index)


def upper_cell(value: int) -> int:
    coord = max(0, value - ORIGIN)
    index = coord // CELL
    if coord % CELL > CELL - 50:
        index += 1
    return min(GRID - 1, index)


def analyze(pack_path: Path) -> dict[str, int]:
    pack = pack_path.read_bytes()
    if len(pack) < 16 or pack[:4] != b"S50P" or struct.unpack_from(">I", pack, 4)[0] != 1:
        raise ValueError("not an SM50 v1 content pack")
    col = pack_entry(pack, b"COLV")
    triangle_count = struct.unpack_from(">I", col)[0]
    if len(col) != 4 + triangle_count * 28:
        raise ValueError("COLV payload has inconsistent triangle count")

    # This follows the engine's 16x16, 1024-unit collision partition. Wall
    # triangles expand their bounds by 100 units before assignment.
    occupancy = [[0, 0, 0] for _ in range(GRID * GRID)]
    node_count = 0
    for i in range(triangle_count):
        tri = col[4 + i * 28:4 + (i + 1) * 28]
        ny = be16s(tri, 20)
        kind = 0 if ny > 164 else 1 if ny < -164 else 2  # floor, ceiling, wall
        xs = [be16s(tri, o) for o in (0, 6, 12)]
        zs = [be16s(tri, o) for o in (4, 10, 16)]
        # Match SM64's lower_cell_index/upper_cell_index boundary rules,
        # including their strict <50 and >CELL_SIZE-50 comparisons.
        x0, x1 = lower_cell(min(xs)), upper_cell(max(xs))
        z0, z1 = lower_cell(min(zs)), upper_cell(max(zs))
        for cz in range(z0, z1 + 1):
            for cx in range(x0, x1 + 1):
                occupancy[cz * GRID + cx][kind] += 1
                node_count += 1

    return {
        "triangles": triangle_count,
        "nodes": node_count,
        "occupied_cells": sum(any(counts) for counts in occupancy),
        "max_cell_nodes": max(map(sum, occupancy), default=0),
        "surfaces_bytes": triangle_count * SURFACE_BYTES_SH4,
        "nodes_bytes": node_count * NODE_BYTES_SH4,
        "total_surface_bytes": triangle_count * SURFACE_BYTES_SH4 + node_count * NODE_BYTES_SH4,
        "default_object_pool_bytes": DEFAULT_OBJECTS * OBJECT_BYTES_SH4,
        "calculator_object_pool_bytes": CALCULATOR_OBJECTS * OBJECT_BYTES_SH4,
        "object_pool_saved_bytes": (DEFAULT_OBJECTS - CALCULATOR_OBJECTS) * OBJECT_BYTES_SH4,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack", type=Path, help="ROM-derived SM50 pack")
    args = parser.parse_args()
    for label, value in analyze(args.pack).items():
        print(f"{label}={value}")


if __name__ == "__main__":
    main()
