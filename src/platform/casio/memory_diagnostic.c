/* Separate opt-in fx-CG50 memory/clock diagnostic.
 * The main SM50 add-in and its linker layout are not changed by this build. */
#include "../../engine/gfx.h"
#include <stdio.h>
#include <string.h>

void *GetVRAMAddress(void);
void Bdisp_PutDisp_DD(void);
void Bdisp_AllClr_VRAM(void);
void Bdisp_EnableColor(int n);
void EnableStatusArea(int opt);
int RTC_GetTicks(void);
int GetKey(int *key);
void *sys_malloc(unsigned int size);
void sys_free(void *ptr);

#define W 198
#define H 112
#define LCD_W 396
#define LCD_H 224
#define EXT_BEGIN 0x8c200000u
#define EXT_END   0x8c800000u
#define EXT_STEP  64u
#define OS_CHUNK  4096u
#define OS_MAX_CHUNKS 64

static u16 sColor[W * H] __attribute__((aligned(4)));
static u16 sDepth[W * H] __attribute__((aligned(4)));
static u8 sTexture[1024] __attribute__((aligned(4)));
static void *sOsBlocks[OS_MAX_CHUNKS];

static void present(void)
{
    u32 *vram = (u32 *)GetVRAMAddress();
    const u16 *src = sColor;
    for (int y = 0; y < H; y++) {
        u32 *r0 = vram + y * 2 * (LCD_W / 2);
        u32 *r1 = r0 + LCD_W / 2;
        for (int x = 0; x < W; x++) {
            u32 c = *src++;
            c |= c << 16;
            r0[x] = c;
            r1[x] = c;
        }
    }
    Bdisp_PutDisp_DD();
}

static void screen(const char *a, const char *b, const char *c, const char *d)
{
    gfx_clear(0x0000);
    gfx_draw_text(4, 8, "SM50 MEMORY DIAGNOSTIC", 0xffff, 1);
    gfx_draw_text(4, 28, a, 0xffff, 1);
    gfx_draw_text(4, 44, b, 0xffe0, 1);
    gfx_draw_text(4, 60, c, 0x07ff, 1);
    gfx_draw_text(4, 76, d, 0x07e0, 1);
    present();
}

static unsigned measure_os_heap(void)
{
    unsigned count = 0;
    while (count < OS_MAX_CHUNKS) {
        void *p = sys_malloc(OS_CHUNK);
        if (!p) break;
        sOsBlocks[count++] = p;
        ((volatile u32 *)p)[0] = 0x534d3530u;
        ((volatile u32 *)p)[OS_CHUNK / 4 - 1] = 0x4f534845u;
        if (((volatile u32 *)p)[0] != 0x534d3530u ||
            ((volatile u32 *)p)[OS_CHUNK / 4 - 1] != 0x4f534845u) break;
    }
    unsigned bytes = count * OS_CHUNK;
    while (count) sys_free(sOsBlocks[--count]);
    return bytes;
}

/* Only write-test the community-reported extension if every word in it is
 * initially zero. Each tested word is restored immediately after the check. */
static int probe_extension(u32 *tested)
{
    volatile u32 *p = (volatile u32 *)EXT_BEGIN;
    const u32 words = (EXT_END - EXT_BEGIN) / 4;
    *tested = 0;
    for (u32 i = 0; i < words; i++) {
        if (p[i] != 0) return 1;
    }
    for (u32 off = 0; off < EXT_END - EXT_BEGIN; off += EXT_STEP) {
        volatile u32 *q = (volatile u32 *)(EXT_BEGIN + off);
        u32 old = *q;
        u32 pattern = (EXT_BEGIN + off) ^ 0xa55a39c7u;
        *q = pattern;
        u32 got = *q;
        *q = old;
        if (got != pattern || *q != old) return 2;
        ++*tested;
    }
    return 0;
}

static unsigned benchmark_present(void)
{
    int start = RTC_GetTicks();
    unsigned frames = 0;
    do {
        for (int i = 0; i < W * H; i++) sColor[i] = (u16)(0x001f + (i & 0x7e0));
        present();
        ++frames;
    } while ((unsigned)(RTC_GetTicks() - start) < 128u);
    return frames;
}

int main(void)
{
    Bdisp_EnableColor(1);
    EnableStatusArea(3);
    Bdisp_AllClr_VRAM();
    gfx_init(sColor, sDepth, W, H, sTexture, sizeof sTexture);

    /* Read-only CPG register. No clock register is modified. */
    u32 frqcr = *(volatile u32 *)0xa4150000u;
    unsigned os_heap = measure_os_heap();
    char line[32];
    snprintf(line, sizeof line, "FRQCR read: %08x", frqcr);
    char heap[32];
    snprintf(heap, sizeof heap, "OS malloc arena: %u KB tested", os_heap / 1024);
    screen(line, heap, "F1: test extended SDRAM", "EXIT: benchmark LCD then quit");

    int key = 0;
    GetKey(&key);
    if (key == 79) {
        screen("Scanning 0x8C200000-0x8C7FFFFF", "Read-only zero check first", "No writes unless all bytes are zero", "Please wait...");
        u32 tested = 0;
        int result = probe_extension(&tested);
        char first[32], second[32];
        if (result == 0) {
            snprintf(first, sizeof first, "Zero scan: 6144 KiB clear");
            snprintf(second, sizeof second, "%u sparse writes restored", tested);
            screen("Extended SDRAM sample PASS", first, second, "EXIT: benchmark LCD then quit");
        } else if (result == 1) {
            screen("Extended SDRAM left untouched", "Region contains nonzero data", "No write test was performed", "EXIT: benchmark LCD then quit");
        } else {
            screen("Extended SDRAM sample failed", "Address or data check mismatch", "The current word was restored", "EXIT: benchmark LCD then quit");
        }
        GetKey(&key);
    }

    unsigned fps = benchmark_present();
    char result[32];
    snprintf(result, sizeof result, "LCD+blit: %u frames / second", fps);
    screen(line, heap, result, "CPU clock is read-only; EXIT to leave");
    GetKey(&key);
    return 0;
}
