#!/usr/bin/env python3
"""Build SM50's original content pack (character, animations, level) as
native N64-format Fast3D display lists + collision + object tables.

All content here is original: a small robot character ("Volt") and an
island test level ("Meadow Isle"). Output: content/sm50.pack (big-endian).
"""
import math, os, random, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbi import *  # noqa

random.seed(1234)
pack = Pack()

# ---------------------------------------------------------------- textures
def noise_tex(w, h, base, var, seed, fn=None):
    rnd = random.Random(seed)
    px = []
    for y in range(h):
        for x in range(w):
            n = rnd.random()
            c = [max(0, min(255, int(base[i] + (n - 0.5) * var[i]))) for i in range(3)]
            if fn:
                c = fn(x, y, c, n)
            px.append((c[0], c[1], c[2], 255))
    return px


def grass_fn(x, y, c, n):
    if (x * 7 + y * 13) % 11 == 0:
        return [c[0] - 20, c[1] + 25, c[2] - 10]
    return c


def brick_fn(x, y, c, n):
    row = y // 8
    off = 8 if row % 2 else 0
    if y % 8 == 7 or (x + off) % 16 == 15:
        return [90, 86, 80]
    return c


def plank_fn(x, y, c, n):
    if y % 8 == 0:
        return [70, 45, 25]
    if (x + (y // 8) * 11) % 32 == 0:
        return [80, 50, 30]
    return c


T = {}
T['grass'] = Texture(pack, 32, 32, noise_tex(32, 32, (70, 150, 60), (40, 50, 30), 1, grass_fn))
T['sand'] = Texture(pack, 32, 32, noise_tex(32, 32, (215, 195, 140), (30, 30, 30), 2))
T['dirt'] = Texture(pack, 32, 32, noise_tex(32, 32, (140, 100, 65), (40, 30, 25), 3))
T['brick'] = Texture(pack, 32, 32, noise_tex(32, 32, (170, 165, 155), (30, 30, 30), 4, brick_fn))
T['plank'] = Texture(pack, 32, 32, noise_tex(32, 32, (165, 115, 65), (30, 20, 15), 5, plank_fn))
T['water'] = Texture(pack, 32, 32, noise_tex(32, 32, (50, 110, 200), (30, 40, 50), 6), fmt="rgba16")
T['metal'] = Texture(pack, 16, 16, noise_tex(16, 16, (120, 130, 150), (20, 20, 20), 7), fmt="i4")

# tree sprite 32x64 with alpha (CI4)
def tree_pixels():
    px = []
    for y in range(64):
        for x in range(32):
            dx = x - 15.5
            a = 0
            c = (0, 0, 0)
            if y >= 44 and abs(dx) < 3.5:
                c = (110, 70, 40); a = 255
            else:
                # three stacked blobs of foliage
                for cy, r in ((14, 12), (24, 14), (34, 15)):
                    if dx * dx + (y - cy) ** 2 < r * r:
                        shade = 1.0 - (y / 64.0) * 0.4 + (0.15 if dx < 0 else 0)
                        c = (int(40 * shade), int(140 * shade), int(50 * shade)); a = 255
                        break
            px.append((c[0], c[1], c[2], a))
    return px


T['tree'] = Texture(pack, 32, 64, tree_pixels(), fmt="ci4")

def coin_pixels():
    px = []
    for y in range(32):
        for x in range(32):
            d = math.hypot(x - 15.5, y - 15.5)
            if d < 14:
                edge = d > 11
                inner = abs(x - 15.5) < 2.5 and abs(y - 15.5) < 8
                if inner:
                    c = (255, 250, 170)
                elif edge:
                    c = (200, 140, 20)
                else:
                    c = (250, 200, 40)
                px.append((c[0], c[1], c[2], 255))
            else:
                px.append((0, 0, 0, 0))
    return px


T['coin'] = Texture(pack, 32, 32, coin_pixels(), fmt="rgba16")

# ---------------------------------------------------------------- helpers
SUN = (0.45, 0.8, 0.35)
SL = math.sqrt(sum(c * c for c in SUN))
SUN = tuple(c / SL for c in SUN)


def bake(n, tint, amb=0.55, dif=0.6):
    d = max(0.0, n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2])
    k = amb + dif * d
    return tuple(max(0, min(255, int(t * k))) for t in tint) + (255,)


def normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    l = math.sqrt(sum(x * x for x in n)) or 1.0
    return tuple(x / l for x in n)


collision = []  # (v0, v1, v2, type)


def add_col(a, b, c, typ=0):
    collision.append((a, b, c, typ))


def box_faces(x0, y0, z0, x1, y1, z1):
    """Returns list of quads (4 corner points, CCW seen from outside)."""
    return [
        ((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)),  # top +y
        ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)),  # bottom -y
        ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)),  # +z
        ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)),  # -z
        ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)),  # +x
        ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)),  # -x
    ]


