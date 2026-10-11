/*
 * SM50 emulator: high-level emulation of the Casio Prizm / fx-CG50 OS
 * syscall interface (the libfxcg calling convention: jump to 0x80020070 with
 * r0 = syscall number, arguments in r4..r7 then on the stack at @r15,
 * result in r0, return address in PR).
 *
 * Syscall numbers verified against libfxcg/libfxcg/syscalls/ *.S.
 *
 * Original code for the SM50 project.
 */
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sm50.h"
#include "cpu.h"

extern CpuState cpu;
extern void *pageTable[0x100000];

/* ------------------------------------------------------------------ */
/* emulated memory access (casio-emu stores each 32-bit word in host    */
/* order, so sub-word accesses use the address XOR trick)               */

static inline u8 *page_of(u32 addr) { return (u8 *)pageTable[addr >> 12]; }

bool mem_ok(u32 addr) { return page_of(addr) != NULL; }

u8 mem_r8(u32 addr) {
  u8 *p = page_of(addr);
  return p ? p[(addr ^ 3) & 0xfff] : 0;
}
u16 mem_r16(u32 addr) {
  u8 *p = page_of(addr);
  return p ? *(u16 *)(p + ((addr ^ 2) & 0xffe)) : 0;
}
u32 mem_r32(u32 addr) {
  u8 *p = page_of(addr);
  return p ? *(u32 *)(p + (addr & 0xffc)) : 0;
}
void mem_w8(u32 addr, u8 v) {
  u8 *p = page_of(addr);
  if (p) p[(addr ^ 3) & 0xfff] = v;
}
void mem_w16(u32 addr, u16 v) {
  u8 *p = page_of(addr);
  if (p) *(u16 *)(p + ((addr ^ 2) & 0xffe)) = v;
}
void mem_w32(u32 addr, u32 v) {
  u8 *p = page_of(addr);
  if (p) *(u32 *)(p + (addr & 0xffc)) = v;
}

static void mem_read_block(u32 addr, void *dst, u32 n) {
  u8 *d = dst;
  for (u32 i = 0; i < n; i++) d[i] = mem_r8(addr + i);
}
static void mem_write_block(u32 addr, const void *src, u32 n) {
  const u8 *s = src;
  for (u32 i = 0; i < n; i++) mem_w8(addr + i, s[i]);
}
static void mem_copy(u32 dst, u32 src, u32 n) {
  if (((dst | src | n) & 3) == 0) {
    for (u32 i = 0; i < n; i += 4) mem_w32(dst + i, mem_r32(src + i));
  } else {
    for (u32 i = 0; i < n; i++) mem_w8(dst + i, mem_r8(src + i));
  }
}

static void mem_read_cstr(u32 addr, char *dst, size_t max) {
  size_t i = 0;
  if (addr == 0) { snprintf(dst, max, "(null)"); return; }
  for (; i + 1 < max; i++) {
    char c = (char)mem_r8(addr + i);
    if (!c) break;
    dst[i] = c;
  }
  dst[i] = 0;
}

/* 16-bit big-endian "FONTCHARACTER" string -> ASCII */
static void mem_read_wstr(u32 addr, char *dst, size_t max) {
  size_t i = 0;
  for (; i + 1 < max; i++) {
    u16 c = mem_r16(addr + 2 * i);
    if (!c) break;
    dst[i] = (c < 0x80) ? (char)c : '?';
  }
  dst[i] = 0;
}

static u32 arg(int n) {
  /* n = 0..3 -> r4..r7, n >= 4 -> stack */
  if (n < 4) return cpu.reg.regArray[4 + n];
  return mem_r32(cpu.reg.r15 + 4 * (n - 4));
}

/* ------------------------------------------------------------------ */
/* display                                                             */

static u16 screen[SM50_LCD_H * SM50_LCD_W];

const u16 *sm50_screen(void) { return screen; }

static void vram_to_screen(int y1, int y2) {
  if (y1 < 0) y1 = 0;
  if (y2 > SM50_LCD_H - 1) y2 = SM50_LCD_H - 1;
  for (int y = y1; y <= y2; y++) {
    u32 a = SM50_VRAM_ADDR + (u32)y * SM50_LCD_W * 2;
    u16 *row = &screen[y * SM50_LCD_W];
    for (int x = 0; x < SM50_LCD_W; x += 2, a += 4) {
      u32 w = mem_r32(a);         /* two big-endian pixels */
      row[x] = (u16)(w >> 16);
      row[x + 1] = (u16)w;
    }
  }
}

