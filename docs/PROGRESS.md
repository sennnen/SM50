# SM64 port progress — 12 October 2026

## Verified with the user's canonical US ROM

- SHA-1 and CIC-6102 header CRC match the canonical release.
- Bob-omb Battlefield segment 7 and its shared texture segments 3, 4 and 9
  decompress from the ROM. Six area display lists contain 1,101 triangles.
- The native collision stream at `0x0700E958` contributes 1,060 triangles.
  The host controller lands on and moves across this terrain.
- The native macro-object list provides five supported yellow coins. The
  native level script supplies the spawn warp and one directly placed star.
  Both coin and star collection were verified in the host runner.
- Fifteen low-poly Mario mesh parts and their textures are read from the ROM.
  Eight small pose sequences are generated independently by the converter.
- A 723 KB `.g3a` with real ROM-derived content passed the add-in checksum
  validator and ran for 80 frames in the headless CG50 emulator. Screenshots
  were visually inspected. The latest run with Mario and Battlefield terrain
  estimated about 29 FPS from instruction counts. This is **not a measured
  frame rate on the physical calculator**.
- The existing synthetic regression suite passes 19/19 groups. Its PNG check
  now uses the Python standard library, so Pillow is unnecessary.

Run `SM64_ROM=/path/to/rom SM50EMU=/path/to/sm50emu make test-rom` for the
real-ROM integration checks. The test writes packs, screenshots and add-ins
only under ignored `build/`.

## Not complete

- The engine does not interpret native SM64 animations or reproduce Mario's
  full movement state machine. Its current movement, camera and poses are
  approximations.
- Most object behaviours are not implemented: enemies, the King Bob-omb
  fight, red coin activation, moving platforms, cannons, switches and warps.
  Only five yellow coins and the directly placed star are active in this
  Battlefield preview.
- The converter handles one exact ROM revision and one level. It does not yet
  interpret arbitrary level scripts or geo layouts, nor does it stream later
  levels from storage.
- Hardware boot, key handling, memory headroom and real frame rate remain
  unverified on an actual fx-CG50.

## Codebase decisions

The `port/` Git checkout was made from the GitHub SM50 branch. The separate
local `sm64/` experiment, including its PS1 port and fx-CG50 renderer test,
was left intact. The SM50 engine already had a pack loader, a fixed-point
renderer, collision and a headless emulator, so it became the integration
base. The PS1 port remains a useful reference, but its unresolved build and
licensing questions made it a poor replacement for the calculator engine.

The next substantive work is native Mario animation decoding, object and
mission state for the first Battlefield star, and enough scene/level scripting
to enter and complete that mission. Rendering fidelity and resolution should
be reduced as needed after measuring on hardware.
