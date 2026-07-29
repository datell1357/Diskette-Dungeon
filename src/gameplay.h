// gameplay.h — 게임플레이 공유 타입/전역 (unity build 내부 헤더)
#ifndef GAMEPLAY_H
#define GAMEPLAY_H
#include "game.h"
#include <stddef.h>

// ----------------------------------------------------------- tiles & rooms
enum { T_FLOOR=0, T_WALL=1, T_DOOR_CLOSED=2, T_DOOR_OPEN=3, T_EXIT=4 };

// 문이 약속하는 보상 (분기 선택)
enum { PROMISE_NONE=0, PROMISE_WEAPON, PROMISE_RELIC, PROMISE_SHARD, PROMISE_HEART };

// ----------------------------------------------------------- items
enum { WPN_SWORD=0, WPN_CANNON, WPN_SPRAY, WPN_GLAIVE, WPN_LANCE, WPN_WAND, WPN_COUNT };
enum { PFX_NONE=0, PFX_HOT, PFX_COLD, PFX_BROKEN, PFX_COMPRESSED, PFX_COUNT };
enum { RELIC_CHECKSUM=0, RELIC_DEFRAG, RELIC_OVERCLOCK, RELIC_BACKUP, RELIC_LUMINANCE, RELIC_COMPRESS, RELIC_BADSECTOR, RELIC_COUNT };

enum {
    WR_SWORD_WAVE=0, WR_SWORD_WHIRL, WR_SWORD_PHASE, WR_SWORD_EXECUTE,
    WR_CANNON_FRAG,  WR_CANNON_RAIL, WR_CANNON_FUSE, WR_CANNON_RECOIL,
    WR_SPRAY_RICO,   WR_SPRAY_WIDE, WR_SPRAY_PIERCE, WR_SPRAY_CHOKE,
    WR_GLAIVE_TWIN,  WR_GLAIVE_ORBIT, WR_GLAIVE_RETURN, WR_GLAIVE_TRAIL,
    WR_LANCE_BLAST,  WR_LANCE_CHARGE, WR_LANCE_PIN, WR_LANCE_PIERCE,
    WR_WAND_FORK,    WR_WAND_CHAIN, WR_WAND_RING, WR_WAND_DELAY,
    WR_COUNT
};

typedef struct { const char* name; float dmg, cooldown, speed; int kb; int spr; } WeaponDef;
typedef struct { const char* name; int kb; const char* desc; } RelicDef;
typedef struct { const char* name; int weapon; int kb; const char* desc; } WeaponRelicDef;
typedef struct { int type, prefix; } Weapon;

// ----------------------------------------------------------- entities
enum { E_SLIME=0, E_BAT, E_WRAITH, E_CHASER, E_GOLEM, E_TURRET, E_SENTINEL, E_DRONE,
       E_BOMBER, E_SNIPER, E_SHIELDER, E_HIVE,
       E_BOSS_ROT, E_BOSS_ECHO, E_BOSS_DEFRAG, E_BOSS_NULL, E_MINI_SLIME, E_ECHO_GHOST, E_TYPE_COUNT };

typedef struct {
    bool active;
    int type;
    v2 pos, vel;
    float hp, maxhp;
    float radius;
    float t0, t1, t2, t3;  // ai timers
    int state, phase;
    float flash;           // 피격 플래시
    float burn, slow, root;      // 디버프 남은 시간
    float player_damage, player_damage_t;
    bool player_damaged, player_damage_crit;
    bool facing_left;
    bool elite;            // 정예 변종
    uint8_t event_trait;
    uint8_t event_bonus;
    float face;            // 바라보는 각도 (패리티 방패)
    v2 target;             // ai 목적지/텔레그래프
    float spawn_t;         // 등장 연출
} Entity;

typedef struct {
    bool active, from_player;
    int kind;              // 시각/거동: 0 탄, 1 빔탄, 2 펠릿, 3 글레이브, 4 랜스, 5 유도, 6 포자, 7 보스탄, 11 화상지대
    v2 pos, vel;
    float life, dmg, radius;
    int pierce;
    int bounces;           // 남은 벽 반사 횟수 (스프레이 도탄 유물)
    int last_hit;          // 같은 적 연속 타격 방지
    float rehit_t, trail_t;
    bool returning;        // glaive
    bool delayed_fuse, fuse_armed;
    float burn, slow;      // 부여 디버프
    bool crit;
    uint32_t attack_group;
} Bullet;

typedef struct {
    bool active, glow;
    v2 pos, vel;
    float life, max_life, size, drag, grav;
    col3 col;
} Particle;

enum { PK_WEAPON=0, PK_RELIC, PK_SHARD, PK_CORE, PK_HEART, PK_BYTE, PK_WRELIC };
typedef struct {
    bool active;
    int type;
    Weapon weapon;         // PK_WEAPON
    int relic;             // PK_RELIC
    int core_id;           // PK_CORE 0..3
    v2 pos;
    float bob;
    bool manual_only;
} Pickup;

