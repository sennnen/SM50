# Super Mario 64 on the fx-CG50: native port in progress

SM50 now has a native SH4 game build that runs the canonical SM64 Mario action
loop against ROM-derived Bob-omb Battlefield collision and placements. The
existing renderer consumes that state. The current port is playable in this
one level, but it is still a partial port: most enemies, level transitions,
the original camera, audio and save progression are not connected. See
[docs/PROGRESS.md](docs/PROGRESS.md) and
[docs/NATIVE_RECOMPILATION.md](docs/NATIVE_RECOMPILATION.md).

The existing preview still renders actual Battlefield geometry, Mario meshes
and animations, and simplified coin/star/boss interactions. Its player movement,
camera and gameplay are approximations and are not the intended final game.

No ROM or extracted Nintendo assets are in this repository. The canonical US
ROM (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`) is needed locally.
The converter validates this hash before it creates a Battlefield pack.

## Build the current native Battlefield game

With the SH4 toolchain and canonical decomp checkout available, run:

```sh
python3 tools/build_sm64_game.py --rom "/path/to/your/sm64.us.z64"
```

This creates `build/native-sm64-game/SM50.g3a` from your local ROM. To also
run it in the headless calculator emulator:

```sh
python3 tools/build_sm64_game.py --rom "/path/to/your/sm64.us.z64" \
  --emulator "/path/to/sm50emu" --frames 100
```

Copy the resulting `.g3a` to the calculator's storage while it is in USB Flash
mode. The older `install-sm50.command` still builds the renderer-only preview.

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