def quad_uv(q, scale):
    """Planar UVs in Q5 texels for a world-space quad, choosing the dominant axis."""
    n = normal(q[0], q[1], q[2])
    ax = max(range(3), key=lambda i: abs(n[i]))
    out = []
    for p in q:
        if ax == 0:
            s, t = p[2], -p[1]
        elif ax == 1:
            s, t = p[0], p[2]
        else:
            s, t = p[0], -p[1]
        out.append((int(s * 32 * 32 / scale), int(t * 32 * 32 / scale)))
    return out, n


class Chunk:
    def __init__(self, layer=0):
        self.mats = {}  # material key -> tris
        self.layer = layer
        self.points = []

    def add_tri(self, mat, verts):
        self.mats.setdefault(mat, []).append(verts)
        self.points += [v[:3] for v in verts]

    def add_quad(self, mat, q, tint, scale=128, col=True, typ=0):
        uv, n = quad_uv(q, scale)
        c = bake(n, tint)
        v = [(int(p[0]), int(p[1]), int(p[2]), uv[i][0], uv[i][1], c) for i, p in enumerate(q)]
        self.add_tri(mat, (v[0], v[1], v[2]))
        self.add_tri(mat, (v[0], v[2], v[3]))
        if col:
            add_col(q[0], q[1], q[2], typ)
            add_col(q[0], q[2], q[3], typ)

    def add_box(self, mat, x0, y0, z0, x1, y1, z1, tint, scale=128, top_mat=None, col=True):
        for i, q in enumerate(box_faces(x0, y0, z0, x1, y1, z1)):
            self.add_quad(top_mat if (i == 0 and top_mat) else mat, q, tint, scale, col)

    def build(self):
        dl = DL()
        dl.pipesync()
        dl.geom_clear(G_LIGHTING | G_TEXTURE_GEN | G_CULL_FRONT)
        dl.geom_set(G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_ZBUFFER)
        if self.layer == 2:
            dl.rendermode(RM_XLU)
        else:
            dl.rendermode(RM_OPA)
        for mat, tris in self.mats.items():
            dl.pipesync()
            if mat is None:
                dl.texture(False)
                dl.cc_shade()
            else:
                dl.texture(True)
                dl.cc_modulate()
                dl.load_texture(T[mat])
            emit_mesh(pack, dl, tris)
        dl.texture(False)
        if self.layer == 2:
            dl.rendermode(RM_OPA)
        dl.end()
        addr = pack.add(dl.bytes())
        xs = [p[0] for p in self.points]; ys = [p[1] for p in self.points]; zs = [p[2] for p in self.points]
        cx, cy, cz = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2
        r = max(math.sqrt((p[0] - cx) ** 2 + (p[1] - cy) ** 2 + (p[2] - cz) ** 2) for p in self.points)
        return struct.pack(">IhhhhB3x", addr, int(cx), int(cy), int(cz), int(min(32767, r + 1)), self.layer)


# ---------------------------------------------------------------- level
WATER_Y = -120
N = 32
SP = 250
HALF = N * SP // 2


def height(x, z):
    d = math.hypot(x, z) / 3600.0
    island = 1.0 - d * d
    h = 180 * island - 150
    h += 90 * math.sin(x / 700.0) * math.cos(z / 900.0)
    # big hill in the north-west
    h += 520 * math.exp(-(((x + 1800) / 900.0) ** 2 + ((z + 1500) / 900.0) ** 2))
    # flatten spawn area
    f = math.exp(-((x / 900.0) ** 2 + ((z - 1200) / 900.0) ** 2))
    h = h * (1 - f) + 40 * f
    return h


