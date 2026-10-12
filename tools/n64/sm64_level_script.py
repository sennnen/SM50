"""Walk the subset of SM64 level-script commands needed for static placements.

Addresses are resolved within a caller-supplied raw segment.  Calls to
other segments are skipped until their data is available to the converter.
"""
import struct


class ScriptError(ValueError):
    pass


def read(segment, entry, segment_id=0x0E, act=1):
    objects = []
    terrain = macro_objects = mario_pos = None
    commands = 0

    def walk(addr, depth):
        nonlocal commands, terrain, macro_objects, mario_pos
        if depth > 16:
            raise ScriptError("level-script call depth exceeded")
        if addr >> 24 != segment_id:
            return
        pos = addr & 0xFFFFFF
        while True:
            if pos + 4 > len(segment):
                raise ScriptError("level script runs past segment")
            op, length = segment[pos], segment[pos + 1]
            if length < 4 or pos + length > len(segment):
                raise ScriptError("invalid command length at 0x%X" % pos)
            cmd = segment[pos:pos + length]
            pos += length
            commands += 1
            if commands > 10000:
                raise ScriptError("level-script command limit exceeded")
            if op == 0x06 and length == 8:  # JUMP_LINK
                walk(struct.unpack_from(">I", cmd, 4)[0], depth + 1)
            elif op in (0x24, 0x2C) and length == 24:  # OBJECT_WITH_ACTS
                if cmd[2] & (1 << (act - 1)):
                    x, y, z, pitch, yaw, roll = struct.unpack_from(">hhhhhh", cmd, 4)
                    param, behaviour = struct.unpack_from(">II", cmd, 16)
                    objects.append((cmd[3], x, y, z, pitch, yaw, roll, param, behaviour))
            elif op == 0x2B and length == 12:  # MARIO_POS
                yaw, x, y, z = struct.unpack_from(">hhhh", cmd, 4)
                mario_pos = (x, y, z, yaw)
            elif op == 0x2E and length == 8:  # TERRAIN
                terrain = struct.unpack_from(">I", cmd, 4)[0]
            elif op == 0x39 and length == 8:  # MACRO_OBJECTS
                macro_objects = struct.unpack_from(">I", cmd, 4)[0]
            elif op in (0x02, 0x07, 0x1C):  # EXIT, RETURN, CLEAR_LEVEL
                return

    walk(entry, 0)
    return {"objects": objects, "terrain": terrain,
            "macro_objects": macro_objects, "mario_pos": mario_pos}
