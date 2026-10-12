"""Build a low-poly Mario skeleton from ROM display-list addresses.

The joints follow the US game's low-poly geo hierarchy.  The ROM supplies
every mesh and texture.  The compact poses below are independently generated
until the native animation stream is interpreted.
"""
import math
import struct


# parent, local pivot, segmented display-list address
PARTS = [
    (-1, (0, 0, 0), 0x04016AB8),       # pelvis
    (0, (68, 0, 0), 0x04017EA0),       # torso
    (1, (87, 0, 0), 0x04018460),       # head, cap on / eyes open
    (1, (67, -10, 79), 0x04016C70),   # left upper arm
    (3, (65, 0, 0), 0x04016D50),       # left forearm
    (4, (60, 0, 0), 0x04016E80),       # left glove
    (1, (68, -10, -79), 0x04016FB0),  # right upper arm
    (6, (65, 0, 0), 0x04017090),       # right forearm
    (7, (60, 0, 0), 0x040171C0),       # right glove
    (0, (13, -8, 42), 0x04017360),    # left thigh
    (9, (89, 0, 0), 0x040174E8),      # left shin
    (10, (67, 0, 0), 0x040176A8),     # left shoe
    (0, (13, -8, -42), 0x04017818),   # right thigh
    (12, (89, 0, 0), 0x04017918),     # right shin
    (13, (67, 0, 0), 0x04017AD8),     # right shoe
]


def build(segs, walker):
    for _, _, addr in PARTS:
        info = walker(segs, addr)
        if info.errors or not info.tris:
            raise ValueError("Mario display list %08X: %s" % (addr, info.errors))

    skeleton = struct.pack(">I", len(PARTS)) + b"".join(
        struct.pack(">hhhhI", parent, *pivot, addr) for parent, pivot, addr in PARTS)

    def frame(phase, action):
        # Rotations are in the model's local X-up coordinates.
        s = math.sin(phase * 2 * math.pi)
        bend = 0
        swing = 0
        if action == 1:  # walk
            swing, bend = int(20 * s), int(12 * abs(s))
        elif action == 2:  # run
            swing, bend = int(34 * s), int(24 * abs(s))
        elif action == 3:  # jump
            swing, bend = -25, 25
        elif action == 4:  # fall
            swing, bend = -10, 12
        elif action == 5:  # land
            swing, bend = 0, 30
        elif action == 6:  # flip
            swing, bend = 45, 35
        elif action == 7:  # hurt
            swing, bend = -30, 20
        rot = [(0, 0, 0)] * len(PARTS)
        def z(part, degrees):
            rot[part] = (0, 0, int(degrees * 65536 / 360) & 0xFFFF)
        z(3, 166 - swing * 0.7)
        z(6, 194 + swing * 0.7)
        z(4, -12); z(7, 12)
        z(9, 180 + swing - bend)
        z(12, 180 - swing - bend)
        z(10, bend); z(13, bend)
        z(11, -70); z(14, 70)
        root_y = 0
        out = bytearray(struct.pack(">h", root_y))
        for xyz in rot:
            out.extend(struct.pack(">HHH", *xyz))
        return out

    specs = [(8, 1), (12, 1), (10, 1), (1, 0), (1, 0), (2, 0), (1, 0), (1, 0)]
    animations = []
    for action, (nframes, loop) in enumerate(specs):
        anim = bytearray(struct.pack(">HHHH", nframes, len(PARTS), loop, 0))
        for f in range(nframes):
            anim.extend(frame(f / nframes, action))
        animations.append(bytes(anim))
    head = bytearray(struct.pack(">I", len(animations)))
    body = bytearray()
    for anim in animations:
        head.extend(struct.pack(">I", 4 + 4 * len(animations) + len(body)))
        body.extend(anim)
        while len(body) % 4:
            body.append(0)
    # scale Q14, Z rotation (X-up to Y-up), world-space Y anchor
    # Double the original 1/4 model scale so he remains legible at 198x112.
    player = struct.pack(">HHh", 8192, 0x4000, -35)
    return skeleton, bytes(head + body), player
