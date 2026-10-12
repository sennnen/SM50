"""Decode Super Mario 64's compact macro-object placement list."""
import struct


class MacroError(ValueError):
    pass


def read(data, offset, limit=4096):
    objects = []
    pos = offset
    for _ in range(limit):
        if pos + 2 > len(data):
            raise MacroError("macro object list runs past segment")
        encoded = struct.unpack_from(">H", data, pos)[0]
        pos += 2
        if encoded == 0x1E:
            return objects
        if pos + 8 > len(data):
            raise MacroError("truncated macro object")
        x, y, z, param = struct.unpack_from(">hhhH", data, pos)
        pos += 8
        preset = (encoded & 0x1FF) - 0x1F
        yaw = ((encoded >> 9) * 0x200) & 0xFFFF
        if preset < 0:
            raise MacroError("invalid macro preset %d" % preset)
        objects.append((preset, x, y, z, yaw, param))
    raise MacroError("macro object limit reached without terminator")
