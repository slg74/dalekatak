/*
 * Dalek Dungeon - Isometric shooter in C with SDL2
 *
 * Controls: W/A/S/D to move, SPACE to fire, ESC to quit
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>

/* ---- Constants ---- */
#define SCREEN_W    1024
#define SCREEN_H    768

/* Isometric tile geometry */
#define TILE_W      64
#define TILE_H      32
#define WALL_H      52

/* World */
#define MAP_W       24
#define MAP_H       24

/* Gameplay */
#define PLAYER_MAX_HP      20
#define ENEMY_MAX_HP       5
#define MAX_ENEMIES        3
#define MAX_BULLETS        64

#define PLAYER_SPEED       0.08f
#define BULLET_SPEED       0.18f
#define ENEMY_SPEED        0.025f

#define PLAYER_FIRE_CD     25   /* frames */
#define ENEMY_FIRE_CD      80
#define ENEMY_AGGRO_RANGE  9.0f
#define BULLET_RANGE       14.0f

/* ---- Map ---- */
/* 0=floor, 1=wall */
static const int MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
    {1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1},
    {1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* ---- Types ---- */
typedef struct {
    float x, y;       /* world position */
    float dx, dy;     /* velocity (normalised * speed) */
    float dist;       /* distance travelled */
    bool  active;
    bool  friendly;   /* true = player shot it */
} Bullet;

typedef struct {
    float x, y;
    int   hp;
    bool  alive;
    int   fire_cd;
} Enemy;

typedef struct {
    float x, y;
    int   hp;
    int   fire_cd;
    float face_x, face_y;
} Player;

/* ---- Globals ---- */
static Player  g_player;
static Enemy   g_enemies[MAX_ENEMIES];
static int     g_num_enemies;
static Bullet  g_bullets[MAX_BULLETS];
static bool    g_game_over;
static bool    g_you_win;

/* ---- Utility ---- */
static bool is_wall(float x, float y) {
    int tx = (int)floorf(x);
    int ty = (int)floorf(y);
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return true;
    return MAP[ty][tx] == 1;
}

static void world_to_screen(float wx, float wy, int *sx, int *sy) {
    /* ISO: camera looks from NW corner */
    int ox = SCREEN_W / 2;
    int oy = SCREEN_H / 5;
    *sx = ox + (int)((wx - wy) * (TILE_W / 2));
    *sy = oy + (int)((wx + wy) * (TILE_H / 2));
}

/* Hit detection in screen space so "what you see is what you hit".
 * vy_offset shifts the target point upward to the visual centre of the sprite. */
static bool screen_hit(float bx, float by, float tx, float ty,
                        float vy_offset, float radius_px) {
    int bsx, bsy, tsx, tsy;
    world_to_screen(bx, by, &bsx, &bsy);
    world_to_screen(tx, ty, &tsx, &tsy);
    tsy -= (int)vy_offset;
    float dx = (float)(bsx - tsx);
    float dy = (float)(bsy - tsy);
    return sqrtf(dx*dx + dy*dy) < radius_px;
}

/* ---- Spawn ---- */
static void spawn_enemies(void) {
    /* Hand-placed in various rooms */
    static const float positions[][2] = {
        { 2.5f,  2.5f},
        {20.5f,  2.5f},
        { 2.5f, 20.5f},
        {20.5f, 20.5f},
        { 7.5f,  7.5f},
        {15.5f,  7.5f},
        { 7.5f, 15.5f},
        {15.5f, 15.5f},
        { 2.5f, 11.5f},
        {20.5f, 11.5f},
        {11.5f,  2.5f},
        {11.5f, 20.5f},
    };
    g_num_enemies = 0;
    for (int i = 0; i < (int)(sizeof(positions)/sizeof(positions[0])); i++) {
        float ex = positions[i][0];
        float ey = positions[i][1];
        if (!is_wall(ex, ey) && g_num_enemies < MAX_ENEMIES) {
            g_enemies[g_num_enemies].x  = ex;
            g_enemies[g_num_enemies].y  = ey;
            g_enemies[g_num_enemies].hp = ENEMY_MAX_HP;
            g_enemies[g_num_enemies].alive    = true;
            g_enemies[g_num_enemies].fire_cd  = rand() % ENEMY_FIRE_CD;
            g_num_enemies++;
        }
    }
}

static void fire_bullet(float x, float y, float tdx, float tdy, bool friendly) {
    float len = sqrtf(tdx*tdx + tdy*tdy);
    if (len < 0.001f) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x        = x;
            g_bullets[i].y        = y;
            g_bullets[i].dx       = (tdx/len) * BULLET_SPEED;
            g_bullets[i].dy       = (tdy/len) * BULLET_SPEED;
            g_bullets[i].dist     = 0.0f;
            g_bullets[i].active   = true;
            g_bullets[i].friendly = friendly;
            return;
        }
    }
}