chunks = []
heights = [[height(-HALF + i * SP, -HALF + j * SP) for i in range(N + 1)] for j in range(N + 1)]
CH = 4  # chunks per side
for cj in range(CH):
    for ci in range(CH):
        ch = Chunk(0)
        step = N // CH
        for j in range(cj * step, (cj + 1) * step):
            for i in range(ci * step, (ci + 1) * step):
                x0, z0 = -HALF + i * SP, -HALF + j * SP
                p = [(x0, heights[j][i], z0), (x0, heights[j + 1][i], z0 + SP),
                     (x0 + SP, heights[j + 1][i + 1], z0 + SP), (x0 + SP, heights[j][i + 1], z0)]
                if max(q[1] for q in p) < WATER_Y - 60:
                    continue  # deep under water: invisible
                avg = sum(q[1] for q in p) / 4
                mat = 'sand' if avg < WATER_Y + 60 else 'grass'
                tint = (255, 255, 255) if mat == 'grass' else (255, 245, 230)
                ch.add_quad(mat, p, tint, scale=256)
        if ch.points:
            chunks.append(ch)

# water (translucent, no collision: falling in respawns the player)
wch = Chunk(2)
W = 6000
for j in range(-2, 2):
    for i in range(-2, 2):
        x0, z0, x1, z1 = i * W / 2, j * W / 2, (i + 1) * W / 2, (j + 1) * W / 2
        wch.add_quad('water', ((x0, WATER_Y, z1), (x1, WATER_Y, z1), (x1, WATER_Y, z0), (x0, WATER_Y, z0)),
                     (255, 255, 255), scale=300, col=False)

# tower with a spiral staircase around it
TX, TZ = 1700, -1500
tb = height(TX, TZ) - 40
tower = Chunk(0)
tower.add_box('brick', TX - 250, tb, TZ - 250, TX + 250, tb + 1300, TZ + 250, (255, 255, 255), scale=200)
steps = 20
for k in range(steps):
    ang = k / steps * 2 * math.pi * 1.5
    r = 380
    cx, cz = TX + r * math.cos(ang), TZ + r * math.sin(ang)
    y = tb + 70 + k * 63
    tower.add_box('plank', cx - 90, y - 30, cz - 90, cx + 90, y, cz + 90, (255, 255, 255), scale=128)
# parapet on top
top = tb + 1300
for dx0, dz0, dx1, dz1 in ((-250, -250, 250, -210), (-250, 210, 250, 250), (-250, -250, -210, 250), (210, -250, 250, 250)):
    tower.add_box('brick', TX + dx0, top, TZ + dz0, TX + dx1, top + 60, TZ + dz1, (230, 230, 230), scale=200)
chunks.append(tower)

# floating platforms leading from the hill top towards the tower
plat = Chunk(0)
hx, hz = -1800, -1500
hy = height(hx, hz)
PLATS = []
for k in range(6):
    t = (k + 1) / 7.0
    px = hx + (TX - 600 - hx) * t
    pz = hz + (TZ - hz) * t + 300 * math.sin(t * math.pi)
    py = hy + 40 + 60 * k
    PLATS.append((px, py, pz))
    plat.add_box('metal', px - 110, py - 40, pz - 110, px + 110, py, pz + 110, (200, 210, 230), scale=96, top_mat='plank')
chunks.append(plat)

# a ramp + plateau near spawn and a bridge
ramp = Chunk(0)
rx, rz = -900, 800
rb = height(rx, rz) - 20
ramp.add_box('brick', rx - 300, rb, rz - 700, rx + 300, rb + 300, rz - 200, (255, 255, 255), scale=200, top_mat='dirt')
q = ((rx - 200, rb + 300, rz - 200), (rx + 200, rb + 300, rz - 200), (rx + 200, rb + 5, rz + 500), (rx - 200, rb + 5, rz + 500))
ramp.add_quad('dirt', q[::-1], (255, 255, 255), scale=200)
ramp.add_quad('brick', ((rx - 200, rb, rz + 500), (rx - 200, rb + 5, rz + 500), (rx - 200, rb + 300, rz - 200), (rx - 200, rb, rz - 200)), (255, 255, 255), scale=200)
ramp.add_quad('brick', ((rx + 200, rb, rz - 200), (rx + 200, rb + 300, rz - 200), (rx + 200, rb + 5, rz + 500), (rx + 200, rb, rz + 500)), (255, 255, 255), scale=200)
# blocks to climb
for k, (bx, bz, bh) in enumerate(((600, 600, 120), (850, 450, 240), (1100, 300, 360))):
    b0 = height(bx, bz) - 20
    ramp.add_box('brick', bx - 100, b0, bz - 100, bx + 100, b0 + bh, bz + 100, (240, 220, 200), scale=128, top_mat='plank')
