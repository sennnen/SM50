#!/usr/bin/env python3
"""g3a_check.py - validate a Prizm/fx-CG .g3a add-in and print its fields.

Usage: g3a_check.py [--dump-icons PREFIX] file.g3a [...]
Exit status 0 when every check passes, 1 otherwise.
Original code for the SM50 project (see mkg3a.py for the layout).
"""
import struct
import sys
import zlib

MAGIC = bytes([0xAA, 0xAC, 0xBD, 0xAF, 0x90, 0x88, 0x9A, 0x8D,
               0xD3, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF])
LANGS = ["en", "es", "de", "fr", "pt", "zh", "un1", "un2"]


def cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def write_png(path, w, h, rgb565_be):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            c = struct.unpack_from(">H", rgb565_be, 2 * (y * w + x))[0]
            r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
            raw += bytes([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)])

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def check(path, dump=None):
    d = open(path, "rb").read()
    ok = True

    def res(cond, what):
        nonlocal ok
        print(f"  [{'OK ' if cond else 'BAD'}] {what}")
        ok = ok and cond

    print(f"{path}: {len(d)} bytes")
    if len(d) < 0x7004:
        res(False, "file shorter than header + checksum")
        return False
    size = len(d)
    res(d[:14] == MAGIC, "magic bytes")
    inv = struct.unpack_from(">I", d, 0x10)[0]
    res(inv == (~size) & 0xFFFFFFFF, f"0x10 inverted size 0x{inv:08X} (expect 0x{(~size) & 0xFFFFFFFF:08X})")
    res(d[0x0E] == (d[0x13] - 0x41) & 0xFF, f"0x0E control byte 0x{d[0x0E]:02X}")
    res(d[0x0F] == 0xFE, f"0x0F = 0x{d[0x0F]:02X} (expect FE)")
    res(d[0x14] == (d[0x13] - 0xB8) & 0xFF, f"0x14 control byte 0x{d[0x14]:02X}")
    fsize = struct.unpack_from(">I", d, 0x5C)[0]
    res(fsize == size, f"0x5C file size {fsize}")
    csize = struct.unpack_from(">I", d, 0x2E)[0]
    res(csize == size - 0x7004, f"0x2E code size {csize} (expect {size - 0x7004})")
    ck = struct.unpack_from(">I", d, 0x20)[0]
    calc = (sum(d[:0x20]) + sum(d[0x24:size - 4])) & 0xFFFFFFFF
    res(ck == calc, f"0x20 checksum 0x{ck:08X} (computed 0x{calc:08X})")
    tail = struct.unpack_from(">I", d, size - 4)[0]
    res(tail == ck, f"trailing checksum 0x{tail:08X}")
    res(d[0x24:0x26] == b"\x01\x01", "0x24 = 01 01")
    iname = cstr(d[0x60:0x6B])
    res(iname.startswith("@"), f"internal name '{iname}'")
    print(f"  basic name : '{cstr(d[0x40:0x5C])}'")
    for i, lc in enumerate(LANGS):
        print(f"  name[{lc:3}] : '{cstr(d[0x6B + 0x18 * i:0x6B + 0x18 * (i + 1)])}'")
    print(f"  version    : '{cstr(d[0x130:0x13C])}'")
    print(f"  timestamp  : '{cstr(d[0x13C:0x14B])}'")
    print(f"  filename   : '{cstr(d[0xEBC:0x1000])}'")
    code = d[0x7000:size - 4]
    print(f"  code       : {len(code)} bytes, first words: {code[:16].hex(' ', 4)}")
    if dump:
        write_png(dump + "_uns.png", 92, 64, d[0x1000:0x1000 + 0x2E00])
        write_png(dump + "_sel.png", 92, 64, d[0x4000:0x4000 + 0x2E00])
        print(f"  icons dumped to {dump}_uns.png / {dump}_sel.png")
    print("  RESULT:", "valid" if ok else "INVALID")
    return ok


def main(argv):
    dump = None
    files = []
    i = 0
    while i < len(argv):
        if argv[i] == "--dump-icons":
            dump = argv[i + 1]
            i += 2
            continue
        files.append(argv[i])
        i += 1
    if not files:
        print(__doc__)
        return 2
    allok = True
    for f in files:
        allok &= check(f, dump)
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
