#!/usr/bin/env python3
"""Build an SM50 engine pack from N64 ROM data described by a JSON manifest.

The manifest names ROM ranges to load into segments (raw or MIO0), the
display lists to draw, and how to derive collision. Everything is generic
N64 data handling; no title-specific knowledge lives here.

  {
    "rom": "game.z64",                       # optional, any byte order
    "sha1": "...",                           # optional expected SHA-1
    "segments": [ {"seg": 7, "rom": [start, end], "mio0": true},
                  {"seg": 9, "file": "blob.bin"} ],
    "chunks":   [ {"dl": "0x07001000", "layer": 0, "translate": [0,0,0],
                   "collision": true} ],
    "spawn": [x, y, z, yaw], "name": "LEVEL", "sky": ["#5D9FFF", "#B6FFFF"]
  }
Layers: 0 opaque, 1 alpha-tested, 2 translucent.
"""
import argparse, json, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n64 import rom as n64rom, mio0, f3d  # noqa: E402
from n64.segments import Segments  # noqa: E402


def num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def rgb565(hexstr):
    h = hexstr.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


class PackWriter:
    def __init__(self):
        self.entries = []

    def add(self, tag, data):
        assert len(tag) == 4
        self.entries.append((tag, bytes(data)))

    def bytes(self):
        hdr = (16 + 12 * len(self.entries) + 15) & ~15
        out = bytearray(b"S50P" + struct.pack(">III", 1, len(self.entries), hdr))
        body = bytearray()
        offs = []
        for tag, d in self.entries:
            while len(body) % 16:
                body.append(0)
            offs.append(len(body))
            body += d
        for (tag, d), o in zip(self.entries, offs):
            out += tag.encode() + struct.pack(">II", o, len(d))
        while len(out) < hdr:
            out.append(0)
        return bytes(out + body)


def build(manifest, base_dir, log=print):
    m = manifest
    rom = None
    if m.get("rom"):
        path = m["rom"] if os.path.isabs(m["rom"]) else os.path.join(base_dir, m["rom"])
        rom, hdr = n64rom.load(path, m.get("sha1"))
        log("ROM: '%s' %s %s, %d bytes, order %s, header CRC %s" % (hdr["name"], hdr["game_id"], hdr["region"],
            hdr["size"], hdr["order"], "ok" if hdr["crc_ok"] else "MISMATCH"))
    segs = Segments()
    pw = PackWriter()
    for s in m.get("segments", []):
        n = num(s["seg"])
        if "file" in s:
            data = open(os.path.join(base_dir, s["file"]), "rb").read()
        else:
            if rom is None:
                raise SystemExit("segment %02X needs a ROM" % n)
            a, b = num(s["rom"][0]), num(s["rom"][1])
            if not 0 <= a < b <= len(rom):
                raise SystemExit("segment %02X: ROM range %X-%X outside the ROM" % (n, a, b))
            data = rom[a:b]
            if s.get("mio0"):
                data = mio0.decompress(data)
        segs.set(n, data)
        pw.add("SG%02X" % n, data)
        log("segment %02X: %d bytes" % (n, len(data)))
    chunks, col = [], []
    total_tris = 0
    for c in m.get("chunks", []):
        dl = num(c["dl"])
        info = f3d.walk(segs, dl)
        if info.errors:
            log("warning: DL %08X: %s" % (dl, "; ".join(sorted(set(info.errors))[:3])))
        tx, ty, tz = (num(v) for v in c.get("translate", [0, 0, 0]))
        b = info.bounds()
        if b is None:
            log("warning: DL %08X draws no triangles" % dl)
            continue
        (cx, cy, cz), r = b
        total_tris += len(info.tris)
        layer = num(c.get("layer", 0))
        chunks.append(struct.pack(">IhhhhBBhhhH", dl, int(cx + tx), int(cy + ty), int(cz + tz),
                                  min(32767, int(r) + 1), layer, 0, tx, ty, tz, 0))
        if c.get("collision", layer != 2):
            for t in info.tris:
                col.append(tuple((p[0] + tx, p[1] + ty, p[2] + tz) for p in t))
    pw.add("LVL2", struct.pack(">I", len(chunks)) + b"".join(chunks))
    colb = bytearray(struct.pack(">I", 0))
    nc = 0
    for a, b_, c_ in col:
        ux, uy, uz = b_[0] - a[0], b_[1] - a[1], b_[2] - a[2]
        vx, vy, vz = c_[0] - a[0], c_[1] - a[1], c_[2] - a[2]
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        ln = math.sqrt(sum(x * x for x in n))
        if ln < 1e-6:
            continue
        vals = [int(v) for p in (a, b_, c_) for v in p] + [int(round(x / ln * 16384)) for x in n]
        if any(abs(v) > 32767 for v in vals):
            continue
        colb += struct.pack(">9h3hHH", *vals, 0, 0)
        nc += 1
    struct.pack_into(">I", colb, 0, nc)
    pw.add("COLV", colb)
    sp = [num(v) for v in m.get("spawn", [0, 0, 0, 0])]
    sky = m.get("sky", ["#5D9FFF", "#B6FFFF"])
    name = m.get("name", "N64 SCENE").upper().encode()[:16]
    pw.add("INFO", struct.pack(">hhhHhhHHHH", sp[0], sp[1], sp[2], sp[3] & 0xFFFF, -32768, -32768,
                               rgb565(sky[0]), rgb565(sky[1]), 0, 0) + name.ljust(16, b"\0"))
    log("chunks: %d, triangles: %d, collision triangles: %d" % (len(chunks), total_tris, nc))
    return pw.bytes()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--rom", help="override the manifest's ROM path")
    a = ap.parse_args()
    m = json.load(open(a.manifest))
    if a.rom:
        m["rom"] = os.path.abspath(a.rom)
    data = build(m, os.path.dirname(os.path.abspath(a.manifest)))
    open(a.output, "wb").write(data)
    print("wrote %s (%d bytes)" % (a.output, len(data)))


if __name__ == "__main__":
    main()
