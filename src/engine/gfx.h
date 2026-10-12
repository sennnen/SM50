/* SM50 renderer: a Fast3D (F3D) display-list interpreter feeding a
 * fixed-point, perspective-correct, z-buffered software rasteriser.
 * Display lists, vertices, lights, matrices and textures use the native
 * N64 big-endian binary layouts; addresses inside display lists are
 * segmented (segment number in the top byte) and resolved through the
 * segment table, exactly as the RSP does. */
#ifndef SM50_GFX_H
#define SM50_GFX_H
#include "types.h"
#include "fixmath.h"

/* F3D opcodes */
#define G_SPNOOP 0x00
#define G_MTX 0x01
#define G_MOVEMEM 0x03
#define G_VTX 0x04
#define G_DL 0x06
#define G_RDPHALF_CONT 0xB2
#define G_RDPHALF_2 0xB3
#define G_RDPHALF_1 0xB4
#define G_CLEARGEOMETRYMODE 0xB6
#define G_SETGEOMETRYMODE 0xB7
#define G_ENDDL 0xB8
#define G_SETOTHERMODE_L 0xB9
#define G_SETOTHERMODE_H 0xBA
#define G_TEXTURE 0xBB
#define G_MOVEWORD 0xBC
#define G_POPMTX 0xBD
#define G_CULLDL 0xBE
#define G_TRI1 0xBF
#define G_NOOP 0xC0
#define G_TEXRECT 0xE4
#define G_RDPLOADSYNC 0xE6
#define G_RDPPIPESYNC 0xE7
#define G_RDPTILESYNC 0xE8
#define G_RDPFULLSYNC 0xE9
#define G_SETSCISSOR 0xED
#define G_LOADTLUT 0xF0
#define G_SETTILESIZE 0xF2
#define G_LOADBLOCK 0xF3
#define G_LOADTILE 0xF4
#define G_SETTILE 0xF5
#define G_FILLRECT 0xF6
#define G_SETFILLCOLOR 0xF7
#define G_SETFOGCOLOR 0xF8
#define G_SETBLENDCOLOR 0xF9
#define G_SETPRIMCOLOR 0xFA
#define G_SETENVCOLOR 0xFB
#define G_SETCOMBINE 0xFC
#define G_SETTIMG 0xFD
#define G_SETZIMG 0xFE
#define G_SETCIMG 0xFF

/* geometry mode bits (F3D) */
#define G_ZBUFFER 0x00000001
#define G_SHADE 0x00000004
#define G_SHADING_SMOOTH 0x00000200
#define G_CULL_FRONT 0x00001000
#define G_CULL_BACK 0x00002000
#define G_FOG 0x00010000
#define G_LIGHTING 0x00020000
#define G_TEXTURE_GEN 0x00040000

/* texture formats */
#define G_IM_FMT_RGBA 0
#define G_IM_FMT_YUV 1
#define G_IM_FMT_CI 2
#define G_IM_FMT_IA 3
#define G_IM_FMT_I 4
#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3

/* movemem / moveword indices (F3D) */
#define G_MV_VIEWPORT 0x80
#define G_MV_L0 0x86
#define G_MV_L1 0x88
#define G_MV_L2 0x8a
#define G_MW_NUMLIGHT 0x02
#define G_MW_SEGMENT 0x06
#define G_MW_FOG 0x08

#define GFX_MAX_SEGMENTS 32
#define GFX_TRANSPARENT 0xF81F /* colour key used for transparent texels */

typedef struct {
    u32 tris_in, tris_drawn, tris_culled, pixels, dl_cmds, vtx_xform, tex_converts;
} GfxStats;

extern GfxStats gGfxStats;

void gfx_init(u16 *colorbuf, u16 *zbuf, int width, int height, u8 *texcache, u32 texcache_size);
void gfx_set_segment(int seg, const void *base);
void gfx_set_segment_sized(int seg, const void *base, u32 size);
const u8 *gfx_seg_to_ptr(u32 addr);
/* fov_deg: vertical field of view, near/far in world units */
void gfx_set_perspective(int fov_deg, s32 near_z, s32 far_z);
void gfx_set_view(const Mat34 *view);
const Mat34 *gfx_get_view(void);
void gfx_clear(u16 color);
void gfx_clear_depth(void);
/* Model transform relative to the world; the modelview becomes view * model. */
void gfx_set_model(const Mat34 *model);
/* Model at position with rotation replaced by the camera-facing (billboard) basis. */
void gfx_set_model_billboard(s32 x, s32 y, s32 z, s32 scale14);
void gfx_run_dl(u32 seg_addr);
void gfx_run_dl_ptr(const u8 *dl);
void gfx_reset_state(void);
/* Force translucent blending (50%, no depth write) regardless of render mode. */
void gfx_force_xlu(int on);
/* Project a world point to screen. Returns 0 if behind the camera. */
int gfx_project_point(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *depth);
/* Sphere visibility test in world coordinates. */
int gfx_sphere_visible(s32 x, s32 y, s32 z, s32 radius);
/* Change the render size (buffers must be large enough); call gfx_set_perspective after. */
void gfx_resize(int width, int height);
int gfx_width(void);
int gfx_height(void);
u16 *gfx_colorbuf(void);

/* 2D helpers for the HUD */
void gfx_fill_rect(int x, int y, int w, int h, u16 color);
void gfx_blend_rect(int x, int y, int w, int h, u16 color);
void gfx_draw_text(int x, int y, const char *s, u16 color, int scale);
int gfx_text_width(const char *s, int scale);

static inline u16 rgb565(int r, int g, int b) {
    return (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
#endif
