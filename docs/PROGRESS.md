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
- The canonical SH4 probe now runs the decompilation's original star and
  yellow/red coin behavior scripts against Battlefield ROM placements. The
  emulator passes star collection, a ROM yellow-coin reward, and red-coin
  trigger advancement. It also collects all eight red coins via canonical
  interaction dispatch and confirms the original red-coin star-spawn behavior
  creates `bhvStarSpawnCoordinates` at the trigger's home position. Its live
  position and yaw are mirrored into a renderer-facing slot using the ROM's
  extracted star model. The canonical check advances the original spawn
  behavior until tangible, collects the star through Mario's interaction path,
  verifies the course star save bit, and confirms object/renderer removal. The
  red coin test can overlap placements, so its first reward check allows
  multiple coins per tick. The star frame itself has not been visually
  inspected and real-hardware behavior is untested. A two-frame emulator run
  estimated 54 FPS after startup; this is instruction-count math, not a
  calculator measurement.
- The existing synthetic regression suite passes 20/20 groups. Its PNG check
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
  now runs canonical `update_mario_inputs` for button/stick normalization,
  action timers and geometry-derived collision flags, then runs the canonical
  same-frame action-group transition loop for stationary, moving, airborne,
  submerged, automatic and object actions. Calculator movement reaches original `act_walking`,
  jump transitions run original `act_jump` and `perform_air_step`, and landing
  and freefall use their original handlers. A 120-frame input run completed
  without faults over the ROM collision mesh. Canonical animation IDs map to
  the eight Mario clips extracted from the ROM. Sound, camera and object
  interactions remain platform hooks; cutscene actions and actual object
  behavior are not yet connected.
- Canonical object-pool startup: the probe now uses the decompilation's
  `try_allocate_object` and player object list for Mario. Its original 608-byte
  `Object` layout is backed by a 64-slot static pool (38,912 bytes); the stock
  240-entry free-list initializer was adapted to seed that bounded pool. The
  120-frame ROM-backed jump/landing run still passes. Other ROM placements do
  not yet execute their behaviors.
- The PS1 port's float/Q12 bridge used bitfield ordering that returned zero on
  this big-endian SH4 target. An endian-safe local bridge fixes that issue;
  direct tests confirm Q12 158 round-trips and 0.5 converts correctly.
- The existing linker map limits `.data + .bss` to 320 KiB and reserves 192
  KiB for stack. Preview data+BSS uses 181,092 bytes; the PS1 Q12 probe uses
  203,692 bytes and the current input/collision probe with the native spawn
  table and canonical object pool uses 259,148 bytes. Its `.data` is 22,900
  bytes, `.bss` is 236,248 bytes, and text is 210,524
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
  measured 320 KiB static budget with 68,532 bytes left for game state after
  the object pool and its list-head reservation.
- The canonical `n64decomp/sm64` CC0 repository is pinned as a submodule at
  `9921382a`. Its stationary, moving, airborne, submerged, automatic and
  object action dispatchers, step, collision and math routines compile for SH4; animation
  requests route to ROM-backed clips. The emulator runs those dispatch groups
  against the actual ROM geometry. Cutscene actions, object interactions,
  object behavior runtime, level scripts and original camera are outstanding.

- Interaction-dispatch experiment: Mario's native tick now invokes the
  decompilation's `mario_process_interactions()` instead of calling the star
  handler directly. The original handler table and collision-object lookup
  compile for SH4; startup validation detects overlap with the ROM-placed star,
  collects it through the original dispatcher, checks the original save-bit
  path, and advances the original star-removal behavior. The emulator passes
  that end-to-end check and a 120-frame jump/landing run. Stubs remain for
  unsupported platform, cap, door and warp systems; ordinary level objects are
  not yet instantiated into the canonical pool, so this does not claim general
  object interaction support.

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

Native ROM-coin integration experiment: the canonical probe sources
Battlefield yellow-coin positions from the ROM-derived `OBJS` pack entry,
allocates canonical SM64 `Object` records, and runs the decompilation's
`bhvYellowCoin` script through its original behavior VM and coin callbacks.
The native tick updates these coin objects before canonical Mario collision
and interaction processing; deactivated coins are hidden in the
renderer-facing object list, and Mario's native coin total is published to
the HUD state. A startup regression places Mario on the first actual
ROM-derived coin and verifies the original hitbox, interaction dispatcher,
`numCoins` increment and coin deletion. The calculator emulator logs
`SM64 canonical ROM yellow coin PASS` and
`SM64 canonical ROM red coin PASS` while loading the canonical ROM pack. The
red-coin check instantiates the original hidden-star trigger and the ROM's red
coins, then verifies canonical Mario interaction increments the trigger and
awards the original coin value. A follow-up check collects all eight ROM coins
through the same dispatcher and confirms `bhvHiddenRedCoinStar` deactivates
after spawning the original `bhvStarSpawnCoordinates` object at its configured
home position. Its changing position and yaw are copied to an `OBJ_STAR` slot
that the existing renderer draws with the ROM-extracted star model. The test
advances the spawn script to tangible state, collects the star using original
Mario/object interaction, checks the expected save bit, and verifies both the
canonical object and renderer slot are removed. The spawned-star frame has not
been visually inspected, and physical-calculator behavior remains unverified.
The calculator emulator estimates about 54 FPS after startup for this probe,
based on instruction counts only.
Golden-coin sparkle behavior is compiled, while audio remains absent. This
code is currently built into the canonical probe variant; promoting the
native object runtime into the default add-in and replacing remaining
approximate objects are still required.

The ROM-backed emulator gate now depends on a fresh `make calc` build and
checks the VRAM canary. It had been packaging the checked-in `dist/SM50-engine.bin`
without rebuilding it, so its old 396x224 blitter still overwrote memory even
though the source had already been corrected to 384x216. Rebuilding and
packaging the current source passes the 40-frame run with the canary intact.

This is a successful native-code action-group and coin-behavior integration,
not yet the original game loop: most level objects and behavior scripts,
cutscene actions, complete level-script execution, original camera,
save/progression and audio are still missing.
The next integration step is to promote this canonical loop out of the probe
build, complete red-coin star progression, and add Battlefield enemy behaviors.
The existing ROM-backed renderer and flash-resident level data remain the
platform backend.
