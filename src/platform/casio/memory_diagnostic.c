/* Separate opt-in fx-CG50 memory/clock diagnostic.
 * The main SM50 add-in and its linker layout are not changed by this build. */
#include "../../engine/gfx.h"
#include "display_layout.h"
#include <stdio.h>
#include <string.h>

void Bdisp_PutDisp_DD(void);
void Bdisp_AllClr_VRAM(void);
void Bdisp_EnableColor(int n);
void EnableStatusArea(int opt);
void PrintXY(int x, int y, char *message, int mode, int color);
int GetKey(int *key);
void *sys_malloc(unsigned int size);
void sys_free(void *ptr);

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

/* Sample the community-reported extension with reversible address/data
 * checks. Two locations are held at distinct patterns together so mirrored
 * addresses cannot pass merely because the same value reads back. */
static int probe_extension(u32 *tested)
{
    volatile u32 *base = (volatile u32 *)EXT_BEGIN;
    *tested = 0;
    for (u32 off = EXT_STEP; off < EXT_END - EXT_BEGIN; off += EXT_STEP) {
        volatile u32 *q = (volatile u32 *)(EXT_BEGIN + off);
        u32 old_base = *base, old_target = *q;
        u32 pat_base = EXT_BEGIN ^ 0x5aa5c33cu;
        u32 pat_target = (EXT_BEGIN + off) ^ 0xa55a39c7u;
        *base = pat_base;
        *q = pat_target;
        u32 got_base = *base, got_target = *q;
        *q = old_target;
        *base = old_base;
        if (got_base != pat_base || got_target != pat_target ||
            *base != old_base || *q != old_target) return 2;
        ++*tested;
    }
    return 0;
}

static int wait_for_key(void)
{
    int key = 0;
    GetKey(&key);
    return key;
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
    screen(line, heap, "Any key: RAM probe", "EXIT: skip; back up first");

    int key = wait_for_key();
    if (key != 47) {
        screen("Sampling 6 MiB RAM", "Writes restored at once", "Checking data/address lines", "Please wait...");
        u32 tested = 0;
        int result = probe_extension(&tested);
        char first[32], second[32];
        if (result == 0) {
            snprintf(first, sizeof first, "6 MiB sample PASS");
            snprintf(second, sizeof second, "%u pairs restored", tested);
            screen(first, second, "RAM data/address OK", "Any key: return to menu");
        } else {
            snprintf(first, sizeof first, "RAM sample failed");
            snprintf(second, sizeof second, "after %u pairs", tested);
            screen(first, second, "Address/data mismatch", "Test values restored");
        }
        wait_for_key();
    }

    else {
        screen(line, heap, "RAM test skipped", "Any key: return to menu");
        wait_for_key();
    }
    return 0;
}