/* ---- Drawing helpers ---- */

/* Fill an isometric diamond */
static void fill_diamond(SDL_Renderer *r, int cx, int cy, int tw, int th) {
    int hw = tw / 2, hh = th / 2;
    for (int dy = -hh; dy <= hh; dy++) {
        float t = 1.0f - fabsf((float)dy / hh);
        int x0 = cx - (int)(t * hw);
        int x1 = cx + (int)(t * hw);
        SDL_RenderDrawLine(r, x0, cy + dy, x1, cy + dy);
    }
}

static void draw_floor_tile(SDL_Renderer *r, int sx, int sy, bool alt) {
    if (alt) SDL_SetRenderDrawColor(r, 52, 72, 52, 255);
    else     SDL_SetRenderDrawColor(r, 44, 62, 44, 255);
    fill_diamond(r, sx, sy, TILE_W, TILE_H);
    /* outline */
    SDL_SetRenderDrawColor(r, 30, 50, 30, 255);
    SDL_RenderDrawLine(r, sx,          sy - TILE_H/2, sx + TILE_W/2, sy);
    SDL_RenderDrawLine(r, sx + TILE_W/2, sy,          sx,            sy + TILE_H/2);
    SDL_RenderDrawLine(r, sx,          sy + TILE_H/2, sx - TILE_W/2, sy);
    SDL_RenderDrawLine(r, sx - TILE_W/2, sy,          sx,            sy - TILE_H/2);
}

static void draw_wall_tile(SDL_Renderer *r, int sx, int sy) {
    int hw = TILE_W / 2, hh = TILE_H / 2;
    /* Top face */
    SDL_SetRenderDrawColor(r, 130, 110, 90, 255);
    fill_diamond(r, sx, sy - WALL_H, TILE_W, TILE_H);
    /* Left face */
    SDL_SetRenderDrawColor(r, 80, 65, 50, 255);
    for (int h = 0; h < WALL_H; h++) {
        SDL_RenderDrawLine(r,
            sx - hw,         sy - h,
            sx,              sy + hh - h);
    }
    /* Right face */
    SDL_SetRenderDrawColor(r, 105, 85, 65, 255);
    for (int h = 0; h < WALL_H; h++) {
        SDL_RenderDrawLine(r,
            sx,              sy + hh - h,
            sx + hw,         sy - h);
    }
    /* Outline edges */
    SDL_SetRenderDrawColor(r, 40, 30, 20, 255);
    /* Top diamond outline */
    SDL_RenderDrawLine(r, sx,    sy-WALL_H-hh, sx+hw, sy-WALL_H);
    SDL_RenderDrawLine(r, sx+hw, sy-WALL_H,    sx,    sy-WALL_H+hh);
    SDL_RenderDrawLine(r, sx,    sy-WALL_H+hh, sx-hw, sy-WALL_H);
    SDL_RenderDrawLine(r, sx-hw, sy-WALL_H,    sx,    sy-WALL_H-hh);
    /* Vertical edges */
    SDL_RenderDrawLine(r, sx-hw, sy-WALL_H, sx-hw, sy);
    SDL_RenderDrawLine(r, sx+hw, sy-WALL_H, sx+hw, sy);
    SDL_RenderDrawLine(r, sx,    sy-WALL_H+hh, sx, sy+hh);
}