void sm50_hle_vram_to_screen(void) { vram_to_screen(0, SM50_LCD_H - 1); }

static void vram_fill(u32 base, u16 color) {
  u32 w = ((u32)color << 16) | color;
  for (u32 i = 0; i < SM50_LCD_W * SM50_LCD_H * 2; i += 4) mem_w32(base + i, w);
}

/* ------------------------------------------------------------------ */
/* heap (metadata kept on the host side, arena in emulated RAM)        */

typedef struct Blk { u32 addr, size; bool used; struct Blk *next; } Blk;
static Blk *heap_list;
static u32 heap_used, heap_peak, heap_allocs, heap_frees, heap_fail;

static void heap_init(void) {
  heap_list = calloc(1, sizeof(Blk));
  heap_list->addr = SM50_HEAP_ADDR;
  heap_list->size = sm50_opt.heap_size;
}

static u32 heap_malloc(u32 sz) {
  if (sz == 0) sz = 1;
  sz = (sz + 7) & ~7u;
  for (Blk *b = heap_list; b; b = b->next) {
    if (b->used || b->size < sz) continue;
    if (b->size - sz >= 16) {
      Blk *n = calloc(1, sizeof(Blk));
      n->addr = b->addr + sz;
      n->size = b->size - sz;
      n->next = b->next;
      b->next = n;
      b->size = sz;
    }
    b->used = true;
    heap_used += b->size;
    if (heap_used > heap_peak) heap_peak = heap_used;
    heap_allocs++;
    return b->addr;
  }
  heap_fail++;
  return 0;
}

static Blk *heap_find(u32 addr) {
  for (Blk *b = heap_list; b; b = b->next)
    if (b->addr == addr && b->used) return b;
  return NULL;
}

static void heap_free(u32 addr) {
  if (!addr) return;
  Blk *b = heap_find(addr);
  if (!b) {
    sm50_log("[heap] WARNING: sys_free(0x%08x) of unknown/freed pointer, PR=%08x\n", addr, cpu.reg.PR);
    return;
  }
  b->used = false;
  heap_used -= b->size;
  heap_frees++;
  /* coalesce all adjacent free blocks */
  for (Blk *p = heap_list; p && p->next;) {
    if (!p->used && !p->next->used) {
      Blk *n = p->next;
      p->size += n->size;
      p->next = n->next;
      free(n);
    } else p = p->next;
  }
}

static u32 heap_realloc(u32 addr, u32 sz) {
  if (!addr) return heap_malloc(sz);
  Blk *b = heap_find(addr);
  if (!b) {
    sm50_log("[heap] WARNING: sys_realloc(0x%08x) of unknown pointer, PR=%08x\n", addr, cpu.reg.PR);
    return 0;
  }
  if (sz == 0) { heap_free(addr); return 0; }
  if (((sz + 7) & ~7u) <= b->size) return addr;
  u32 n = heap_malloc(sz);
  if (!n) return 0;
  mem_copy(n, addr, b->size);
  heap_free(addr);
  return n;
}

/* ------------------------------------------------------------------ */
/* Bfile: storage memory backed by a host directory (\\fls0\ -> dir)  */

#define MAX_FILES 16
typedef struct { FILE *f; int mode; char path[1024]; } HFile;
static HFile files[MAX_FILES];
#define HANDLE_BASE 0x100

/* error codes (approximation of the Fugue/Bfile IML_FILEERR_* values) */
#define FERR_NOTFOUND   (-1)
#define FERR_PARAM      (-2)
#define FERR_PATH       (-3)
#define FERR_FULL       (-5)
#define FERR_EXISTS     (-13)

