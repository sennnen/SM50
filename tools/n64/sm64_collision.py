"""Read the collision command stream used by Super Mario 64.

The input is a decompressed segment and a segment-local offset.  This module
only interprets geometry and surface IDs; it does not execute game code.
"""
import struct


class CollisionError(ValueError):
    pass


def read(data, offset):
    def word(pos):
        if pos < 0 or pos + 2 > len(data):
            raise CollisionError("collision stream runs past segment at 0x%X" % pos)
        return struct.unpack_from(">H", data, pos)[0]

    def signed(pos):
        v = word(pos)
        return v - 0x10000 if v & 0x8000 else v

    if word(offset) != 0x40:
        raise CollisionError("expected vertex command 0x40 at 0x%X" % offset)
    count = word(offset + 2)
    if count > 16384:
        raise CollisionError("unreasonable vertex count %d" % count)
    pos = offset + 4
    vertices = []
    for _ in range(count):
        vertices.append((signed(pos), signed(pos + 2), signed(pos + 4)))
        pos += 6

    triangles = []
    while True:
        surface = word(pos)
        pos += 2
        if surface == 0x41:  # collision triangles end; object/water data follows
            break
        if surface >= 0x40 and surface < 0x65:
            raise CollisionError("unexpected collision command 0x%X at 0x%X" % (surface, pos - 2))
        n = word(pos)
        pos += 2
        if n > 16384 or len(triangles) + n > 65535:
            raise CollisionError("unreasonable triangle count %d" % n)
        for _ in range(n):
            indices = (word(pos), word(pos + 2), word(pos + 4))
            pos += 6
            if any(i >= count for i in indices):
                raise CollisionError("triangle vertex index outside table")
            # These four surface types carry one extra parameter per triangle.
            if surface in (0x04, 0x0E, 0x2C, 0x2D):
                word(pos)
                pos += 2
            triangles.append((tuple(vertices[i] for i in indices), surface))
    return triangles
