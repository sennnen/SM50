"""N64 ROM container handling: byte order, header, CIC-6102 CRC, hashes."""
import hashlib
import struct

Z64_MAGIC = b"\x80\x37\x12\x40"   # big-endian (native)
V64_MAGIC = b"\x37\x80\x40\x12"   # byte-swapped 16-bit
N64_MAGIC = b"\x40\x12\x37\x80"   # little-endian 32-bit


class RomError(Exception):
    pass


def detect_order(data):
    m = bytes(data[:4])
    if m == Z64_MAGIC:
        return "z64"
    if m == V64_MAGIC:
        return "v64"
    if m == N64_MAGIC:
        return "n64"
    raise RomError("not an N64 ROM image (unknown first word %s)" % m.hex())


def to_z64(data):
    """Return (big-endian bytes, original order)."""
    order = detect_order(data)
    if len(data) % 4:
        raise RomError("ROM size %d is not a multiple of 4" % len(data))
    b = bytearray(data)
    if order == "v64":
        b[0::2], b[1::2] = b[1::2], b[0::2]
    elif order == "n64":
        b[0::4], b[1::4], b[2::4], b[3::4] = b[3::4], b[2::4], b[1::4], b[0::4]
    return bytes(b), order


def from_z64(data, order):
    """Convert big-endian bytes to the requested byte order (for tests/tools)."""
    b = bytearray(data)
    if order == "v64":
        b[0::2], b[1::2] = b[1::2], b[0::2]
    elif order == "n64":
        b[0::4], b[1::4], b[2::4], b[3::4] = b[3::4], b[2::4], b[1::4], b[0::4]
    return bytes(b)


def cic6102_crc(z64):
    """Header checksum for CIC-NUS-6102 boot code (covers 1 MiB from 0x1000)."""
    if len(z64) < 0x101000:
        raise RomError("ROM too small for checksum")
    M = 0xFFFFFFFF
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
    words = struct.unpack(">%dI" % (0x100000 // 4), z64[0x1000:0x101000])
    for d in words:
        if (t6 + d) & M < t6:
            t4 = (t4 + 1) & M
        t6 = (t6 + d) & M
        t3 ^= d
        s = d & 0x1F
        r = ((d << s) | (d >> (32 - s))) & M if s else d
        t5 = (t5 + r) & M
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & M
    return (t6 ^ t4 ^ t3) & M, (t5 ^ t2 ^ t1) & M


def header(z64):
    name = z64[0x20:0x34].decode("ascii", "replace").rstrip(" \0")
    crc1, crc2 = struct.unpack(">II", z64[0x10:0x18])
    return {
        "name": name,
        "crc1": crc1,
        "crc2": crc2,
        "media": chr(z64[0x3B]) if 32 <= z64[0x3B] < 127 else "?",
        "game_id": z64[0x3C:0x3E].decode("ascii", "replace"),
        "region": chr(z64[0x3E]) if 32 <= z64[0x3E] < 127 else "?",
        "version": z64[0x3F],
        "entry": struct.unpack(">I", z64[8:12])[0],
        "size": len(z64),
        "sha1": hashlib.sha1(z64).hexdigest(),
    }


def fix_crc(z64):
    """Return ROM bytes with a recomputed 6102 header checksum (used for synthetic fixtures)."""
    c1, c2 = cic6102_crc(z64)
    b = bytearray(z64)
    b[0x10:0x18] = struct.pack(">II", c1, c2)
    return bytes(b)


def load(path, expect_sha1=None):
    raw = open(path, "rb").read()
    z64, order = to_z64(raw)
    h = header(z64)
    h["order"] = order
    c1, c2 = cic6102_crc(z64)
    h["crc_ok"] = (c1, c2) == (h["crc1"], h["crc2"])
    if expect_sha1 and h["sha1"] != expect_sha1.lower():
        raise RomError("SHA-1 %s does not match the expected %s" % (h["sha1"], expect_sha1))
    return z64, h