static bool calc_path_to_host(u32 wstr, char *out, size_t max) {
  char cp[512];
  mem_read_wstr(wstr, cp, sizeof cp);
  const char *p = cp;
  if (!strncasecmp(p, "\\\\fls0\\", 7)) p += 7;
  else if (!strncasecmp(p, "\\\\fls0", 6) && p[6] == 0) p += 6;
  else {
    sm50_log("[bfile] unsupported path '%s'\n", cp);
    return false;
  }
  if (!sm50_opt.fs_dir) { sm50_log("[bfile] no --fs directory given; '%s' refused\n", cp); return false; }
  char rel[512];
  size_t j = 0;
  for (; *p && j + 1 < sizeof rel; p++) {
    char c = *p;
    if (c == '\\') c = '/';
    if (c == '.' && p[1] == '.') return false; /* no escaping */
    rel[j++] = c;
  }
  rel[j] = 0;
  snprintf(out, max, "%s/%s", sm50_opt.fs_dir, rel);
  return true;
}

static HFile *get_file(int h) {
  int i = h - HANDLE_BASE;
  if (i < 0 || i >= MAX_FILES || !files[i].f) return NULL;
  return &files[i];
}

static int bfile_open(u32 name, int mode) {
  char hp[1024];
  if (!calc_path_to_host(name, hp, sizeof hp)) return FERR_PATH;
  const char *fm = (mode == 0 || mode == 1) ? "rb" : "r+b";
  FILE *f = fopen(hp, fm);
  if (!f) return FERR_NOTFOUND;
  for (int i = 0; i < MAX_FILES; i++) {
    if (!files[i].f) {
      files[i].f = f;
      files[i].mode = mode;
      snprintf(files[i].path, sizeof files[i].path, "%s", hp);
      return HANDLE_BASE + i;
    }
  }
  fclose(f);
  return FERR_FULL;
}

static int bfile_create(u32 name, int mode, u32 psize) {
  char hp[1024];
  if (!calc_path_to_host(name, hp, sizeof hp)) return FERR_PATH;
  struct stat st;
  if (stat(hp, &st) == 0) return FERR_EXISTS;
  if (mode == 5) return mkdir(hp, 0777) == 0 ? 0 : FERR_PATH;
  if (mode != 1) return FERR_PARAM;
  u32 size = psize ? mem_r32(psize) : 0;
  FILE *f = fopen(hp, "wb");
  if (!f) return FERR_PATH;
  static const u8 zero[4096];
  for (u32 left = size; left;) {
    u32 n = left > sizeof zero ? (u32)sizeof zero : left;
    fwrite(zero, 1, n, f);
    left -= n;
  }
  fclose(f);
  return 0;
}

static int bfile_read(int h, u32 buf, int size, int pos) {
  HFile *hf = get_file(h);
  if (!hf || size < 0) return FERR_PARAM;
  if (pos >= 0) fseek(hf->f, pos, SEEK_SET);
  u8 tmp[4096];
  int total = 0;
  while (total < size) {
    int want = size - total > (int)sizeof tmp ? (int)sizeof tmp : size - total;
    int got = (int)fread(tmp, 1, (size_t)want, hf->f);
    if (got <= 0) break;
    mem_write_block(buf + (u32)total, tmp, (u32)got);
    total += got;
  }
  return total;
}

static int bfile_write(int h, u32 buf, int size) {
  HFile *hf = get_file(h);
  if (!hf || size < 0) return FERR_PARAM;
  if (hf->mode < 2) return FERR_PARAM;
  u8 tmp[4096];
  int total = 0;
  while (total < size) {
    int n = size - total > (int)sizeof tmp ? (int)sizeof tmp : size - total;
    mem_read_block(buf + (u32)total, tmp, (u32)n);
    if ((int)fwrite(tmp, 1, (size_t)n, hf->f) != n) return FERR_FULL;
    total += n;
  }
  fflush(hf->f);
  return (int)ftell(hf->f);
}

static int bfile_size(int h) {
  HFile *hf = get_file(h);
  if (!hf) return FERR_PARAM;
  long cur = ftell(hf->f);
  fseek(hf->f, 0, SEEK_END);
  long sz = ftell(hf->f);
  fseek(hf->f, cur, SEEK_SET);
  return (int)sz;
}

static int bfile_close(int h) {
  HFile *hf = get_file(h);
  if (!hf) return FERR_PARAM;
  fclose(hf->f);
  hf->f = NULL;
  return 0;
}

static int bfile_delete(u32 name) {
  char hp[1024];
  if (!calc_path_to_host(name, hp, sizeof hp)) return FERR_PATH;
  if (remove(hp) == 0) return 0;
  return FERR_NOTFOUND;
}

