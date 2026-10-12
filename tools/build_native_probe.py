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
    parser.add_argument("--source", choices=("ps1", "canonical"), default="ps1",
                        help="source project for original movement code")
    parser.add_argument("--canonical-source", type=Path,
                        help="canonical decomp checkout (default: deps/n64decomp-sm64)")
    parser.add_argument("--skip-emulator", action="store_true")
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--keys", type=Path,
                        help="optional calculator-emulator key script")
    args = parser.parse_args()
    rom = args.rom.expanduser().resolve()
    if not rom.is_file():
        parser.error(f"ROM not found: {rom}")

    out = ROOT / "build" / "native-sm64-probe"
    out.mkdir(parents=True, exist_ok=True)
    pack = out / "bob.pack"
    if args.source == "canonical":
        source = (args.canonical_source or ROOT / "deps" / "n64decomp-sm64").expanduser().resolve()
        if not (source / "src/game/mario_actions_moving.c").is_file():
            parser.error(f"canonical source checkout not found at {source}; initialize deps/n64decomp-sm64")
        name = "SM64CanonicalProbe"
        run(["make", "calc-canonical-probe", f"CANONICAL_SM64={source}"])
        marker = "SM64 canonical PASS"
    else:
        name = "SM64NativeProbe"
        run(["make", "calc-native-probe"])
        marker = "SM64 Q12 PASS"
    run(["python3", "tools/n64pack.py", "manifests/sm64-us-bob.json",
         "--rom", str(rom), "-o", str(pack)])
    run(["python3", "tools/analyze_sm64_ram.py", str(pack)])
    g3a = out / f"{name}.g3a"
    run(["python3", "tools/build-g3a.py", f"build/calc/{name}.bin",
         str(pack), str(g3a), "--name", "SM64Probe"])
    print(f"Built local-ROM add-in: {g3a}")

    if not args.skip_emulator:
        emulator = Path(os.environ.get("SM50EMU", ROOT / "build" / "sm50emu"))
        if emulator.is_file():
            log = out / "emulator.log"
            command = [str(emulator), "--max-frames", str(args.frames)]
            if args.keys:
                keys = args.keys.expanduser().resolve()
                if not keys.is_file():
                    parser.error(f"key script not found: {keys}")
                command.extend(["--keys", str(keys)])
            command.extend(["--log", str(log), str(g3a)])
            result = run(command, capture=True)
            print(result.stdout, end="")
            if marker not in result.stdout:
                raise SystemExit("native movement probe did not report PASS")
        else:
            print(f"Emulator not found at {emulator}; add-in was built but not run.")


if __name__ == "__main__":
    main()
