"""Minimal Fast3D (F3D) display-list encoder and a pack "linker".

Produces native N64 big-endian binary display lists, vertices, lights and
textures. Addresses are segmented: segment 0x07 = start of the pack.
"""
import struct

SEG = 0x07

G_MTX, G_MOVEMEM, G_VTX, G_DL = 0x01, 0x03, 0x04, 0x06
G_CLEARGEOMETRYMODE, G_SETGEOMETRYMODE, G_ENDDL = 0xB6, 0xB7, 0xB8
G_SETOTHERMODE_L, G_SETOTHERMODE_H, G_TEXTURE, G_MOVEWORD = 0xB9, 0xBA, 0xBB, 0xBC
G_TRI1 = 0xBF
G_RDPLOADSYNC, G_RDPPIPESYNC, G_RDPTILESYNC = 0xE6, 0xE7, 0xE8
G_LOADTLUT, G_SETTILESIZE, G_LOADBLOCK, G_SETTILE = 0xF0, 0xF2, 0xF3, 0xF5
G_SETPRIMCOLOR, G_SETENVCOLOR, G_SETCOMBINE, G_SETTIMG = 0xFA, 0xFB, 0xFC, 0xFD

G_ZBUFFER, G_SHADE, G_SHADING_SMOOTH = 0x1, 0x4, 0x200
G_CULL_FRONT, G_CULL_BACK, G_LIGHTING, G_TEXTURE_GEN = 0x1000, 0x2000, 0x20000, 0x40000
G_IM_FMT_RGBA, G_IM_FMT_CI, G_IM_FMT_IA, G_IM_FMT_I = 0, 2, 3, 4
G_IM_SIZ_4b, G_IM_SIZ_8b, G_IM_SIZ_16b, G_IM_SIZ_32b = 0, 1, 2, 3
G_TX_WRAP, G_TX_MIRROR, G_TX_CLAMP = 0, 1, 2
G_MV_L0, G_MW_NUMLIGHT = 0x86, 0x02

# render modes (OTHERMODE_L bits, shifted already)
RM_OPA = 0x0F0A4000 & ~0x4000 | 0x20  # z compare+update, opaque (FORCE_BL clear)
RM_XLU = 0x00404000 | 0x10             # force blend, z compare, no z update


def w(op, hi=0):
    return op << 24 | hi


class Pack:
    """Append-only blob with 8-byte aligned allocations; returns segmented addresses."""

    def __init__(self):
        self.data = bytearray()
        self.entries = []  # (tag, offset, size)

    def alloc(self, payload, align=8):
        while len(self.data) % align:
            self.data.append(0)
        off = len(self.data)
        self.data += payload
        return (SEG << 24) | off, off

    def add(self, payload, align=8):
        return self.alloc(payload, align)[0]

    def entry(self, tag, payload):
        addr, off = self.alloc(payload)
        self.entries.append((tag, off, len(payload)))
        return addr

    def finish(self):
        """File = header + data. Header: 'S50P', version, n_entries, data_start, then
        n_entries x (tag[4], offset, size) with offsets relative to data_start.
        The engine maps segment 0x07 to file_base + data_start."""
        hdr_size = (16 + 12 * len(self.entries) + 15) & ~15
        out = bytearray(b"S50P" + struct.pack(">III", 1, len(self.entries), hdr_size))
        for tag, off, size in self.entries:
            out += tag.encode() + struct.pack(">II", off, size)
        while len(out) < hdr_size:
            out.append(0)
        return bytes(out) + bytes(self.data)