chunks.append(ramp)

# ---------------------------------------------------------------- objects
OBJ_COIN, OBJ_STAR, OBJ_TREE, OBJ_SPAWN, OBJ_BLOB, OBJ_SIGN = 1, 2, 3, 4, 5, 6
objects = []
SPAWN = (0, height(0, 1200) + 10, 1200, 0x8000)
objects.append((OBJ_SPAWN, *SPAWN[:3], SPAWN[3], 0))
objects.append((OBJ_STAR, TX, top + 120, TZ, 0, 0))
# star 2: above the last floating platform
objects.append((OBJ_STAR, int(PLATS[-1][0]), int(PLATS[-1][1] + 130), int(PLATS[-1][2]), 0, 1))
# coins: ring around spawn, line up the ramp, on steps, on platforms
for k in range(8):
    a = k / 8 * 2 * math.pi
    x, z = 400 * math.cos(a), 1200 + 400 * math.sin(a)
    objects.append((OBJ_COIN, int(x), int(height(x, z) + 60), int(z), 0, 0))
for k in range(5):
    t = k / 4
    x, z = rx, rz + 450 - t * 600
    y = rb + 5 + (300 - 5) * min(1, max(0, (rz + 500 - z) / 700)) + 60
    objects.append((OBJ_COIN, int(x), int(y), int(z), 0, 0))
for k in range(0, steps, 3):
    ang = k / steps * 2 * math.pi * 1.5
    objects.append((OBJ_COIN, int(TX + 380 * math.cos(ang)), int(tb + 70 + k * 63 + 60), int(TZ + 380 * math.sin(ang)), 0, 0))
for (px, py, pz) in PLATS:
    objects.append((OBJ_COIN, int(px), int(py + 60), int(pz), 0, 0))
# trees
for k in range(26):
    while True:
        x, z = random.uniform(-3000, 3000), random.uniform(-3000, 3000)
        h = height(x, z)
        if h > WATER_Y + 80 and math.hypot(x - TX, z - TZ) > 700 and math.hypot(x, z - 1200) > 900 and math.hypot(x, z - 2100) > 700 and \
                math.hypot(x - rx, z - rz) > 800:
            break
    objects.append((OBJ_TREE, int(x), int(h), int(z), 0, 0))
# patrolling blobs (param = patrol radius)
for (x, z) in ((800, -200), (-1200, -400), (300, -1800)):
    objects.append((OBJ_BLOB, x, int(height(x, z)), z, 0, 300))

# ---------------------------------------------------------------- models
def lit_material(dl, color, amb=0.45):
    la = pack.add(light_bytes(int(color[0] * (1 - amb)), int(color[1] * (1 - amb)), int(color[2] * (1 - amb)), 0x28, 0x28, 0x28))
    aa = pack.add(ambient_bytes(int(color[0] * amb), int(color[1] * amb), int(color[2] * amb)))
    dl.lights1(la, aa)


def mesh_box(cx, cy, cz, sx, sy, sz):
    """Triangles of a box with per-face normals encoded as signed bytes."""
    x0, x1, y0, y1, z0, z1 = cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, cz - sz / 2, cz + sz / 2
    tris = []
    for q in box_faces(x0, y0, z0, x1, y1, z1):
        n = normal(q[0], q[1], q[2])
        nb = tuple(int(round(c * 127)) for c in n) + (255,)
        v = [(int(p[0]), int(p[1]), int(p[2]), 0, 0, nb) for p in q]
        tris += [(v[0], v[1], v[2]), (v[0], v[2], v[3])]
    return tris


