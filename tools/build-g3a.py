#!/usr/bin/env python3
"""Attach an SM50 data pack to the prebuilt engine image and produce SM50.g3a.
usage: build-g3a.py engine.bin data.pack SM50.g3a [--name SM50]"""
import argparse, os, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("engine"); ap.add_argument("pack"); ap.add_argument("output")
ap.add_argument("--name", default="SM50")
a = ap.parse_args()
eng = open(a.engine, "rb").read()
pack = open(a.pack, "rb").read()
if pack[:4] != b"S50P":
    sys.exit("error: %s is not an SM50 pack" % a.pack)
body = eng + b"\0" * ((-len(eng)) % 16) + pack
limit = 16 << 20
if len(body) > limit:
    sys.exit("error: add-in would be %d bytes (limit %d)" % (len(body), limit))
tmp = a.output + ".bin"
open(tmp, "wb").write(body)
here = os.path.dirname(os.path.abspath(__file__))
r = subprocess.run([sys.executable, os.path.join(here, "mkg3a.py"), "-n", "basic:" + a.name, tmp, a.output])
os.remove(tmp)
if r.returncode:
    sys.exit(r.returncode)
print("add-in %s: engine %d + pack %d bytes" % (a.output, len(eng), len(pack)))
