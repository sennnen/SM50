#!/usr/bin/env python3
"""Validate and describe an N64 ROM (any byte order): n64info.py game.z64 [--normalize out.z64]"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n64 import rom, mio0  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("rom"); ap.add_argument("--normalize"); ap.add_argument("--mio0", action="store_true", help="list MIO0 blocks")
a = ap.parse_args()
try:
    z, h = rom.load(a.rom)
except (rom.RomError, OSError) as e:
    sys.exit("error: %s" % e)
print("title   : %s" % h["name"])
print("id      : %s%s%s v%d" % (h["media"], h["game_id"], h["region"], h["version"]))
print("size    : %d bytes (%d MiB)" % (h["size"], h["size"] >> 20))
print("order   : %s%s" % (h["order"], "" if h["order"] == "z64" else " (converted to big-endian)"))
print("crc     : %08X %08X %s" % (h["crc1"], h["crc2"], "ok" if h["crc_ok"] else "MISMATCH (not CIC-6102 or modified)"))
print("sha1    : %s" % h["sha1"])
if a.mio0:
    blocks = list(mio0.scan(z))
    print("mio0    : %d blocks" % len(blocks))
    for off, size in blocks:
        print("  %08X -> %d bytes" % (off, size))
if a.normalize:
    open(a.normalize, "wb").write(z)
    print("wrote %s" % a.normalize)
