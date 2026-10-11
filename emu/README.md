# sm50emu: headless fx-CG50 test emulator

`sm50emu` runs SM50 `.g3a` add-ins without a calculator so that tests can be
automated. It is built on [casio-emu](https://github.com/Heath123/casio-emu)
(a C SH4A interpreter that emulates the CG50 hardware) plus our own code:

* a headless frontend (no Qt/SDL),
* high-level emulation (HLE) of the OS syscalls that libfxcg-style add-ins use,
* scripted keys, screenshots, limits and a performance summary.

casio-emu has no explicit license, so **none of its source is in this repo**.
`fetch-and-build.sh` downloads it at a pinned commit and applies
`casio-emu.patch`, a small patch with two hooks. Everything in `emu/src/`,
`emu/test-addin/` and `tools/` is our own code.

## Build

```sh
emu/fetch-and-build.sh [WORKDIR]     # default WORKDIR=/home/user/ext/casio-emu-build
# -> WORKDIR/build-headless/sm50emu
```

The script clones `Heath123/casio-emu` at
`5d34e298b94be65f7ab0505d3c0f9b9ed802ac4b` and applies the patch. It then
builds `emu/src/*.c` together with casio-emu's core (not its GUI) using
`emu/Makefile`, with `-O2 -flto -g` by default (override with `OPT=...`).
The only requirements are a C compiler, make, git (or curl) and patch.
Running it again only rebuilds.

`casio-emu.patch` makes two changes:
1. `interpreter.c`: whenever PC is in `0x80020000-0x80021FFF` (or its P2
   alias at `0xA002xxxx`), it calls `sm50_hle_hook()`. That function handles
   the syscall entry point `0x80020070`, the exit address that main() returns
   to, and the exception vectors at `VBR(0x80020F00)+0x100/0x400/0x600`.
2. `memory.c`: the add-in ROM window grows from 2 MB to 16 MB.

## Run

```sh
sm50emu [options] addin.g3a
  --max-frames N      stop after N presents (exit 0)
  --max-seconds S     stop after S *emulated* seconds = instructions / 117.96 MHz (exit 0)
  --max-wall S        stop after S wall-clock seconds (exit 2)
  --keys FILE         key script (see below)
  --outdir DIR        screenshot directory (created if needed)
  --shot-at N[,N..]   PNG after present N  -> DIR/frame_00000N.png
  --shot-every N      PNG every N presents
  --shot-final        PNG at exit -> DIR/final.png
  --fs DIR            host directory that backs \\fls0\ for the Bfile syscalls
  --heap-kb N         sys_malloc arena size (default 256)
  --present-ms X      add X ms per Bdisp_PutDisp_DD to the hardware estimate (default 0)
  --log FILE          log: debug prints, syscall warnings, summary
  --frame-stats FILE  CSV: frame,instructions,est_hw_ms for every present
  --trace-syscalls    log every syscall and key event
  --quiet             with --log, do not echo log lines to stderr
```

Exit codes: 0 means a normal stop (main returned, a limit was reached, or
GetKey ran out of script). 1 means the emulator core aborted (for example an
invalid write or an unaligned access). 2 means `--max-wall` was hit. 3 means
a CPU exception (bad pointer or illegal instruction), and a report is printed.
64-73 are usage or I/O errors.

At exit it prints a summary like this:

```
frames presented  : 200 (Bdisp_PutDisp_DD 120, OS/GetKey presents 2, idle frames while blocked 78)
instructions      : 171702170 (+ 153348000 idle cycles in OS waits)
first frame       : 1441914 instructions (includes start-up)
instr/frame (2..N): avg 1430745  min 1430731  max 1431252  over 119 frames
est. HW ms/frame  : avg 12.13 ... (ESTIMATE: 1 instr = 1 cycle @ 117.96 MHz; ...)
wall time         : 0.946 s, emulator speed 181.6 MIPS
stack high-water  : 348 bytes below 0x8c080000
final screen crc32: 930bbfaf
heap (sys_malloc) : arena 256 KB at 0x8c300000, in use 0 B, peak 4000 B, ...
unknown syscalls  : 0x1234(x1)
```

The ms/frame figure is only an **estimate**. It assumes one instruction per
cycle at 117.96 MHz and ignores cache misses, memory wait states, pipeline
stalls and the LCD transfer time inside Bdisp_PutDisp_DD (`--present-ms` can
add that time). Treat it as a way to compare builds, not as a measurement of
real hardware. The CRC is computed over the last presented frame, which
makes it useful for golden-image tests.

### Key script

```
# comment
<frame> down <KEY>
<frame> up <KEY>
<frame> press <KEY> [hold]     # down at frame, up at frame+hold (default 2)
```

`frame` counts the presents done so far. Events for frame F take effect right
after the F-th present (frame 0 means before the first instruction), so the
add-in sees them in the iteration that draws frame F+1. The key names follow
libfxcg `KEY_PRGM_*` naming:
`F1..F6 SHIFT OPTN VARS MENU LEFT UP DOWN RIGHT ALPHA SQUARE POW EXIT XTT LOG
LN SIN COS TAN FRAC FD LPAR RPAR COMMA STORE 0..9 DEL MUL DIV PLUS MINUS DOT
EXP NEG EXE AC`. A raw basic keycode can be given as `#27`, and a
`KEY_PRGM_` prefix is accepted.

Scripted keys drive the key-matrix registers at `0xA44B0000..0xA44B000B`. The
bits are laid out exactly as the classic `keydown()` reads them
(`row=code%10, col=code/10-1, word=row>>1, bit=col+8*(row&1)`). Reads of 8,
16 and 32 bits all work, so a 12-byte memcpy of the registers is fine. The
same state feeds `PRGM_GetKey_OS`, `GetKeyWait_OS` and `Keyboard_SpyMatrixCode`.

`GetKey` blocks in emulated time. It presents VRAM, then advances "idle
frames" (1/60 s each) until the next scripted `down` event at or after the
current frame, and returns the matching `KEY_CTRL_*`/`KEY_CHAR_*` code. If
the script has no presses left, the emulation ends with exit code 0.

### Example

```sh
emu/run-test.sh            # build everything, run the test add-in, check the screenshots
```

## Syscalls implemented

All numbers were checked against `libfxcg/libfxcg/syscalls/*.S`. A few
numbers in the original task list were wrong: EnableStatusArea is `0x02B7`
(not 0x1D81), DisplayStatusArea is `0x1D81`, GetKey is `0x0EAB`,
Bfile_SeekFile_OS is `0x1DA9`, and `0x1DAB` is Bfile_TellFile_OS.

| no. | name | behaviour |
|---|---|---|
| 0x0029 | GlibAddinAplExecutionCheck | no-op |
| 0x002B | GlibGetOSVersionInfo | 3.60 |
| 0x01E6 | GetVRAMAddress | `0xAC200000` |
| 0x1E50 | GetSecondaryVRAMAddress | `0xAC240000` |
| 0x025F | Bdisp_PutDisp_DD | VRAM -> LCD, frame++, screenshots, key events, limits |
| 0x0260 | Bdisp_PutDisp_DD_stripe | copies rows y1..y2 (does not count a frame) |
| 0x0272 | Bdisp_AllClr_VRAM | VRAM = white |
| 0x0275 | Bdisp_Fill_VRAM | VRAM = color |
| 0x0263 / 0x0267 | Bdisp_SetPoint_VRAM / GetPoint_VRAM | |
| 0x1E62 / 0x1E63 | SaveVRAM_1 / LoadVRAM_1 | via the secondary VRAM |
| 0x0921, 0x02B7, 0x02B8, 0x1D81, 0x1D77, 0x02A3, 0x02A8, 0x1D7F, 0x02C7 | EnableColor, EnableStatusArea, DefineStatusAreaFlags, DisplayStatusArea, DefineStatusMessage, FrameColor, DrawFrame, EnableDisplayHeader, HourGlass | no-op (the status bar is **not** drawn) |
| 0x18F9, 0x01F9, 0x1883, 0x1865 | PrintXY, Print_OS, PrintLine, PrintLine2 | text goes to the log only (no font rendering) |
| 0x02C1 | RTC_GetTicks | `(instructions + idle cycles) * 128 / 117960000`, so it is deterministic |
| 0x02C2 | RTC_Elapsed_ms | same time base |
| 0x1BB4 | OS_InnerWait_ms | adds idle cycles (time passes, no instructions run) |
| 0x0EAB | GetKey | blocks on the script, see above |
| 0x12BF | GetKeyWait_OS | type 1 (KEYWAIT_HALTOFF_TIMEROFF) polls; other types block like GetKey. col/row = code/10, code%10 |
| 0x0D39 | PRGM_GetKey_OS | non-blocking, buf[1]=code/10, buf[2]=(code%10)<<4 |
| 0x12C9 | Keyboard_SpyMatrixCode | non-blocking |
| 0x1EA6, 0x1EA7, 0x1E99, 0x0111, 0x0112, 0x0EA1 | Getkey-to-menu flags and Bkey flags | no-op / 0 |
| 0x1F44 / 0x1F42 / 0x1F46 | sys_malloc / sys_free / sys_realloc | first-fit arena at `0x8C300000`, 8-byte aligned, metadata on the host; usage stats and warnings on a bad free |
| 0x1DA3 | Bfile_OpenFile_OS | modes 0/1 read, 2/3/4 read-write; the file must exist; handles start at 0x100 |
| 0x1DA4 | Bfile_CloseFile_OS | |
| 0x1DAC | Bfile_ReadFile_OS | readpos >= 0 seeks first, -1 means the current position; returns bytes read |
| 0x1DAF | Bfile_WriteFile_OS | writes at the current position; returns the new position |
| 0x1DA6 | Bfile_GetFileSize_OS | |
| 0x1DA9 / 0x1DAB | Bfile_SeekFile_OS / Bfile_TellFile_OS | |
| 0x1DAE | Bfile_CreateEntry_OS | mode 1 creates a zero-filled file of `*size` bytes; mode 5 creates a folder; returns -13 if the entry exists |
| 0x1DB4 / 0x1DB3 | Bfile_DeleteEntry / Bfile_RenameEntry | |
| 0x1DA5 | Bfile_GetMediaFree_OS | reports 16 MB |
| 0x1DDC / 0x1DDB | Bfile_StrToName_ncpy / Bfile_NameToStr_ncpy | |
| 0x1E6E | SetQuitHandler | stored, never called |
| 0x1A2C | GetStackPtr | r15 |
| 0x1349, 0x1DD1 | strlen, sys_memcmp | |
| **0x7FFF** | SM50 debug print | r4 = a NUL-terminated string, printed to stdout and the log. **Emulator only**: debug builds call it; the real OS has no 0x7FFF |

Any other syscall is logged with its number, PR, r4 and r5 (the first three
times), returns 0, and is listed in the summary.

File names arrive as big-endian 16-bit strings (`\\fls0\SM50.dat`). Only
`\\fls0\` is supported, and it maps to the `--fs` directory, with
subfolders becoming host subdirectories and `..` refused. If `--fs` is not
given, all Bfile calls fail with -3. The error codes (-1 not found, -2 bad
parameter, -3 bad path, -5 full, -13 exists) only approximate the real
values, so the add-in should test for `< 0` only. Real Fugue
filesystem quirks are **not** reproduced: files grow freely and odd sizes
are allowed.

## Memory map (emulator)

| range | what |
|---|---|
| `0x00300000` + 16 MB | add-in code (g3a minus its 0x7000-byte header), read/write in the emulator |
| `0x08100000-0x0817FFFF` | **512 KB** static RAM window (casio-emu aliases it to physical `0x8C160000`). The test add-in's linker script puts .data and .bss here |
| `0x8C000000-0x8C7FFFFF` | 8 MB RAM (also uncached at `0xAC000000`). It is filled with `0x55` at start, so uninitialised memory reads as 0x55 bytes |
| `0x8C080000` | initial r15. The stack grows down toward `0x8C000000` (512 KB); the summary reports the high-water mark |
| `0xAC200000` (`0x8C200000`) | VRAM, 396x224 RGB565 big-endian, 177408 bytes |
| `0xAC240000` | secondary VRAM (SaveVRAM_1/LoadVRAM_1) |
| `0x8C300000` | sys_malloc arena (`--heap-kb`, default 256 KB, max 4 MB) |
| `0xE5200000` | 4 KB ILRAM; also XRAM `0xE5000000`, YRAM `0xE5010000` (8 KB each) |
| `0x00000000-0x0000FFFF` | **mapped** (casio-emu quirk): NULL-pointer accesses here do *not* fault |

None of these areas overlap. Anything else is unmapped: reads raise a
TLB-miss exception (reported, exit 3), and writes abort the core (exit 1).
Unaligned accesses abort the core (exit 1).

Some things on real hardware are not confirmed, so check them on a
calculator. The libfxcg linker script only assumes 64 KB at `0x08100004`.
The real OS heap may be smaller than the 256 KB arena (the summary warns
above 128 KB). The real stack location and size differ from the emulator's.

## Known limitations

* There is no OS, so the status bar, fonts, menus and dialogs are not rendered.
  The MENU key returns `KEY_CTRL_MENU` and does not leave the add-in.
* Timer_Install/Start and other interrupt-driven OS services are not
  implemented and return 0. The add-in should use RTC_GetTicks.
* Cycle timing is not modelled (see the estimate caveat above). casio-emu's
  hardware timers, RTC and DMA run on their own approximate time base. Only
  the HLE syscalls use the 117.96 MHz instruction clock.
* Exceptions are not delivered to the add-in. They end the run with a report
  that includes EXPEVT, TEA, SPC and code-like words found on the stack.
* FPU instructions are not relevant (the target is m4-nofpu).
* Add-ins that bypass the OS and drive the LCD directly (gint) still run on
  casio-emu's hardware model. Their screenshots fall back to the emulated LCD
  contents, but frames are only counted through the syscalls.

## Files

* `fetch-and-build.sh`, `casio-emu.patch`, `Makefile`: the build
* `src/sm50_main.c`: CLI, frame accounting, screenshots, limits, summary
* `src/sm50_hle.c`: syscall HLE, heap, Bfile, display
* `src/sm50_keys.c`: key script and key-matrix MMIO
* `src/sm50_png.c`: PNG writer with no dependencies
* `test-addin/`: the test add-in (crt0, syscall stubs, linker script, key
  scripts, screenshot checker)
* `run-test.sh`: builds and runs it end to end
* `../tools/mkg3a.py`, `../tools/g3a_check.py`: g3a packer and validator
