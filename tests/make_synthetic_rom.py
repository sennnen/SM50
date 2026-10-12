#!/usr/bin/env python3
"""Build a synthetic N64 ROM fixture (abstract test geometry) plus a manifest,
exercising the generic ROM pipeline: byte order, header CRC, MIO0, multiple
segments with cross-segment references, F3D display lists, several texture
formats, layered (translucent) geometry and collision extraction.

Writes tests/fixtures/synthetic.{z64,v64,n64} and synthetic.json."""
import json, math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "content"))
from n64 import rom as n64rom, mio0  # noqa: E402
import gbi  # noqa: E402

OUT = os.path.join(HERE, "fixtures")
os.makedirs(OUT, exist_ok=True)

# --- segment 9: raw texture bank (checker, RGBA16) -------------------------
seg9 = bytearray()
for y in range(32):
    for x in range(32):
        c = (200, 200, 210) if ((x // 8) + (y // 8)) % 2 else (90, 90, 120)
        seg9 += struct.pack(">H", (c[0] >> 3) << 11 | (c[1] >> 3) << 6 | (c[2] >> 3) << 1 | 1)


class SegTex:  # texture living in another segment
    def __init__(self, addr, w, h, fmt, siz):
        self.addr, self.w, self.h, self.fmt, self.siz, self.pal_addr, self.palette = addr, w, h, fmt, siz, 0, []


checker = SegTex(0x09000000, 32, 32, gbi.G_IM_FMT_RGBA, gbi.G_IM_SIZ_16b)

# --- segment 7: geometry + local textures (MIO0 compressed in the ROM) -----
pk = gbi.Pack()
stripes = gbi.Texture(pk, 32, 32, [((255, 140, 40, 255) if (x + y) % 16 < 8 else (60, 60, 60, 255))
                                   for y in range(32) for x in range(32)], fmt="ci4")
ramp = gbi.Texture(pk, 16, 16, [(x * 16, y * 16, 128, 255) for y in range(16) for x in range(16)], fmt="ia8")
water = gbi.Texture(pk, 16, 16, [(40, 120, 220, 255)] * 256, fmt="rgba16")


def quad(p, uv, c):
    v = [(int(p[i][0]), int(p[i][1]), int(p[i][2]), uv[i][0], uv[i][1], c) for i in range(4)]
    return [(v[0], v[1], v[2]), (v[0], v[2], v[3])]


def mesh_dl(tex, tris, xlu=False):
    dl = gbi.DL()
    dl.pipesync()
    dl.geom_clear(gbi.G_LIGHTING)
    dl.geom_set(gbi.G_SHADE | gbi.G_CULL_BACK | gbi.G_ZBUFFER)
    if xlu:
        dl.rendermode(gbi.RM_XLU)
    dl.texture(True)
    dl.cc_modulate()
    dl.load_texture(tex)
    gbi.emit_mesh(pk, dl, tris)
    dl.texture(False)
    dl.end()
    return pk.add(dl.bytes())


def h(x, z):
    return 120 * math.sin(x / 600.0) * math.cos(z / 700.0)


ground = []
N, S = 16, 300
for j in range(N):
    for i in range(N):
        x0, z0 = -N * S / 2 + i * S, -N * S / 2 + j * S
        p = [(x0, h(x0, z0), z0), (x0, h(x0, z0 + S), z0 + S), (x0 + S, h(x0 + S, z0 + S), z0 + S), (x0 + S, h(x0 + S, z0), z0)]
        shade = 200 + int(40 * math.sin(i + j))
        ground += quad(p, [(0, 0), (0, 1024), (1024, 1024), (1024, 0)], (shade, shade, shade, 255))
ground_dl = mesh_dl(checker, ground)

pillars = []
for k in range(6):
    a = k / 6 * 2 * math.pi
    cx, cz, w, ht = 1500 * math.cos(a), 1500 * math.sin(a), 120, 300 + 150 * k
    y0 = h(cx, cz) - 20
    x0, x1, z0, z1 = cx - w, cx + w, cz - w, cz + w
    faces = [((x0, y0, z1), (x1, y0, z1), (x1, y0 + ht, z1), (x0, y0 + ht, z1)),
             ((x1, y0, z0), (x0, y0, z0), (x0, y0 + ht, z0), (x1, y0 + ht, z0)),
             ((x1, y0, z1), (x1, y0, z0), (x1, y0 + ht, z0), (x1, y0 + ht, z1)),
             ((x0, y0, z0), (x0, y0, z1), (x0, y0 + ht, z1), (x0, y0 + ht, z0)),
             ((x0, y0 + ht, z1), (x1, y0 + ht, z1), (x1, y0 + ht, z0), (x0, y0 + ht, z0))]
    for f in faces:
        pillars += quad(f, [(0, 1024), (1024, 1024), (1024, 0), (0, 0)], (255, 255, 255, 255))
pillar_dl = mesh_dl(stripes, pillars)
ramp_tris = quad([(-300, 0, 600), (300, 0, 600), (300, 250, 100), (-300, 250, 100)], [(0, 512), (512, 512), (512, 0), (0, 0)],
                 (255, 255, 255, 255))
ramp_dl = mesh_dl(ramp, ramp_tris)
pool_dl = mesh_dl(water, quad([(-900, 60, -400), (-300, 60, -400), (-300, 60, -1000), (-900, 60, -1000)],
                              [(0, 0), (512, 0), (512, 512), (0, 512)], (255, 255, 255, 255)), xlu=True)
# a parent list calling the others through G_DL (exercises nesting)
root = gbi.DL()
root.dl(ground_dl)
root.dl(ramp_dl)
root.end()
root_dl = pk.add(root.bytes())
seg7 = bytes(pk.data)

# --- ROM image ----------------------------------------------------------------
ROM_SIZE = 8 << 20
rom = bytearray(ROM_SIZE)
rom[0:4] = n64rom.Z64_MAGIC
rom[4:8] = struct.pack(">I", 0x0000000F)
rom[8:12] = struct.pack(">I", 0x80246000)
rom[0x20:0x34] = b"SM50 SYNTHETIC TEST "
rom[0x3B:0x40] = b"NSTE\0"
for i in range(0x1000, 0x101000, 4):        # deterministic pseudo-code area
    rom[i:i + 4] = struct.pack(">I", (i * 2654435761) & 0xFFFFFFFF)
SEG7_AT, SEG9_AT = 0x200000, 0x300000
c7 = mio0.compress(seg7)
rom[SEG7_AT:SEG7_AT + len(c7)] = c7
rom[SEG9_AT:SEG9_AT + len(seg9)] = seg9
z64 = n64rom.fix_crc(bytes(rom))
for order in ("z64", "v64", "n64"):
    open(os.path.join(OUT, "synthetic." + order), "wb").write(n64rom.from_z64(z64, order))

manifest = {
    "rom": "synthetic.z64",
    "name": "SYNTHETIC TEST",
    "segments": [{"seg": 7, "rom": [hex(SEG7_AT), hex(SEG7_AT + len(c7))], "mio0": True},
                 {"seg": 9, "rom": [hex(SEG9_AT), hex(SEG9_AT + len(seg9))]}],
    "chunks": [{"dl": hex(root_dl), "layer": 0},
               {"dl": hex(pillar_dl), "layer": 0},
               {"dl": hex(pool_dl), "layer": 2, "collision": False}],
    "spawn": [0, 100, 1200, 0x8000],
}
json.dump(manifest, open(os.path.join(OUT, "synthetic.json"), "w"), indent=2)
print("seg7 %d bytes (MIO0 %d), seg9 %d bytes, sha1 %s" % (len(seg7), len(c7), len(seg9), n64rom.header(z64)["sha1"]))
