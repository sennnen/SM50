# Super Mario 64 on the fx-CG50: native port in progress

The project is shifting from a ROM-rendered preview to a native recompilation
of Super Mario 64's original gameplay code. The existing renderer and ROM
pipeline remain in place as candidate backends. A SH4 experiment now compiles
and executes original-derived walking acceleration and Q12 slope movement
alongside ROM-derived Bob-omb Battlefield geometry. This is a narrow porting
probe, **not the full game**; see [docs/PROGRESS.md](docs/PROGRESS.md) and
[docs/NATIVE_RECOMPILATION.md](docs/NATIVE_RECOMPILATION.md).

The existing preview still renders actual Battlefield geometry, Mario meshes
and animations, and simplified coin/star/boss interactions. Its player movement,
camera and gameplay are approximations and are not the intended final game.

No ROM or extracted Nintendo assets are in this repository. The canonical US
ROM (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`) is needed locally.
The converter validates this hash before it creates a Battlefield pack.

## Build the current Battlefield preview on macOS

Connect the calculator in USB Flash mode, then run:

```sh
./install-sm50.command "/path/to/your/sm64.us.z64"
```

The installer uses `manifests/sm64-us-bob.json`, Python 3 and the committed
prebuilt engine. It creates `build/SM50.g3a` and copies it to the calculator.
It does not require a development toolchain. To build without copying, run:

```sh
python3 tools/n64pack.py manifests/sm64-us-bob.json --rom "/path/to/your/sm64.us.z64" -o build/us-bob.pack
python3 tools/build-g3a.py dist/SM50-engine.bin build/us-bob.pack build/SM50.g3a
```

Generated files stay in ignored `build/`. Never add your ROM, packs, screenshots
of ROM content or generated add-ins to Git.

## Developer verification

```sh
make host
SM64_ROM="/path/to/your/sm64.us.z64" make test-rom
```

Set `SM50EMU=/path/to/sm50emu` to include the calculator emulator in
`test-rom`. `make test` runs the 19 original synthetic groups. `make calc`
rebuilds the engine when an `sh-elf` toolchain is available.

To compile the native movement probe and run it with a local ROM in the
calculator emulator:

```sh
python3 tools/build_native_probe.py --rom "/path/to/your/sm64.us.z64"
```

The ROM converter and renderer are independently written. The canonical
[SM64 decompilation](https://github.com/n64decomp/sm64) is now the intended
game-logic source base; the local PS1 port is a technical reference. No ROM,
extracted assets or third-party game source are committed here.
