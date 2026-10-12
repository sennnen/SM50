"""Segmented address space: segment number (top byte) -> bytes."""


class SegmentError(Exception):
    pass


class Segments:
    def __init__(self):
        self.seg = {}

    def set(self, n, data):
        if not 0 <= n < 32:
            raise SegmentError("segment %d out of range" % n)
        self.seg[n] = bytes(data)

    def resolve(self, addr):
        n, off = (addr >> 24) & 0x1F, addr & 0xFFFFFF
        if n not in self.seg:
            raise SegmentError("address %08X: segment %02X not loaded" % (addr, n))
        if off >= len(self.seg[n]):
            raise SegmentError("address %08X beyond segment %02X (size 0x%X)" % (addr, n, len(self.seg[n])))
        return self.seg[n], off

    def read(self, addr, size):
        d, off = self.resolve(addr)
        if off + size > len(d):
            raise SegmentError("read of %d bytes at %08X overruns segment" % (size, addr))
        return d[off:off + size]