static int bfile_rename(u32 a, u32 b) {
  char ha[1024], hb[1024];
  if (!calc_path_to_host(a, ha, sizeof ha) || !calc_path_to_host(b, hb, sizeof hb)) return FERR_PATH;
  struct stat st;
  if (stat(ha, &st) != 0) return FERR_NOTFOUND;
  if (stat(hb, &st) == 0) return FERR_EXISTS;
  return rename(ha, hb) == 0 ? 0 : FERR_PATH;
}

/* ------------------------------------------------------------------ */
/* keys                                                                */

static int basic_to_getkey(int b) {
  switch (b) {
    case 79: return 30009; case 69: return 30010; case 59: return 30011;
    case 49: return 30012; case 39: return 30013; case 29: return 30014;
    case 78: return 30006; case 68: return 30008; case 58: return 30016;
    case 48: return 30003; case 38: return 30020; case 28: return 30018;
    case 77: return 30007; case 67: return 0x8b; case 57: return 0xa8;
    case 47: return 30002; case 37: return 30023; case 27: return 30021;
    case 76: return 30001; case 66: return 0x95; case 56: return 0x85;
    case 46: return 0x81; case 36: return 0x82; case 26: return 0x83;
    case 75: return 0xbb; case 65: return 30026; case 55: return 0x28;
    case 45: return 0x29; case 35: return 0x2c; case 25: return 0x0e;
    case 74: return '7'; case 64: return '8'; case 54: return '9'; case 44: return 30025;
    case 73: return '4'; case 63: return '5'; case 53: return '6'; case 43: return 0xa9; case 33: return 0xb9;
    case 72: return '1'; case 62: return '2'; case 52: return '3'; case 42: return 0x89; case 32: return 0x99;
    case 71: return '0'; case 61: return 0x2e; case 51: return 0x0f; case 41: return 0x87; case 31: return 30004;
    case 10: return 30015;
  }
  return 0;
}

/* Block (in emulated time) until the next scripted key press.  Returns the
 * basic keycode, or ends the emulation when the script is exhausted. */
static int wait_scripted_key(const char *who) {
  long f;
  int code = sm50_keys_next_down(&f);
  if (!code) {
    sm50_os_present();
    char msg[128];
    snprintf(msg, sizeof msg, "%s blocked with no more scripted key presses", who);
    sm50_finish(0, msg);
  }
  sm50_os_present();
  while (sm50_frames < f) sm50_virtual_frame();
  return code;
}

/* ------------------------------------------------------------------ */
/* syscall dispatch                                                    */

typedef struct { u32 num; u32 count; } Unknown;
static Unknown unknowns[256];
static int n_unknowns;
static u64 syscall_count;
static u32 quit_handler;

static void note_unknown(u32 num) {
  for (int i = 0; i < n_unknowns; i++) {
    if (unknowns[i].num == num) {
      if (++unknowns[i].count <= 3)
        sm50_log("[hle] unknown syscall 0x%04X (PR=%08x r4=%08x r5=%08x) -> 0\n", num, cpu.reg.PR, cpu.reg.r4, cpu.reg.r5);
      return;
    }
  }
  if (n_unknowns < 256) unknowns[n_unknowns++] = (Unknown){num, 1};
  sm50_log("[hle] unknown syscall 0x%04X (PR=%08x r4=%08x r5=%08x) -> 0\n", num, cpu.reg.PR, cpu.reg.r4, cpu.reg.r5);
}

static u32 rtc_ticks(void) {
  return (u32)(sm50_cycles() * 128 / SM50_CPU_HZ);
}

