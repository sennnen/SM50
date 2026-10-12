/*
 * SM50 headless emulator frontend + OS syscall HLE for casio-emu.
 * Original code for the SM50 project.
 */
#ifndef SM50_H
#define SM50_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "int.h"

/* ---- Emulated machine constants ---- */
#define SM50_CPU_HZ        117960000ULL   /* assumed core clock for estimates */
#define SM50_LCD_W         396
#define SM50_LCD_H         224
#define SM50_OS_VRAM_W     384
#define SM50_OS_VRAM_H     216
#define SM50_OS_VRAM_BYTES (SM50_OS_VRAM_W * SM50_OS_VRAM_H * 2u)
#define SM50_VRAM_GUARD_BYTES 0x4000u

/* Memory map chosen for HLE (all inside casio-emu's 8 MB RAM at 0x8c000000,
 * which is also visible uncached at 0xac000000). */
#define SM50_VRAM_ADDR     0xAC200000u    /* returned by GetVRAMAddress   */
#define SM50_VRAM2_ADDR    0xAC240000u    /* GetSecondaryVRAMAddress      */
#define SM50_HEAP_ADDR     0x8C300000u    /* sys_malloc arena             */
#define SM50_HEAP_MAX      (4u << 20)     /* arena may grow up to 4 MB    */
#define SM50_EXIT_MAGIC    0x80020200u    /* initial PR: main() returns here */

/* syscall entry point (P1) and its P2 alias */
#define SM50_SYSCALL_P1    0x80020070u
#define SM50_SYSCALL_P2    0xA0020070u

/* ---- options / global state (sm50_main.c) ---- */
typedef struct {
  long max_frames;          /* <=0: unlimited */
  double max_seconds;       /* emulated seconds, <=0: unlimited */
  double max_wall;          /* wall clock seconds, <=0: unlimited */
  const char *keys_path;
  const char *outdir;
  const char *fs_dir;
  const char *log_path;
  const char *stats_csv;
  long shot_every;
  long *shot_at;
  int n_shot_at;
  bool shot_final;
  bool quiet;
  bool trace_syscalls;
  u32 heap_size;
} Sm50Options;

extern Sm50Options sm50_opt;
extern FILE *sm50_logf;

/* frame/present accounting */
extern long sm50_frames;            /* number of presents so far */
extern u64 sm50_idle_cycles;        /* time spent in OS waits (not executed) */

void sm50_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void sm50_finish(int code, const char *reason) __attribute__((noreturn));

/* called by the HLE when the add-in presents a frame (copies VRAM first). */
void sm50_present(void);
/* the OS itself presents VRAM (GetKey): counted as a frame, not in instruction stats */
void sm50_os_present(void);
/* advance the frame counter without new VRAM content (e.g. while blocked in GetKey) */
void sm50_virtual_frame(void);
u64 sm50_cycles(void);              /* executed instructions + idle cycles */

/* key script (sm50_keys.c) */
int  sm50_keys_load(const char *path);
void sm50_keys_apply(long frame);   /* apply all events with frame <= given */
/* For GetKey: returns basic keycode of next unconsumed "down" event, and its
 * frame in *frame; 0 if none. Marks it consumed. */
int  sm50_keys_next_down(long *frame);
int  sm50_key_by_name(const char *name);
const char *sm50_key_name(int basic);
bool sm50_key_is_down(int basic);
int  sm50_key_first_down(void);
void sm50_keys_install_mmio(void);
long sm50_keys_last_frame(void);

/* HLE (sm50_hle.c) */
bool sm50_hle_hook(void);           /* called from the interpreter */
void sm50_hle_init(void);
void sm50_hle_report(FILE *f);
const u16 *sm50_screen(void);       /* last presented frame, native RGB565 */
void sm50_hle_vram_to_screen(void); /* copy VRAM to the emulated LCD */
void sm50_hle_vram_guard_init(void);
u32 sm50_hle_vram_guard_errors(void);

/* emulated memory helpers */
bool mem_ok(u32 addr);
u8   mem_r8(u32 addr);
u16  mem_r16(u32 addr);
u32  mem_r32(u32 addr);
void mem_w8(u32 addr, u8 v);
void mem_w16(u32 addr, u16 v);
void mem_w32(u32 addr, u32 v);

/* PNG writer (sm50_png.c) */
int sm50_write_png(const char *path, const u16 *rgb565, int w, int h);
u32 sm50_crc32(u32 crc, const void *data, size_t len);

#endif
