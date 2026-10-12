/*
 * SM50 emulator test add-in (original code).
 *  - fills VRAM with a gradient
 *  - draws a square moved by the arrow keys (read from the key matrix
 *    registers, the classic Prizm keydown() technique)
 *  - presents every frame with Bdisp_PutDisp_DD
 *  - exercises sys_malloc, Bfile and RTC_GetTicks, prints debug lines with
 *    the emulator-only syscall 0x7FFF
 *  - returns on EXIT
 */
typedef unsigned short u16;
typedef unsigned int u32;

void *GetVRAMAddress(void);
void Bdisp_PutDisp_DD(void);
void Bdisp_AllClr_VRAM(void);
void Bdisp_EnableColor(int n);
void EnableStatusArea(int opt);
int RTC_GetTicks(void);
int GetKey(int *key);
void *sys_malloc(int size);
void sys_free(void *p);
int Bfile_OpenFile_OS(const u16 *name, int mode, int zero);
int Bfile_CloseFile_OS(int h);
int Bfile_ReadFile_OS(int h, void *buf, int size, int pos);
int Bfile_WriteFile_OS(int h, const void *buf, int size);
int Bfile_GetFileSize_OS(int h);
int Bfile_CreateEntry_OS(const u16 *name, int mode, int *size);
int Bfile_DeleteEntry(const u16 *name);
void Bfile_StrToName_ncpy(u16 *dst, const char *src, int n);
void sm50_debug_print(const char *s);
int sm50_unknown_probe(int a);

#define LCD_W 384
#define LCD_H 216

#define KEY_LEFT 38
#define KEY_RIGHT 27
#define KEY_UP 28
#define KEY_DOWN 37
#define KEY_EXIT 47
#define KEY_SHIFT 78
#define KEY_F1 79
#define KEY_F2 69
#define KEY_CTRL_EXE 30004

static const volatile u16 *const keyboard_register = (const volatile u16 *)0xA44B0000;

static int keydown(int basic_keycode) {
  int row = basic_keycode % 10;
  int col = basic_keycode / 10 - 1;
  int word = row >> 1;
  int bit = col + 8 * (row & 1);
  return (keyboard_register[word] & (1 << bit)) != 0;
}

/* tiny formatter: appends text / signed decimal to a buffer */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_d(char *p, int v) {
  char tmp[12];
  int n = 0;
  unsigned int u = v < 0 ? -(unsigned int)v : (unsigned int)v;
  if (v < 0) *p++ = '-';
  do { tmp[n++] = '0' + u % 10; u /= 10; } while (u);
  while (n) *p++ = tmp[--n];
  *p = 0;
  return p;
}

static volatile int data_counter = 7; /* .data */
static volatile int bss_zero[64];     /* .bss  */

static void file_test(void) {
  u16 name[32];
  char line[96], *p;
  Bfile_StrToName_ncpy(name, "\\\\fls0\\SM50TEST.dat", 31);
  int size = 16;
  Bfile_DeleteEntry(name);
  int r = Bfile_CreateEntry_OS(name, 1, &size);
  int h = Bfile_OpenFile_OS(name, 2 /* WRITE */, 0);
  int w = Bfile_WriteFile_OS(h, "hello, fls0!\0\0\0\0", 16);
  Bfile_CloseFile_OS(h);
  char buf[17];
  h = Bfile_OpenFile_OS(name, 0 /* READ */, 0);
  int fs = Bfile_GetFileSize_OS(h);
  int rd = Bfile_ReadFile_OS(h, buf, 16, 0);
  Bfile_CloseFile_OS(h);
  buf[16] = 0;
  p = put_s(line, "file: create=");
  p = put_d(p, r);
  p = put_s(p, " handle=");
  p = put_d(p, h);
  p = put_s(p, " write=");
  p = put_d(p, w);
  p = put_s(p, " size=");
  p = put_d(p, fs);
  p = put_s(p, " read=");
  p = put_d(p, rd);
  p = put_s(p, " text=");
  put_s(p, buf);
  sm50_debug_print(line);
}