static void draw_humanoid(SDL_Renderer *r, int sx, int sy) {
    /* Shadow */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);
    fill_diamond(r, sx, sy + 2, 20, 6);

    /* Legs */
    SDL_SetRenderDrawColor(r, 30, 70, 150, 255);
    SDL_Rect leg_l = {sx - 9, sy - 14, 7, 14};
    SDL_Rect leg_r = {sx + 2, sy - 14, 7, 14};
    SDL_RenderFillRect(r, &leg_l);
    SDL_RenderFillRect(r, &leg_r);

    /* Torso */
    SDL_SetRenderDrawColor(r, 40, 120, 200, 255);
    SDL_Rect torso = {sx - 10, sy - 30, 20, 16};
    SDL_RenderFillRect(r, &torso);

    /* Arms */
    SDL_SetRenderDrawColor(r, 30, 100, 180, 255);
    SDL_Rect arm_l = {sx - 16, sy - 30, 6, 12};
    SDL_Rect arm_r = {sx + 10, sy - 30, 6, 12};
    SDL_RenderFillRect(r, &arm_l);
    SDL_RenderFillRect(r, &arm_r);

    /* Neck */
    SDL_SetRenderDrawColor(r, 200, 160, 120, 255);
    SDL_Rect neck = {sx - 3, sy - 34, 6, 5};
    SDL_RenderFillRect(r, &neck);

    /* Head */
    SDL_SetRenderDrawColor(r, 220, 180, 130, 255);
    SDL_Rect head = {sx - 7, sy - 46, 14, 14};
    SDL_RenderFillRect(r, &head);

    /* Visor */
    SDL_SetRenderDrawColor(r, 80, 200, 255, 255);
    SDL_Rect visor = {sx - 5, sy - 43, 10, 4};
    SDL_RenderFillRect(r, &visor);
}

static void draw_dalek(SDL_Renderer *r, int sx, int sy) {
    /* Shadow */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);
    fill_diamond(r, sx, sy + 2, 22, 7);

    /* Skirt base */
    SDL_SetRenderDrawColor(r, 160, 160, 170, 255);
    SDL_Rect base = {sx - 13, sy - 14, 26, 14};
    SDL_RenderFillRect(r, &base);

    /* Bumps on skirt */
    SDL_SetRenderDrawColor(r, 120, 120, 130, 255);
    for (int i = 0; i < 3; i++) {
        SDL_Rect bump = {sx - 9 + i * 8, sy - 8, 5, 5};
        SDL_RenderFillRect(r, &bump);
    }

    /* Mid section */
    SDL_SetRenderDrawColor(r, 140, 140, 155, 255);
    SDL_Rect mid = {sx - 10, sy - 24, 20, 10};
    SDL_RenderFillRect(r, &mid);

    /* Dome head */
    SDL_SetRenderDrawColor(r, 120, 120, 180, 255);
    SDL_Rect dome = {sx - 8, sy - 36, 16, 14};
    SDL_RenderFillRect(r, &dome);

    /* Dome top round */
    SDL_SetRenderDrawColor(r, 100, 100, 160, 255);
    SDL_Rect dome_top = {sx - 5, sy - 40, 10, 6};
    SDL_RenderFillRect(r, &dome_top);

    /* Eye stalk */
    SDL_SetRenderDrawColor(r, 80, 80, 90, 255);
    SDL_RenderDrawLine(r, sx + 6, sy - 34, sx + 16, sy - 36);
    SDL_SetRenderDrawColor(r, 255, 60, 60, 255);
    SDL_Rect eye = {sx + 14, sy - 39, 6, 6};
    SDL_RenderFillRect(r, &eye);
    SDL_SetRenderDrawColor(r, 255, 200, 200, 255);
    SDL_Rect eye_hl = {sx + 15, sy - 38, 2, 2};
    SDL_RenderFillRect(r, &eye_hl);

    /* Gun arm */
    SDL_SetRenderDrawColor(r, 90, 90, 100, 255);
    SDL_RenderDrawLine(r, sx - 8, sy - 22, sx - 20, sy - 18);
    SDL_Rect gun_ball = {sx - 23, sy - 21, 6, 6};
    SDL_SetRenderDrawColor(r, 70, 70, 80, 255);
    SDL_RenderFillRect(r, &gun_ball);

    /* Plunger arm */
    SDL_SetRenderDrawColor(r, 90, 90, 100, 255);
    SDL_RenderDrawLine(r, sx + 8, sy - 22, sx + 20, sy - 18);
    SDL_Rect plunger = {sx + 19, sy - 22, 4, 8};
    SDL_SetRenderDrawColor(r, 100, 80, 60, 255);
    SDL_RenderFillRect(r, &plunger);
}

