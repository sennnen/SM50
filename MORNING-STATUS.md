# SM50 status (archived snapshot before ROM integration)

For current results, see `docs/PROGRESS.md`.

## What works (verified, `make test`: 19/19 groups)

**ROM pipeline (generic N64, original code, no dependencies beyond Python 3):**

| Stage | Tool | Verified by |
|---|---|---|
| Byte order (.z64 / .v64 / .n64) to big-endian | `tools/n64/rom.py` | all three orders give byte-identical packs |
| Header parse, CIC-6102 CRC check, SHA-1 | `tools/n64info.py` | CRC matches, corruption detected |
| MIO0 decompression (plus encoder for fixtures, block scan) | `tools/n64/mio0.py` | round trip, corrupt-header rejection |
| Segmented address space (segments `0x00`-`0x1F`) | `tools/n64/segments.py`, engine `gfx_set_segment` | cross-segment texture references render |
| F3D display-list walking (nested `G_DL`, branches, vertex cache) | `tools/n64/f3d.py` | exact triangle counts |
| Bounds and culling spheres, collision from display-list triangles | `tools/n64pack.py` | player walks on ROM-derived geometry |
| N64 textures: RGBA16/32, CI4/8, IA4/8/16, I4/8 (and PNG export) | engine `gfx.c`, `tools/n64/tex.py` | per-format pixel tests |
| Translucent layer, per-chunk translation | `LVL2` pack entries | rendered |
| Pack appended to the prebuilt engine image, no recompile | `tools/build-g3a.py`, `dist/SM50-engine.bin` | boots in the CG50 emulator |

**Calculator engine:**
- Perspective-correct, z-buffered F3D rasteriser using integer maths only.
- Collision grid, a third-person controller, and an occlusion-aware camera.
- Rendering at 198x112; F4 switches to 132x74.
- Estimated speed in the emulator: 18 fps on the original test level and 42 fps on the synthetic ROM scene. These are instruction-count estimates; real hardware will be slower.

**Tooling:** sh-elf GCC 14.3 toolchain, headless emulator (`emu/`) with syscall emulation, scripted keys, screenshots and a PC profiler, and the macOS installer.

## Install flow

```
./install-sm50.command [rom] [rom-manifest.json]
```

1. Validates the ROM.
2. Converts it with the manifest (format: `python3 tools/n64pack.py --help`).
3. Attaches the result to `dist/SM50-engine.bin`.
4. Copies the add-in to the calculator and verifies the copy.

This has been tested end to end with `tests/fixtures/synthetic.*` (all byte orders) against a simulated calculator volume.

## Remaining gap for Super Mario 64

SM50 includes no manifest or interpreter for Super Mario 64's own data structures, and I won't be writing that part. Specifically, it lacks:
- level scripts
- geo layouts
- the SM64 collision and animation formats
- object behaviours
- Mario's model and controller

With your ROM and no manifest, the installer validates the ROM and then stops with an explanation.

## Not verified (needs the physical calculator)

- Boot on real hardware.
- Key matrix reads at 0xA44B0000.
- The real frame rate.
- The MENU round trip through `GetKey`.

GitHub push is still refused (403: the Claude GitHub App has no access to `sennnen/SM50`). All work is committed locally on `claude/sm64-casio-calculator-port-io5xjp` and delivered as the archive.

## Layout

- `tools/n64/`: ROM, MIO0, segment, F3D and texture modules
- `tools/n64pack.py`, `tools/n64info.py`, `tools/build-g3a.py`, `tools/mkg3a.py`, `tools/g3a_check.py`
- `src/engine/`: renderer, collision, controller and pack loader
- `src/platform/casio/`, `src/platform/host/`
- `emu/`, `tests/` (synthetic ROM generator in `tests/make_synthetic_rom.py`)
- The original test level and character (`tools/content/`) remain only as test fixtures; they are no longer the default add-in.
