#ifndef SM50_DISPLAY_LAYOUT_H
#define SM50_DISPLAY_LAYOUT_H

/* GetVRAMAddress() exposes the OS's centered 384x216 RGB565 surface.
 * The physical LCD is 396x224; the surrounding border is not writable VRAM. */
#define SM50_OS_VRAM_W 384
#define SM50_OS_VRAM_H 216
#define SM50_OS_VRAM_PIXELS (SM50_OS_VRAM_W * SM50_OS_VRAM_H)
#define SM50_OS_VRAM_BYTES (SM50_OS_VRAM_PIXELS * 2)

#define SM50_RENDER_2X_W 192
#define SM50_RENDER_2X_H 108
#define SM50_RENDER_3X_W 128
#define SM50_RENDER_3X_H 72

#define SM50_VRAM_2X_LAST_PIXEL \
    (((SM50_RENDER_2X_H - 1) * 2 + 1) * SM50_OS_VRAM_W \
     + (SM50_RENDER_2X_W - 1) * 2 + 1)
#define SM50_VRAM_3X_LAST_PIXEL \
    (((SM50_RENDER_3X_H - 1) * 3 + 2) * SM50_OS_VRAM_W \
     + (SM50_RENDER_3X_W - 1) * 3 + 2)

#if SM50_RENDER_2X_W * 2 != SM50_OS_VRAM_W || SM50_RENDER_2X_H * 2 != SM50_OS_VRAM_H
#error "2x renderer must exactly cover the OS VRAM surface"
#endif
#if SM50_RENDER_3X_W * 3 != SM50_OS_VRAM_W || SM50_RENDER_3X_H * 3 != SM50_OS_VRAM_H
#error "3x renderer must exactly cover the OS VRAM surface"
#endif

#endif
