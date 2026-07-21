// 디스켓 던전 — The Last Read
// 공모전 제약: 단독 exe, 압축해제 후 1,474,560 bytes 이하, 외부 파일 없음.
// 모든 에셋은 절차 생성 (코드가 그래픽/사운드를 만든다).
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "vendor/sokol_app.h"
#include "vendor/sokol_gfx.h"
#include "vendor/sokol_glue.h"
#include "vendor/sokol_log.h"
#include "vendor/sokol_time.h"
#include "vendor/sokol_audio.h"
#include "vendor/sokol_gl.h"

// ---------------------------------------------------------------- constants
#define VIRT_W 480
#define VIRT_H 270
#define GLOW_W 120
#define GLOW_H 68
#define TILE 16
#define MAX_ROOM_W 45      // 최대 방 폭 (타일)
#define MAX_ROOM_H 28      // 최대 방 높이 (타일)
#define DT (1.0f/60.0f)

// 방 벽 방향 (문/입구): 오른쪽/아래/왼쪽/위
enum { DIR_R=0, DIR_D=1, DIR_L=2, DIR_U=3 };
static inline int opposite(int dir){ return (dir+2)&3; }

#define DISK_BYTES 1474560 // 1.44MB — 가방 총용량이자 공모전 용량 제한
#define MAX_ENTITIES 256
#define MAX_BULLETS 512
#define MAX_PARTICLES 2048
#define MAX_LIGHTS 24
#define MAX_FLOATERS 32
#define MAX_PICKUPS 64

// palette (01-art-direction.md)
#define C_DARK0   0xFF100710u  // ABGR packed? we use RGBA helpers instead
// colors are passed as float triplets via col3(); hex helpers below.

typedef struct { float x, y; } v2;
static inline v2 V2(float x, float y){ v2 v={x,y}; return v; }
static inline v2 v2add(v2 a, v2 b){ return V2(a.x+b.x, a.y+b.y); }
static inline v2 v2sub(v2 a, v2 b){ return V2(a.x-b.x, a.y-b.y); }
static inline v2 v2scale(v2 a, float s){ return V2(a.x*s, a.y*s); }
static inline float v2len(v2 a){ return sqrtf(a.x*a.x+a.y*a.y); }
static inline v2 v2norm(v2 a){ float l=v2len(a); return l>0.0001f? v2scale(a,1.0f/l):V2(0,0); }
static inline float clampf(float v, float a, float b){ return v<a?a:(v>b?b:v); }
static inline float lerpf(float a, float b, float t){ return a+(b-a)*t; }
static inline float fractf(float x){ return x - floorf(x); }

// deterministic rng (xorshift) — 시드 기반 던전 재현
typedef struct { uint64_t s; } Rng;
static inline uint32_t rng_u32(Rng* r){
    uint64_t x = r->s; x ^= x<<13; x ^= x>>7; x ^= x<<17; r->s = x;
    return (uint32_t)(x >> 32);
}
static inline float rng_f(Rng* r){ return (rng_u32(r) >> 8) * (1.0f/16777216.0f); }
static inline float rng_range(Rng* r, float a, float b){ return a + rng_f(r)*(b-a); }
static inline int rng_i(Rng* r, int n){ return n<=0?0:(int)(rng_u32(r) % (uint32_t)n); }

// ---------------------------------------------------------------- render API
typedef struct { float r,g,b; } col3;
static inline col3 COL(uint32_t hex){ // 0xRRGGBB
    col3 c = { ((hex>>16)&255)/255.0f, ((hex>>8)&255)/255.0f, (hex&255)/255.0f };
    return c;
}

void render_init(void);
void render_begin_frame(void);
void render_set_ambient(float a);
void render_set_scanline(float s);
void render_end_frame(float time, float flash, float fade, col3 fade_col, float shake_x, float shake_y);
void render_shutdown(void);

// drawing contexts
void draw_scene_begin(float cam_x, float cam_y);
void draw_light_begin(float cam_x, float cam_y);
void draw_glow_begin(float cam_x, float cam_y);
void draw_ui_begin(void);

// primitives (current sgl context)
void draw_quad(float x, float y, float w, float h, col3 c, float a);
void draw_sprite(int sprite_id, float x, float y, float w, float h, col3 tint, float a, bool flip_x, float rot);
void draw_codex_sprite(int sprite_id, float x, float y, float w, float h, col3 tint, float a);
void draw_light_blob(float x, float y, float radius, col3 c, float intensity);
void draw_shadowed_light(float x, float y, float radius, col3 c, float intensity); // player key light w/ shadows
void draw_glow_blob(float x, float y, float radius, col3 c, float intensity);
void draw_line(float x0,float y0,float x1,float y1,float thick,col3 c,float a);

// text
float text_width(const char* utf8, float scale);
void draw_text(const char* utf8, float x, float y, float scale, col3 c, float a);
void draw_text_center(const char* utf8, float cx, float y, float scale, col3 c, float a);

// occluders for the key-light shadow pass (filled per-frame by game)
void shadow_clear(void);
void shadow_add_box(float x, float y, float w, float h);

// sprites — procedural atlas ids (assets.c)
enum {
    SPR_WHITE = 0,    // 1x1 white
    SPR_FLAME,        // player ember
    SPR_SLIME, SPR_BAT, SPR_WRAITH, SPR_CHASER, SPR_GOLEM, SPR_TURRET, SPR_SENTINEL, SPR_DRONE,
    SPR_BOMBER, SPR_SNIPER, SPR_SHIELDER, SPR_HIVE,
    SPR_BOSS_ROT, SPR_BOSS_ECHO, SPR_BOSS_DEFRAG, SPR_BOSS_NULL,
    SPR_SWORD, SPR_CANNON, SPR_SPRAY, SPR_GLAIVE, SPR_LANCE, SPR_WAND,
    SPR_RELIC, SPR_SHARD, SPR_CORE_SHARD, SPR_HEART, SPR_EXIT, SPR_BYTE,
    SPR_TILE_FLOOR_A, SPR_TILE_FLOOR_B, SPR_TILE_FLOOR_C, SPR_TILE_WALL, SPR_TILE_WALL_TOP,
    SPR_COUNT
};
void assets_init(void);
void font_init(void);

// audio (audio.c)
void audio_init(void);
void audio_shutdown(void);
typedef enum {
    SFX_HIT, SFX_HURT, SFX_DASH, SFX_PICKUP, SFX_SHARD, SFX_CORE_SHARD,
    SFX_SHOOT, SFX_ENEMY_DIE, SFX_DOOR, SFX_BOSS_DIE, SFX_DENY, SFX_UI,
    SFX_DROP, SFX_HEAL, SFX_BOSS_ROAR, SFX_ENDING,
} SfxId;
void sfx_play(SfxId id);
void music_set(int track); // -1 off, 0 title, 1..4 biomes, 5 boss, 6 ending
void audio_set_bgm_enabled(bool enabled);
void audio_set_sfx_enabled(bool enabled);

// game (game.c)
void game_init(void);
void game_frame(void);
void game_event(const sapp_event* e);

#endif
