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
    assert "collision triangles: 1060" in output and "88 parsed, 13 supported" in output
    assert "21 objects parsed, 3 supported" in output
    pack = bytearray(open(pack_path, "rb").read())
    assert pack[:4] == b"S50P"
    col, _ = entry(pack, b"COLV")
    objs, _ = entry(pack, b"OBJS")
    sobj, sobj_size = entry(pack, b"SOBJ")
    skel, _ = entry(pack, b"SKEL")
    anim, _ = entry(pack, b"ANIM")
    assert struct.unpack_from(">I", pack, col)[0] == 1060
    assert struct.unpack_from(">I", pack, objs)[0] == 16
    assert struct.unpack_from(">I", pack, sobj)[0] == 21
    assert sobj_size == 4 + 21 * 24
    raw_spawns = [struct.unpack_from(">HhhhhhhhII", pack, sobj + 4 + i * 24)
                  for i in range(21)]
    assert sum(1 for s in raw_spawns if s[9] != 0) == 21
    objects = [(objs + 4 + i * 12, struct.unpack_from(">HhhhHH", pack, objs + 4 + i * 12))
               for i in range(16)]
    assert [o[1][0] for o in objects].count(6) == 8
    assert [o[1][0] for o in objects].count(7) == 1
    assert [o[1][0] for o in objects].count(8) == 1
    assert struct.unpack_from(">I", pack, skel)[0] == 20
    assert struct.unpack_from(">I", pack, anim)[0] == 8
    idle = anim + struct.unpack_from(">I", pack, anim + 4)[0]
    assert struct.unpack_from(">HH", pack, idle) == (30, 20)
    stride = 2 + 20 * 6
    assert pack[idle + 8:idle + 8 + stride] != pack[idle + 8 + stride:idle + 8 + 2 * stride]
    print("PASS canonical ROM converts: Battlefield geometry, collision, objects, Mario meshes and animations")

    log = os.path.join(OUT, "walk.log")
    output = run("./build/host/sm50-host", "--pack", pack_path, "--frames", "45",
                 "--log", log, "--outdir", OUT, "--shot-at", "35")
    assert "21 SM64 placements" in output
    assert "6 chunks, 1060 col tris, 16 objects, 21 SM64 placements, 20 parts, 8 anims" in output
    rows = open(log).read().splitlines()
    assert "ground=1" in rows[35]
    assert int(re.search(r"tris=(\d+)", rows[35]).group(1)) > 100
    print("PASS host renders and grounds Mario on ROM-derived Battlefield collision")

    # Move the spawn in a private test copy to the first ROM-derived coin.
    coin_type, x, y, z, _, _ = next(o for _, o in objects if o[0] == 1)
    assert coin_type == 1
    info, _ = entry(pack, b"INFO")
    struct.pack_into(">hhh", pack, info, x, y, z)
    coin_path = os.path.join(OUT, "coin-check.pack")
    open(coin_path, "wb").write(pack)
    output = run("./build/host/sm50-host", "--pack", coin_path, "--frames", "3")
    assert "coins=1" in output
    print("PASS ROM-derived coin can be collected")

    star_type, x, y, z, _, _ = next(o for _, o in objects if o[0] == 2)
    assert star_type == 2
    struct.pack_into(">hhh", pack, info, x, y, z)
    star_path = os.path.join(OUT, "star-check.pack")
    open(star_path, "wb").write(pack)
    output = run("./build/host/sm50-host", "--pack", star_path, "--frames", "3")
    assert "stars=1" in output
    print("PASS level-script-derived star can be collected")

    # Stack the eight ROM-placed red coins at the hidden star trigger in a
    # private pack copy. The trigger must become a collectible star only then.
    trigger_type, tx, ty, tz, _, _ = next(o for _, o in objects if o[0] == 7)
    assert trigger_type == 7
    for off, o in objects:
        if o[0] == 6:
            struct.pack_into(">hhh", pack, off + 2, tx, ty, tz)
    struct.pack_into(">hhh", pack, info, tx, ty, tz)
    red_path = os.path.join(OUT, "red-coin-check.pack")
    open(red_path, "wb").write(pack)
    output = run("./build/host/sm50-host", "--pack", red_path, "--frames", "4")
    assert "red=8" in output and "stars=1" in output
    print("PASS eight ROM-derived red coins reveal and award the hidden star")

    # Put the ROM-derived boss at the spawn for a deterministic interaction
    # check. Start with two hits so one grab/throw must reveal his star.
    boss_pack = bytearray(open(pack_path, "rb").read())
    sx, _, sz = struct.unpack_from(">hhh", boss_pack, info)
    struct.pack_into(">h", boss_pack, info + 2, 0)
    boss_off, boss = next((off, o) for off, o in objects if o[0] == 8)
    assert boss[1:4] == (1636, 4242, -5567)
    struct.pack_into(">hhhHH", boss_pack, boss_off + 2, sx, 0, sz + 180, 0, 2)
    boss_path = os.path.join(OUT, "boss-check.pack")
    open(boss_path, "wb").write(boss_pack)
    key_path = os.path.join(OUT, "boss-check.keys")
    open(key_path, "w").write("0 action\n1\n12 action\n13\n")
    boss_log = os.path.join(OUT, "boss-check.log")
    run("./build/host/sm50-host", "--pack", boss_path, "--frames", "42",
        "--keys", key_path, "--log", boss_log)
    boss_rows = open(boss_log).read().splitlines()
    assert "boss=1,2" in boss_rows[0] and "boss=2,2" in boss_rows[12]
    assert "boss=-1,-1" in boss_rows[40]
    print("PASS ROM-placed King Bob-omb can be grabbed, thrown and reveal his star")

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
