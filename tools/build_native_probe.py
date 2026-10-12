#!/usr/bin/env python3
"""Build and optionally run the native SM64 movement probe with a local ROM."""
import argparse
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(command, *, capture=False):
    return subprocess.run(command, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=Path, help="local US SM64 ROM")
    parser.add_argument("--skip-emulator", action="store_true")
    parser.add_argument("--frames", type=int, default=1)
    args = parser.parse_args()
    rom = args.rom.expanduser().resolve()
    if not rom.is_file():
        parser.error(f"ROM not found: {rom}")

    run(["make", "calc-native-probe"])
    out = ROOT / "build" / "native-sm64-probe"
    out.mkdir(parents=True, exist_ok=True)
    pack = out / "bob.pack"
    g3a = out / "SM64NativeProbe.g3a"
    run(["python3", "tools/n64pack.py", "manifests/sm64-us-bob.json",
         "--rom", str(rom), "-o", str(pack)])
    run(["python3", "tools/build-g3a.py", "build/calc/SM64NativeProbe.bin",
         str(pack), str(g3a), "--name", "SM64Probe"])
    print(f"Built local-ROM add-in: {g3a}")

    if not args.skip_emulator:
        emulator = Path(os.environ.get("SM50EMU", ROOT / "build" / "sm50emu"))
        if emulator.is_file():
            log = out / "emulator.log"
            result = run([str(emulator), "--max-frames", str(args.frames),
                          "--log", str(log), str(g3a)], capture=True)
            print(result.stdout, end="")
            if "SM64 Q12 PASS" not in result.stdout:
                raise SystemExit("native movement probe did not report PASS")
        else:
            print(f"Emulator not found at {emulator}; add-in was built but not run.")


if __name__ == "__main__":
    main()
