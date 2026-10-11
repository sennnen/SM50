/*
 * SM50 emulator: headless frontend for casio-emu.
 *
 *   sm50emu [options] addin.g3a
 *
 * Replaces casio-emu's Qt/SDL GUI (it implements the gui.h interface:
 * updateDisplay / runMainLoop) and adds frame accounting, screenshots,
 * scripted keys, limits and a performance summary.
 *
 * Original code for the SM50 project.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "sm50.h"
#include "cpu.h"
#include "interpreter.h"
#include "gui/gui.h"

extern CpuState cpu;
extern u64 iterations;          /* casio-emu interpreter.c: executed instructions */

Sm50Options sm50_opt = { .heap_size = 256 * 1024 };
static double present_ms; /* --present-ms: assumed HW cost of one Bdisp_PutDisp_DD */
FILE *sm50_logf;
long sm50_frames;
u64 sm50_idle_cycles;

static struct timespec wall_start;
static u64 last_present_iters;
static u64 frame_min = ~0ull, frame_max, frame_sum, frame_n; /* frames 2..N */
static u64 first_frame_iters;
static FILE *stats_f;
static bool finished;

static bool hw_display_seen;
static u16 hw_display[SM50_LCD_W * SM50_LCD_H];

static double wall_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)(t.tv_sec - wall_start.tv_sec) + (t.tv_nsec - wall_start.tv_nsec) / 1e9;
}

u64 sm50_cycles(void) { return iterations + sm50_idle_cycles; }

void sm50_log(const char *fmt, ...) {
  va_list ap;
  if (sm50_logf) {
    va_start(ap, fmt);
    vfprintf(sm50_logf, fmt, ap);
    va_end(ap);
    fflush(sm50_logf);
  }
  if (!sm50_opt.quiet || !sm50_logf) {
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
  }
}

static long n_putdisp, n_os_presents, n_virtual;

static void shot(const char *name) {
  if (!sm50_opt.outdir) return;
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", sm50_opt.outdir, name);
  /* an add-in that never presented through the OS (e.g. drives the LCD
   * controller directly) -> use casio-emu's emulated LCD contents */
  const u16 *img = (n_putdisp || n_os_presents || !hw_display_seen) ? sm50_screen() : hw_display;
  if (sm50_write_png(path, img, SM50_LCD_W, SM50_LCD_H) == 0)
    sm50_log("[shot] frame %ld -> %s\n", sm50_frames, path);
  else
    sm50_log("[shot] failed to write %s: %s\n", path, strerror(errno));
}

static void check_limits(void) {
  if (sm50_opt.max_frames > 0 && sm50_frames >= sm50_opt.max_frames)
    sm50_finish(0, "--max-frames reached");
  if (sm50_opt.max_seconds > 0 && (double)sm50_cycles() / SM50_CPU_HZ >= sm50_opt.max_seconds)
    sm50_finish(0, "--max-seconds (emulated) reached");
  if (sm50_opt.max_wall > 0 && wall_now() >= sm50_opt.max_wall)
    sm50_finish(2, "--max-wall (wall clock) reached");
}

static void frame_done(bool real) {
  sm50_frames++;
  u64 d = iterations - last_present_iters;
  last_present_iters = iterations;
  if (real) {
    if (n_putdisp == 1) first_frame_iters = d;
    else {
      if (d < frame_min) frame_min = d;
      if (d > frame_max) frame_max = d;
      frame_sum += d;
      frame_n++;
    }
    if (stats_f) fprintf(stats_f, "%ld,%llu,%.3f\n", sm50_frames, (unsigned long long)d, d * 1000.0 / SM50_CPU_HZ);
  }
  bool want = false;
  if (sm50_opt.shot_every > 0 && sm50_frames % sm50_opt.shot_every == 0) want = true;
  for (int i = 0; i < sm50_opt.n_shot_at; i++) if (sm50_opt.shot_at[i] == sm50_frames) want = true;
  if (want) {
    char name[64];
    snprintf(name, sizeof name, "frame_%06ld.png", sm50_frames);
    shot(name);
  }
  sm50_keys_apply(sm50_frames);
  check_limits();
}

