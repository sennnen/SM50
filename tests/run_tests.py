#!/usr/bin/env python3
"""SM50 automated tests: unit tests, deterministic gameplay runs on the host
build, content-pack validation, g3a validation and (if built) an emulator run
of the real calculator add-in with screenshot checks."""
import json, os, re, struct, subprocess, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "test-results")
os.makedirs(OUT, exist_ok=True)
results = []


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def record(name, ok, detail=""):
    results.append((name, ok, detail))
    print(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else ""))


# 1. unit tests (sanitizers on)
r = run(["sh", "-c", "cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -std=c99 -D_DEFAULT_SOURCE "
         "-o build/unit_tests tests/unit_tests.c src/engine/fixmath.c src/engine/tables.c src/engine/gfx.c src/engine/world.c "
         "&& ./build/unit_tests"])
m = re.search(r"(\d+)/(\d+) checks passed", r.stdout)
record("unit tests (math, F3D formats/depth/culling/clipping, collision)", r.returncode == 0,
       m.group(0) if m else (r.stdout + r.stderr)[-400:])

# 2. content pack structure
pack = open(os.path.join(ROOT, "content/sm50.pack"), "rb").read()
ok = pack[:4] == b"S50P"
n, ds = struct.unpack(">II", pack[8:16])
tags = [pack[16 + i * 12:20 + i * 12].decode() for i in range(n)]
need = {"INFO", "LVLM", "COLV", "OBJS", "SKEL", "ANIM", "MDLS"}
record("content pack layout", ok and need <= set(tags), f"{len(pack)} bytes, entries {tags}")


def host(script, frames, extra=()):
    log = os.path.join(OUT, os.path.basename(script) + ".log")
    r = run(["./build/host/sm50-host", "--frames", str(frames), "--keys", script, "--log", log, "--outdir", OUT] + list(extra))
    rows = []
    for line in open(log):
        d = dict(kv.split("=") for kv in line.split()[1:])
        rows.append({k: int(v) for k, v in d.items() if v.lstrip("-").isdigit()})
    return r, rows


# 3. gameplay: run forward, jump, turn
r, rows = host("tests/scripts/run_jump.txt", 200, ["--shot-at", "45,150"])
moved = rows[100]["z"] < rows[5]["z"] - 1000
jumped = any(rw["act"] in (2, 3) and rw["ground"] == 0 for rw in rows[38:50])
landed = rows[52]["ground"] == 1
coin = rows[-1]["coins"] >= 1
record("gameplay: walking/running moves the player", moved, f"z {rows[5]['z']} -> {rows[100]['z']}")
record("gameplay: jump leaves the ground and lands", jumped and landed)
record("gameplay: coin collection", coin, f"coins={rows[-1]['coins']}")
record("gameplay: enemy contact causes damage/knockback", any(rw["act"] == 9 for rw in rows) and rows[-1]["health"] < 8,
       f"health={rows[-1]['health']}")
fell = all(rw["y"] > -150 for rw in rows)
record("gameplay: never falls through terrain", fell, f"min y={min(rw['y'] for rw in rows)}")

# 4. full route to the star (scripted speedrun of the test level)
if os.path.exists(os.path.join(ROOT, "tests/scripts/star_route.txt")):
    r, rows = host("tests/scripts/star_route.txt", 700, ["--route", "tests/scripts/star_route.wp", "--shot-at", "330,699"])
    record("gameplay: autopilot climbs the tower stairs and collects the star", rows[-1]["stars"] >= 1,
           f"final pos ({rows[-1]['x']},{rows[-1]['y']},{rows[-1]['z']}) stars={rows[-1]['stars']} coins={rows[-1]['coins']}")

    r, rows = host("tests/scripts/star_route.txt", 400, ["--route", "tests/scripts/star2_route.wp", "--shot-at", "200"])
    record("gameplay: autopilot crosses the floating platforms and collects star 2", rows[-1]["stars"] >= 1,
           f"final pos ({rows[-1]['x']},{rows[-1]['y']},{rows[-1]['z']}) stars={rows[-1]['stars']}")

