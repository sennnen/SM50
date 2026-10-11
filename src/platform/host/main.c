/* Host (Linux/macOS) headless runner: deterministic, scripted input,
 * PPM screenshots, per-frame state log. Used for automated tests. */
#include "../../engine/game.h"
#include "../../engine/gfx.h"
#include "../../engine/world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static u16 *sFb, *sZb;
static u8 sTexCache[96 * 1024];
static u8 sColWork[64 * 1024];

static struct { int frame; u16 buttons; } sScript[1024];
static int sScriptLen;

static u16 parse_buttons(char *s) {
    u16 b = 0;
    for (char *t = strtok(s, " \t\r\n,+"); t; t = strtok(NULL, " \t\r\n,+")) {
        if (!strcmp(t, "up")) b |= BTN_UP; else if (!strcmp(t, "down")) b |= BTN_DOWN;
        else if (!strcmp(t, "left")) b |= BTN_LEFT; else if (!strcmp(t, "right")) b |= BTN_RIGHT;
        else if (!strcmp(t, "jump")) b |= BTN_JUMP; else if (!strcmp(t, "action")) b |= BTN_ACTION;
        else if (!strcmp(t, "caml")) b |= BTN_CAM_L; else if (!strcmp(t, "camr")) b |= BTN_CAM_R;
        else if (!strcmp(t, "zoom")) b |= BTN_CAM_ZOOM; else if (!strcmp(t, "pause")) b |= BTN_PAUSE;
    }
    return b;
}

static void load_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f) && sScriptLen < 1024) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *rest; int fr = (int)strtol(line, &rest, 10);
        sScript[sScriptLen].frame = fr;
        sScript[sScriptLen].buttons = parse_buttons(rest);
        sScriptLen++;
    }
    fclose(f);
}

static void write_ppm(const char *path, int w, int h, int scale) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w * scale, h * scale);
    for (int y = 0; y < h * scale; y++)
        for (int x = 0; x < w * scale; x++) {
            u16 c = sFb[(y / scale) * w + x / scale];
            u8 rgb[3] = { (u8)(((c >> 11) & 31) * 255 / 31), (u8)(((c >> 5) & 63) * 255 / 63), (u8)((c & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *pack_path = "content/sm50.pack", *outdir = ".", *keys = NULL, *log_path = NULL;
    int frames = 120, shot_every = 0, w = 198, h = 112, scale = 2;
    int shots[64], nshots = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else if (!strcmp(argv[i], "--outdir") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-at") && i + 1 < argc) {
            for (char *t = strtok(argv[++i], ","); t && nshots < 64; t = strtok(NULL, ",")) shots[nshots++] = atoi(t);
        } else if (!strcmp(argv[i], "--res") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &w, &h); scale = w >= 300 ? 1 : 2; }
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    if (keys) load_script(keys);
    FILE *pf = fopen(pack_path, "rb");
    if (!pf) { perror(pack_path); return 1; }
    fseek(pf, 0, SEEK_END); long psize = ftell(pf); fseek(pf, 0, SEEK_SET);
    u8 *pack = malloc(psize);
    if (fread(pack, 1, psize, pf) != (size_t)psize) return 1;
    fclose(pf);
    sFb = malloc(w * h * 2); sZb = malloc(w * h * 2);
    gfx_init(sFb, sZb, w, h, sTexCache, sizeof sTexCache);
    gfx_set_perspective(50, 64, 16000);
    int r = game_init(pack, (u32)psize, sColWork, sizeof sColWork);
    if (r) { fprintf(stderr, "game_init failed: %d\n", r); return 1; }
    printf("level '%s': %d chunks, %u col tris, %d objects, %d parts, %d anims\n", gLevel.name, gLevel.n_chunks,
           col_tri_count(), gLevel.n_objs, gLevel.n_parts, gLevel.n_anims);
    FILE *lf = log_path ? fopen(log_path, "w") : NULL;
    u16 held = 0; int si = 0;
    u64 tris = 0, px = 0;
    clock_t t0 = clock();
    for (int f = 0; f < frames; f++) {
        while (si < sScriptLen && sScript[si].frame <= f) held = sScript[si++].buttons;
        game_tick(held);
        memset(&gGfxStats, 0, sizeof gGfxStats);
        game_render(1, 300);
        tris += gGfxStats.tris_drawn; px += gGfxStats.pixels;
        const Player *p = &gGame.p;
        if (lf) fprintf(lf, "%d x=%d y=%d z=%d act=%d ground=%d fwd=%d vy=%d coins=%d health=%d stars=%d tris=%u px=%u\n", f,
                        p->x >> 8, p->y >> 8, p->z >> 8, p->action, p->on_ground, p->fwd >> 8, p->vy >> 8, p->coins,
                        p->health, p->stars, gGfxStats.tris_drawn, gGfxStats.pixels);
        int shoot = shot_every && (f % shot_every == shot_every - 1);
        for (int k = 0; k < nshots; k++) if (shots[k] == f) shoot = 1;
        if (shoot) {
            char path[512]; snprintf(path, sizeof path, "%s/frame_%04d.ppm", outdir, f);
            write_ppm(path, w, h, scale);
        }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("frames=%d avg_tris=%.1f avg_pixels=%.0f host_ms_per_frame=%.3f\n", frames, (double)tris / frames,
           (double)px / frames, secs * 1000.0 / frames);
    const Player *p = &gGame.p;
    printf("final x=%d y=%d z=%d act=%d coins=%d health=%d stars=%d\n", p->x >> 8, p->y >> 8, p->z >> 8, p->action,
           p->coins, p->health, p->stars);
    if (lf) fclose(lf);
    return 0;
}
