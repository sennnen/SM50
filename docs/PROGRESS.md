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
- The canonical probe now constructs original `Surface` records and 16x16
  linked partitions from the ROM's actual 1,060 Battlefield collision
  triangles. It executes canonical `find_floor`, wall and ceiling checks,
  `update_walking_speed`, and `perform_ground_step` for 30 consecutive ticks.
  Mario moves 413.1 units in the diagnostic. The probe resets him to the ROM's
  actual spawn and yaw with zero initial speed before runtime. Each game tick
  now calls canonical stationary, moving, airborne, submerged, automatic and
  object action dispatch. Calculator movement reaches original `act_walking`,
  jump transitions run original `act_jump` and `perform_air_step`, and landing
  and freefall use their original handlers. A 120-frame input run completed
  without faults over the ROM collision mesh. Canonical animation IDs map to
  the eight Mario clips extracted from the ROM. Sound, camera and object
  interactions remain platform hooks; cutscene actions and actual object
  behavior are not yet connected.
- The PS1 port's float/Q12 bridge used bitfield ordering that returned zero on
  this big-endian SH4 target. An endian-safe local bridge fixes that issue;
  direct tests confirm Q12 158 round-trips and 0.5 converts correctly.
- The existing linker map limits `.data + .bss` to 320 KiB and reserves 192
  KiB for stack. Preview data+BSS uses 181,092 bytes; the PS1 Q12 probe uses
  203,692 bytes and the current six-group action probe with the native spawn
  table uses 218,936 bytes. Its `.data` is 22,900 bytes, `.bss` is 196,036
  bytes, and text is 208,716
  bytes.
  The 120-frame emulator run used about 5 KiB of stack high-water, an emulator
  estimate. The canonical probe allocates 98,880 bytes for surfaces and 6,000
  links. The emulator reports a separate 256 KiB allocator arena, not
  confirmed on hardware.
- SH4 layout measurements from canonical decomp give `MarioState` 200 B,
  `Surface` 48 B and `Object` 608 B. Reducing its 200-entry pool to 64 saves
  82,688 bytes. (The PS1 fork's compressed object and surface are 548/40 B.)
  A new ROM-pack analyzer
  reproduces the original surface-cell boundary rules: Battlefield needs 1,060
  surfaces and 5,340 links (93,600 bytes), versus 158,400 bytes in the stock
  canonical reservations. Static partitions plus the 64-object pool fit the
  measured 320 KiB static budget with 69,832 bytes left for game state after
  the 64-object pool reservation.
- The canonical `n64decomp/sm64` CC0 repository is pinned as a submodule at
  `9921382a`. Its stationary, moving, airborne, submerged, automatic and
  object action dispatchers, step, collision and math routines compile for SH4; animation
  requests route to ROM-backed clips. The emulator runs those dispatch groups
  against the actual ROM geometry. Cutscene actions, object interactions,
  object behavior runtime, level scripts and original camera are outstanding.

- ROM-derived object handoff: the converter writes every one of Bob-omb
  Battlefield's 21 level-script placements to a new `SOBJ` pack entry, keeping
  the source model, transform, segmented behavior address and behavior
  parameters. The existing renderer-facing `OBJS` chunk remains compatible.
  The engine loads these into a bounded 64-record table and reports the count;
  canonical behavior execution is the next integration step.
- Object-runtime boundary experiment: canonical behavior bytecode compiles for
  SH4 (~22 KiB read-only), but selected scripts have link-time dependencies on
  original object callbacks, level collision symbols and animation assets.
  A behavior-address lookup by itself cannot execute these scripts safely; the
  interpreter, object setup and required callbacks need to be brought over
  together.

This is a successful native-code action-group integration, not yet the original
game loop: Mario object interactions, cutscene actions, behavior
scripts, level scripts, camera, save/progression and audio are still missing.
The next integration step is a bounded canonical object pool and behavior
runtime connected to Bob-omb Battlefield's parsed spawns, while retaining the
current ROM-backed renderer and flash-resident level data.
