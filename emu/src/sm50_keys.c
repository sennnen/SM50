/*
 * SM50 emulator: scripted keyboard input.
 *
 * Keys are identified by "basic keycodes" (libfxcg KEY_PRGM_* numbering:
 * tens digit = column 1..7 counted from the right, units digit = row 1..9
 * counted from the bottom).  The key matrix seen by the add-in at
 * 0xA44B0000..0xA44B000B is driven directly from the scripted state, with
 * the same bit layout as the classic Prizm keydown() routine:
 *   row = code % 10; col = code / 10 - 1;
 *   word = row >> 1; bit = col + 8 * (row & 1);
 *   pressed = ((u16*)0xA44B0000)[word] & (1 << bit)
 *
 * Original code for the SM50 project.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "sm50.h"
#include "hardware/hwRegisters.h"

/* casio-emu's key data registers (hardware/keyboard/keyboard.c) */
extern u16 KIUDATA[6];

typedef struct { const char *name; int code; } KeyName;

static const KeyName key_names[] = {
  {"F1", 79}, {"F2", 69}, {"F3", 59}, {"F4", 49}, {"F5", 39}, {"F6", 29},
  {"SHIFT", 78}, {"OPTN", 68}, {"VARS", 58}, {"MENU", 48}, {"LEFT", 38}, {"UP", 28},
  {"ALPHA", 77}, {"SQUARE", 67}, {"X2", 67}, {"POW", 57}, {"EXIT", 47}, {"DOWN", 37}, {"RIGHT", 27},
  {"XTT", 76}, {"A", 76}, {"LOG", 66}, {"LN", 56}, {"SIN", 46}, {"COS", 36}, {"TAN", 26}, {"F", 26},
  {"FRAC", 75}, {"FD", 65}, {"LPAR", 55}, {"RPAR", 45}, {"COMMA", 35}, {"STORE", 25},
  {"7", 74}, {"8", 64}, {"9", 54}, {"DEL", 44},
  {"4", 73}, {"5", 63}, {"6", 53}, {"MUL", 43}, {"MULT", 43}, {"DIV", 33},
  {"1", 72}, {"2", 62}, {"3", 52}, {"PLUS", 42}, {"ADD", 42}, {"MINUS", 32}, {"SUB", 32},
  {"0", 71}, {"DOT", 61}, {"DP", 61}, {"EXP", 51}, {"NEG", 41}, {"PMINUS", 41},
  {"EXE", 31}, {"RETURN", 31}, {"ENTER", 31},
  {"AC", 10}, {"ACON", 10},
  {NULL, 0}
};

int sm50_key_by_name(const char *name) {
  for (const KeyName *k = key_names; k->name; k++)
    if (!strcasecmp(k->name, name)) return k->code;
  if (!strncasecmp(name, "KEY_PRGM_", 9)) return sm50_key_by_name(name + 9);
  /* numeric basic keycode, e.g. "#27" or "k27" */
  if ((name[0] == '#' || name[0] == 'k' || name[0] == 'K') && isdigit((unsigned char)name[1])) {
    int c = atoi(name + 1);
    if (c >= 10 && c <= 79) return c;
  }
  return 0;
}

const char *sm50_key_name(int basic) {
  for (const KeyName *k = key_names; k->name; k++)
    if (k->code == basic) return k->name;
  return "?";
}

/* ---------------- key state / matrix ---------------- */

static bool key_down[80];

static void matrix_set(int code, bool down) {
  int row = code % 10, col = code / 10 - 1;
  if (col < 0) col = 0; /* AC/ON (10): not part of the real matrix, use bit 0 */
  int word = row >> 1, bit = col + 8 * (row & 1);
  if (word < 0 || word > 5) return;
  if (down) KIUDATA[word] |= (u16)(1u << bit);
  else KIUDATA[word] &= (u16)~(1u << bit);
}

static void key_set(int code, bool down) {
  if (code <= 0 || code >= 80) return;
  key_down[code] = down;
  matrix_set(code, down);
}

bool sm50_key_is_down(int basic) {
  return basic > 0 && basic < 80 && key_down[basic];
}

int sm50_key_first_down(void) {
  for (int c = 10; c < 80; c++) if (key_down[c]) return c;
  return 0;
}

