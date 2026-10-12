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

The two global surface partitions add 12,288 bytes of BSS. The canonical probe
currently uses 201,584 bytes of static data+BSS. Combining those partitions,
the probe, and a 64-object pool would use about 253 KiB of the 320 KiB static
allowance, leaving roughly 73 KiB for remaining gameplay globals and native
tables. Level-sized surfaces would use 93,600 bytes from the separate game
heap; its real capacity still needs measurement on hardware. This is viable
for a first level, but must be rechecked against the complete port. The link
arrays also need headroom for dynamic surfaces and alternate levels, so 5,340
is the measured Battlefield static-collision count only.

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

The probe now also calls the existing ROM collision backend at Battlefield's
spawn point, converts its floor normal into the PS1/original `Surface` layout,
and runs the original walking-speed update on that real surface. The emulator
reports floor height 0, normal Y 16384 (Q14), and 30-tick walking speed
98099/4096. This validates one narrow backend boundary; it is not yet the
original ground-step solver or full walking action.

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

The ROM-derived pack, add-in and logs are written under ignored `build/`.

## Next integration boundary

The experiment proves that original-derived movement routines and Q12 math can
be linked into the existing SH4 add-in, and that a real ROM floor can be
presented to the original surface ABI. It does not run the full Mario action
dispatcher, collision solver, object behavior VM, level scripts or original
camera. The next port layer should bring in the canonical Mario state/action
dispatcher and per-frame update, then replace this single floor-query adapter
with the complete `Surface` partition and ground-step interfaces. After that,
import behavior scripts and the bounded subset of level/area execution needed
for Battlefield. Keep rendering as a backend that consumes the resulting
original game state and ROM geometry.