# 5. generic ROM pipeline on the synthetic fixture
sys.path.insert(0, os.path.join(ROOT, "tools"))
from n64 import rom as n64rom, mio0 as n64mio0, f3d as n64f3d  # noqa: E402
from n64.segments import Segments  # noqa: E402
r = run([sys.executable, "tests/make_synthetic_rom.py"])
record("pipeline: synthetic ROM fixture generated", r.returncode == 0, r.stdout.strip()[-120:])
packs = {}
for order in ("z64", "v64", "n64"):
    out = os.path.join(OUT, "synth_%s.pack" % order)
    r = run([sys.executable, "tools/n64pack.py", "tests/fixtures/synthetic.json", "--rom", "tests/fixtures/synthetic." + order, "-o", out])
    packs[order] = open(out, "rb").read() if r.returncode == 0 else b""
record("pipeline: z64/v64/n64 byte orders convert to identical packs",
       packs["z64"] and packs["z64"] == packs["v64"] == packs["n64"], "%d bytes" % len(packs["z64"]))
z, h = n64rom.load(os.path.join(ROOT, "tests/fixtures/synthetic.v64"))
bad = bytearray(z); bad[0x5000] ^= 0xFF
record("pipeline: header CRC verified, corruption detected",
       h["crc_ok"] and n64rom.cic6102_crc(bytes(bad)) != (h["crc1"], h["crc2"]), "order=%s name=%s" % (h["order"], h["name"]))
blob = os.urandom(3000) + bytes(range(256)) * 40 + b"\0" * 5000
c = n64mio0.compress(blob)
try:
    n64mio0.decompress(c[:4] + b"\xff" * 4 + c[8:]); corrupt_ok = False
except Exception:
    corrupt_ok = True
record("pipeline: MIO0 round trip and corrupt-header rejection", n64mio0.decompress(c) == blob and corrupt_ok,
       "%d -> %d bytes" % (len(blob), len(c)))
m = json.load(open(os.path.join(ROOT, "tests/fixtures/synthetic.json")))
segs = Segments()
for sg in m["segments"]:
    a, b = int(sg["rom"][0], 16), int(sg["rom"][1], 16)
    segs.set(sg["seg"], n64mio0.decompress(z[a:b]) if sg.get("mio0") else z[a:b])
tri_counts = [len(n64f3d.walk(segs, int(ch["dl"], 16)).tris) for ch in m["chunks"]]
record("pipeline: F3D walker follows nested G_DL across segments", tri_counts == [514, 60, 2], "tris per chunk %s" % tri_counts)
synth = os.path.join(OUT, "synth_z64.pack")
r = run(["./build/host/sm50-host", "--pack", synth, "--frames", "40", "--keys", "tests/scripts/run_jump.txt",
         "--outdir", OUT, "--shot-at", "39"])
m2 = re.search(r"avg_tris=([\d.]+)", r.stdout)
record("pipeline: host engine renders the ROM-derived pack", r.returncode == 0 and m2 and float(m2.group(1)) > 50,
       "avg tris drawn %s" % (m2.group(1) if m2 else "?"))

# 6. g3a validation (prebuilt engine + attached packs)
engine_bin = os.path.join(ROOT, "build/calc/SM50.bin")
g3a = os.path.join(OUT, "SM50-test.g3a")
g3a_synth = os.path.join(OUT, "SM50-synth.g3a")
run([sys.executable, "tools/build-g3a.py", engine_bin, "content/sm50.pack", g3a])
run([sys.executable, "tools/build-g3a.py", engine_bin, synth, g3a_synth])
if os.path.exists(g3a):
    r = run([sys.executable, "tools/g3a_check.py", g3a])
    record("g3a container checksums", r.returncode == 0, r.stdout.strip().splitlines()[-1] if r.stdout else r.stderr[-200:])
