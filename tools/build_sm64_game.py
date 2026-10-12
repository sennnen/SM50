#!/usr/bin/env python3
"""Build a playable canonical-SM64 calculator add-in from a local ROM.

The ROM and extracted pack stay under the ignored build/ directory. Only the
independently built engine source belongs in version control.
"""
import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(command, *, capture=False):
    return subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path, help="local US SM64 ROM")
    parser.add_argument("--canonical-source", type=Path,
                        help="canonical decomp checkout (default: deps/n64decomp-sm64)")
    parser.add_argument("--output", type=Path,
                        help="output .g3a path (default: build/native-sm64-game/SM50.g3a)")
    parser.add_argument("--emulator", type=Path,
                        help="optionally run the finished add-in in a CG50 emulator")
    parser.add_argument("--frames", type=int, default=100)
    parser.add_argument("--keys", type=Path, help="optional emulator key script")
    args = parser.parse_args()

    rom = args.rom.expanduser().resolve()
    if not rom.is_file():
        parser.error(f"ROM not found: {rom}")
    source = (args.canonical_source or ROOT / "deps" / "n64decomp-sm64").expanduser().resolve()
    if not (source / "src/game/mario_actions_moving.c").is_file():
        parser.error(f"canonical source checkout not found at {source}")
    out = (args.output or ROOT / "build/native-sm64-game/SM50.g3a").expanduser()
    if not out.is_absolute():
        out = ROOT / out
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    work = ROOT / "build/native-sm64-game"
    work.mkdir(parents=True, exist_ok=True)
    pack = work / "bob.pack"

    run(["make", "calc-canonical-game", f"CANONICAL_SM64={source}"])
    run(["python3", "tools/n64pack.py", "manifests/sm64-us-bob.json",
         "--rom", str(rom), "-o", str(pack)])
    run(["python3", "tools/analyze_sm64_ram.py", str(pack)])
    run(["python3", "tools/build-g3a.py", "build/calc/SM50.bin",
         str(pack), str(out), "--name", "SM50"])
    run(["python3", "tools/g3a_check.py", str(out)])

    if args.emulator:
        emulator = args.emulator.expanduser().resolve()
        command = [str(emulator), "--max-frames", str(args.frames)]
        if args.keys:
            command.extend(["--keys", str(args.keys.expanduser().resolve())])
        command.append(str(out))
        result = run(command, capture=True)
        print(result.stdout, end="")
        if f"frames presented  : {args.frames}" not in result.stdout:
            raise SystemExit("emulator did not present the requested frame count")
        if "OS VRAM guard     : 0/4096 guard words corrupted" not in result.stdout:
            raise SystemExit("emulator detected an OS VRAM overwrite")
    print(f"Built playable native-SM64 add-in: {out}")


if __name__ == "__main__":
    main()
