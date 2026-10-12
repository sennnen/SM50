"""N64 texture decoding to RGBA8888 and a minimal PNG writer (no dependencies)."""
import struct
import zlib

RGBA, YUV, CI, IA, I = 0, 1, 2, 3, 4
B4, B8, B16, B32 = 0, 1, 2, 3


def _5551(v):
    r, g, b = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if v & 1 else 0)


def decode(data, fmt, siz, w, h, palette=None):
    n = w * h
    out = []
    for i in range(n):
        if fmt == RGBA and siz == B16:
            out.append(_5551(struct.unpack_from(">H", data, i * 2)[0]))
        elif fmt == RGBA and siz == B32:
            out.append(tuple(data[i * 4:i * 4 + 4]))
        elif fmt == CI:
            idx = (data[i >> 1] >> (0 if i & 1 else 4)) & 0xF if siz == B4 else data[i]
            out.append(_5551(struct.unpack_from(">H", palette, idx * 2)[0]) if palette else (idx, idx, idx, 255))
        elif fmt == IA and siz == B4:
            v = (data[i >> 1] >> (0 if i & 1 else 4)) & 0xF
            g = (v >> 1) * 255 // 7
            out.append((g, g, g, 255 if v & 1 else 0))
        elif fmt == IA and siz == B8:
            v = data[i]
            out.append(((v >> 4) * 17,) * 3 + ((v & 15) * 17,))
        elif fmt == IA and siz == B16:
            out.append((data[i * 2],) * 3 + (data[i * 2 + 1],))
        elif fmt == I and siz == B4:
            g = ((data[i >> 1] >> (0 if i & 1 else 4)) & 0xF) * 17
            out.append((g, g, g, g))
        elif fmt == I and siz == B8:
            out.append((data[i],) * 4)
        else:
            raise ValueError("unsupported texture format fmt=%d siz=%d" % (fmt, siz))
    return out


def byte_size(fmt, siz, w, h):
    return w * h * [4, 8, 16, 32][siz] // 8


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + bytes(c for p in rgba[y * w:(y + 1) * w] for c in p) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)