def mesh_sphere(cx, cy, cz, r, rings=6, segs=8, sy=1.0):
    tris = []
    def pt(i, j):
        th = math.pi * i / rings
        ph = 2 * math.pi * j / segs
        n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
        nb = tuple(int(round(c * 127)) for c in n) + (255,)
        return (int(cx + r * n[0]), int(cy + r * n[1] * sy), int(cz + r * n[2]), 0, 0, nb)
    for i in range(rings):
        for j in range(segs):
            a, b, c, d = pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)
            if i != 0:
                tris.append((a, d, c))
            if i != rings - 1:
                tris.append((a, c, b))
    return tris


def part_dl(pieces):
    """pieces: list of (color, tris)"""
    dl = DL()
    dl.pipesync()
    dl.texture(False)
    dl.cc_shade()
    dl.geom_set(G_LIGHTING | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK)
    for color, tris in pieces:
        lit_material(dl, color)
        emit_mesh(pack, dl, tris)
    dl.geom_clear(G_LIGHTING)
    dl.end()
    return pack.add(dl.bytes())


ORANGE, WHITE, CYAN, DARK, YEL = (240, 120, 30), (235, 235, 240), (40, 220, 240), (60, 60, 70), (255, 220, 40)
# Volt skeleton: (parent, pivot(x,y,z), pieces)
parts = [
    (-1, (0, 0, 0), [(DARK, mesh_box(0, 0, 0, 48, 22, 30))]),                                  # 0 pelvis
    (0, (0, 10, 0), [(ORANGE, mesh_box(0, 28, 0, 60, 52, 36)), (YEL, mesh_box(0, 34, 19, 22, 18, 4))]),  # 1 torso
    (1, (0, 56, 0), [(WHITE, mesh_sphere(0, 30, 0, 32, 6, 8)), (CYAN, mesh_box(0, 33, 24, 40, 14, 14)),
                     (DARK, mesh_box(0, 68, 0, 4, 18, 4)), (YEL, mesh_sphere(0, 80, 0, 7, 3, 6))]),     # 2 head
    (1, (38, 48, 0), [(WHITE, mesh_box(0, -18, 0, 16, 38, 16))]),                               # 3 L arm
    (3, (0, -38, 0), [(ORANGE, mesh_box(0, -14, 0, 14, 28, 14)), (WHITE, mesh_sphere(0, -32, 0, 10, 3, 6))]),  # 4 L hand
    (1, (-38, 48, 0), [(WHITE, mesh_box(0, -18, 0, 16, 38, 16))]),                              # 5 R arm
    (5, (0, -38, 0), [(ORANGE, mesh_box(0, -14, 0, 14, 28, 14)), (WHITE, mesh_sphere(0, -32, 0, 10, 3, 6))]),  # 6 R hand
    (0, (15, -8, 0), [(WHITE, mesh_box(0, -17, 0, 20, 34, 20))]),                               # 7 L thigh
    (7, (0, -34, 0), [(ORANGE, mesh_box(0, -14, 0, 18, 28, 18)), (DARK, mesh_box(0, -32, 6, 22, 10, 34))]),  # 8 L shin
    (0, (-15, -8, 0), [(WHITE, mesh_box(0, -17, 0, 20, 34, 20))]),                              # 9 R thigh
    (9, (0, -34, 0), [(ORANGE, mesh_box(0, -14, 0, 18, 28, 18)), (DARK, mesh_box(0, -32, 6, 22, 10, 34))]),  # 10 R shin
]
PELVIS_Y = 78
skel = struct.pack(">I", len(parts))
for parent, piv, pieces in parts:
    skel += struct.pack(">hhhhI", parent, *piv, part_dl(pieces))

# animations: dense keyframes. frame = rooty(s16) + nparts * (rx,ry,rz) u16
DEG = 65536 / 360.0
NP = len(parts)


def pose(frame_fn, nframes, loop=True):
    data = struct.pack(">HHHH", nframes, NP, 1 if loop else 0, 0)
    for f in range(nframes):
        t = f / nframes
        rooty, rots = frame_fn(t)
        data += struct.pack(">h", int(rooty))
        for p in range(NP):
            r = rots.get(p, (0, 0, 0))
            data += struct.pack(">HHH", *[int(round(a * DEG)) & 0xFFFF for a in r])
    return data


S = math.sin
TAU = 2 * math.pi


