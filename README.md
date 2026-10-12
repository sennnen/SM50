# SM64 on the fx-CG50: ROM-driven port in progress

This project has an original fixed-point calculator engine and a converter for
data in a user-supplied Super Mario 64 ROM. It now renders **actual Bob-omb
Battlefield geometry, low-poly Mario meshes and selected Mario animations from
the US ROM**. Its ROM-placed red coins reveal a hidden star when all eight are
collected. Movement and camera remain independently implemented approximations.
The complete game is **not yet playable**; see [docs/PROGRESS.md](docs/PROGRESS.md).

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

The converter, renderer and engine are independently written. The
[SM64 decompilation](https://github.com/n64decomp/sm64) and the local PS1
port were used as technical references for ROM formats and addresses; no
decompiled game source is included in this repository.
