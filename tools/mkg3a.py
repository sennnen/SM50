#!/usr/bin/env python3
"""mkg3a.py - build a Casio fx-CG10/20/50 (Prizm) .g3a add-in container.

Original implementation for the SM50 project (format knowledge from public
documentation of the g3a layout; no third-party code is included).

Usage:
  mkg3a.py [-n lc:name]... [-i uns:file.png] [-i sel:file.png] [-V version]
           input.bin output.g3a

  -n lc:name   lc = basic | internal | en | es | de | fr | pt | zh
               (an empty lc, i.e. ":name" or just "name", means basic)
  -i uns:FILE  unselected menu icon, 92x64 PNG (or raw .bin RGB565 BE)
  -i sel:FILE  selected menu icon
  -V version   version string, default 01.00.0000
  --timestamp "YYYY.MMDD.HHMM"  (default: $SOURCE_DATE_EPOCH or now)

Layout (all multi-byte integers big-endian):
  0x0000 14  magic AA AC BD AF 90 88 9A 8D D3 FF FE FF FE FF
  0x000E  1  control byte A = byte[0x13] - 0x41
  0x000F  1  0xFE
  0x0010  4  ~file_size
  0x0014  1  control byte B = byte[0x13] - 0xB8
  0x0016  2  (CRC, unused, 0)
  0x0020  4  checksum = sum of all bytes of header (with this field = 0)
             and of the code; the same value is appended as the last 4
             bytes of the file
  0x0024  2  01 01
  0x002E  4  code size (= offset of the trailing checksum from 0x7000)
  0x0040 28  basic name
  0x005C  4  file_size (= 0x7000 + code size + 4)
  0x0060 11  internal name, '@' + upper case
  0x006B 8x24 localized names (en es de fr pt zh + 2 spare)
  0x0130 12  version "01.00.0000"
  0x013C 14  timestamp "YYYY.MMDD.HHMM"
  0x0EBC 324 file name (e.g. "SM50.g3a")
  0x1000     unselected icon, 92x64 RGB565 BE (0x2E00 bytes)
  0x4000     selected icon,   92x64 RGB565 BE
  0x7000     code (loaded at 0x00300000)
  EOF-4   4  checksum copy
"""
import argparse
import os
import struct
import sys
import time
import zlib

ICON_W, ICON_H = 92, 64
ICON_BYTES = ICON_W * ICON_H * 2
HEADER_SIZE = 0x7000
MAGIC = bytes([0xAA, 0xAC, 0xBD, 0xAF, 0x90, 0x88, 0x9A, 0x8D,
               0xD3, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF])
LANGS = ["en", "es", "de", "fr", "pt", "zh", "un1", "un2"]
MAX_CODE = 16 * 1024 * 1024


