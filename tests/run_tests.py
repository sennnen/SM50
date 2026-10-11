#!/usr/bin/env python3
"""SM50 automated tests: unit tests, deterministic gameplay runs on the host
build, content-pack validation, g3a validation and (if built) an emulator run
of the real calculator add-in with screenshot checks."""
import os, re, struct, subprocess, sys

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
        rows.append({k: int(v) for k, v in d.items()})
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
    r, rows = host("tests/scripts/star_route.txt", 1400, ["--shot-at", "1399"])
    record("gameplay: scripted route reaches the star", rows[-1]["stars"] >= 1,
           f"final pos ({rows[-1]['x']},{rows[-1]['y']},{rows[-1]['z']}) stars={rows[-1]['stars']} coins={rows[-1]['coins']}")

# 5. g3a validation
g3a = os.path.join(ROOT, "build/calc/SM50.g3a")
if os.path.exists(g3a):
    r = run([sys.executable, "tools/g3a_check.py", g3a])
    record("g3a container checksums", r.returncode == 0, r.stdout.strip().splitlines()[-1] if r.stdout else r.stderr[-200:])
else:
    record("g3a container checksums", False, "build/calc/SM50.g3a missing (run make calc)")

# 6. emulator: run the real add-in
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
        from PIL import Image
        a = Image.open(os.path.join(eo, "frame_000001.png")).convert("RGB")
        b = Image.open(os.path.join(eo, "frame_000070.png")).convert("RGB")
        colours = len(set(b.get_flattened_data()))
        diff = sum(1 for p, q in zip(a.get_flattened_data(), b.get_flattened_data()) if p != q)
        record("emulator: 3D scene rendered and changes with input", colours > 50 and diff > 5000,
               f"{colours} colours in frame 70, {diff} pixels changed since frame 1")
    except ImportError:
        record("emulator: screenshot analysis", False, "PIL not installed")
else:
    record("emulator run", False, "emulator or g3a missing")

with open(os.path.join(OUT, "results.txt"), "w") as f:
    for name, ok, detail in results:
        f.write(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else "") + "\n")
failed = [r for r in results if not r[1]]
print(f"\n{len(results) - len(failed)}/{len(results)} test groups passed")
sys.exit(1 if failed else 0)