def anim_idle(t):
    b = S(t * TAU)
    return PELVIS_Y + 2 * b, {1: (2 * b, 0, 0), 2: (-3 * b, 8 * S(t * TAU * 0.5), 0), 3: (0, 0, 8 + 3 * b), 5: (0, 0, -8 - 3 * b),
                              4: (-10, 0, 0), 6: (-10, 0, 0)}


def anim_walk(t):
    a = S(t * TAU)
    return PELVIS_Y + 4 * abs(S(t * TAU)), {
        1: (6, -6 * a, 0), 2: (-4, 6 * a, 0),
        7: (-32 * a, 0, 0), 8: (max(0, 40 * S(t * TAU + 1.6)), 0, 0),
        9: (32 * a, 0, 0), 10: (max(0, 40 * S(t * TAU + 1.6 + math.pi)), 0, 0),
        3: (30 * a, 0, 6), 4: (-20, 0, 0), 5: (-30 * a, 0, -6), 6: (-20, 0, 0)}


def anim_run(t):
    a = S(t * TAU)
    return PELVIS_Y - 4 + 8 * abs(S(t * TAU)), {
        1: (18, -10 * a, 0), 2: (-14, 8 * a, 0),
        7: (-55 * a, 0, 0), 8: (max(5, 75 * S(t * TAU + 1.4)), 0, 0),
        9: (55 * a, 0, 0), 10: (max(5, 75 * S(t * TAU + 1.4 + math.pi)), 0, 0),
        3: (55 * a, 0, 10), 4: (-60, 0, 0), 5: (-55 * a, 0, -10), 6: (-60, 0, 0)}


def anim_jump(t):
    return PELVIS_Y, {1: (-8, 0, 0), 7: (-50, 0, 0), 8: (70, 0, 0), 9: (-10, 0, 0), 10: (30, 0, 0),
                      3: (-150, 0, 20), 4: (-10, 0, 0), 5: (-150, 0, -20), 6: (-10, 0, 0)}


def anim_fall(t):
    a = S(t * TAU)
    return PELVIS_Y, {1: (5, 0, 0), 7: (-25 + 8 * a, 0, 0), 8: (40, 0, 0), 9: (-5 - 8 * a, 0, 0), 10: (25, 0, 0),
                      3: (-30, 0, 70 + 10 * a), 5: (-30, 0, -70 - 10 * a), 4: (-20, 0, 0), 6: (-20, 0, 0)}


def anim_land(t):
    k = S(t * math.pi)
    return PELVIS_Y - 22 * k, {1: (20 * k, 0, 0), 7: (-50 * k, 0, 0), 8: (90 * k, 0, 0), 9: (-50 * k, 0, 0), 10: (90 * k, 0, 0),
                               3: (20 * k, 0, 25), 5: (20 * k, 0, -25)}


def anim_flip(t):
    return PELVIS_Y, {1: (-20, 0, 0), 7: (-90, 0, 0), 8: (110, 0, 0), 9: (-90, 0, 0), 10: (110, 0, 0),
                      3: (-60, 0, 30), 5: (-60, 0, -30), 2: (-25, 0, 0)}


def anim_hurt(t):
    return PELVIS_Y, {1: (-25, 0, 0), 2: (-20, 0, 0), 3: (-100, 0, 60), 5: (-100, 0, -60), 7: (30, 0, 0), 9: (30, 0, 0)}


anims = [pose(anim_idle, 60), pose(anim_walk, 30), pose(anim_run, 18), pose(anim_jump, 1, False),
         pose(anim_fall, 20), pose(anim_land, 8, False), pose(anim_flip, 1, False), pose(anim_hurt, 1, False)]
anim_blob = struct.pack(">I", len(anims))
offs = []
body = b""
for a in anims:
    offs.append(len(body))
    body += a
    while len(body) % 4:
        body += b"\0"
anim_blob += b"".join(struct.pack(">I", 4 + 4 * len(anims) + o) for o in offs) + body