void sm50_present(void) {
  sm50_hle_vram_to_screen();
  n_putdisp++;
  frame_done(true);
}

void sm50_os_present(void) {
  /* the OS shows VRAM itself (e.g. inside GetKey): not an add-in frame */
  sm50_hle_vram_to_screen();
  n_os_presents++;
  frame_done(false);
}

void sm50_virtual_frame(void) {
  /* time passes while the OS waits for a key (~60 Hz) */
  sm50_idle_cycles += SM50_CPU_HZ / 60;
  n_virtual++;
  frame_done(false);
}

static void sm50_prof_write(void);
static void sm50_prof_enable(const char *path);
static void print_summary(FILE *f, int code, const char *reason) {
  sm50_prof_write();
  double wall = wall_now();
  double emu_s = (double)sm50_cycles() / SM50_CPU_HZ;
  fprintf(f, "\n==== sm50emu summary ====\n");
  fprintf(f, "exit reason       : %s (code %d)\n", reason, code);
  fprintf(f, "frames presented  : %ld (Bdisp_PutDisp_DD %ld, OS/GetKey presents %ld, idle frames while blocked %ld)\n",
          sm50_frames, n_putdisp, n_os_presents, n_virtual);
  fprintf(f, "instructions      : %llu (+ %llu idle cycles in OS waits)\n",
          (unsigned long long)iterations, (unsigned long long)sm50_idle_cycles);
  if (n_putdisp >= 1)
    fprintf(f, "first frame       : %llu instructions (includes start-up)\n", (unsigned long long)first_frame_iters);
  if (frame_n) {
    double avg = (double)frame_sum / frame_n;
    fprintf(f, "instr/frame (2..N): avg %.0f  min %llu  max %llu  over %llu frames\n", avg,
            (unsigned long long)frame_min, (unsigned long long)frame_max, (unsigned long long)frame_n);
    fprintf(f, "est. HW ms/frame  : avg %.2f  min %.2f  max %.2f  (ESTIMATE: 1 instr = 1 cycle @ 117.96 MHz;\n"
               "                    ignores caches, memory waits, pipeline stalls and OS/LCD transfer time)\n",
            avg * 1000.0 / SM50_CPU_HZ, frame_min * 1000.0 / SM50_CPU_HZ, frame_max * 1000.0 / SM50_CPU_HZ);
    if (present_ms > 0)
      fprintf(f, "est. HW ms/frame  : avg %.2f including --present-ms %.2f per Bdisp_PutDisp_DD\n",
              avg * 1000.0 / SM50_CPU_HZ + present_ms, present_ms);
    fprintf(f, "est. HW fps       : %.1f\n", 1000.0 / (avg * 1000.0 / SM50_CPU_HZ + present_ms));
  }
  fprintf(f, "emulated time     : %.3f s\n", emu_s);
  fprintf(f, "wall time         : %.3f s, emulator speed %.1f MIPS\n", wall,
          wall > 0 ? (double)iterations / wall / 1e6 : 0.0);
  /* casio-emu fills RAM with 0x55 at start: find the deepest stack write */
  u32 sp_top = 0x8c080000u, lo = 0x8c000000u;
  u32 a = lo;
  while (a < sp_top && mem_r32(a) == 0x55555555u) a += 4;
  fprintf(f, "stack high-water  : %u bytes below 0x%08x (emulator stack area 0x%08x-0x%08x)\n",
          sp_top - a, sp_top, lo, sp_top);
  const u16 *scr = sm50_screen();
  fprintf(f, "final screen crc32: %08x\n", sm50_crc32(0, scr, SM50_LCD_W * SM50_LCD_H * 2));
  sm50_hle_report(f);
}

