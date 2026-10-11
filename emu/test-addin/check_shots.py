#!/usr/bin/env python3
"""Check that the test add-in's square is where the key script put it.
Usage: check_shots.py OUTDIR FRAME:X:Y [...]   (X,Y = top-left of the 24x24 square)
Original code for the SM50 project."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from mkg3a import read_png_rgb  # noqa: E402  (small PNG decoder)


def white_bbox(path):
    w, h, px = read_png_rgb(path)
    xs, ys = [], []
    for i, c in enumerate(px):
        if c == (255, 255, 255):
            xs.append(i % w)
            ys.append(i // w)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def main(argv):
    outdir, ok = argv[0], True
    for spec in argv[1:]:
        f, x, y = (int(v) for v in spec.split(":"))
        path = os.path.join(outdir, f"frame_{f:06d}.png")
        got = white_bbox(path)
        want = (x + 2, y + 2, x + 21, y + 21)  # white interior of the square
        good = got == want
        ok &= good
        print(f"{'PASS' if good else 'FAIL'} frame {f}: square interior {got}, expected {want}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
