"""Static analysis of Fast3D (F3D) display lists: walks G_DL calls/branches,
tracks the 16-entry vertex cache and reports triangles in model space,
texture loads and bounds. Used for culling data and collision extraction."""
import struct

G_VTX, G_DL, G_ENDDL, G_TRI1, G_MTX, G_POPMTX = 0x04, 0x06, 0xB8, 0xBF, 0x01, 0xBD
G_SETTIMG, G_LOADBLOCK, G_LOADTILE, G_SETTILE, G_SETTILESIZE, G_LOADTLUT = 0xFD, 0xF3, 0xF4, 0xF5, 0xF2, 0xF0
G_SETGEOMETRYMODE, G_CLEARGEOMETRYMODE = 0xB7, 0xB6


class DLInfo:
    def __init__(self):
        self.tris = []          # list of ((x,y,z),(x,y,z),(x,y,z))
        self.textures = set()   # (addr, fmt, siz)
        self.commands = 0
        self.unknown = {}
        self.errors = []

    def bounds(self):
        pts = [p for t in self.tris for p in t]
        if not pts:
            return None
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
        c = [(lo[i] + hi[i]) / 2 for i in range(3)]
        r = max(sum((p[i] - c[i]) ** 2 for i in range(3)) ** 0.5 for p in pts)
        return c, r


KNOWN = {0x00, 0x01, 0x03, 0x04, 0x06, 0xB2, 0xB3, 0xB4, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE,
         0xBF, 0xC0, 0xE4, 0xE6, 0xE7, 0xE8, 0xE9, 0xED, 0xF0, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
         0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}


def walk(segs, addr, info=None, depth=0, max_cmds=200000):
    info = info or DLInfo()
    vbuf = [(0, 0, 0)] * 32
    timg = None
    stack = [addr]
    while stack:
        pc = stack.pop()
        while True:
            if info.commands > max_cmds:
                info.errors.append("command limit reached")
                return info
            try:
                w0, w1 = struct.unpack(">II", segs.read(pc, 8))
            except Exception as e:  # noqa: BLE001
                info.errors.append(str(e))
                break
            info.commands += 1
            op = w0 >> 24
            pc += 8
            if op not in KNOWN:
                info.unknown[op] = info.unknown.get(op, 0) + 1
            if op == G_VTX:
                n, v0 = ((w0 >> 20) & 0xF) + 1, (w0 >> 16) & 0xF
                try:
                    raw = segs.read(w1, n * 16)
                except Exception as e:  # noqa: BLE001
                    info.errors.append(str(e))
                    continue
                for i in range(n):
                    vbuf[v0 + i] = struct.unpack(">hhh", raw[i * 16:i * 16 + 6])
            elif op == G_TRI1:
                a, b, c = ((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10
                info.tris.append((vbuf[a], vbuf[b], vbuf[c]))
            elif op == G_DL:
                if (w0 >> 16) & 0xFF:
                    pc = w1
                else:
                    if depth > 16:
                        info.errors.append("display list nesting too deep")
                    else:
                        walk(segs, w1, info, depth + 1, max_cmds)
            elif op == G_ENDDL:
                break
            elif op == G_SETTIMG:
                timg = (w1, (w0 >> 21) & 7, (w0 >> 19) & 3)
            elif op in (G_LOADBLOCK, G_LOADTILE) and timg:
                info.textures.add(timg)
    return info