# object models: coin (billboard), star (lit mesh), tree (billboard), blob, sign
def billboard_dl(tex, w, h, yoff):
    dl = DL()
    dl.pipesync()
    dl.geom_clear(G_LIGHTING | G_CULL_BACK)
    dl.geom_set(G_SHADE)
    dl.texture(True)
    dl.cc_modulate()
    dl.load_texture(T[tex], G_TX_CLAMP, G_TX_CLAMP)
    c = (255, 255, 255, 255)
    tw, th = T[tex].w * 32, T[tex].h * 32
    v = [(-w // 2, yoff, 0, 0, th, c), (w // 2, yoff, 0, tw, th, c), (w // 2, yoff + h, 0, tw, 0, c), (-w // 2, yoff + h, 0, 0, 0, c)]
    emit_mesh(pack, dl, [(v[0], v[1], v[2]), (v[0], v[2], v[3])])
    dl.texture(False)
    dl.geom_set(G_CULL_BACK)
    dl.end()
    return pack.add(dl.bytes())


def star_tris(r_out=60, r_in=26, depth=20):
    tris = []
    pts = []
    for k in range(10):
        a = math.pi / 2 + k * math.pi / 5
        r = r_out if k % 2 == 0 else r_in
        pts.append((r * math.cos(a), r * math.sin(a)))
    for k in range(10):
        a, b = pts[k], pts[(k + 1) % 10]
        for side in (1, -1):
            tip = (0, 0, depth * side)
            p0 = (a[0], a[1], 0); p1 = (b[0], b[1], 0)
            tri = (tip, p0, p1) if side > 0 else (tip, p1, p0)
            n = normal(*tri)
            nb = tuple(int(round(c * 127)) for c in n) + (255,)
            tris.append(tuple((int(p[0]), int(p[1]), int(p[2]), 0, 0, nb) for p in tri))
    return tris


def lit_dl(pieces):
    return part_dl(pieces)


blob_pieces = [((160, 60, 200), mesh_sphere(0, 45, 0, 50, 6, 10, sy=0.8)),
               (WHITE, mesh_sphere(-18, 62, 38, 12, 3, 6)), (WHITE, mesh_sphere(18, 62, 38, 12, 3, 6)),
               (DARK, mesh_sphere(-18, 62, 48, 5, 3, 6)), (DARK, mesh_sphere(18, 62, 48, 5, 3, 6))]
models = [
    (billboard_dl('coin', 80, 80, -40), 50, 1),                                   # 0 coin
    (lit_dl([(YEL, star_tris())]), 80, 0),                                         # 1 star
    (billboard_dl('tree', 300, 600, 0), 400, 1),                                   # 2 tree
    (lit_dl(blob_pieces), 80, 0),                                                  # 3 blob
]
mdl_blob = struct.pack(">I", len(models)) + b"".join(struct.pack(">IhH", a, r, f) for a, r, f in models)

# ---------------------------------------------------------------- collision
col = bytearray(struct.pack(">I", len(collision)))
for a, b, c, typ in collision:
    n = normal(a, b, c)
    vals = [int(round(v)) for p in (a, b, c) for v in p] + [int(round(x * 16384)) for x in n]
    col += struct.pack(">9h3hHH", *vals, typ, 0)

objs = struct.pack(">I", len(objects)) + b"".join(struct.pack(">HhhhHH", t, int(x), int(y), int(z), yaw & 0xFFFF, p)
                                                  for t, x, y, z, yaw, p in objects)
lvlm = struct.pack(">I", len(chunks)) + b"".join(ch.build() for ch in chunks + [wch])
lvlm = struct.pack(">I", len(chunks) + 1) + lvlm[4:]
name = b"MEADOW ISLE"
info = struct.pack(">hhhHhhHHHH", int(SPAWN[0]), int(SPAWN[1]), int(SPAWN[2]), SPAWN[3], WATER_Y, -3000,
                   0x5D9F, 0xB6FF, 0x7BEF, 0) + name.ljust(16, b"\0")

pack.entry("INFO", info)
pack.entry("LVLM", lvlm)
pack.entry("COLV", bytes(col))
pack.entry("OBJS", objs)
pack.entry("SKEL", skel)
pack.entry("ANIM", anim_blob)
pack.entry("MDLS", mdl_blob)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "../../content/sm50.pack")
data = pack.finish()
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
open(out, "wb").write(data)
ntris = sum(len(t) for ch in chunks + [wch] for t in ch.mats.values())
print(f"wrote {out}: {len(data)} bytes, {len(chunks)+1} chunks, {ntris} level tris, {len(collision)} collision tris, {len(objects)} objects")