class DL:
    def __init__(self):
        self.cmds = []

    def raw(self, w0, w1):
        self.cmds.append((w0 & 0xFFFFFFFF, w1 & 0xFFFFFFFF))

    def vtx(self, addr, n, v0=0):
        self.raw(w(G_VTX, ((n - 1) << 20) | (v0 << 16) | (n * 16)), addr)

    def tri(self, a, b, c):
        self.raw(w(G_TRI1), (a * 10) << 16 | (b * 10) << 8 | (c * 10))

    def dl(self, addr, branch=False):
        self.raw(w(G_DL, (1 if branch else 0) << 16), addr)

    def end(self):
        self.raw(w(G_ENDDL), 0)

    def pipesync(self):
        self.raw(w(G_RDPPIPESYNC), 0)

    def geom_set(self, bits):
        self.raw(w(G_SETGEOMETRYMODE), bits)

    def geom_clear(self, bits):
        self.raw(w(G_CLEARGEOMETRYMODE), bits)

    def texture(self, on, s=0xFFFF, t=0xFFFF, tile=0):
        self.raw(w(G_TEXTURE, (tile << 8) | (1 if on else 0)), s << 16 | t)

    def combine(self, a, b, c, d, aa=7, ab=7, ac=7, ad=7):
        """Same mode for both cycles. Colour (a-b)*c+d."""
        w0 = (a & 0xF) << 20 | (c & 0x1F) << 15 | (aa & 7) << 12 | (ac & 7) << 9 | (a & 0xF) << 5 | (c & 0x1F)
        w1 = ((b & 0xF) << 28 | (b & 0xF) << 24 | (aa & 7) << 21 | (ac & 7) << 18 | (d & 7) << 15 |
              (ab & 7) << 12 | (ad & 7) << 9 | (d & 7) << 6 | (ab & 7) << 3 | (ad & 7))
        self.raw(w(G_SETCOMBINE, w0), w1)

    def cc_shade(self):
        self.combine(15, 15, 31, 4)

    def cc_modulate(self):
        self.combine(1, 8, 4, 7)

    def cc_decal(self):
        self.combine(15, 15, 31, 1)

    def cc_prim_shade(self):
        self.combine(3, 8, 4, 7)

    def rendermode(self, mode):
        self.raw(w(G_SETOTHERMODE_L, (3 << 8) | 29), mode)

    def primcolor(self, r, g, b, a=255):
        self.raw(w(G_SETPRIMCOLOR), r << 24 | g << 16 | b << 8 | a)

    def lights1(self, addr_diffuse, addr_ambient):
        self.raw(w(G_MOVEWORD, (0 << 8) | G_MW_NUMLIGHT), 0x80000000 + 2 * 32)
        self.raw(w(G_MOVEMEM, (G_MV_L0 << 16) | 16), addr_diffuse)
        self.raw(w(G_MOVEMEM, ((G_MV_L0 + 2) << 16) | 8), addr_ambient)

    def load_texture(self, tex, cms=G_TX_WRAP, cmt=G_TX_WRAP):
        """gsDPLoadTextureBlock equivalent for a Texture object."""
        fmt, siz, wd, ht = tex.fmt, tex.siz, tex.w, tex.h
        if tex.pal_addr:
            self.raw(w(G_SETTIMG, (G_IM_FMT_RGBA << 21) | (G_IM_SIZ_16b << 19)), tex.pal_addr)
            self.raw(w(G_SETTILE, 256), 7 << 24)
            self.raw(w(G_RDPLOADSYNC), 0)
            self.raw(w(G_LOADTLUT), 7 << 24 | ((len(tex.palette) - 1) & 0x3FF) << 14)
            self.pipesync()
        load_siz = G_IM_SIZ_16b if siz < G_IM_SIZ_16b else siz
        bpp = [4, 8, 16, 32][siz]
        texels = wd * ht * bpp // [4, 8, 16, 32][load_siz] if siz < 2 else wd * ht
        self.raw(w(G_SETTIMG, (fmt << 21) | (load_siz << 19)), tex.addr)
        self.raw(w(G_SETTILE, (fmt << 21) | (load_siz << 19)), 7 << 24)
        self.raw(w(G_RDPLOADSYNC), 0)
        dxt = min(2047, (2048 + max(1, wd * bpp // 64) - 1) // max(1, wd * bpp // 64))
        self.raw(w(G_LOADBLOCK), 7 << 24 | (texels - 1) << 12 | dxt)
        self.pipesync()
        line = (wd * bpp // 8 + 7) // 8
        masks, maskt = wd.bit_length() - 1, ht.bit_length() - 1
        self.raw(w(G_SETTILE, (fmt << 21) | (siz << 19) | (line << 9)),
                 0 << 24 | (cmt << 18) | (maskt << 14) | (cms << 8) | (masks << 4))
        self.raw(w(G_SETTILESIZE), 0 << 24 | ((wd - 1) << 2) << 12 | ((ht - 1) << 2))

    def bytes(self):
        return b"".join(struct.pack(">II", a, b) for a, b in self.cmds)


class Texture:
    def __init__(self, pack, w, h, rgba_pixels, fmt="rgba16"):
        """rgba_pixels: list of (r,g,b,a) rows*cols."""
        self.w, self.h = w, h
        self.pal_addr = 0
        self.palette = []
        if fmt == "rgba16":
            self.fmt, self.siz = G_IM_FMT_RGBA, G_IM_SIZ_16b
            data = bytearray()
            for r, g, b, a in rgba_pixels:
                data += struct.pack(">H", (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | (1 if a >= 128 else 0))
        elif fmt == "ci4":
            self.fmt, self.siz = G_IM_FMT_CI, G_IM_SIZ_4b
            pal = []
            idx = []
            for p in rgba_pixels:
                q = ((p[0] >> 3), (p[1] >> 3), (p[2] >> 3), 1 if p[3] >= 128 else 0)
                if q not in pal:
                    if len(pal) < 16:
                        pal.append(q)
                    else:
                        q = min(pal, key=lambda c: sum((c[i] - q[i]) ** 2 for i in range(3)))
                idx.append(pal.index(q))
            self.palette = pal + [(0, 0, 0, 0)] * (16 - len(pal))
            pdata = b"".join(struct.pack(">H", r << 11 | g << 6 | b << 1 | a) for r, g, b, a in self.palette)
            self.pal_addr = pack.add(pdata)
            data = bytearray((idx[i] << 4) | (idx[i + 1] if i + 1 < len(idx) else 0) for i in range(0, len(idx), 2))
        elif fmt == "ia8":
            self.fmt, self.siz = G_IM_FMT_IA, G_IM_SIZ_8b
            data = bytearray(((sum(p[:3]) // 3) >> 4) << 4 | (p[3] >> 4) for p in rgba_pixels)
        elif fmt == "i4":
            self.fmt, self.siz = G_IM_FMT_I, G_IM_SIZ_4b
            vals = [(sum(p[:3]) // 3) >> 4 for p in rgba_pixels]
            data = bytearray((vals[i] << 4) | vals[i + 1] for i in range(0, len(vals), 2))
        else:
            raise ValueError(fmt)
        self.addr = pack.add(bytes(data))


def vtx_bytes(x, y, z, s, t, c):
    """c = (r,g,b,a) colour or (nx,ny,nz,a) normal (signed bytes)."""
    return struct.pack(">hhhHhhBBBB", int(x), int(y), int(z), 0, int(s), int(t),
                       c[0] & 0xFF, c[1] & 0xFF, c[2] & 0xFF, c[3] & 0xFF)


def light_bytes(r, g, b, dx, dy, dz):
    return struct.pack(">BBBxBBBxbbbx", r, g, b, r, g, b, dx, dy, dz)


def ambient_bytes(r, g, b):
    return struct.pack(">BBBxBBBx", r, g, b, r, g, b)


def emit_mesh(pack, dl, tris):
    """tris: list of 3-tuples of vertex tuples (x,y,z,s,t,(c4)). Batches into 16-vertex loads."""
    i = 0
    while i < len(tris):
        verts, keys, batch = [], {}, []
        while i < len(tris):
            need = [v for v in tris[i] if v not in keys]
            uniq = []
            for v in need:
                if v not in uniq:
                    uniq.append(v)
            if len(verts) + len(uniq) > 16:
                break
            for v in uniq:
                keys[v] = len(verts)
                verts.append(v)
            batch.append(tuple(keys[v] for v in tris[i]))
            i += 1
        addr = pack.add(b"".join(vtx_bytes(*v[:5], v[5]) for v in verts))
        dl.vtx(addr, len(verts), 0)
        for a, b, c in batch:
            dl.tri(a, b, c)
