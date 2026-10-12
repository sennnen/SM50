/* Separate opt-in fx-CG50 memory/clock diagnostic.
 * The main SM50 add-in and its linker layout are not changed by this build. */
#include "../../engine/gfx.h"
#include "display_layout.h"
#include <stdio.h>
#include <string.h>

void *GetVRAMAddress(void);
void Bdisp_PutDisp_DD(void);
void Bdisp_AllClr_VRAM(void);
void Bdisp_EnableColor(int n);
void EnableStatusArea(int opt);
void PrintXY(int x, int y, char *message, int mode, int color);
int RTC_GetTicks(void);
int GetKey(int *key);
void *sys_malloc(unsigned int size);
void sys_free(void *ptr);

#define LCD_W SM50_OS_VRAM_W
#define LCD_H SM50_OS_VRAM_H
#define EXT_BEGIN 0x8c200000u
#define EXT_END   0x8c800000u
#define EXT_STEP  64u
#define OS_CHUNK  4096u
#define OS_MAX_CHUNKS 64

static void *sOsBlocks[OS_MAX_CHUNKS];

static void print_line(int row, const char *text, int color)
{
    char padded[40];
    int i = 0;
    padded[0] = ' ';
    padded[1] = ' ';
    while (i < (int)sizeof(padded) - 3 && text[i]) {
        padded[i + 2] = text[i];
        i++;
    }
    padded[i + 2] = '\0';
    PrintXY(1, row, padded, 0, color);
}

static void screen(const char *a, const char *b, const char *c, const char *d)
{
    Bdisp_AllClr_VRAM();
    print_line(1, "SM50 MEMORY TEST", 7);
    print_line(2, a, 7);
    print_line(3, b, 6);
    print_line(4, c, 3);
    print_line(5, d, 2);
    Bdisp_PutDisp_DD();
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
    volatile u16 *vram = (volatile u16 *)GetVRAMAddress();
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            vram[y * LCD_W + x] = ((x / 32 + y / 24) & 1) ? 0x07e0 : 0x001f;
        }
    }
    int start = RTC_GetTicks();
    unsigned frames = 0;
    do {
        Bdisp_PutDisp_DD();
        ++frames;
    } while ((unsigned)(RTC_GetTicks() - start) < 128u);
    return frames;
}

static int key_down(int code)
{
    const volatile unsigned short *kr = (const volatile unsigned short *)0xa44b0000;
    int row = code % 10, col = code / 10 - 1;
    int word = row >> 1, bit = col + 8 * (row & 1);
    return (kr[word] >> bit) & 1;
}

static void wait_for_key(int wanted)
{
    int key = 0;
    for (;;) {
        GetKey(&key);
        if (key != wanted) continue;
        while (key_down(key)) { }
        return;
    }
}

int main(void)
{
    Bdisp_EnableColor(1);
    EnableStatusArea(3);
    Bdisp_AllClr_VRAM();

    /* Read-only CPG register. No clock register is modified. */
    u32 frqcr = *(volatile u32 *)0xa4150000u;
    unsigned os_heap = measure_os_heap();
    char line[32];
    snprintf(line, sizeof line, "FRQCR 0x%08x", frqcr);
    char heap[32];
    snprintf(heap, sizeof heap, "OS heap %u KB", os_heap / 1024);
    screen(line, heap, "F1: test extra RAM", "EXIT: skip RAM test");

    int key = 0;
    for (;;) {
        GetKey(&key);
        if (key == 79 || key == 47) {
            while (key_down(key)) { }
            break;
        }
    }
    if (key == 79) {
        screen("Scanning 6 MiB RAM", "Zero check is read-only", "Sparse test only if all zero", "Please wait...");
        u32 tested = 0;
        int result = probe_extension(&tested);
        char first[32], second[32];
        if (result == 0) {
            snprintf(first, sizeof first, "6 MiB zero scan PASS");
            snprintf(second, sizeof second, "%u writes restored", tested);
            screen(first, second, "RAM sample passed", "Press key for LCD test");
        } else if (result == 1) {
            screen("RAM region nonzero", "No write test done", "Region left untouched", "Press key for LCD test");
        } else {
            screen("RAM sample failed", "Address/data mismatch", "Tried word restored", "Press key for LCD test");
        }
        wait_for_key(47);
    }

    unsigned fps = benchmark_present();
    char result[32];
    snprintf(result, sizeof result, "LCD transfer %u FPS", fps);
    screen(line, heap, result, "EXIT: return to menu");
    wait_for_key(47);
    return 0;
}