void sm50_finish(int code, const char *reason) {
  finished = true;
  if (sm50_opt.shot_final) shot("final.png");
  print_summary(stdout, code, reason);
  if (sm50_logf) { print_summary(sm50_logf, code, reason); fclose(sm50_logf); sm50_logf = NULL; }
  if (stats_f) fclose(stats_f);
  fflush(stdout);
  exit(code);
}

static void at_exit_handler(void) {
  if (finished) return;
  finished = true;
  /* casio-emu exits directly on fatal memory errors */
  fprintf(stdout, "\n*** emulator core exited abnormally (PC=%08x PR=%08x)\n", cpu.reg.PC, cpu.reg.PR);
  print_summary(stdout, 1, "emulator core abort (see message above)");
  if (sm50_logf) print_summary(sm50_logf, 1, "emulator core abort");
}

/* ---- gui.h interface expected by casio-emu ---- */
void initGui(void) {}

void updateDisplay(u16 *vram) {
  /* casio-emu calls this once per emulated 60 Hz period with the
   * R61524 controller's pixel memory (used by add-ins that drive the LCD
   * directly instead of via Bdisp_PutDisp_DD). Keep a copy for reference. */
  memcpy(hw_display, vram, sizeof hw_display);
  hw_display_seen = true;
}

void runMainLoop(void (*callback)(void)) {
  /* Called by startInterpreter() after the core is initialised. */
  cpu.reg.PR = SM50_EXIT_MAGIC;  /* main() returning lands in our hook */
  sm50_keys_install_mmio();
  sm50_hle_init();
  sm50_keys_apply(0);
  for (;;) {
    callback();
    check_limits();
  }
}

static void usage(const char *argv0) {
  fprintf(stderr,
    "usage: %s [options] addin.g3a\n"
    "  --max-frames N      stop after N Bdisp_PutDisp_DD presents\n"
    "  --max-seconds S     stop after S emulated seconds (instructions / 117.96 MHz)\n"
    "  --max-wall S        stop after S wall-clock seconds (exit code 2)\n"
    "  --keys FILE         key script: '<frame> down|up KEY' or '<frame> press KEY [hold]'\n"
    "  --outdir DIR        directory for screenshots (created if missing)\n"
    "  --shot-at N[,N..]   screenshot after the given presents (frame_NNNNNN.png)\n"
    "  --shot-every N      screenshot every N presents\n"
    "  --shot-final        screenshot at exit (final.png)\n"
    "  --fs DIR            host directory backing \\\\fls0\\ for Bfile syscalls\n"
    "  --present-ms X      add X ms per Bdisp_PutDisp_DD to the HW frame-time estimate\n"
    "  --heap-kb N         sys_malloc arena size in KB (default 256)\n"
    "  --log FILE          log file (debug prints, syscall warnings, summary)\n"
    "  --profile FILE      write a per-address instruction histogram of the add-in code\n"
    "  --frame-stats FILE  CSV of instructions per presented frame\n"
    "  --trace-syscalls    log every syscall and key event\n"
    "  --quiet             do not echo log lines to stderr when --log is given\n",
    argv0);
  exit(64);
}

