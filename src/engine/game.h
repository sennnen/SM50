#ifndef SM50_GAME_H
#define SM50_GAME_H
#include "types.h"
#include "fixmath.h"

/* input buttons */
#define BTN_UP 0x0001
#define BTN_DOWN 0x0002
#define BTN_LEFT 0x0004
#define BTN_RIGHT 0x0008
#define BTN_JUMP 0x0010
#define BTN_ACTION 0x0020
#define BTN_CAM_L 0x0040
#define BTN_CAM_R 0x0080
#define BTN_CAM_ZOOM 0x0100
#define BTN_PAUSE 0x0200
#define BTN_QUIT 0x0400

/* player actions */
enum {
    ACT_IDLE, ACT_WALK, ACT_JUMP, ACT_DOUBLE_JUMP, ACT_TRIPLE_JUMP, ACT_FREEFALL, ACT_LAND,
    ACT_GROUND_POUND, ACT_WALL_KICK, ACT_KNOCKBACK, ACT_SLIDE, ACT_WIN, ACT_COUNT
};
/* animation ids in the pack */
enum { ANIM_IDLE, ANIM_WALK, ANIM_RUN, ANIM_JUMP, ANIM_FALL, ANIM_LAND, ANIM_FLIP, ANIM_HURT };

typedef struct {
    s32 x, y, z;          /* Q8 */
    s32 vy;               /* Q8 per frame */
    s32 fwd;              /* forward speed, Q8 per frame */
    s32 sx, sz;           /* sliding / knockback velocity Q8 */
    u16 face_yaw;         /* facing direction */
    u16 roll;             /* flip rotation (triple jump) */
    int action, action_timer, prev_action;
    int jump_chain, chain_timer;
    int wall_timer; u16 wall_yaw;
    s32 floor_y;          /* Q8 */
    s32 floor_ny;
    int on_ground;
    int anim, anim_frame;  /* frame in Q4 for speed control */
    int health, coins, red_coins, stars, invuln, lives;
    s32 safe_x, safe_y, safe_z;
} Player;

typedef struct {
    u16 yaw; s16 pitch; s32 dist, target_dist;
    s32 x, y, z;        /* world units */
    s32 fx, fy, fz;     /* focus */
    int zoom;
} Camera;

typedef struct {
    Player p;
    Camera cam;
    u32 frame, ticks;
    u16 buttons, pressed, prev_buttons;
    int paused, won, win_timer, title, stars_total;
    char msg[32]; int msg_timer;
    u32 perf_logic_us, perf_render_us;
} Game;

extern Game gGame;

int game_init(const void *pack, u32 size, u8 *work, u32 work_size);
void game_tick(u16 buttons);   /* one 30 Hz logic step */
void game_render(int show_fps, int fps10);
void player_tick(Player *p, u16 buttons, u16 pressed, u16 cam_yaw);
void player_spawn(Player *p);
#endif