static void draw_hp_bar(SDL_Renderer *r, int sx, int sy, int hp, int max_hp) {
    int bar_w = 32, bar_h = 4;
    int bx = sx - bar_w/2, by = sy - 54;
    SDL_SetRenderDrawColor(r, 40, 0, 0, 220);
    SDL_Rect bg = {bx, by, bar_w, bar_h};
    SDL_RenderFillRect(r, &bg);
    int fill = (int)((float)hp / max_hp * bar_w);
    if (fill < 0) fill = 0;
    SDL_SetRenderDrawColor(r, 0, 210, 60, 255);
    SDL_Rect fg = {bx, by, fill, bar_h};
    SDL_RenderFillRect(r, &fg);
}

/* Draw a bullet */
static void draw_bullet(SDL_Renderer *r, float wx, float wy, bool friendly) {
    int sx, sy;
    world_to_screen(wx, wy, &sx, &sy);
    if (friendly) {
        SDL_SetRenderDrawColor(r, 0, 210, 255, 255);
    } else {
        SDL_SetRenderDrawColor(r, 255, 110, 0, 255);
    }
    SDL_Rect br = {sx - 3, sy - 4, 6, 6};
    SDL_RenderFillRect(r, &br);
    /* glow */
    if (friendly) SDL_SetRenderDrawColor(r, 100, 240, 255, 120);
    else          SDL_SetRenderDrawColor(r, 255, 200, 80, 120);
    SDL_Rect glow = {sx - 5, sy - 6, 10, 10};
    SDL_RenderDrawRect(r, &glow);
}

/* Overlay text via coloured rectangles (no font needed) */
static void draw_overlay(SDL_Renderer *r, bool game_over, bool you_win) {
    if (!game_over && !you_win) return;
    /* Dim screen */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Banner */
    int bw = 400, bh = 80;
    int bx = (SCREEN_W - bw)/2, by = (SCREEN_H - bh)/2;
    if (game_over) SDL_SetRenderDrawColor(r, 180, 20, 20, 255);
    else           SDL_SetRenderDrawColor(r, 20, 160, 40, 255);
    SDL_Rect banner = {bx, by, bw, bh};
    SDL_RenderFillRect(r, &banner);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &banner);
    /* Thin decorative inner line */
    SDL_Rect inner = {bx+4, by+4, bw-8, bh-8};
    SDL_RenderDrawRect(r, &inner);

    /* Pixel-art style letters: GAME OVER or YOU WIN (coloured bars) */
    /* Using simple coloured rectangles as stand-in labels */
    if (game_over) {
        /* Red pulses on banner */
        SDL_SetRenderDrawColor(r, 255, 80, 80, 255);
        SDL_Rect line1 = {bx+20, by+20, bw-40, 15};
        SDL_Rect line2 = {bx+20, by+45, bw-40, 15};
        SDL_RenderFillRect(r, &line1);
        SDL_RenderFillRect(r, &line2);
    } else {
        SDL_SetRenderDrawColor(r, 80, 255, 120, 255);
        SDL_Rect line1 = {bx+20, by+25, bw-40, 30};
        SDL_RenderFillRect(r, &line1);
    }
}