/* Size-agnostic MMIO handler for the key data registers so that byte,
 * word and longword reads (e.g. memcpy of the 12-byte block) all work. */
static u32 kiu_read(u32 addr, u32 size) {
  u32 off = addr - 0xa44b0000u;
  u32 v = 0;
  for (u32 i = 0; i < size; i++) {
    u32 o = off + i;
    u8 b = 0;
    if (o < 12) {
      u16 w = KIUDATA[o >> 1];
      b = (o & 1) ? (u8)(w & 0xff) : (u8)(w >> 8);
    }
    v = (v << 8) | b;
  }
  return v;
}

static void kiu_write(u32 addr, u32 value, u32 size) {
  (void)addr; (void)value; (void)size; /* read-only from the add-in's view */
}

void sm50_keys_install_mmio(void) {
  for (u32 a = 0; a < 12; a++)
    defineRegCBUnsized("SM50 key data", kiu_read, kiu_write, 0xa44b0000u + a);
}

/* ---------------- script ---------------- */

typedef struct { long frame; int code; bool down; bool consumed; } KeyEvent;
static KeyEvent *events;
static int n_events, cap_events, next_apply;

static void add_event(long frame, int code, bool down) {
  if (n_events == cap_events) {
    cap_events = cap_events ? cap_events * 2 : 64;
    events = realloc(events, sizeof(KeyEvent) * cap_events);
  }
  events[n_events++] = (KeyEvent){frame, code, down, false};
}

static int cmp_event(const void *a, const void *b) {
  const KeyEvent *x = a, *y = b;
  if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
  /* releases before presses within the same frame, otherwise keep order */
  if (x->down != y->down) return x->down ? 1 : -1;
  return 0;
}

/*
 * Script format (one event per line, '#' starts a comment):
 *   <frame> down <KEY>
 *   <frame> up <KEY>
 *   <frame> press <KEY> [hold]    -> down at frame, up at frame+hold (default 2)
 * <frame> is the number of Bdisp_PutDisp_DD presents done so far: events for
 * frame F are applied right after the F-th present (frame 0 = at start).
 */
int sm50_keys_load(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); return -1; }
  char line[256];
  int lineno = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char act[32], key[64];
    long frame, hold = 2;
    int n = sscanf(line, "%ld %31s %63s %ld", &frame, act, key, &hold);
    if (n <= 0) continue;
    if (n < 3) { fprintf(stderr, "%s:%d: bad line\n", path, lineno); fclose(f); return -1; }
    int code = sm50_key_by_name(key);
    if (!code) { fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineno, key); fclose(f); return -1; }
    if (!strcasecmp(act, "down")) add_event(frame, code, true);
    else if (!strcasecmp(act, "up")) add_event(frame, code, false);
    else if (!strcasecmp(act, "press")) {
      if (hold < 1) hold = 1;
      add_event(frame, code, true);
      add_event(frame + hold, code, false);
    } else { fprintf(stderr, "%s:%d: unknown action '%s'\n", path, lineno, act); fclose(f); return -1; }
  }
  fclose(f);
  /* stable sort (insertion sort; scripts are small) */
  for (int i = 1; i < n_events; i++) {
    KeyEvent e = events[i];
    int j = i - 1;
    while (j >= 0 && cmp_event(&events[j], &e) > 0) { events[j + 1] = events[j]; j--; }
    events[j + 1] = e;
  }
  return n_events;
}

void sm50_keys_apply(long frame) {
  while (next_apply < n_events && events[next_apply].frame <= frame) {
    KeyEvent *e = &events[next_apply++];
    key_set(e->code, e->down);
    if (sm50_opt.trace_syscalls)
      sm50_log("[keys] frame %ld: %s %s\n", frame, e->down ? "down" : "up", sm50_key_name(e->code));
  }
}

int sm50_keys_next_down(long *frame) {
  for (int i = 0; i < n_events; i++) {
    KeyEvent *e = &events[i];
    if (!e->down || e->consumed) continue;
    /* only events that have not been applied yet, or were applied this frame */
    if (i < next_apply && e->frame < sm50_frames) { e->consumed = true; continue; }
    e->consumed = true;
    *frame = e->frame;
    return e->code;
  }
  return 0;
}

long sm50_keys_last_frame(void) {
  return n_events ? events[n_events - 1].frame : -1;
}
