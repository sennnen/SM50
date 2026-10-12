"""MIO0 (LZ-style) codec used by many N64 titles."""
import struct


class Mio0Error(Exception):
    pass


def decompress(data, offset=0):
    d = memoryview(data)[offset:]
    if bytes(d[:4]) != b"MIO0":
        raise Mio0Error("no MIO0 header at 0x%X" % offset)
    size, comp_off, raw_off = struct.unpack(">III", d[4:16])
    if size > 64 << 20 or comp_off > len(d) or raw_off > len(d):
        raise Mio0Error("corrupt MIO0 header at 0x%X" % offset)
    out = bytearray()
    bit_pos = 16
    bits, nbits = 0, 0
    cp, rp = comp_off, raw_off
    while len(out) < size:
        if nbits == 0:
            bits = struct.unpack(">I", d[bit_pos:bit_pos + 4])[0]
            bit_pos += 4
            nbits = 32
        flag = bits & 0x80000000
        bits = (bits << 1) & 0xFFFFFFFF
        nbits -= 1
        if flag:
            out.append(d[rp])
            rp += 1
        else:
            v = (d[cp] << 8) | d[cp + 1]
            cp += 2
            length = (v >> 12) + 3
            dist = (v & 0xFFF) + 1
            start = len(out) - dist
            if start < 0:
                raise Mio0Error("back-reference before start of output")
            for i in range(length):
                out.append(out[start + i])
    return bytes(out[:size])


def compress(data):
    """Greedy MIO0 encoder (window 4096, lengths 3..18)."""
    data = bytes(data)
    flags, comp, raw = [], bytearray(), bytearray()
    i, n = 0, len(data)
    index = {}
    while i < n:
        best_len, best_dist = 0, 0
        if i + 3 <= n:
            for j in reversed(index.get(data[i:i + 3], [])[-64:]):
                if i - j > 4096:
                    break
                l = 3
                while l < 18 and i + l < n and data[j + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_dist = l, i - j
                    if l == 18:
                        break
        if best_len >= 3:
            flags.append(0)
            v = ((best_len - 3) << 12) | (best_dist - 1)
            comp += struct.pack(">H", v)
            step = best_len
        else:
            flags.append(1)
            raw.append(data[i])
            step = 1
        for k in range(i, min(i + step, n - 2)):
            index.setdefault(data[k:k + 3], []).append(k)
        i += step
    bitbytes = bytearray()
    for k in range(0, len(flags), 32):
        w = 0
        for b, f in enumerate(flags[k:k + 32]):
            if f:
                w |= 0x80000000 >> b
        bitbytes += struct.pack(">I", w)
    comp_off = 16 + len(bitbytes)
    raw_off = comp_off + len(comp)
    return b"MIO0" + struct.pack(">III", n, comp_off, raw_off) + bytes(bitbytes) + bytes(comp) + bytes(raw)


def scan(data, start=0, end=None):
    """Find plausible MIO0 blocks: yields (offset, decompressed_size)."""
    end = len(data) if end is None else end
    pos = data.find(b"MIO0", start, end)
    while pos != -1:
        if pos % 4 == 0 and pos + 16 <= len(data):
            size, c, r = struct.unpack(">III", data[pos + 4:pos + 16])
            if 0 < size < 16 << 20 and 16 <= c <= r < size + 0x100000:
                yield pos, size
        pos = data.find(b"MIO0", pos + 4, end)