// 범위지정 공격 — 바닥에 예고된 원형 위험구역 (DEFRAG 등)
#define MAX_ZONES 40
typedef struct {
    bool active;
    int kind;      // 0 원형, 1 가로줄, 2 세로줄
    v2 pos;        // 원: 중심 / 가로줄: y만 / 세로줄: x만 사용
    float r;       // 원: 반경 / 줄: 절반 두께
    float t;       // 폭발까지 남은 시간 (0이면 폭발)
    float warn;    // 총 예고 시간 (진행도 시각화용)
} AoeZone;

typedef struct { float x,y; char text[48]; float t; col3 c; bool screen_fixed; } Floater;
typedef struct {
    v2 pos;
    float t, hp, damage, radius;
    uint32_t attack_group;
    int hits;
    bool crit, elite;
} EnemyFeedback;

// ----------------------------------------------------------- run state
// ----------------------------------------------------------- memory events
enum { MEM_TAG_COURAGE=0, MEM_TAG_KINSHIP, MEM_TAG_PROMISE, MEM_TAG_COUNT=3 };
enum { MEM_EVENT_NONE=0, MEM_EVENT_ECHO, MEM_EVENT_CORRUPTED };
enum { MEM_STATE_NONE=0, MEM_STATE_AVAILABLE, MEM_STATE_RESOLVED };
enum { MEM_DECISION_NONE=0, MEM_DECISION_KEEP, MEM_DECISION_DISCARD };
enum { ELITE_NONE=0, ELITE_VOLATILE, ELITE_HASTE };
enum { DEATH_REASON_NONE=0, DEATH_REASON_DAMAGE, DEATH_REASON_BOMBER, DEATH_REASON_BURN };

typedef struct {
    // Totals saturate at 255; gameplay effects clamp kept counts to two.
    uint8_t kept[3], discarded[3];
    // Packed as (decision << 4) | (event_type << 2) | tag.
    // log_head points to the oldest entry; the ring holds at most eight.
    uint8_t log[8], log_count, log_head;
    uint8_t pending_trait;       // ELITE_NONE when no Corrupted trait is queued.
} MemoryRunState;

typedef struct {
    v2 pos, vel;
    float hp; int maxhp;
    int shield_maxhp;
    float shield, light_shield_cap;
    Weapon weapon;
    float attack_cd, charge;   // charge: 캐논
    uint32_t attack_group, impact_group;
    bool charging;
    float dash_t, dash_cd, iframes;
    v2 dash_dir;
    bool relics[RELIC_COUNT];
    int wrelics[2];            // 보유한 무기 유물 (빈 슬롯 = -1), 최대 2개
    int shards;                // 일반 추억 조각 수
    uint8_t cores;             // 핵심 조각 비트마스크 (#1~#4)
    float heal_timer;          // checksum
    v2 aim;
    bool glaive_out;
    float anim_t, squash;
    bool moving;
    int last_weight_tier;      // 무게 구간 변화 감지 (1가벼움 2적정 3무거움)
} Player;

typedef struct {
    uint8_t tiles[MAX_ROOM_H][MAX_ROOM_W];
    int w, h;                  // 이 방의 실제 크기 (타일)
    int biome;                 // 0..3
    int idx;                   // 0..8 (8=boss)
    bool cleared;
    int event_type, event_tag, event_trait, event_state;
    int event_tile_x, event_tile_y;
    v2 event_pos;
    uint8_t event_place_pass;
    float event_player_dist;
    float event_min_pickup_dist;
    float event_min_door_dist;
    int promise;               // 이 방의 보상 약속
    int door_promise[2];       // 출구 문 2개의 약속 (0이면 닫힘)
    int door_count;
    int door_dir[2];           // 각 문이 붙은 벽 (DIR_*)
    int door_x[2], door_y[2];  // 각 문의 첫 타일 좌표
    int entry_dir;             // 플레이어가 스폰되는 벽 방향
    bool is_boss;
} Room;

enum { ST_BOOT=0, ST_INTRO, ST_TITLE, ST_OPTIONS, ST_PLAY, ST_FLASHBACK, ST_INVENTORY,
       ST_RELIC_SWAP, ST_PAUSE, ST_DEAD, ST_ENDING, ST_EPILOGUE, ST_UPGRADE, ST_CODEX, ST_CODEX_DETAIL,
       ST_WEAPON_SELECT, ST_DIFFICULTY_SELECT, ST_TRAINING };

typedef struct {
    uint32_t magic, version;
    uint32_t bytes_currency;
    uint32_t unlocked_weapons; // bitmask
    uint32_t best_biome, runs, wins;
    uint32_t true_clear;       // NG+ 해금
    uint32_t opt_scanline, opt_shake;
    uint32_t last_seed;
    uint32_t upg[5];           // 영구 강화: 무결성/공격/이속/빛/대시
    uint32_t intro_seen;
    uint32_t intro_replay_queued;
    uint32_t opt_bgm, opt_sfx;
    uint32_t checksum;
} MetaSave;
typedef char MetaSave_size_must_be_84[(sizeof(MetaSave)==84)?1:-1];
typedef char MetaSave_checksum_offset_must_be_80[(offsetof(MetaSave,checksum)==80)?1:-1];