# ---------------------------------------------------------------- PNG input
def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png_rgb(path):
    """Minimal PNG decoder: 8-bit gray/RGB/palette/gray+A/RGBA, no interlace.
    Returns (w, h, [(r,g,b), ...]); alpha is composited onto white."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
    if depth != 8 or interlace:
        raise ValueError(f"{path}: only 8-bit non-interlaced PNGs are supported "
                         f"(depth={depth}, interlace={interlace})")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * chans
    prev = bytearray(stride)
    out = []
    p = 0
    for _y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - chans] if i >= chans else 0
            b = prev[i]
            c = prev[i - chans] if i >= chans else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 0xFF
        prev = line
        for x in range(w):
            px = line[x * chans:(x + 1) * chans]
            if ctype == 0:
                r = g = bb = px[0]; al = 255
            elif ctype == 2:
                r, g, bb = px; al = 255
            elif ctype == 3:
                idx = px[0]
                r, g, bb = plte[3 * idx:3 * idx + 3]
                al = trns[idx] if trns and idx < len(trns) else 255
            elif ctype == 4:
                r = g = bb = px[0]; al = px[1]
            else:
                r, g, bb, al = px
            if al != 255:
                r = (r * al + 255 * (255 - al)) // 255
                g = (g * al + 255 * (255 - al)) // 255
                bb = (bb * al + 255 * (255 - al)) // 255
            out.append((r, g, bb))
    return w, h, out


def rgb_to_565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_icon(path):
    if path.lower().endswith(".bin"):
        data = open(path, "rb").read()
        if len(data) != ICON_BYTES:
            raise ValueError(f"{path}: raw icon must be {ICON_BYTES} bytes")
        return data
    w, h, px = read_png_rgb(path)
    if (w, h) != (ICON_W, ICON_H):
        raise ValueError(f"{path}: icon must be {ICON_W}x{ICON_H}, got {w}x{h}")
    return b"".join(struct.pack(">H", rgb_to_565(*c)) for c in px)


def default_icon(selected):
    """Plain generated icon: vertical gradient with a 2 px frame."""
    out = bytearray()
    for y in range(ICON_H):
        for x in range(ICON_W):
            if x < 2 or y < 2 or x >= ICON_W - 2 or y >= ICON_H - 2:
                c = rgb_to_565(255, 160, 0) if selected else rgb_to_565(40, 40, 40)
            else:
                t = y * 255 // (ICON_H - 1)
                c = rgb_to_565(40 + t // 3, 80 + t // 2, 200) if not selected else \
                    rgb_to_565(255, 200 - t // 3, 60)
            out += struct.pack(">H", c)
    return bytes(out)


# ---------------------------------------------------------------- builder
def put(buf, off, data, maxlen=None):
    if isinstance(data, str):
        data = data.encode("ascii", "replace")
    if maxlen is not None:
        data = data[:maxlen]
    buf[off:off + len(data)] = data


def build_g3a(code, names, icon_uns, icon_sel, version, timestamp, filename):
    if len(code) > MAX_CODE:
        raise ValueError("code larger than 16 MB")
    basic = names.get("basic") or os.path.splitext(filename)[0]
    internal = names.get("internal") or basic
    size = HEADER_SIZE + len(code) + 4

    h = bytearray(HEADER_SIZE)
    put(h, 0x0000, MAGIC)
    put(h, 0x0010, struct.pack(">I", (~size) & 0xFFFFFFFF))
    h[0x000E] = (h[0x0013] - 0x41) & 0xFF
    h[0x000F] = 0xFE
    h[0x0014] = (h[0x0013] - 0xB8) & 0xFF
    h[0x0024] = 0x01
    h[0x0025] = 0x01
    put(h, 0x002E, struct.pack(">I", len(code)))
    put(h, 0x0040, basic, 0x1C - 1)
    put(h, 0x005C, struct.pack(">I", size))
    iname = ("@" + internal.upper())[:0xB - 1]
    put(h, 0x0060, iname)
    for i, lc in enumerate(LANGS):
        put(h, 0x006B + 0x18 * i, names.get(lc) or basic, 0x18 - 1)
    put(h, 0x0130, version, 0xC - 1)
    put(h, 0x013C, timestamp, 14)
    put(h, 0x0EBC, filename, 0x144 - 1)
    put(h, 0x1000, icon_uns)
    put(h, 0x4000, icon_sel)

    cksum = (sum(h) + sum(code)) & 0xFFFFFFFF
    put(h, 0x0020, struct.pack(">I", cksum))
    return bytes(h) + bytes(code) + struct.pack(">I", cksum)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Build a Prizm/fx-CG .g3a add-in")
    ap.add_argument("-n", dest="names", action="append", default=[], metavar="lc:name")
    ap.add_argument("-i", dest="icons", action="append", default=[], metavar="uns|sel:file")
    ap.add_argument("-V", dest="version", default="01.00.0000")
    ap.add_argument("--timestamp", default=None)
    ap.add_argument("input")
    ap.add_argument("output")
    a = ap.parse_args(argv)

    names = {}
    for n in a.names:
        lc, sep, val = n.partition(":")
        if not sep:
            lc, val = "basic", n
        lc = lc or "basic"
        if lc not in ["basic", "internal"] + LANGS[:6]:
            ap.error(f"unknown name language '{lc}'")
        names[lc] = val
    icon_uns = icon_sel = None
    for spec in a.icons:
        kind, sep, path = spec.partition(":")
        if kind not in ("uns", "sel") or not sep:
            ap.error(f"bad icon spec '{spec}' (use uns:FILE or sel:FILE)")
        data = load_icon(path)
        if kind == "uns":
            icon_uns = data
        else:
            icon_sel = data
    if icon_uns is None:
        icon_uns = default_icon(False)
    if icon_sel is None:
        icon_sel = default_icon(True)

    if a.timestamp:
        ts = a.timestamp
    else:
        epoch = os.environ.get("SOURCE_DATE_EPOCH")
        t = time.gmtime(int(epoch)) if epoch else time.localtime()
        ts = time.strftime("%Y.%m%d.%H%M", t)

    code = open(a.input, "rb").read()
    out = build_g3a(code, names, icon_uns, icon_sel, a.version, ts,
                    os.path.basename(a.output))
    with open(a.output, "wb") as f:
        f.write(out)
    print(f"{a.output}: {len(out)} bytes, code {len(code)} bytes, "
          f"checksum 0x{struct.unpack('>I', out[-4:])[0]:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