else:
    record("g3a container checksums", False, "build/calc/SM50.g3a missing (run make calc)")

# 7. emulator: run the real add-in
emu = os.environ.get("SM50EMU", "/home/user/ext/casio-emu-build/build-headless/sm50emu")
if os.path.exists(emu) and os.path.exists(g3a):
    eo = os.path.join(OUT, "emu")
    os.makedirs(eo, exist_ok=True)
    r = run([emu, "--keys", "tests/scripts/emu_play.txt", "--outdir", eo, "--shot-at", "1,40,70,95", "--shot-final",
             "--max-frames", "100", "--max-wall", "300", "--log", os.path.join(eo, "log.txt"),
             "--frame-stats", os.path.join(eo, "frames.csv"), g3a], timeout=400)
    open(os.path.join(eo, "summary.txt"), "w").write(r.stdout + r.stderr)
    ok = r.returncode == 0 and "frames presented  : 100" in r.stdout
    fps = re.search(r"est\. HW fps\s*:\s*([\d.]+)", r.stdout)
    record("emulator: add-in boots and presents 100 frames", ok, f"est. {fps.group(1) if fps else '?'} fps (instruction-count estimate)")
    try:
        def read_emulator_png(path):
            # The bundled emulator emits RGB8 PNGs with filter type zero.
            # Read those directly so the test has no Pillow dependency.
            data = open(path, "rb").read()
            assert data[:8] == b"\x89PNG\r\n\x1a\n"
            pos, compressed, width, height = 8, bytearray(), 0, 0
            while pos < len(data):
                length = struct.unpack_from(">I", data, pos)[0]
                tag, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
                pos += 12 + length
                if tag == b"IHDR":
                    width, height = struct.unpack_from(">II", body)
                    assert body[8:13] == b"\x08\x02\x00\x00\x00"
                elif tag == b"IDAT":
                    compressed.extend(body)
                elif tag == b"IEND":
                    break
            raw = zlib.decompress(compressed)
            row = width * 3 + 1
            assert len(raw) == row * height
            assert all(raw[y * row] == 0 for y in range(height))
            return [raw[y * row + 1 + x * 3:y * row + 4 + x * 3]
                    for y in range(height) for x in range(width)]

        a = read_emulator_png(os.path.join(eo, "frame_000001.png"))
        b = read_emulator_png(os.path.join(eo, "frame_000070.png"))
        colours = len(set(b))
        diff = sum(1 for p, q in zip(a, b) if p != q)
        record("emulator: 3D scene rendered and changes with input", colours > 50 and diff > 5000,
               f"{colours} colours in frame 70, {diff} pixels changed since frame 1")
    except (AssertionError, OSError, zlib.error) as exc:
        record("emulator: screenshot analysis", False, str(exc))
    r = run([emu, "--keys", "tests/scripts/emu_play.txt", "--outdir", eo, "--shot-at", "60", "--max-frames", "80",
             "--max-wall", "300", g3a_synth], timeout=400)
    open(os.path.join(eo, "summary-synth.txt"), "w").write(r.stdout + r.stderr)
    fps = re.search(r"est\. HW fps\s*:\s*([\d.]+)", r.stdout)
    record("emulator: engine + ROM-derived pack (synthetic ROM) runs on the CG50 target",
           r.returncode == 0 and "frames presented  : 80" in r.stdout, "est. %s fps" % (fps.group(1) if fps else "?"))
else:
    record("emulator run", False, "emulator or g3a missing")

with open(os.path.join(OUT, "results.txt"), "w") as f:
    for name, ok, detail in results:
        f.write(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else "") + "\n")
failed = [r for r in results if not r[1]]
print(f"\n{len(results) - len(failed)}/{len(results)} test groups passed")
sys.exit(1 if failed else 0)
