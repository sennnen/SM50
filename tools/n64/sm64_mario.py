"""Low-poly Mario geo hierarchy and ROM display-list addresses (US release)."""
import struct

from . import sm64_anim


# The 20 animated nodes include pivots without geometry. Their order matches
# the native animation channels after the three root-translation channels.
# Each tuple: parent, local pivot, segmented display list (zero means pivot).
PARTS = [
    (-1, (0, 0, 0), 0),
    (0, (0, 0, 0), 0x04016AB8),       # pelvis
    (1, (68, 0, 0), 0x04017EA0),      # torso
    (2, (87, 0, 0), 0x04018460),      # head, cap on / eyes open
    (2, (67, -10, 79), 0),
    (4, (0, 0, 0), 0x04016C70),       # left upper arm
    (5, (65, 0, 0), 0x04016D50),      # left forearm
    (6, (60, 0, 0), 0x04016E80),      # left glove
    (2, (68, -10, -79), 0),
    (8, (0, 0, 0), 0x04016FB0),       # right upper arm
    (9, (65, 0, 0), 0x04017090),      # right forearm
    (10, (60, 0, 0), 0x040171C0),     # right glove
    (1, (13, -8, 42), 0),
    (12, (0, 0, 0), 0x04017360),      # left thigh
    (13, (89, 0, 0), 0x040174E8),     # left shin
    (14, (67, 0, 0), 0x040176A8),     # left shoe
    (1, (13, -8, -42), 0),
    (16, (0, 0, 0), 0x04017818),      # right thigh
    (17, (89, 0, 0), 0x04017918),     # right shin
    (18, (67, 0, 0), 0x04017AD8),     # right shoe
]


def build(segs, walker, rom, animation_table):
    for _, _, addr in PARTS:
        if not addr:
            continue
        info = walker(segs, addr)
        if info.errors or not info.tris:
            raise ValueError("Mario display list %08X: %s" % (addr, info.errors))
    skeleton = struct.pack(">I", len(PARTS)) + b"".join(
        struct.pack(">hhhhI", parent, *pivot, addr) for parent, pivot, addr in PARTS)
    animations, metadata = sm64_anim.convert(rom, animation_table, joints=len(PARTS))
    # Enlarge the source's 1/4 model scale for the calculator's 198x112 view.
    # Native rotation channels already orient the X-up source geometry.
    player = struct.pack(">HHh", 8192, 0, -35)
    return skeleton, animations, player, metadata
