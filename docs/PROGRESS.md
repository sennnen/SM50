# SM64 port progress — 12 October 2026

## Verified with the user's canonical US ROM

- SHA-1 and CIC-6102 header CRC match the canonical release.
- Bob-omb Battlefield segment 7 and its shared texture segments 3, 4 and 9
  decompress from the ROM. Six area display lists contain 1,101 triangles.
- The native collision stream at `0x0700E958` contributes 1,060 triangles.
  The host controller lands on and moves across this terrain.
- The native macro-object list provides five supported yellow coins and eight
  red coins. The native level script supplies the spawn warp, a directly placed
  star, the red-coin hidden-star trigger and King Bob-omb's placement. Collection
  of both coin types and both star paths was verified in the host runner. A
  simplified grab/throw encounter reveals the boss star after three hits;
  a deterministic host test checks the third hit.
- Twenty low-poly Mario geo joints and their textures are read from the ROM.
  Eight action animations are decoded from the native ROM animation table.
  The current controller chooses the actions; root translation and original
  animation timing are not yet reproduced.
- An 821 KB `.g3a` with real ROM-derived content passed the add-in checksum
  validator and ran for 80 frames in the headless CG50 emulator. Screenshots
  were visually inspected. The latest 80-frame run with native Mario poses and
  Battlefield terrain estimated about 59 FPS from instruction counts; a
  36-frame summit view with the boss estimated about 43 FPS. This is **not a measured
  frame rate on the physical calculator**.
- The existing synthetic regression suite passes 19/19 groups. Its PNG check
  now uses the Python standard library, so Pillow is unnecessary.

Run `SM64_ROM=/path/to/rom SM50EMU=/path/to/sm50emu make test-rom` for the
real-ROM integration checks. The test writes packs, screenshots and add-ins
only under ignored `build/`.

## Not complete

- The engine does not reproduce Mario's full movement state machine. Movement
  and camera are approximations, and the native animation poses need further
  visual tuning on the calculator screen.
- Most object behaviours are not implemented: other enemies, moving platforms,
  cannons, switches and warps. The King Bob-omb encounter has only a simplified
  grab/throw state machine and selected static ROM meshes, without native animations or
  exact throw rules. Five yellow coins, eight red coins and three star paths
  are active in this Battlefield preview.
- The converter handles one exact ROM revision and one level. It does not yet
  interpret arbitrary level scripts or geo layouts, nor does it stream later
  levels from storage.
- Hardware boot, key handling, memory headroom and real frame rate remain
  unverified on an actual fx-CG50.

## Codebase decisions

The `port/` Git checkout was made from the GitHub SM50 branch. The separate
local `sm64/` experiment, including its PS1 port and fx-CG50 renderer test,
was left intact. The SM50 engine already had a pack loader, a fixed-point
renderer, collision and a headless emulator, so it remains the integration
base. The game-logic direction is now native recompilation: port the canonical
decompilation's Mario/action/object/level code and keep this renderer behind a
backend boundary. The PS1 project is being used as a source-port and memory
optimization case study rather than copied as a whole.

## Native recompilation experiment — 12 October 2026

- The local PS1 port identifies itself as a fork of the full SM64 decompilation
  and keeps the original C gameplay/action organization while replacing
  platform-specific input, audio, display-list, render-graph and math paths.
- The PS1 port mixes fixed-point hot paths and float state; it does not simply
  convert all game math to integers. Its selective Q12 approach is more useful
  here than wholesale float replacement.
- Six original/PS1-source translation units compile for SH4. With function
  section garbage collection, the add-in retains and executes the original
  walking-speed update, slope acceleration, Q12 slide update and math bridge.
  The probe runs beside the real ROM-derived Battlefield renderer in the
  headless calculator emulator. After 30 ticks, walking speed is 98099/4096
  units and slope velocity is 4110/4096 units.
- The probe now asks the ROM-derived collision backend for the actual floor at
  Mario's Battlefield spawn point, converts its Q14 normal into the original
  SM64 `Surface` format, and runs that walking-speed update against it. The
  emulator reports floor Y 0, normal Y 16384 and speed 98099/4096 after 30
  ticks. This is a verified collision adapter seam, not the complete ground
  solver or action dispatcher.
- The PS1 port's float/Q12 bridge used bitfield ordering that returned zero on
  this big-endian SH4 target. An endian-safe local bridge fixes that issue;
  direct tests confirm Q12 158 round-trips and 0.5 converts correctly.
- The existing linker map limits `.data + .bss` to 320 KiB and reserves 192
  KiB for stack. Preview data+BSS uses 181,092 bytes; the PS1 Q12 probe uses
  203,692 bytes and the canonical probe uses 201,584 bytes. The emulator reports a separate
  256 KiB allocator arena, which is not yet confirmed on hardware.
- SH4 layout measurements from canonical decomp give `MarioState` 200 B,
  `Surface` 48 B and `Object` 608 B. Reducing its 200-entry pool to 64 saves
  82,688 bytes. (The PS1 fork's compressed object and surface are 548/40 B.)
  A new ROM-pack analyzer
  reproduces the original surface-cell boundary rules: Battlefield needs 1,060
  surfaces and 5,340 links (93,600 bytes), versus 158,400 bytes in the stock
  canonical reservations. Static partitions plus the 64-object pool fit the
  measured 320 KiB static budget with about 73 KiB left for game state.
- The canonical `n64decomp/sm64` CC0 repository is pinned as a submodule at
  `9921382a`. Its unmodified `mario_actions_moving.c` and `math_util.c` compile
  to SH4 and link into a separate add-in. `update_walking_speed` executes in
  the CG50 emulator against the real spawn floor and returns 98099/4096 after
  30 ticks, matching the PS1-port result. This confirms the canonical source,
  not only the PS1 fork, is a viable native starting point.

This is a successful native-code experiment, not yet an original game loop:
Mario's full action dispatcher, collision queries, object behavior scripts,
level scripts, camera, save/progression and animations are still not running
from original game code. The next integration step is the original per-frame
Mario/action dispatcher connected to ROM-derived collision surfaces. Then
bring up level/area scripts and object behaviors with bounded object pools and
flash-resident level/animation data. Keep the existing renderer as the backend.
