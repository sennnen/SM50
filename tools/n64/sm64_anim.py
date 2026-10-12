"""Decode Mario's ROM animation table into dense joint rotations.

The native table stores each animation as channel (count, offset) pairs plus
signed 16-bit sample values. The engine pack keeps a bounded dense subset
for the actions its current controller can play.
"""
import struct


class AnimationError(ValueError):
    pass


# Engine actions: idle, walk, run, jump, fall, land, flip, hurt.
ACTION_IDS = (0xC5, 0x48, 0x72, 0x4D, 0x56, 0x4E, 0x04, 0x2D)


def convert(rom, table_offset, ids=ACTION_IDS, joints=20):
    if table_offset + 8 > len(rom):
        raise AnimationError("Mario animation table outside ROM")
    total = struct.unpack_from(">I", rom, table_offset)[0]
    if not 1 <= total <= 1024:
        raise AnimationError("unreasonable Mario animation count")
    animations = []
    metadata = []
    for anim_id in ids:
        if anim_id >= total:
            raise AnimationError("Mario animation %d missing" % anim_id)
        off, size = struct.unpack_from(">II", rom, table_offset + 8 + anim_id * 8)
        pos = table_offset + off
        if size < 24 or pos + size > len(rom):
            raise AnimationError("Mario animation %d outside ROM" % anim_id)
        flags, divisor, start, loop_start, end, bones = struct.unpack_from(">hhhhhh", rom, pos)
        values_off, index_off, declared = struct.unpack_from(">III", rom, pos + 12)
        if bones != joints or declared != size or not 0 <= start < end <= 256:
            raise AnimationError("Mario animation %d has unsupported header" % anim_id)
        if index_off + (bones + 1) * 12 > size or values_off >= size:
            raise AnimationError("Mario animation %d has invalid channel pointers" % anim_id)
        channel_count = (bones + 1) * 3
        channels = [struct.unpack_from(">HH", rom, pos + index_off + i * 4)
                    for i in range(channel_count)]
        value_count = (size - values_off) // 2
        if any(count == 0 or offset + count > value_count for count, offset in channels):
            raise AnimationError("Mario animation %d has invalid channel range" % anim_id)
        loop = 0 if flags & 1 else 1
        animation = bytearray(struct.pack(">HHHH", end - start, joints, loop, 0))
        for frame in range(start, end):
            # Root translation is handled by the controller. Native joint
            # rotations retain their original angle units and frame timing.
            animation.extend(struct.pack(">h", 0))
            for bone in range(joints):
                for axis in range(3):
                    count, offset = channels[3 + bone * 3 + axis]
                    sample = offset + min(frame, count - 1)
                    angle = struct.unpack_from(">H", rom, pos + values_off + sample * 2)[0]
                    animation.extend(struct.pack(">H", angle))
        animations.append(bytes(animation))
        metadata.append((anim_id, end - start, loop))
    head = bytearray(struct.pack(">I", len(animations)))
    body = bytearray()
    for animation in animations:
        head.extend(struct.pack(">I", 4 + 4 * len(animations) + len(body)))
        body.extend(animation)
        while len(body) % 4:
            body.append(0)
    return bytes(head + body), metadata
