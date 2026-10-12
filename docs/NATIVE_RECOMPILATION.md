# Native SM64 recompilation investigation

## Direction

Use the decompiled game's C gameplay code as the simulation layer. Keep the
existing ROM-derived content converter, SH4 renderer, add-in packaging,
installer and emulator harness. Replace the calculator-specific player and
object simulation incrementally with original SM64 state/action and behavior
code; do not add new approximations to those systems.

The PS1 project is a useful source-port case study, not a drop-in calculator
port. Its README identifies it as a fork of `n64decomp/sm64`, targeting PS1 and
PC only. It retains the original C game/action organization while replacing
platform services, much of the render graph, audio, input and display-list
handling. It selectively adds Q12 fixed-point vectors and math, retains float
fields and a soft-float implementation, compresses Mario animations, quantizes
textures and preprocesses N64 display lists for PS1 rendering. It documents
levels that still fail from memory pressure and asset-load stalls, so the PS1
results establish feasibility of reuse, not a ready-made memory/performance
solution.

The source checkout at `../psx2` has no root license file and contains local
uncommitted renderer/build changes. Keep it as a local reference. The canonical
decompilation is pinned as the `deps/n64decomp-sm64` submodule at commit
`9921382a68bb0c865e5e45eb594d9c64db59b1af`; that repository identifies its
license as CC0. This is the selected source base. The PS1 port remains useful
for studying its Q12 math, asset compression and renderer adaptations, but its
different data layouts and platform code should not be the foundation for the
calculator's game-logic build.

`libsm64` is another CC0 derivative with a clean external API, but its stated
scope is Mario movement and rendering as a library. It does not provide the
full area scripts, object behavior execution, star progression or original
game loop needed by this project, so importing it alone would miss the main
goal. Reusing canonical decomp source gives us those systems as they are
ported, while the SM50 renderer remains the graphics backend.

## Calculator budgets measured in this checkout

`src/platform/casio/prizm.ld` maps code at `0x00300000` and provides 512 KiB of
app RAM at `0x08100000`; this project reserves 192 KiB for stack and caps
`.data + .bss` at 320 KiB. The product's 16 MB figure is user storage/flash,
not equivalent to writable runtime RAM. The content pack is appended to the
add-in and read in place; the current Battlefield pack is 676,436 bytes, while
the game-state and renderer buffers occupy RAM.

| Build | text | data | bss | RAM data+bss |
| --- | ---: | ---: | ---: | ---: |
| Existing preview | 116,056 B | 48 B | 181,044 B | 181,092 B |
| Preview + native probe | 127,720 B | 22,580 B | 181,100 B | 203,680 B |
| Canonical ground-step probe | 133,644 B | 22,628 B | 193,420 B | 216,048 B |
| Canonical input/collision + spawn table + 64 canonical objects | 210,524 B | 22,900 B | 236,248 B | 259,148 B |

The probe adds 22,532 bytes of initialized data, principally the Q12
sine/arctangent tables. This leaves about 124 KiB inside the current static
RAM ceiling, before accounting for stack. The emulator reports a separate
256 KiB `sys_malloc` arena and 5 KiB stack high-water for the one-frame probe;
that emulator heap figure is not a measurement of available real-hardware
heap. Keep level/animation assets in flash and load only active-level data and
bounded object pools into RAM. The renderer and existing world state already
consume most of the safe static allocation.

## Measured SM64 state and collision pools

Cross-compiling canonical `struct MarioState`, `Surface`, and `Object` for SH4
gives 200, 48, and 608 bytes respectively. The 200-entry canonical object pool
would consume 121,600 bytes of static RAM; 64 entries consume 38,912 bytes and
save 82,688 bytes. This is a level/gameplay tradeoff, not a free optimization:
retain important actors and prioritize discarding effects or unimportant
objects when the pool is full. The PS1 fork compresses these layouts; its
smaller `Object` (548 B) and `Surface` (40 B) sizes must not be used to budget
the canonical build.

The stock surface loader reserves 6,000 eight-byte `SurfaceNode`s and 2,300
48-byte canonical `Surface`s, totaling 158,400 bytes per level. The new
`tools/analyze_sm64_ram.py` applies the original game's 16-by-16 partition and
50-unit cell-boundary rules to the ROM-derived Battlefield collision stream.
It reports 1,060 surfaces, 5,340 cell links, 256 occupied cells, and a maximum
of 99 links in one cell. Exact-sized canonical arrays use 93,600 bytes,
64,800 bytes less than the stock reservations. Run it against a generated pack:

```sh
python3 tools/analyze_sm64_ram.py build/native-sm64-probe/bob.pack
```

The canonical input/collision probe, 64-entry native spawn table, original
64-entry `Object` pool, and object-list heads use 259,148 bytes of static
data+BSS, leaving 68,532 bytes within the 320 KiB allowance for more gameplay
state. Mario now occupies a slot allocated by the decompilation's original
`try_allocate_object` and resides in the original player object list. The stock
free-list initializer assumes 240 entries, so this probe seeds the free list
for the reduced pool. The canonical surface and link arrays use 93,600 bytes
of the separate allocator; the probe reserves 6,000 links, so the emulator
reports 98,880 bytes allocated. Its 256 KiB heap is an emulator measurement
only; physical calculator heap capacity remains unknown.
The link arrays need headroom for dynamic surfaces and alternate levels, so
5,340 is only the measured Battlefield static-collision count.