int main(int isAppli, unsigned short optionNum) {
  char line[96], *p;
  (void)isAppli; (void)optionNum;
  Bdisp_EnableColor(1);
  EnableStatusArea(3);
  Bdisp_AllClr_VRAM();
  u16 *vram = (u16 *)GetVRAMAddress();

  p = put_s(line, "test add-in start: vram=0x");
  {
    static const char hex[] = "0123456789abcdef";
    u32 a = (u32)vram;
    for (int i = 7; i >= 0; i--) *p++ = hex[(a >> (4 * i)) & 15];
    *p = 0;
  }
  p = put_s(p, " data=");
  p = put_d(p, data_counter);
  p = put_s(p, " bss=");
  p = put_d(p, bss_zero[3]);
  sm50_debug_print(line);

  int *heap = (int *)sys_malloc(1000 * sizeof(int));
  for (int i = 0; i < 1000; i++) heap[i] = i * 3;
  int sum = 0;
  for (int i = 0; i < 1000; i++) sum += heap[i];
  sys_free(heap);
  p = put_s(line, "heap: sum=");
  put_d(p, sum);
  sm50_debug_print(line);

  file_test();
  sm50_unknown_probe(42);

  int x = 180, y = 100, frame = 0;
  int t0 = RTC_GetTicks();
  for (;;) {
    if (keydown(KEY_EXIT)) break;
    if (keydown(KEY_F1)) {
      /* deliberate crash to test the emulator's exception report */
      sm50_debug_print("F1: reading an unmapped address");
      x += *(volatile int *)0x12345678;
    }
    if (keydown(KEY_F2)) {
      sm50_debug_print("F2: writing an unmapped address");
      *(volatile int *)0x12345678 = x;
    }
    int speed = keydown(KEY_SHIFT) ? 6 : 3;
    if (keydown(KEY_LEFT)) x -= speed;
    if (keydown(KEY_RIGHT)) x += speed;
    if (keydown(KEY_UP)) y -= speed;
    if (keydown(KEY_DOWN)) y += speed;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > LCD_W - 24) x = LCD_W - 24;
    if (y > LCD_H - 24) y = LCD_H - 24;

    /* gradient background, slowly scrolling */
    for (int j = 0; j < LCD_H; j++) {
      u16 *row = vram + j * LCD_W;
      int g = ((j + frame) >> 2) & 63;
      for (int i = 0; i < LCD_W; i++) {
        int r = (i * 31) / LCD_W;
        row[i] = (u16)((r << 11) | (g << 5) | (31 - r));
      }
    }
    /* the square */
    for (int j = 0; j < 24; j++)
      for (int i = 0; i < 24; i++)
        vram[(y + j) * LCD_W + x + i] = (i < 2 || j < 2 || i > 21 || j > 21) ? 0x0000 : 0xFFFF;

    Bdisp_PutDisp_DD();
    frame++;
    if (frame == 20) {
      /* key matrix through 8/16/32-bit accesses (RIGHT is held now) */
      p = put_s(line, "keymatrix: w3=");
      p = put_d(p, keyboard_register[3]);
      p = put_s(p, " l1=");
      p = put_d(p, (int)((const volatile u32 *)0xA44B0000)[1]);
      p = put_s(p, " b6=");
      put_d(p, ((const volatile unsigned char *)0xA44B0000)[6]);
      sm50_debug_print(line);
    }
    if (frame % 30 == 0) {
      p = put_s(line, "frame ");
      p = put_d(p, frame);
      p = put_s(p, " square at ");
      p = put_d(p, x);
      p = put_s(p, ",");
      p = put_d(p, y);
      p = put_s(p, " ticks=");
      put_d(p, RTC_GetTicks() - t0);
      sm50_debug_print(line);
    }
  }
  p = put_s(line, "EXIT pressed at frame ");
  p = put_d(p, frame);
  p = put_s(p, ", square at ");
  p = put_d(p, x);
  p = put_s(p, ",");
  put_d(p, y);
  sm50_debug_print(line);

  /* GetKey (blocking OS key input) until EXE */
  for (;;) {
    int key = 0;
    GetKey(&key);
    p = put_s(line, "GetKey -> ");
    put_d(p, key);
    sm50_debug_print(line);
    if (key == KEY_CTRL_EXE) break;
  }
  return 1;
}