typedef struct {
    MemoryRunState memory;
    int state;
    float state_t;
    uint32_t run_seed;
    int difficulty;            // 0 쉬움 1 보통 2 어려움
    bool ngplus;
    Room room;
    Player pl;
    Entity ents[MAX_ENTITIES];
    Bullet bullets[MAX_BULLETS];
    Particle parts[MAX_PARTICLES];
    Pickup pickups[MAX_PICKUPS];
    Floater floaters[MAX_FLOATERS];
    EnemyFeedback enemy_feedback[MAX_ENTITIES];
    // 플레이어 위치 히스토리 (망령/추격자/메아리)
    v2 history[256]; int hist_head;
    // juice
    float hitstop, shake, timescale, flash_white, fade, fade_dir;
    col3 fade_col;
    int fade_next_state;
    float time;                // 누적 시간 (셰이더)
    float run_time;
    int bytes_run;             // 이번 런에서 모은 바이트
    int death_source_type;
    float ambient_mul;         // 보스 연출용
    float light_mul;           // NULL 보스 빛 흡수
    // 전환
    int pending_door;          // 들어간 문 (promise)
    int pending_entry_dir;     // 다음 방에서 플레이어가 스폰될 벽
    v2 cam;                    // 카메라 위치 (큰 방 스크롤)
    // 메시지
    char msg[128]; float msg_t;
    // 플래시백
    int fb_core; float fb_t;
    // 엔딩
    int ending;                // 0 빈손 1 표준 2 완전 복구 3 진엔딩
    // 타이틀 메뉴
    int menu_sel; int title_weapon; uint32_t title_seed; bool seed_edit;
    int options_return_state;
    int codex_section, codex_page, codex_detail, codex_focus;
    int intro_page;
    // 사망 통계
    float dead_t;
    MetaSave meta;
    bool boss_intro; float boss_intro_t;
    int kills;
    float room_t;              // 현재 방 체류 시간 (남은 적 표시용)
    int upg_sel;
    int relic_swap_type, relic_swap_id, relic_swap_pickup, relic_swap_sel;
    int relic_swap_slots[4];
    bool run_settled;
    bool training_active;
    AoeZone zones[MAX_ZONES];  // 범위지정 공격 예고
} Game;

extern Game G;
extern const WeaponDef weapon_defs[WPN_COUNT];
extern const char* prefix_names[PFX_COUNT];
extern const RelicDef relic_defs[RELIC_COUNT];
extern const WeaponRelicDef weapon_relic_defs[WR_COUNT];
bool player_has_wrelic(int wr);
int player_wrelic_count(void);
bool event_resolve_choice(int decision);
void event_assign_pending_trait(void);

// game.c
void room_generate(int biome, int idx, int promise, int entry_dir);
bool tile_solid(int tx, int ty);
v2 resolve_collision(v2 pos, v2 vel, float radius, float dt);
v2 push_out_of_walls(v2 pos, float radius);
void meta_save(void);
void meta_load(void);
int portal_list(v2* out, int* exit_flag, int max);
void start_run(void);
void start_run_with_seed(uint32_t seed);
void start_run_after_intro(void);
void start_training(void);
enum { SETTLE_DEATH=0, SETTLE_FORFEIT };
bool settle_run_once(int reason);
#ifdef DD_DEBUG
void dd_debug_reset_meta_save_events(void);
int dd_debug_meta_save_events(void);
#endif
void spawn_particle(v2 pos, v2 vel, float life, float size, col3 c, bool glow, float drag, float grav);
void burst(v2 pos, int n, col3 c, float speed, float life, float size, bool glow);
void spawn_pickup(int type, v2 pos, Weapon w, int relic, int core_id);
v2 reward_label_pos(const Pickup* pickup);
void clear_reward_label_obstacles(void);
void add_floater(v2 pos, const char* text, col3 c);
void add_fixed_floater(float x, float y, const char* text, col3 c);
void set_msg(const char* m);
void open_doors(void);
void on_room_cleared(void);
int player_used_kb(void);
int player_item_kb(int kb);
int player_raw_kb(void);
int player_capacity_kb(void);
float weight_frac(void);
float capacity_frac(void);
float player_light_radius(void);
float player_light_shield_limit(void);
void player_sync_light_shield(void);
void player_restore_light_shield(void);
float player_speed_mul(void);
float player_attack_damage(void);
void player_take_damage(v2 from);
void player_take_damage_amount(v2 from, float damage);
void fade_to(int next_state, col3 c);
#ifdef DD_DEBUG
void dd_debug_clear_room_enemies(void);
#endif

// combat.c
void spawn_enemy(int type, v2 pos);
void spawn_boss(int biome);
void update_play(float dt);
void player_drop_shard(void);
bool player_try_pickup(Pickup* p);
void player_confirm_relic_swap(int slot);

// ui_story.c
void draw_play(void);
void hud_draw(void);

#endif
