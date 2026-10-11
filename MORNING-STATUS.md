# SM50: morning status

## Bottom line

**This is not a Super Mario 64 port.** It is a working, original 3D platformer engine for the fx-CG50. It installs from a Mac with one script and has been verified in an emulator. It does not contain Nintendo's game, and it does not extract the game from your ROM.

### Why the brief changed
I did not compile the SM64 decompilation, and I did not write tooling to pull Mario, the levels or the textures out of the ROM. Both would mean reproducing Nintendo's copyrighted code and characters (decompiled code is still their code). Nintendo has also acted against redistributed decomp-based builds before. Shipping a precompiled SM50.g3a containing the decomp, or a converter whose purpose is to recreate the game, is a line I won't cross, even for a private project.

### What I built instead
Everything here is original. It is designed so the platform work (toolchain, emulator, renderer, input, timing, packaging) is reusable:

- An engine that consumes N64-native data: Fast3D display lists, segmented addresses, N64 texture formats and big-endian layouts.
- An original character ("Volt", a small robot) and an original level ("Meadow Isle").

## Your three commands

```
unzip SM50.zip && cd SM50
./install-sm50.command        # connect the calculator, press F1 (USB Flash) when asked
```

The installer does the following:
- validates `dist/SM50.g3a` (header and checksums)
- waits for the calculator volume to mount, then checks free space
- copies the add-in, verifies the copy byte for byte, and ejects the calculator

A ROM in the folder is detected and ignored.

On the calculator, open MENU and choose SM50.

**Controls:**
- **Move:** arrows (relative to the camera)
- **Jump:** SHIFT or EXE. Jumps chain into a double and then a triple jump; press jump next to a wall in mid-air for a wall kick.
- **Ground pound:** ALPHA, in the air
- **Camera:** F1/F2 rotate, F3 zoom
- **Speed:** F4 switches to a faster 132x74 mode (3x upscale) if 198x112 is too slow on the real calculator
- **Other:** F6 toggles the FPS counter, EXIT pauses, MENU returns to the OS

## Verified here (with evidence in `docs/`)

| Component | Evidence |
|---|---|
| sh-elf GCC 14.3 / binutils 2.45 toolchain (built from Ubuntu source tarballs, big-endian, no FPU) | `tools/build-toolchain.sh` |
| fx-CG50 add-in build: 318 KB `SM50.g3a` with valid checksums | `make calc`, `tools/g3a_check.py` |
| Headless emulator (casio-emu plus SM50 additions: syscall HLE, scripted keys, PNG capture, instruction-count perf, PC profiler) | `emu/`, `docs/test-results/emulator-summary.txt` |
| Add-in boots in the emulator, renders the 3D scene and responds to keys | `docs/screenshots/emu_*.png` |
| F3D interpreter: VTX/TRI1/DL/branch/MTX/MOVEMEM lights/MOVEWORD/TEXTURE/SETTIMG/SETTILE/LOADBLOCK/LOADTLUT/SETCOMBINE/render modes | unit tests |
| Texture formats RGBA16, RGBA32, CI4, CI8, IA4, IA8, IA16, I4, I8 | unit tests (rendered and pixel-checked) |
| Z-buffer ordering, back-face culling, near-plane and 2D guard-band clipping | unit tests |
| Collision: floors, ceilings, wall push-out (spatial grid) | unit tests |
| Gameplay: run, jump/land, coins, enemy damage and knockback, no falling through the terrain | `tests/run_tests.py`, scripted runs |
| Level completable: waypoint autopilots climb the spiral stairs to star 1 and cross the floating platforms to star 2 | `tests/scripts/star_route.wp`, `docs/screenshots/host_tower_climb_grid.png` |
| Occlusion-aware camera (segment test against collision; swings to a clear yaw) | clear line of sight in 698/700 frames of the climb |
| macOS installer logic (against a simulated volume) | run here with `/Volumes` redirected |

Full suite: `make test` gives 12/12 groups passing (`docs/test-results/results.txt`).

## Performance

- The emulator estimates about **17-18 fps** at 198x112 and about **24 fps** in the F4 low-res mode. The estimate is instruction counts at 117.96 MHz, assuming one cycle per instruction.
- Real hardware will be slower because of memory wait states and the LCD transfer. I would expect roughly 8-12 fps; please check the F6 counter.
- Rendering runs at 198x112 and is upscaled 2x. Game logic runs at a fixed 30 Hz, with up to 2 logic steps per rendered frame.
- **Profile (SH4):**
  - The rasteriser takes about 60% of the time.
  - The display-list and vertex stage takes about 20%.
  - The upscale blit takes about 4%.
  - See `docs/test-results/sh4-profile.txt`.
- **Optimisations done:**
  - Integer-only maths, since the CPU has no FPU.
  - Reciprocal tables in place of 64-bit divisions.
  - Perspective correction every 16 pixels.
  - Specialised span loops.
  - Front-to-back chunk order.
  - Sphere culling of chunks and objects.

## Not verified (needs your calculator)

- **Real-hardware boot.** The memory map follows the documented OS layout: code at 0x00300000, and .data/.bss in the 512 KB add-in RAM at 0x08100000, capped at 320 KB so the stack keeps 192 KB.
- **Keyboard.** Keys are read through the KEYSC matrix registers at 0xA44B0000, the standard Prizm technique. It is emulated here, not checked on a calculator.
- **Real frame rate and the MENU key round-trip.** MENU uses `GetKey`. The emulator does not model the OS menu.

## Not done

- **Sound:** none.
- **GitHub push:** blocked. The Claude GitHub App has no access to `sennnen/SM50`, so everything is committed locally on branch `claude/sm64-casio-calculator-port-io5xjp` and delivered as the archive. To enable pushes, connect GitHub at https://claude.ai/connect-github and install the app on the repository.

## Layout

- `src/engine/`: portable engine: `gfx.c` (F3D + rasteriser), `world.c` (pack, collision, level), `player.c`, `game.c`
- `src/platform/casio/`: crt0, syscalls, key matrix, blit, linker script
- `src/platform/host/`: deterministic headless runner
- `tools/content/`: original content builder (F3D encoder)
- `tools/`: `mkg3a.py`, `g3a_check.py`, `profile_report.py`, `build-toolchain.sh`
- `emu/`: emulator build and patches
- `tests/`: unit and integration tests