int main(int argc, char **argv) {
  const char *g3a = NULL;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(argv[0]), ""))
    if (!strcmp(a, "--profile")) { extern int sm50_prof_on; sm50_prof_enable(NEXT()); }
    else if (!strcmp(a, "--max-frames")) sm50_opt.max_frames = atol(NEXT());
    else if (!strcmp(a, "--max-seconds")) sm50_opt.max_seconds = atof(NEXT());
    else if (!strcmp(a, "--max-wall")) sm50_opt.max_wall = atof(NEXT());
    else if (!strcmp(a, "--keys")) sm50_opt.keys_path = NEXT();
    else if (!strcmp(a, "--outdir")) sm50_opt.outdir = NEXT();
    else if (!strcmp(a, "--fs")) sm50_opt.fs_dir = NEXT();
    else if (!strcmp(a, "--log")) sm50_opt.log_path = NEXT();
    else if (!strcmp(a, "--frame-stats")) sm50_opt.stats_csv = NEXT();
    else if (!strcmp(a, "--heap-kb")) sm50_opt.heap_size = (u32)atol(NEXT()) * 1024;
    else if (!strcmp(a, "--shot-every")) sm50_opt.shot_every = atol(NEXT());
    else if (!strcmp(a, "--shot-final")) sm50_opt.shot_final = true;
    else if (!strcmp(a, "--present-ms")) present_ms = atof(NEXT());
    else if (!strcmp(a, "--trace-syscalls")) sm50_opt.trace_syscalls = true;
    else if (!strcmp(a, "--quiet")) sm50_opt.quiet = true;
    else if (!strcmp(a, "--shot-at")) {
      char *s = strdup(NEXT());
      for (char *t = strtok(s, ","); t; t = strtok(NULL, ",")) {
        sm50_opt.shot_at = realloc(sm50_opt.shot_at, sizeof(long) * (sm50_opt.n_shot_at + 1));
        sm50_opt.shot_at[sm50_opt.n_shot_at++] = atol(t);
      }
      free(s);
    } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) usage(argv[0]);
    else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "unknown option %s\n", a); usage(argv[0]); }
    else if (!g3a) g3a = a;
    else usage(argv[0]);
  }
  if (!g3a) usage(argv[0]);
  if (sm50_opt.heap_size < 4096 || sm50_opt.heap_size > SM50_HEAP_MAX) {
    fprintf(stderr, "--heap-kb must be between 4 and %u\n", SM50_HEAP_MAX / 1024);
    return 64;
  }
  FILE *probe = fopen(g3a, "rb");
  if (!probe) { perror(g3a); return 66; }
  fclose(probe);
  if (sm50_opt.log_path) {
    sm50_logf = fopen(sm50_opt.log_path, "w");
    if (!sm50_logf) { perror(sm50_opt.log_path); return 73; }
  }
  if (sm50_opt.outdir) mkdir(sm50_opt.outdir, 0777);
  if (sm50_opt.fs_dir) mkdir(sm50_opt.fs_dir, 0777);
  if (sm50_opt.stats_csv) {
    stats_f = fopen(sm50_opt.stats_csv, "w");
    if (stats_f) fprintf(stats_f, "frame,instructions,est_hw_ms\n");
  }
  if (sm50_opt.keys_path) {
    int n = sm50_keys_load(sm50_opt.keys_path);
    if (n < 0) return 65;
    sm50_log("[keys] %d events loaded from %s\n", n, sm50_opt.keys_path);
  }
  clock_gettime(CLOCK_MONOTONIC, &wall_start);
  atexit(at_exit_handler);
  startInterpreter(g3a);
  return 0;
}


/* ---- PC sampling profiler (--profile FILE): counts every instruction
 * executed inside the add-in code window 0x00300000..0x00B00000. ---- */
int sm50_prof_on;
static unsigned int *sm50_prof_counts;
static const char *sm50_prof_path;
#define PROF_BASE 0x00300000u
#define PROF_SPAN 0x00800000u
void sm50_prof_sample(unsigned int pc) {
  unsigned int off = pc - PROF_BASE;
  if (off < PROF_SPAN) sm50_prof_counts[off >> 1]++;
}
static void sm50_prof_write(void) {
  if (!sm50_prof_on || !sm50_prof_path) return;
  FILE *f = fopen(sm50_prof_path, "w");
  if (!f) return;
  for (unsigned int i = 0; i < PROF_SPAN / 2; i++)
    if (sm50_prof_counts[i]) fprintf(f, "%08x %u\n", PROF_BASE + i * 2, sm50_prof_counts[i]);
  fclose(f);
}

static void sm50_prof_enable(const char *path) {
  sm50_prof_counts = calloc(PROF_SPAN / 2, sizeof(unsigned int));
  sm50_prof_path = path;
  sm50_prof_on = sm50_prof_counts != NULL;
}