## fx-CG50 RAM and clock headroom

The 320 KiB `.data + .bss` limit is a project guardrail, not a measurement of
all SDRAM available to an add-in. The ordinary add-in image and the current
startup/stack arrangement use the `0x08100000` window. The calculator's
separate SDRAM map starts at `0x8c000000`; it is not made available to
`sys_malloc` merely because physical RAM exists there. The emulator's
256 KiB heap at `0x8c300000` is its own test arena and says nothing about the
calculator's OS heap or ownership of that address on hardware.

Community reverse-engineering reports that roughly 6 MiB beginning at
`0x8c200000` is often usable by CG50 add-ins. The same reports warn that the
region is outside the normal OS allocator, that future OS versions could use
it, and recommend checking that it is zero before writing. The built-in
Python environment has reportedly used part of this region. Treat this as
promising, version-dependent capacity, not guaranteed free memory. See
[WikiPrizm's process and heap notes](https://cemetech.gitlab.io/wikiprizm/OS_Information/Processes/)
and the [developer discussion of the 8 MiB RAM map](https://www.cemetech.net/forum/viewtopic.php?t=17679).

`make calc-memory-diagnostic` builds a standalone `SM50MemoryDiagnostic.g3a`.
It measures normal OS allocations in 4 KiB blocks, reads the CPG frequency
register, and measures the framebuffer blit plus OS display call. F1 performs
a read-only zero scan of `0x8c200000..0x8c7fffff`; it only proceeds to sparse
write/read/restore samples if the whole range reads as zero. It never changes
clock registers. This is a cautious first probe, not proof that every byte is
available or safe across OS versions. Back up the calculator before running
it. It has been built and run through the software emulator's normal path;
the emulator does not emulate this extended RAM region or actual clocks, so
hardware results remain unmeasured.

The stock clock reference lists F1 as 116 MHz CPU / 58 MHz bus, F4 as 232 /
58 MHz, and F5 as 189 / 94 MHz on the CG50. These are community clock presets,
not measurements from this project. The bus-limited F5 preset may outperform
F4 for framebuffer and SDRAM traffic; the compute-heavy renderer may favor
F4. We will compare both only after the calculator diagnostic can report
real-device results. The register read is diagnostic evidence; this build
does not switch clock modes. See [gint's CG50 clock reference](https://classpaddev.github.io/wiki/gint/reference/kernel/clock/).

gint provides established clock, timer, keyboard, display, and custom-heap
interfaces, but it does not add CPU frequency or physical RAM. The project
toolchain has fxSDK support but no installed `libgint-cg`, so a gint build
comparison is not currently available locally. The current direct syscall
platform is already small; a migration is justified only if a gint prototype
measures a concrete memory, display, or timing benefit. Neither path changes
the SH4 no-FPU constraint: floating-point still needs software support, while
fixed-point is appropriate for hot movement/rendering paths.

## SH4 experiment

`make calc-native-probe` compiles PS1-port game source as SH4 big-endian,
`-m4-nofpu` code. Function-section garbage collection retains the original
`update_walking_speed`, `apply_slope_accel` and Q12 `update_sliding_angleq`
paths without pulling in the PS1 renderer. The probe invokes 30 ticks of the
original walking-speed update and 30 ticks of slope sliding, then the ordinary
SM50 add-in loads and renders Bob-omb Battlefield from the supplied ROM.

The first run found that the PS1 port's bitfield-based float/Q12 conversion
does not work on this SH4 ABI: Q12 2048 converted to float zero. The local
`ps1_softfloat_bridge.c` now decodes IEEE-754 fields using integer bit masks;
the emulator confirms Q12 158 round-trips, slope velocity reaches 4110/4096,
and walking velocity reaches 98099/4096 after 30 ticks. Emulator throughput
is only an instruction-count estimate and has not been measured on calculator
hardware.

The canonical probe builds original 48-byte `Surface` structures and 16-by-16
linked partitions from the ROM's 1,060 collision triangles. It runs the
decompilation's own `find_floor`, wall and ceiling collision queries,
`update_walking_speed`, and `perform_ground_step` for 30 consecutive ticks on
that geometry. The emulator reports floor height 0, normal Y 16384 (Q14),
walking speed 98099/4096, and 413.1 units of diagnostic forward movement. The
probe then resets Mario to the ROM's spawn position, yaw and zero velocity so
the diagnostic does not affect play. Each game tick runs canonical
`update_mario_inputs`: button edges and timers, stick magnitude/yaw, plus
floor, wall, ceiling, water, gas and off-floor input flags from ROM collision.
The canonical action-group loop also follows same-frame transitions through
stationary, moving, airborne, submerged, automatic and object actions.
Movement reaches `act_walking`; jumping reaches `set_mario_action`,
`act_jump`, `common_air_action_step`, `perform_air_step` and original landing
actions over the ROM-derived surfaces.

The eight animation IDs already extracted from the ROM are mapped from
canonical animation requests to the renderer's idle, walk, run, jump, fall,
land, flip and hurt clips. Audio and camera remain platform stubs. Native
object interactions are also still stubbed, and cutscene actions are not
dispatched yet. A scripted run exercised a jump, movement and a double jump
from the landing action. The 120-frame run completed without faults and
rendered the ROM's Battlefield geometry and Mario. Its
roughly 49 FPS instruction-count estimate is not a calculator measurement.
The probe uses adapters for renderer-independent globals and unsupported game
systems.

Build and run this ROM-local experiment with:

```sh
python3 tools/build_native_probe.py --rom "/path/to/your/SM64 ROM.z64"
```

To build and run the canonical decompilation's float-based walking-speed
routine instead of the PS1 fork's Q12 port, initialize the submodule and use:

```sh
git submodule update --init deps/n64decomp-sm64
python3 tools/build_native_probe.py --source canonical --rom "/path/to/your/SM64 ROM.z64"
```

To exercise the original jump/landing slice with scripted calculator input:

```sh
python3 tools/build_native_probe.py --source canonical --frames 120 \
  --keys tests/inputs/canonical-double-jump.keys --rom "/path/to/your/SM64 ROM.z64"
```

The ROM-derived pack, add-in and logs are written under ignored `build/`.

The converter also preserves all 21 original Battlefield level-script
placements in an `SOBJ` chunk, including models, full transforms, behavior
segment addresses and behavior parameters. The existing compact `OBJS` chunk
remains unchanged for the renderer and legacy engine. `level_load` retains the
raw records in a bounded 64-entry table; the canonical behavior interpreter
does not consume them yet. This prevents the current three-entry compatibility
mapping from discarding data needed by the native object runtime.

The canonical `data/behavior_data.c` compiles for SH4 to about 22 KiB of
read-only bytecode, but it does not link in isolation: selected Battlefield
scripts carry relocations to native object callbacks, level collision symbols,
and animation data from the original engine. Thus resolving the ROM's segment
addresses to C array names is insufficient. The next object-runtime slice must
port or adapt the interpreter, initialize canonical `Object` state, and connect
the relevant native callbacks and asset references as a coherent subset before
any of those scripts can safely execute.

## Next integration boundary

The experiment now runs canonical stationary, moving, airborne, submerged,
automatic and object action dispatch with the decompilation's floor, wall and
ceiling queries against ROM collision. Mario's native tick calls the original
`mario_process_interactions()` dispatcher; a startup self-check collides with
the actual ROM-placed Battlefield star, exercises the canonical save-bit and
star-dance path, then verifies the original star deletion behavior. This
proves the handler table can run natively with the current adapters, but the
other 20 level-script placements and macro objects are not yet initialized in
the canonical pool. Unsupported cap, warp, save and sound dependencies use
backend stubs, and the fatal `abort()` path remains guarded. The cutscene action
group, general object behaviors, level scripts, original camera, audio and
persistent save progression remain unconnected. The next port layer should
initialize retained Battlefield spawns in the bounded canonical pool, run their
canonical behavior scripts, and route collision results through the original
dispatcher. Keep rendering
as a backend that consumes original game state and ROM geometry.

## PS1 port comparison and object-runtime link test

The local `../psx2` checkout is a useful engineering reference, not a drop-in
port layer. Its README says it forked the decompilation and then became PS1/PC
only; it adapts large portions of game math to fixed-point vectors/matrices,
keeps low-precision soft-float for remaining float code, rewrites the render
graph walker, and compiles RSP display lists to a compact PS1 format. It also
compresses Mario animation data from 580,632 to 190,324 bytes and places it in
VRAM. Its own notes report level-load crashes and recommend 8 MiB in emulators
to reduce crashes; retail PS1 has 2 MiB main RAM. Those graphics, VRAM, and
memory assumptions do not transfer to the calculator. The useful lesson is to
keep original gameplay code while replacing math/render/audio/platform seams,
not to adopt the PS1 renderer or its global memory layout. The PS1 fork's local
worktree also contains unrelated uncommitted edits; it has been left untouched.

On this SH4 toolchain, canonical `behavior_script.c` compiles to 4,524 B of
text, and `data/behavior_data.c` compiles to 22,172 B of script data. A first
whole-source link experiment that rooted `cur_obj_update` and `bhvStar` did not
link: the command table retains engine hooks, and the behavior source units
expose callbacks and data references for camera, dialog, sound, animation, and
level assets that this backend does not define. The experiment was removed
rather than leaving a broken build target. This does not make the native route
infeasible; it identifies the next seam precisely. A working object runtime
needs a generated behavior-address catalog and a linkable subset of canonical
scripts/callbacks, with real platform hooks for the subset being enabled.
Linking the complete behavior and object-helper translation units as a single
unit is too broad for the current backend. No gameplay credit is assigned to
this compile-only result.