static u32 do_syscall(u32 num) {
  switch (num) {
    /* ---- system / misc ---- */
    case 0x0029: /* GlibAddinAplExecutionCheck */
      return 0;
    case 0x002B: /* GlibGetOSVersionInfo(char*major, char*minor, short*release, short*build) */
      if (arg(0)) mem_w8(arg(0), 3);
      if (arg(1)) mem_w8(arg(1), 0x60);
      if (arg(2)) mem_w16(arg(2), 0);
      if (arg(3)) mem_w16(arg(3), 0);
      return 0;
    case 0x1E6E: /* SetQuitHandler(void(*)(void)) */
      quit_handler = arg(0);
      return 0;
    case 0x1BB4: /* OS_InnerWait_ms(int ms) */
      sm50_idle_cycles += (u64)(i32)arg(0) * (SM50_CPU_HZ / 1000);
      return 0;
    case 0x1A2C: /* GetStackPtr */
      return cpu.reg.r15;
    case 0x7FFF: { /* SM50 emulator debug print (r4 = NUL-terminated string) */
      char s[1024];
      mem_read_cstr(arg(0), s, sizeof s);
      printf("%s", s);
      size_t l = strlen(s);
      if (l == 0 || s[l - 1] != '\n') printf("\n");
      fflush(stdout);
      if (sm50_logf) {
        fprintf(sm50_logf, "[dbg f%ld] %s%s", sm50_frames, s, (l && s[l - 1] == '\n') ? "" : "\n");
      }
      return 0;
    }

    /* ---- display ---- */
    case 0x01E6: /* GetVRAMAddress */
      return SM50_VRAM_ADDR;
    case 0x1E50: /* GetSecondaryVRAMAddress */
      return SM50_VRAM2_ADDR;
    case 0x025F: /* Bdisp_PutDisp_DD */
      sm50_present();
      return 0;
    case 0x0260: /* Bdisp_PutDisp_DD_stripe(int y1, int y2) */
      vram_to_screen((int)arg(0), (int)arg(1));
      return 0;
    case 0x0272: /* Bdisp_AllClr_VRAM */
      vram_fill(SM50_VRAM_ADDR, 0xffff);
      return 0;
    case 0x0275: /* Bdisp_Fill_VRAM(int color, int mode) */
      vram_fill(SM50_VRAM_ADDR, (u16)arg(0));
      return 0;
    case 0x0263: { /* Bdisp_SetPoint_VRAM(int x, int y, int color) */
      i32 x = (i32)arg(0), y = (i32)arg(1);
      if (x >= 0 && x < SM50_LCD_W && y >= 0 && y < SM50_LCD_H)
        mem_w16(SM50_VRAM_ADDR + (u32)(y * SM50_LCD_W + x) * 2, (u16)arg(2));
      return 0;
    }
    case 0x0267: { /* Bdisp_GetPoint_VRAM(int x, int y) */
      i32 x = (i32)arg(0), y = (i32)arg(1);
      if (x >= 0 && x < SM50_LCD_W && y >= 0 && y < SM50_LCD_H)
        return mem_r16(SM50_VRAM_ADDR + (u32)(y * SM50_LCD_W + x) * 2);
      return 0;
    }
    case 0x1E62: /* SaveVRAM_1 */
      mem_copy(SM50_VRAM2_ADDR, SM50_VRAM_ADDR, SM50_LCD_W * SM50_LCD_H * 2);
      return 0;
    case 0x1E63: /* LoadVRAM_1 */
      mem_copy(SM50_VRAM_ADDR, SM50_VRAM2_ADDR, SM50_LCD_W * SM50_LCD_H * 2);
      return 0;
    case 0x0921: /* Bdisp_EnableColor / EnableColor */
    case 0x02B7: /* EnableStatusArea */
    case 0x02B8: /* DefineStatusAreaFlags */
    case 0x1D81: /* DisplayStatusArea */
    case 0x1D77: /* DefineStatusMessage */
    case 0x02A3: /* FrameColor */
    case 0x02A8: /* DrawFrame */
    case 0x1D7F: /* EnableDisplayHeader */
    case 0x02C7: /* HourGlass */
      return 0;
    case 0x18F9: /* PrintXY(x, y, str, mode, color): text is logged only */
    case 0x01F9: /* Print_OS(str, mode, zero) */
    case 0x1883: /* PrintLine */
    case 0x1865: { /* PrintLine2 */
      char s[256];
      u32 p = (num == 0x18F9) ? arg(2) : arg(0);
      mem_read_cstr(p, s, sizeof s);
      sm50_log("[text 0x%04X] %s\n", num, (num == 0x18F9 && strlen(s) >= 2) ? s + 2 : s);
      return 0;
    }

    /* ---- timing ---- */
    case 0x02C1: /* RTC_GetTicks: 1/128 s since start, from emulated cycles */
      return rtc_ticks();
    case 0x02C2: { /* RTC_Elapsed_ms(start, duration_ms) */
      u32 now = rtc_ticks();
      u32 elapsed_ms = (now - arg(0)) * 1000 / 128;
      return elapsed_ms >= arg(1) ? 1 : 0;
    }

    /* ---- keyboard ---- */
    case 0x0EAB: { /* GetKey(int *key) */
      int b = wait_scripted_key("GetKey");
      if (arg(0)) mem_w32(arg(0), (u32)basic_to_getkey(b));
      return 1;
    }
    case 0x12BF: { /* GetKeyWait_OS(int*col, int*row, type, timeout, menu, u16*keycode) */
      u32 type = arg(2);
      int b;
      if (type == 1 /* KEYWAIT_HALTOFF_TIMEROFF: no wait */ ) {
        b = sm50_key_first_down();
        if (!b) return 0; /* KEYREP_NOEVENT */
      } else {
        b = wait_scripted_key("GetKeyWait_OS");
      }
      if (arg(0)) mem_w32(arg(0), (u32)(b / 10));
      if (arg(1)) mem_w32(arg(1), (u32)(b % 10));
      if (arg(5)) mem_w16(arg(5), 0);
      return 1; /* KEYREP_KEYEVENT */
    }
    case 0x0D39: { /* PRGM_GetKey_OS(unsigned char *buf) : non-blocking */
      int b = sm50_key_first_down();
      u32 p = arg(0);
      for (int i = 0; i < 12; i++) mem_w8(p + i, 0);
      mem_w8(p + 1, (u8)(b / 10));
      mem_w8(p + 2, (u8)((b % 10) << 4));
      return 0;
    }
    case 0x12C9: { /* Keyboard_SpyMatrixCode(char*col, char*row) */
      int b = sm50_key_first_down();
      if (!b) return 0;
      mem_w8(arg(0), (u8)(b / 10));
      mem_w8(arg(1), (u8)(b % 10));
      return 1;
    }
    case 0x1EA6: case 0x1EA7: case 0x0111: case 0x0112: case 0x0EA1:
      return 0;
    case 0x1E99: return 0;

    /* ---- heap ---- */
    case 0x1F44: return heap_malloc(arg(0));
    case 0x1F42: heap_free(arg(0)); return 0;
    case 0x1F46: return heap_realloc(arg(0), arg(1));

    /* ---- Bfile ---- */
    case 0x1DA3: return (u32)bfile_open(arg(0), (int)arg(1));
    case 0x1DA4: return (u32)bfile_close((int)arg(0));
    case 0x1DAC: return (u32)bfile_read((int)arg(0), arg(1), (int)arg(2), (int)arg(3));
    case 0x1DAF: return (u32)bfile_write((int)arg(0), arg(1), (int)arg(2));
    case 0x1DA6: return (u32)bfile_size((int)arg(0));
    case 0x1DAE: return (u32)bfile_create(arg(0), (int)arg(1), arg(2));
    case 0x1DB4: return (u32)bfile_delete(arg(0));
    case 0x1DB3: return (u32)bfile_rename(arg(0), arg(1));
    case 0x1DA9: { /* Bfile_SeekFile_OS(handle, pos) */
      HFile *hf = get_file((int)arg(0));
      if (!hf) return (u32)FERR_PARAM;
      if (fseek(hf->f, (long)(i32)arg(1), SEEK_SET) != 0) return (u32)FERR_PARAM;
      return (u32)ftell(hf->f);
    }
    case 0x1DAB: { /* Bfile_TellFile_OS(handle) */
      HFile *hf = get_file((int)arg(0));
      return hf ? (u32)ftell(hf->f) : (u32)FERR_PARAM;
    }
    case 0x1DA5: /* Bfile_GetMediaFree_OS(u16*media, int*free) */
      if (arg(1)) { mem_w32(arg(1), 0); if (mem_ok(arg(1) + 4)) mem_w32(arg(1) + 4, 16u << 20); }
      return 0;
    case 0x1DDC: { /* Bfile_StrToName_ncpy(u16*dest, const char*src, n) */
      u32 d = arg(0), s = arg(1), n = arg(2), i = 0;
      for (; i < n; i++) {
        u8 c = mem_r8(s + i);
        if (!c) break;
        mem_w16(d + 2 * i, c);
      }
      if (i < n) mem_w16(d + 2 * i, 0);
      return 0;
    }
    case 0x1DDB: { /* Bfile_NameToStr_ncpy(char*dest, const u16*src, n) */
      u32 d = arg(0), s = arg(1), n = arg(2), i = 0;
      for (; i < n; i++) {
        u16 c = mem_r16(s + 2 * i);
        if (!c) break;
        mem_w8(d + i, (u8)c);
      }
      if (i < n) mem_w8(d + i, 0);
      return 0;
    }

    /* ---- small libc-like helpers ---- */
    case 0x1349: { /* strlen */
      u32 p = arg(0), n = 0;
      while (mem_r8(p + n)) n++;
      return n;
    }
    case 0x1DD1: { /* sys_memcmp */
      for (u32 i = 0; i < arg(2); i++) {
        int d = (int)mem_r8(arg(0) + i) - (int)mem_r8(arg(1) + i);
        if (d) return (u32)d;
      }
      return 0;
    }
  }
  note_unknown(num);
  return 0;
}

