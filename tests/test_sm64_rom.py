#!/usr/bin/env python3
"""Opt-in integration test using a locally supplied canonical US SM64 ROM.

Generated packs, screenshots and add-ins stay under ignored build/.
"""
import os
import re
import struct
import subprocess
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "sm64-rom-test")
ROM = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else ""


def run(*args):
    p = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if p.returncode:
        raise RuntimeError("%s failed:\n%s\n%s" % (args[0], p.stdout[-2000:], p.stderr[-2000:]))
    return p.stdout


def entry(pack, tag):
    n, base = struct.unpack_from(">II", pack, 8)
    for i in range(n):
        p = 16 + i * 12
        if pack[p:p + 4] == tag:
            off, size = struct.unpack_from(">II", pack, p + 4)
            return base + off, size
    raise AssertionError("missing pack entry %s" % tag)


def main():
    if not os.path.isfile(ROM):
        raise SystemExit("pass your ROM path or set SM64_ROM before make test-rom")
    os.makedirs(OUT, exist_ok=True)
    pack_path = os.path.join(OUT, "bob.pack")
    output = run(sys.executable, "tools/n64pack.py", "manifests/sm64-us-bob.json",
                 "--rom", ROM, "-o", pack_path)
    assert "header CRC ok" in output and "triangles: 1101" in output
    assert "collision triangles: 1060" in output and "88 parsed, 5 supported" in output
    assert "21 objects parsed, 1 supported" in output
    pack = bytearray(open(pack_path, "rb").read())
    assert pack[:4] == b"S50P"
    col, _ = entry(pack, b"COLV")
    objs, _ = entry(pack, b"OBJS")
    skel, _ = entry(pack, b"SKEL")
    assert struct.unpack_from(">I", pack, col)[0] == 1060
    assert struct.unpack_from(">I", pack, objs)[0] == 6
    assert struct.unpack_from(">I", pack, skel)[0] == 15
    print("PASS canonical ROM converts: Battlefield geometry, collision, coins, star, Mario meshes")

    log = os.path.join(OUT, "walk.log")
    output = run("./build/host/sm50-host", "--pack", pack_path, "--frames", "45",
                 "--log", log, "--outdir", OUT, "--shot-at", "35")
    assert "6 chunks, 1060 col tris, 6 objects, 15 parts, 8 anims" in output
    rows = open(log).read().splitlines()
    assert "ground=1" in rows[35]
    assert int(re.search(r"tris=(\d+)", rows[35]).group(1)) > 100
    print("PASS host renders and grounds Mario on ROM-derived Battlefield collision")

    # Move the spawn in a private test copy to the first ROM-derived coin.
    coin_type, x, y, z, _, _ = struct.unpack_from(">HhhhHH", pack, objs + 4)
    assert coin_type == 1
    info, _ = entry(pack, b"INFO")
    struct.pack_into(">hhh", pack, info, x, y, z)
    coin_path = os.path.join(OUT, "coin-check.pack")
    open(coin_path, "wb").write(pack)
    output = run("./build/host/sm50-host", "--pack", coin_path, "--frames", "3")
    assert "coins=1" in output
    print("PASS ROM-derived coin can be collected")

    star_type, x, y, z, _, _ = struct.unpack_from(">HhhhHH", pack, objs + 4 + 5 * 12)
    assert star_type == 2
    struct.pack_into(">hhh", pack, info, x, y, z)
    star_path = os.path.join(OUT, "star-check.pack")
    open(star_path, "wb").write(pack)
    output = run("./build/host/sm50-host", "--pack", star_path, "--frames", "3")
    assert "stars=1" in output
    print("PASS level-script-derived star can be collected")

    g3a = os.path.join(OUT, "SM64-BOB.g3a")
    run(sys.executable, "tools/build-g3a.py", "dist/SM50-engine.bin", pack_path, g3a)
    run(sys.executable, "tools/g3a_check.py", g3a)
    print("PASS calculator add-in container validates (%d bytes)" % os.path.getsize(g3a))
    emu = os.environ.get("SM50EMU")
    if emu:
        output = run(emu, "--keys", "tests/scripts/emu_play.txt", "--outdir", OUT,
                     "--shot-at", "35", "--max-frames", "40", "--max-wall", "120", g3a)
        assert "frames presented  : 40" in output
        print("PASS calculator emulator renders 40 frames of ROM-derived content")


if __name__ == "__main__":
    main()