/* ---- HUD ---- */
static void draw_hud(SDL_Renderer *r, int player_hp) {
    /* Background bar */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_Rect hud_bg = {8, 8, 220, 26};
    SDL_RenderFillRect(r, &hud_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* HP fill */
    float ratio = (float)player_hp / PLAYER_MAX_HP;
    if (ratio < 0) ratio = 0;
    int r_val = (int)(255 * (1.0f - ratio));
    int g_val = (int)(200 * ratio);
    SDL_SetRenderDrawColor(r, r_val, g_val, 30, 255);
    SDL_Rect hp_fill = {12, 12, (int)(210 * ratio), 18};
    SDL_RenderFillRect(r, &hp_fill);

    SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
    SDL_Rect hp_outline = {12, 12, 210, 18};
    SDL_RenderDrawRect(r, &hp_outline);

    /* Enemies remaining */
    int alive = 0;
    for (int i = 0; i < g_num_enemies; i++) if (g_enemies[i].alive) alive++;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect ecnt_bg = {8, 40, 100, 20};
    SDL_RenderFillRect(r, &ecnt_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    /* Draw alive-count as small red squares */
    for (int i = 0; i < alive && i < 12; i++) {
        SDL_SetRenderDrawColor(r, 200, 60, 60, 255);
        SDL_Rect dot = {12 + i*7, 44, 5, 12};
        SDL_RenderFillRect(r, &dot);
    }
    SDL_SetRenderDrawColor(r, 150, 50, 50, 255);
    SDL_Rect ec_border = {10, 42, 95, 16};
    SDL_RenderDrawRect(r, &ec_border);
}

/* ---- Main ---- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Dalek Dungeon",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "Window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { fprintf(stderr, "Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* Init game state */
    memset(g_bullets, 0, sizeof(g_bullets));
    g_player.x      = 11.5f;
    g_player.y      = 11.5f;
    g_player.hp     = PLAYER_MAX_HP;
    g_player.fire_cd= 0;
    g_player.face_x = 1.0f;
    g_player.face_y = 0.0f;
    g_game_over     = false;
    g_you_win       = false;
    spawn_enemies();

    bool keys[SDL_NUM_SCANCODES];
    memset(keys, 0, sizeof(keys));
    bool running = true;

    while (running) {
        /* --- Events --- */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                keys[ev.key.keysym.scancode] = true;
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
                /* Restart */
                if ((g_game_over || g_you_win) && ev.key.keysym.scancode == SDL_SCANCODE_R) {
                    memset(g_bullets, 0, sizeof(g_bullets));
                    g_player.x      = 11.5f;
                    g_player.y      = 11.5f;
                    g_player.hp     = PLAYER_MAX_HP;
                    g_player.fire_cd= 0;
                    g_player.face_x = 1.0f;
                    g_player.face_y = 0.0f;
                    g_game_over     = false;
                    g_you_win       = false;
                    spawn_enemies();
                }
            }
            if (ev.type == SDL_KEYUP) keys[ev.key.keysym.scancode] = false;
        }

        if (!g_game_over && !g_you_win) {
            /* --- Player movement --- */
            float mdx = 0.0f, mdy = 0.0f;
            /* Map screen directions to world-space diagonals for ISO projection:
             * screen-left  = world (-1, +1), screen-right = world (+1, -1)
             * screen-up    = world (-1, -1), screen-down  = world (+1, +1) */
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    { mdx -= 1.0f; mdy -= 1.0f; }
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  { mdx += 1.0f; mdy += 1.0f; }
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  { mdx -= 1.0f; mdy += 1.0f; }
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) { mdx += 1.0f; mdy -= 1.0f; }

            if (mdx != 0.0f || mdy != 0.0f) {
                float mlen = sqrtf(mdx*mdx + mdy*mdy);
                mdx /= mlen; mdy /= mlen;
                g_player.face_x = mdx;
                g_player.face_y = mdy;
                float cr = 0.28f;
                float nx = g_player.x + mdx * PLAYER_SPEED;
                float ny = g_player.y + mdy * PLAYER_SPEED;
                if (!is_wall(nx + cr, g_player.y) && !is_wall(nx - cr, g_player.y))
                    g_player.x = nx;
                if (!is_wall(g_player.x, ny + cr) && !is_wall(g_player.x, ny - cr))
                    g_player.y = ny;
            }

            /* --- Player fire --- */
            if (g_player.fire_cd > 0) g_player.fire_cd--;
            if (keys[SDL_SCANCODE_SPACE] && g_player.fire_cd == 0) {
                fire_bullet(g_player.x, g_player.y,
                            g_player.face_x, g_player.face_y, true);
                g_player.fire_cd = PLAYER_FIRE_CD;
            }

            /* --- Update bullets --- */
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!g_bullets[i].active) continue;
                g_bullets[i].x    += g_bullets[i].dx;
                g_bullets[i].y    += g_bullets[i].dy;
                g_bullets[i].dist += BULLET_SPEED;

                if (g_bullets[i].dist > BULLET_RANGE || is_wall(g_bullets[i].x, g_bullets[i].y)) {
                    g_bullets[i].active = false;
                    continue;
                }

                if (g_bullets[i].friendly) {
                    /* hit enemies — check in screen space, offset to dalek visual centre */
                    for (int j = 0; j < g_num_enemies; j++) {
                        if (!g_enemies[j].alive) continue;
                        if (screen_hit(g_bullets[i].x, g_bullets[i].y,
                                       g_enemies[j].x, g_enemies[j].y,
                                       22.0f, 28.0f)) {
                            int dmg = 1 + rand() % 5;  /* 1-5 */
                            g_enemies[j].hp -= dmg;
                            if (g_enemies[j].hp <= 0) g_enemies[j].alive = false;
                            g_bullets[i].active = false;
                            break;
                        }
                    }
                } else {
                    /* hit player — offset to humanoid visual centre */
                    if (screen_hit(g_bullets[i].x, g_bullets[i].y,
                                   g_player.x, g_player.y,
                                   26.0f, 24.0f)) {
                        int dmg = 1 + rand() % 3;  /* 1-3 */
                        g_player.hp -= dmg;
                        if (g_player.hp < 0) g_player.hp = 0;
                        g_bullets[i].active = false;
                    }
                }
            }

            /* --- Update enemies --- */
            for (int i = 0; i < g_num_enemies; i++) {
                if (!g_enemies[i].alive) continue;
                float edx = g_player.x - g_enemies[i].x;
                float edy = g_player.y - g_enemies[i].y;
                float dist = sqrtf(edx*edx + edy*edy);

                if (dist < ENEMY_AGGRO_RANGE) {
                    /* Move toward player */
                    float cr = 0.28f;
                    float nx = g_enemies[i].x + (edx/dist) * ENEMY_SPEED;
                    float ny = g_enemies[i].y + (edy/dist) * ENEMY_SPEED;
                    if (!is_wall(nx + cr, g_enemies[i].y) && !is_wall(nx - cr, g_enemies[i].y))
                        g_enemies[i].x = nx;
                    if (!is_wall(g_enemies[i].x, ny + cr) && !is_wall(g_enemies[i].x, ny - cr))
                        g_enemies[i].y = ny;

                    /* Shoot */
                    if (g_enemies[i].fire_cd > 0) g_enemies[i].fire_cd--;
                    if (g_enemies[i].fire_cd == 0 && dist < ENEMY_AGGRO_RANGE - 1.0f) {
                        /* add small random spread */
                        float spread = ((float)(rand() % 200) - 100.0f) / 1000.0f;
                        fire_bullet(g_enemies[i].x, g_enemies[i].y,
                                    edx + spread, edy + spread, false);
                        g_enemies[i].fire_cd = ENEMY_FIRE_CD;
                    }
                }
            }

            /* --- Check win/lose --- */
            if (g_player.hp <= 0) g_game_over = true;
            int alive_count = 0;
            for (int i = 0; i < g_num_enemies; i++) if (g_enemies[i].alive) alive_count++;
            if (alive_count == 0) g_you_win = true;
        }

        /* ====== RENDER ====== */
        SDL_SetRenderDrawColor(ren, 15, 18, 25, 255);
        SDL_RenderClear(ren);

        /*
         * Isometric painter's algorithm: draw tiles along diagonals
         * from back (low wx+wy) to front (high wx+wy).
         * Interleave entity drawing at their tile depth.
         */
        for (int diag = 0; diag < MAP_W + MAP_H - 1; diag++) {
            /* Tiles on this diagonal */
            for (int ty = 0; ty < MAP_H; ty++) {
                int tx = diag - ty;
                if (tx < 0 || tx >= MAP_W) continue;

                int sx, sy;
                world_to_screen((float)tx + 0.5f, (float)ty + 0.5f, &sx, &sy);

                if (MAP[ty][tx] == 1) {
                    draw_wall_tile(ren, sx, sy);
                } else {
                    draw_floor_tile(ren, sx, sy, (tx + ty) % 2 == 0);
                }
            }

            /* Enemies whose floor tile is on this diagonal */
            for (int i = 0; i < g_num_enemies; i++) {
                if (!g_enemies[i].alive) continue;
                int etx = (int)floorf(g_enemies[i].x);
                int ety = (int)floorf(g_enemies[i].y);
                if (etx + ety != diag) continue;
                int sx, sy;
                world_to_screen(g_enemies[i].x, g_enemies[i].y, &sx, &sy);
                draw_dalek(ren, sx, sy);
                draw_hp_bar(ren, sx, sy, g_enemies[i].hp, ENEMY_MAX_HP);
            }

            /* Player */
            {
                int ptx = (int)floorf(g_player.x);
                int pty = (int)floorf(g_player.y);
                if (ptx + pty == diag) {
                    int sx, sy;
                    world_to_screen(g_player.x, g_player.y, &sx, &sy);
                    draw_humanoid(ren, sx, sy);
                }
            }
        }

        /* Bullets drawn on top (they're small, depth sorting less critical) */
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!g_bullets[i].active) continue;
            draw_bullet(ren, g_bullets[i].x, g_bullets[i].y, g_bullets[i].friendly);
        }

        /* HUD */
        draw_hud(ren, g_player.hp);

        /* Overlay */
        draw_overlay(ren, g_game_over, g_you_win);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