static void report_exception(void) {
  fprintf(stderr, "\n*** CPU exception/interrupt vectored to %08x (no OS handler)\n", cpu.reg.PC);
  fprintf(stderr, "    EXPEVT=%03x TEA=%08x SPC=%08x SSR=%08x PR=%08x\n",
          cpu.reg.EXPEVT, cpu.reg.TEA, cpu.reg.SPC, cpu.reg.SSR, cpu.reg.PR);
  fprintf(stderr, "    (0x040/0x060 = TLB miss read/write i.e. bad pointer, 0x0e0/0x100 = address error, 0x180 = illegal instruction)\n");
  fprintf(stderr, "    stack words that look like code addresses:");
  for (u32 i = 0; i < 0x400; i += 4) {
    u32 v = mem_r32(cpu.reg.SGR + i);
    if (v >= 0x00300000 && v < 0x01300000) fprintf(stderr, " %08x", v);
  }
  fprintf(stderr, "\n");
}

bool sm50_hle_hook(void) {
  u32 pc = cpu.reg.PC & 0xDFFFFFFFu; /* fold P2 onto P1 */
  if (pc == SM50_SYSCALL_P1) {
    u32 num = cpu.reg.r0;
    syscall_count++;
    if (sm50_opt.trace_syscalls)
      sm50_log("[sys] %04X r4=%08x r5=%08x r6=%08x r7=%08x PR=%08x\n", num,
               cpu.reg.r4, cpu.reg.r5, cpu.reg.r6, cpu.reg.r7, cpu.reg.PR);
    u32 ret = do_syscall(num);
    cpu.reg.r0 = ret;
    cpu.reg.PC = cpu.reg.PR;
    return true;
  }
  if (pc == SM50_EXIT_MAGIC) {
    char msg[64];
    snprintf(msg, sizeof msg, "add-in returned from main (r0=%d)", (int)cpu.reg.r0);
    sm50_finish(0, msg);
  }
  if (pc >= 0x80021000u && pc < 0x80022000u) {
    report_exception();
    sm50_finish(3, "CPU exception");
  }
  return false;
}

void sm50_hle_init(void) {
  heap_init();
  for (int i = 0; i < SM50_LCD_W * SM50_LCD_H; i++) screen[i] = 0xffff;
  vram_fill(SM50_VRAM_ADDR, 0xffff);
  vram_fill(SM50_VRAM2_ADDR, 0xffff);
}

void sm50_hle_report(FILE *f) {
  fprintf(f, "syscalls          : %llu\n", (unsigned long long)syscall_count);
  fprintf(f, "heap (sys_malloc) : arena %u KB at 0x%08x, in use %u B, peak %u B, %u allocs, %u frees, %u failed\n",
          sm50_opt.heap_size / 1024, SM50_HEAP_ADDR, heap_used, heap_peak, heap_allocs, heap_frees, heap_fail);
  if (heap_peak > 128 * 1024)
    fprintf(f, "                    WARNING: peak heap use > 128 KB; the real OS heap may be smaller than this arena\n");
  if (n_unknowns) {
    fprintf(f, "unknown syscalls  :");
    for (int i = 0; i < n_unknowns; i++) fprintf(f, " 0x%04X(x%u)", unknowns[i].num, unknowns[i].count);
    fprintf(f, "\n");
  }
}
