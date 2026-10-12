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
renderer, collision and a headless emulator, so it became the integration
base. The PS1 port remains a useful reference, but its unresolved build and
licensing questions made it a poor replacement for the calculator engine.

The next substantive work is fuller King Bob-omb behavior and enough scene/level scripting
to enter and complete that mission. Rendering fidelity and resolution should
be reduced as needed after measuring on hardware.
