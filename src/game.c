// game.c — 월드/던전 생성/무게 시스템/세이브/공용 헬퍼
#include "game.h"
#include "gameplay.h"
#include <stddef.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

Game G;
#ifdef DD_DEBUG
/*
 * The command line is parsed before game_init() runs (main.c owns DBG_CFG).
 * Keep this latch in game.c so every meta write, including writes reached
 * through production gameplay helpers, observes the sterile diagnostic mode.
 */
static bool dd_debug_clean_profile;
static int dd_debug_meta_save_count;

void dd_debug_set_clean_profile(bool enabled){
    dd_debug_clean_profile = enabled;
}

bool dd_debug_clean_profile_active(void){
    return dd_debug_clean_profile;
}
#endif

// ----------------------------------------------------------- data tables
const WeaponDef weapon_defs[WPN_COUNT] = {
    {"포인터 블레이드", 3.0f, 0.26f,   0, 64, SPR_SWORD},
    {"비트 캐논",       2.0f, 0.50f, 430, 96, SPR_CANNON},
    {"패킷 스프레이",   1.1f, 0.46f, 320, 80, SPR_SPRAY},
    {"루프 글레이브",   3.0f, 0.20f, 260, 88, SPR_GLAIVE},
    {"널 랜스",         5.5f, 0.78f, 310, 72, SPR_LANCE},
    {"에코 완드",       1.3f, 0.30f, 240, 84, SPR_WAND},
};
const char* prefix_names[PFX_COUNT] = { "", "과열된 ", "차가운 ", "깨진 ", "압축된 " };
const RelicDef relic_defs[RELIC_COUNT] = {
    {"체크섬",        64, "주기적으로 무결성 0.5 회복"},
    {"디프래그 코어", 56, "빈 용량만큼 빨라진다"},
    {"오버클럭",      72, "공격속도 +30%, 최대 무결성 -1"},
    {"백업 비트",     96, "치명상 시 1회 부활"},
    {"루미넌스",      64, "빛 반경 +30%"},
    {"압축 알고리즘", 48, "모든 아이템 용량 -20%"},
    {"배드섹터 부적", 56, "무결성 2 이하일 때 피해 +50%"},
};
const WeaponRelicDef weapon_relic_defs[WR_COUNT] = {
    {"검기 칩",     WPN_SWORD,  88, "공속 -30%, 공격 시 관통 검기 발사 (벽에 닿으면 소멸)"},
    {"회전 베기",   WPN_SWORD,  80, "휘두를 때 전방향을 베고 적 탄을 상쇄한다"},
    {"위상 스텝",   WPN_SWORD, 104, "처치 시 가까운 적에게 이동해 200% 위상 공격"},
    {"처형 루틴",   WPN_SWORD,  96, "25% 이하 일반 적 처형, 0.25 회복과 50% 범위 피해"},
    {"파편 탄두",   WPN_CANNON, 96, "폭발 시|6갈래 파편 분열"},
    {"관통 레일",   WPN_CANNON, 88, "완충 발사가 화면을 가르는 관통 레일이 된다"},
    {"지연 신관",   WPN_CANNON, 88, "벽이나 적에 닿으면 잠시 뒤 넓게 폭발"},
    {"반동 증폭기", WPN_CANNON, 80, "완충 포탄 피해·폭발 반경 증가, 강한 반동"},
    {"도탄 코덱",   WPN_SPRAY,  80, "탄이 벽에 한 번 튕긴다"},
    {"광역 분사",   WPN_SPRAY,  72, "5발 → 8발 광각 분사 (사거리 짧음)"},
    {"관통 플렉스", WPN_SPRAY,  88, "4발 분사가 적 한 명을 관통한다"},
    {"수렴 초크",   WPN_SPRAY,  88, "좁은 분사로 사거리와 피해가 증가한다"},
    {"쌍날 루프",   WPN_GLAIVE, 96, "글레이브를 앞뒤 두 개 동시에 던진다"},
    {"이중 투척",   WPN_GLAIVE, 88, "같은 방향으로 글레이브 2개 투척"},
    {"회수 가속기", WPN_GLAIVE, 80, "귀환 속도 +30%, 귀환 피해 +40%"},
    {"화염 궤적",   WPN_GLAIVE, 88, "지나간 자리에 3초 화상지대 (초당 공격력 50%)"},
    {"충격 말뚝",   WPN_LANCE,  96, "끝·적·벽 충돌 시 공격력 40% 폭발"},
    {"돌격 창",     WPN_LANCE,  88, "사거리 68 근접 찌르기, 0.5초 무적 돌진"},
    {"핀 고정",     WPN_LANCE,  80, "적중 시 소멸·속박, 0.5초 후 40% 범위 피해"},
    {"관통 충전",   WPN_LANCE,  88, "관통할 때마다 이후 피해 +20%"},
    {"분기 호출",   WPN_WAND,   88, "유도탄 2→4발"},
    {"연쇄 메아리", WPN_WAND,   80, "탄 명중 시 가까운 적에게 연쇄한다"},
    {"공명 고리",   WPN_WAND,   88, "명중 지점 주변에 40% 공명 피해를 준다"},
    {"폭우",        WPN_WAND,   80, "1.5초 차지, 단계당 탄 +2·피해 +7.5%"},
};
bool player_has_wrelic(int wr){ return G.pl.wrelics[0]==wr || G.pl.wrelics[1]==wr; }
int player_wrelic_count(void){ int n=0; if(G.pl.wrelics[0]>=0)n++; if(G.pl.wrelics[1]>=0)n++; return n; }

// ----------------------------------------------------------- meta save
static void save_path(char* buf, size_t n){
#if defined(_WIN32)
    const char* base = getenv("APPDATA");
    if (!base) base = ".";
    snprintf(buf, n, "%s\\DisketteDungeon", base);
#else
    const char* home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, n, "%s/Library/Application Support/DisketteDungeon", home);
#endif
}
enum { META_V1_WORDS=12, META_V2_WORDS=17, META_V3_WORDS=19, META_V4_WORDS=21 };
static uint32_t meta_words_checksum(const uint32_t* p, size_t words){
    uint32_t sum = 0x1D15C0DE;
    for (size_t i=0;i<words;i++) sum = sum*31u + p[i];
    return sum;
}
static uint32_t meta_checksum(const MetaSave* m){
    return meta_words_checksum((const uint32_t*)m,offsetof(MetaSave,checksum)/4);
}
void meta_save(void){
#ifdef DD_DEBUG
    dd_debug_meta_save_count++;
    if (dd_debug_clean_profile) return;
#endif
    char dir[512], path[600];
    save_path(dir,sizeof(dir));
#if defined(_WIN32)
    char cmd[600]; (void)cmd;
    CreateDirectoryA(dir, NULL);
    snprintf(path,sizeof(path),"%s\\save.bin",dir);
#else
    char mk[600]; snprintf(mk,sizeof(mk),"mkdir -p \"%s\"",dir); system(mk);
    snprintf(path,sizeof(path),"%s/save.bin",dir);
#endif
    G.meta.magic=0xD15C0DE7u; G.meta.version=4;
    G.meta.checksum = meta_checksum(&G.meta);
    FILE* f = fopen(path,"wb");
    if (f){ fwrite(&G.meta,sizeof(G.meta),1,f); fclose(f); }
}
#ifdef DD_DEBUG
void dd_debug_reset_meta_save_events(void){ dd_debug_meta_save_count=0; }
int dd_debug_meta_save_events(void){ return dd_debug_meta_save_count; }
#endif
void meta_load(void){
#ifdef DD_DEBUG
    /*
     * DBG_CFG is deliberately populated before the Sokol init callback, while
     * game_init() (and therefore meta_load()) runs inside that callback.
     */
    if (DBG_CFG.clean_profile) dd_debug_set_clean_profile(true);
#endif
    memset(&G.meta,0,sizeof(G.meta));
    G.meta.unlocked_weapons = 1u<<WPN_SWORD;
    G.meta.opt_scanline = 1; G.meta.opt_shake = 1;
    G.meta.opt_bgm = 1; G.meta.opt_sfx = 1; G.meta.intro_replay_queued = 1;
#ifdef DD_DEBUG
    if (dd_debug_clean_profile) return;
#endif
    char dir[512], path[600];
    save_path(dir,sizeof(dir));
#if defined(_WIN32)
    snprintf(path,sizeof(path),"%s\\save.bin",dir);
#else
    snprintf(path,sizeof(path),"%s/save.bin",dir);
#endif
    FILE* f = fopen(path,"rb");
    if (!f) return;
    uint32_t words[META_V4_WORDS]={0};
    size_t nw = fread(words,sizeof(uint32_t),META_V4_WORDS,f);
    int extra = fgetc(f);
    fclose(f);
    if (extra!=EOF || words[0]!=0xD15C0DE7u) return;
    if (words[1]==4 && nw==META_V4_WORDS &&
        words[META_V4_WORDS-1]==meta_words_checksum(words,META_V4_WORDS-1)){
        MetaSave m;
        memcpy(&m,words,sizeof m);
        G.meta=m;
    } else if (words[1]==3 && nw==META_V3_WORDS &&
        words[META_V3_WORDS-1]==meta_words_checksum(words,META_V3_WORDS-1)){
        MetaSave m;
        memcpy(&m,words,sizeof m);
        m.opt_bgm=1; m.opt_sfx=1;
        G.meta=m;
    } else if (words[1]==2 && nw==META_V2_WORDS &&
               words[META_V2_WORDS-1]==meta_words_checksum(words,META_V2_WORDS-1)){
        MetaSave m={0};
        m.magic=words[0]; m.version=words[1]; m.bytes_currency=words[2];
        m.unlocked_weapons=words[3]; m.best_biome=words[4]; m.runs=words[5];
        m.wins=words[6]; m.true_clear=words[7]; m.opt_scanline=words[8];
        m.opt_shake=words[9]; m.last_seed=words[10];
        memcpy(m.upg,&words[11],sizeof m.upg);
        m.opt_bgm=1; m.opt_sfx=1;
        G.meta=m;
    } else if (words[1]==1 && nw==META_V1_WORDS &&
               words[META_V1_WORDS-1]==meta_words_checksum(words,META_V1_WORDS-1)){
        MetaSave m={0};
        m.magic=words[0]; m.version=words[1]; m.bytes_currency=words[2];
        m.unlocked_weapons=words[3]; m.best_biome=words[4]; m.runs=words[5];
        m.wins=words[6]; m.true_clear=words[7]; m.opt_scanline=words[8];
        m.opt_shake=words[9]; m.last_seed=words[10];
        m.opt_bgm=1; m.opt_sfx=1;
        G.meta=m;
    }
    if (!(G.meta.unlocked_weapons & (1u<<WPN_SWORD))) G.meta.unlocked_weapons |= 1u<<WPN_SWORD;
}

// ----------------------------------------------------------- helpers
void set_msg(const char* m){ snprintf(G.msg,sizeof(G.msg),"%s",m); G.msg_t=3.0f; }

void add_floater(v2 pos, const char* text, col3 c){
    for (int i=0;i<MAX_FLOATERS;i++){
        if (G.floaters[i].t<=0){
            Floater* f=&G.floaters[i];
            f->x=pos.x; f->y=pos.y; f->t=1.4f; f->c=c; f->screen_fixed=false;
            snprintf(f->text,sizeof(f->text),"%s",text);
            return;
        }
    }
}

void add_fixed_floater(float x, float y, const char* text, col3 c){
    for (int i=0;i<MAX_FLOATERS;i++){
        if (G.floaters[i].t<=0){
            Floater* f=&G.floaters[i];
            f->x=x; f->y=y; f->t=1.4f; f->c=c; f->screen_fixed=true;
            snprintf(f->text,sizeof(f->text),"%s",text);
            return;
        }
    }
}

void spawn_particle(v2 pos, v2 vel, float life, float size, col3 c, bool glow, float drag, float grav){
    for (int i=0;i<MAX_PARTICLES;i++){
        Particle* p=&G.parts[i];
        if (!p->active){
            *p=(Particle){true,glow,pos,vel,life,life,size,drag,grav,c};
            return;
        }
    }
}
static Rng fxrng = { 0xABCDEF12345ull };
void burst(v2 pos, int n, col3 c, float speed, float life, float size, bool glow){
    for (int i=0;i<n;i++){
        float a = rng_f(&fxrng)*6.2832f;
        float s = speed*(0.3f+rng_f(&fxrng)*0.7f);
        spawn_particle(pos, V2(cosf(a)*s,sinf(a)*s), life*(0.5f+rng_f(&fxrng)*0.5f),
                       size*(0.6f+rng_f(&fxrng)*0.8f), c, glow, 4.0f, 0);
    }
}

void spawn_pickup(int type, v2 pos, Weapon w, int relic, int core_id){
    // 벽 안에 떨어지면 가까운 바닥으로 스냅
    int tx=(int)(pos.x/TILE), ty=(int)(pos.y/TILE);
    if (tile_solid(tx,ty)){
        bool found=false;
        for (int r=1;r<=4&&!found;r++)
            for (int dy=-r;dy<=r&&!found;dy++)
                for (int dx=-r;dx<=r&&!found;dx++)
                    if (!tile_solid(tx+dx,ty+dy)){
                        pos=V2((tx+dx)*TILE+8.0f,(ty+dy)*TILE+8.0f);
                        found=true;
                    }
    }
    for (int i=0;i<MAX_PICKUPS;i++){
        if (!G.pickups[i].active){
            G.pickups[i]=(Pickup){true,type,w,relic,core_id,pos,rng_f(&fxrng)*6.0f};
            return;
        }
    }
}

v2 reward_label_pos(const Pickup* pickup){
    float x=pickup->pos.x;
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* other=&G.pickups[i];
        if (!other->active || other==pickup || (other->type!=PK_WEAPON &&
            other->type!=PK_RELIC && other->type!=PK_WRELIC)) continue;
        float gap=fabsf(other->pos.x-pickup->pos.x);
        if (gap<184.0f)
            x+=(pickup->pos.x<other->pos.x?-1.0f:1.0f)*(184.0f-gap)*0.5f;
    }
    return V2(x,pickup->pos.y+18.0f);
}

void clear_reward_label_obstacles(void){
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pickup=&G.pickups[i];
        if (!pickup->active || (pickup->type!=PK_WEAPON &&
            pickup->type!=PK_RELIC && pickup->type!=PK_WRELIC)) continue;
        v2 label=reward_label_pos(pickup);
        int tx0=(int)floorf((label.x-90.0f)/TILE);
        int tx1=(int)floorf((label.x+90.0f)/TILE);
        int ty0=(int)floorf((label.y-5.0f)/TILE);
        int ty1=(int)floorf((label.y+70.0f)/TILE);
        if (tx0<0) tx0=0;
        if (tx1>G.room.w-1) tx1=G.room.w-1;
        if (ty0<0) ty0=0;
        if (ty1>G.room.h-1) ty1=G.room.h-1;
        for (int ty=ty0;ty<=ty1;ty++) for (int tx=tx0;tx<=tx1;tx++)
            if (tile_solid(tx,ty)) G.room.tiles[ty][tx]=T_FLOOR;
    }
}

void fade_to(int next_state, col3 c){
    G.fade_dir = 1.0f; G.fade_col = c; G.fade_next_state = next_state;
}

// ----------------------------------------------------------- 무게(용량) 시스템
static int item_kb(int kb){
    if (G.pl.relics[RELIC_COMPRESS]) return kb*4/5;
    return kb;
}
int player_item_kb(int kb){ return item_kb(kb); }
// 디스크에 실제로 차지하는 용량(압축 적용) — 용량 게이트/HUD 게이지용
int player_used_kb(void){
    int kb = item_kb(weapon_defs[G.pl.weapon.type].kb);
    if (G.pl.weapon.prefix==PFX_COMPRESSED) kb = kb*3/4;
    kb += G.pl.shards * item_kb(64);
    for (int i=0;i<4;i++) if (G.pl.cores & (1<<i)) kb += item_kb(128);
    for (int i=0;i<3;i++) kb += G.memory.kept[i] * item_kb(64);
    return kb;
}
// 압축을 무시한 '진짜로 들고 다니는 무게' — 공격/빛/이속 스탯의 기준.
// 압축은 용량만 줄이고 이미 획득한 스탯은 유지해야 하므로 스탯은 raw로 계산한다.
int player_raw_kb(void){
    int kb = weapon_defs[G.pl.weapon.type].kb;
    kb += G.pl.shards * 64;
    for (int i=0;i<4;i++) if (G.pl.cores & (1<<i)) kb += 128;
    for (int i=0;i<3;i++) kb += G.memory.kept[i] * 64;
    return kb;
}
int player_capacity_kb(void){ return 1440; }
// 무게 분율(스탯용): 압축 무시한 raw 무게 기준
float weight_frac(void){ return clampf(player_raw_kb()/(float)player_capacity_kb(),0,1); }
// 용량 분율(게이지/빈공간용): 압축 적용된 디스크 사용량 기준
float capacity_frac(void){ return clampf(player_used_kb()/(float)player_capacity_kb(),0,1); }
float player_light_radius(void){
    float r = (62.0f + weight_frac()*58.0f +
               8.0f*(G.memory.kept[MEM_TAG_PROMISE]>2?2:G.memory.kept[MEM_TAG_PROMISE])) *
              (G.pl.relics[RELIC_LUMINANCE] ? 1.3f : 1.0f);
    return r * G.light_mul;
}
static float player_light_progress(void){
    const float base_radius=62.0f;
    const float max_radius=(62.0f+58.0f+16.0f)*1.3f;
    return clampf((player_light_radius()-base_radius)/(max_radius-base_radius),0.0f,1.0f);
}
static float player_light_shield_progress(void){
    return clampf((player_light_radius()-62.0f)/(160.0f-62.0f),0.0f,1.0f);
}
float player_light_shield_limit(void){
    int cap=G.pl.shield_maxhp<5?G.pl.shield_maxhp:5;
    float start_shield=fminf((float)cap,G.meta.upg[3]*0.5f);
    float light_shield=floorf(player_light_shield_progress()*(float)cap*2.0f)*0.5f;
    return fminf((float)cap,start_shield+light_shield);
}
void player_sync_light_shield(void){
    float cap=player_light_shield_limit();
    G.pl.shield=fminf(cap,G.pl.shield+fmaxf(0.0f,cap-G.pl.light_shield_cap));
    G.pl.light_shield_cap=cap;
}
void player_restore_light_shield(void){
    player_sync_light_shield();
    G.pl.shield=fminf(player_light_shield_limit(),G.pl.shield+1.0f);
}
float player_speed_mul(void){
    float m = 1.15f-player_light_progress()*0.55f;
    if (G.pl.relics[RELIC_DEFRAG]) m += (1.0f-capacity_frac())*0.2f; // 디프래그: 빈 '용량'만큼 가속
    return m * (1.0f + G.meta.upg[2]*0.03f);
}
float player_attack_damage(void){
    float damage=weapon_defs[G.pl.weapon.type].dmg;
    damage *= 1.0f + fminf((float)G.memory.kept[MEM_TAG_COURAGE],2.0f)*0.05f;
    damage *= 1.0f + weight_frac()*0.5f;
    damage *= 1.0f + G.meta.upg[1]*0.05f;
    if (G.pl.relics[RELIC_BADSECTOR] && G.pl.hp<=2.0f) damage*=1.5f;
    return damage;
}

// ----------------------------------------------------------- tiles
bool tile_solid(int tx, int ty){
    if (tx<0||ty<0||tx>=G.room.w||ty>=G.room.h) return true;
    uint8_t t = G.room.tiles[ty][tx];
    return t==T_WALL || t==T_DOOR_CLOSED;
}

// 원 vs 타일 충돌 해소 (축 분리 이동)
v2 resolve_collision(v2 pos, v2 vel, float radius, float dt){
    v2 np = pos;
    np.x += vel.x*dt;
    {
        int ty0=(int)((np.y-radius)/TILE), ty1=(int)((np.y+radius)/TILE);
        int tx0=(int)((np.x-radius)/TILE), tx1=(int)((np.x+radius)/TILE);
        for (int ty=ty0;ty<=ty1;ty++) for (int tx=tx0;tx<=tx1;tx++){
            if (!tile_solid(tx,ty)) continue;
            float bx0=tx*TILE, bx1=bx0+TILE, by0=ty*TILE, by1=by0+TILE;
            float cx=clampf(np.x,bx0,bx1), cy=clampf(np.y,by0,by1);
            float dx=np.x-cx, dy=np.y-cy;
            if (dx*dx+dy*dy < radius*radius){
                if (vel.x>0) np.x = bx0-radius-0.01f; else if (vel.x<0) np.x = bx1+radius+0.01f;
                (void)cy; (void)dy;
            }
        }
    }
    np.y += vel.y*dt;
    {
        int ty0=(int)((np.y-radius)/TILE), ty1=(int)((np.y+radius)/TILE);
        int tx0=(int)((np.x-radius)/TILE), tx1=(int)((np.x+radius)/TILE);
        for (int ty=ty0;ty<=ty1;ty++) for (int tx=tx0;tx<=tx1;tx++){
            if (!tile_solid(tx,ty)) continue;
            float bx0=tx*TILE, bx1=bx0+TILE, by0=ty*TILE, by1=by0+TILE;
            float cx=clampf(np.x,bx0,bx1), cy=clampf(np.y,by0,by1);
            float dx=np.x-cx, dy=np.y-cy;
            if (dx*dx+dy*dy < radius*radius){
                if (vel.y>0) np.y = by0-radius-0.01f; else if (vel.y<0) np.y = by1+radius+0.01f;
                (void)cx; (void)dx;
            }
        }
    }
    return np;
}

// 벽에 박힌 원을 가장 얕은 축으로 밀어낸다 (충돌 해소가 남긴 겹침/모서리 끼임 보정)
v2 push_out_of_walls(v2 pos, float radius){
    for (int iter=0; iter<2; iter++){
        int tx0=(int)((pos.x-radius)/TILE), tx1=(int)((pos.x+radius)/TILE);
        int ty0=(int)((pos.y-radius)/TILE), ty1=(int)((pos.y+radius)/TILE);
        float best=0; v2 push=V2(0,0);
        for (int ty=ty0;ty<=ty1;ty++) for (int tx=tx0;tx<=tx1;tx++){
            if (!tile_solid(tx,ty)) continue;
            float bx0=tx*TILE, bx1=bx0+TILE, by0=ty*TILE, by1=by0+TILE;
            float cx=clampf(pos.x,bx0,bx1), cy=clampf(pos.y,by0,by1);
            float dx=pos.x-cx, dy=pos.y-cy;
            float d2=dx*dx+dy*dy;
            if (d2>=radius*radius) continue;            // 겹침 없음
            float pen, nx, ny;
            if (d2>0.0001f){                            // 면/모서리 — 법선 방향으로
                float d=sqrtf(d2); nx=dx/d; ny=dy/d; pen=radius-d;
            } else {                                    // 중심이 타일 안 — 가장 가까운 벽면으로
                float l=pos.x-bx0, r=bx1-pos.x, u=pos.y-by0, dn=by1-pos.y;
                float m=l; nx=-1; ny=0;
                if (r<m){ m=r; nx=1; ny=0; }
                if (u<m){ m=u; nx=0; ny=-1; }
                if (dn<m){ m=dn; nx=0; ny=1; }
                pen=radius+m;
            }
            if (pen>best){ best=pen; push=V2(nx*pen,ny*pen); }
        }
        if (best<=0) break;
        pos=v2add(pos,push);
    }
    return pos;
}

// ----------------------------------------------------------- dungeon gen
static Rng room_rng;

// 벽 방향 dir, 그 벽 위 위치 인덱스 pos(0..len-1)에 해당하는 타일 좌표를 채운다.
// (문은 2칸이므로 pos는 시작 칸. 문은 벽을 따라 이어지는 방향으로 2칸 차지)
static void wall_tile_coord(int dir, int along, int* ox, int* oy){
    Room* r=&G.room;
    switch (dir){
        case DIR_R: *ox=r->w-1; *oy=along; break;
        case DIR_L: *ox=0;      *oy=along; break;
        case DIR_D: *oy=r->h-1; *ox=along; break;
        default:    *oy=0;      *ox=along; break; // DIR_U
    }
}

// 한 벽에 2칸짜리 문을 놓는다. door_dir/x/y 기록.
static void put_door(int slot, int dir, int along){
    Room* r=&G.room;
    int x0,y0,x1,y1;
    wall_tile_coord(dir,along,&x0,&y0);
    // 벽을 따라 인접 2번째 칸
    int along2 = along+1;
    wall_tile_coord(dir,along2,&x1,&y1);
    r->tiles[y0][x0]=T_DOOR_CLOSED;
    r->tiles[y1][x1]=T_DOOR_CLOSED;
    r->door_dir[slot]=dir; r->door_x[slot]=x0; r->door_y[slot]=y0;
}

// 벽 길이(코너 제외 가능 범위)에서 중앙 60% 안의 무작위 시작 위치
static int door_along(int dir){
    Room* r=&G.room;
    int len = (dir==DIR_R||dir==DIR_L)? r->h : r->w;
    int lo = 1 + (int)(len*0.2f);
    int hi = len-2 - (int)(len*0.2f) - 1; // 2칸 문 + 코너 회피
    if (hi<lo) hi=lo;
    return lo + rng_i(&room_rng, hi-lo+1);
}

static void place_doors(void){
    Room* r=&G.room;
    // 분기: 3의 배수 방은 문 2개 (보상 선택), 그 외 1개
    bool branch = (r->idx%3==2) && !r->is_boss && r->idx<8;
    r->door_count = branch?2:1;
    int promises[4]={PROMISE_WEAPON,PROMISE_RELIC,PROMISE_SHARD,PROMISE_HEART};
    int p0 = promises[rng_i(&room_rng,4)];
    int p1 = promises[rng_i(&room_rng,4)];
    while (r->door_count==2 && p1==p0) p1 = promises[rng_i(&room_rng,4)];
    r->door_promise[0]=p0; r->door_promise[1]=p1;
    // 입구가 아닌 3개의 벽 중 무작위 선택
    int cand[3]; int nc=0;
    for (int d=0;d<4;d++) if (d!=r->entry_dir){ cand[nc++]=d; }
    int d0 = cand[rng_i(&room_rng,nc)];
    put_door(0, d0, door_along(d0));
    if (r->door_count==2){
        // 두 번째 문: 다른 벽 우선
        int d1 = cand[rng_i(&room_rng,nc)];
        int guard=0;
        while (d1==d0 && guard++<6) d1 = cand[rng_i(&room_rng,nc)];
        put_door(1, d1, door_along(d1));
    }
}

static const int biome_enemies[4][6] = {
    { E_SLIME, E_BAT, E_BOMBER, E_SLIME, E_BAT, E_BOMBER },
    { E_WRAITH, E_CHASER, E_TURRET, E_SNIPER, E_BOMBER, E_HIVE },
    { E_GOLEM, E_TURRET, E_CHASER, E_SHIELDER, E_SNIPER, E_HIVE },
    { E_SENTINEL, E_DRONE, E_SNIPER, E_SHIELDER, E_GOLEM, E_TURRET },
};

static const char* story_biome_entry_notice(int biome){
    static const char* notices[4]={
        "정상 인덱스가 신호를 붙잡았다.",
        "읽힌 흔적이 오래된 놀이를 비춘다.",
        "흩어진 기록 사이에 작별이 남아 있다.",
        "마지막 판독 신호가 복구를 기다린다.",
    };
    return biome>=0&&biome<4?notices[biome]:"복구 신호가 잠시 흔들린다.";
}

static uint8_t story_entry_notice_mask;
static bool story_emit_biome_entry_notice(int biome,int idx){
    uint8_t bit;
    if (idx!=0 || biome<0 || biome>=4) return false;
    bit=(uint8_t)(1u<<biome);
    if (story_entry_notice_mask&bit) return false;
    story_entry_notice_mask|=bit;
    set_msg(story_biome_entry_notice(biome));
    return true;
}

#ifdef DD_DEBUG
void dd_debug_story_entry_notice_reset(void){ story_entry_notice_mask=0; }
int dd_debug_story_entry_notice_probe(int biome,int idx){
    return story_emit_biome_entry_notice(biome,idx)?1:0;
}
#endif

// 입구 벽 방향에 따른 플레이어 스폰 타일 (2.5칸 안쪽 중앙)
static v2 spawn_pos_for(int entry_dir, int w, int h){
    switch (entry_dir){
        case DIR_R: return V2((w-2.5f)*TILE, (h/2)*TILE+TILE*0.5f);
        case DIR_D: return V2((w/2)*TILE+TILE*0.5f, (h-2.5f)*TILE);
        case DIR_U: return V2((w/2)*TILE+TILE*0.5f, 2.5f*TILE);
        default:    return V2(2.5f*TILE, (h/2)*TILE+TILE*0.5f); // DIR_L
    }
}

void room_generate(int biome, int idx, int promise, int entry_dir){
    Room* r=&G.room;
    memset(r,0,sizeof(*r));
    r->biome=biome; r->idx=idx; r->promise=promise; r->entry_dir=entry_dir;
    r->is_boss = (idx==8);
    room_rng.s = (uint64_t)G.run_seed*1000003ull + (uint64_t)biome*101ull + (uint64_t)idx*13ull + 7ull;
    if (!room_rng.s) room_rng.s=1;

    // --- 방 크기 (room_rng로 결정적, 다른 롤보다 먼저 뽑는다)
    int W, H;
    if (idx==0 && biome==0){ W=30; H=17; }       // 튜토리얼 고정
    else if (r->is_boss){ W=40; H=24; }          // 보스방 고정
    else {
        W = 28 + rng_i(&room_rng, 9);            // 28..36
        H = 16 + rng_i(&room_rng, 7);            // 16..22
    }
    if (W>MAX_ROOM_W) W=MAX_ROOM_W;
    if (H>MAX_ROOM_H) H=MAX_ROOM_H;
    r->w=W; r->h=H;

    // 외벽
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        r->tiles[y][x] = (x==0||y==0||x==W-1||y==H-1)? T_WALL : T_FLOOR;

    // 내부 장애물 (보스방은 개방)
    if (!r->is_boss){
        // 정형 대칭 패턴 — 공간과 조화되는 장애물
        int pat = rng_i(&room_rng,4);
        if (pat==0){
            // 2x2 기둥 4개, 방의 1/3·2/3 지점 대칭 배치
            int pxs[2]={W/3, W*2/3-1};
            int pys[2]={H/3-1, H*2/3};
            for (int ky=0;ky<2;ky++) for (int kx=0;kx<2;kx++)
                for (int j=0;j<2;j++) for (int i2=0;i2<2;i2++)
                    r->tiles[pys[ky]+j][pxs[kx]+i2]=T_WALL;
        } else if (pat==1){
            // 위/아래 벽에서 마주 보고 뻗는 돌출 벽 (통로 형성)
            int x=4+rng_i(&room_rng,W-10);
            int len=3+rng_i(&room_rng,3);
            for (int j=1;j<=len && j<H-1;j++){
                r->tiles[j][x]=T_WALL; if(x+1<W-1) r->tiles[j][x+1]=T_WALL;
                r->tiles[H-1-j][x]=T_WALL; if(x+1<W-1) r->tiles[H-1-j][x+1]=T_WALL;
            }
        } else if (pat==2){
            // 중앙 상하 가로 블록 — 가운데 길은 열어둠
            int w2=4+rng_i(&room_rng,4);
            int x=(W-w2)/2;
            for (int i2=0;i2<w2;i2++){
                r->tiles[H/2-3][x+i2]=T_WALL;
                r->tiles[H/2+4][x+i2]=T_WALL;
            }
        } // pat==3: 빈 방 (전투 공간)
        // 단편화 지대(3바이옴)는 미로 느낌: 추가 칸막이
        if (biome==2){
            for (int c=0;c<2;c++){
                int x=4+rng_i(&room_rng,W-9);
                int gap=2+rng_i(&room_rng,H>10?H-10:1);
                for (int y2=2;y2<H-2;y2++)
                    if (y2<gap || y2>gap+5) r->tiles[y2][x]=T_WALL;
            }
        }
        // 모서리 컷 (L/T자형 변형) — 스폰/문 모서리는 나중에 강제 카브로 보호
        int ncut = rng_i(&room_rng,3); // 0..2
        for (int c=0;c<ncut;c++){
            int cw = 2 + rng_i(&room_rng, W/4>2?W/4-2:1);
            int ch = 2 + rng_i(&room_rng, H/4>2?H/4-2:1);
            int corner = rng_i(&room_rng,4); // 0 TL 1 TR 2 BL 3 BR
            int x0 = (corner&1)? W-1-cw : 1;
            int y0 = (corner&2)? H-1-ch : 1;
            for (int yy=y0; yy<y0+ch && yy<H-1; yy++)
                for (int xx=x0; xx<x0+cw && xx<W-1; xx++)
                    r->tiles[yy][xx]=T_WALL;
        }
    }

    place_doors();

    // --- 플레이어 스폰 + 3x3 강제 카브
    v2 spawn = spawn_pos_for(entry_dir, W, H);
    int stx=(int)(spawn.x/TILE), sty=(int)(spawn.y/TILE);
    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++){
        int nx=stx+dx, ny=sty+dy;
        if (nx>0&&ny>0&&nx<W-1&&ny<H-1) r->tiles[ny][nx]=T_FLOOR;
    }

    // 문 앞 타일 좌표 (안쪽으로 1칸)
    int din[2][2]; // [door][0]=x [door][1]=y
    for (int dnum=0; dnum<r->door_count; dnum++){
        int dx2=0, dy2=0;
        switch (r->door_dir[dnum]){
            case DIR_R: dx2=-1; break;
            case DIR_L: dx2= 1; break;
            case DIR_D: dy2=-1; break;
            default:    dy2= 1; break;
        }
        din[dnum][0]=r->door_x[dnum]+dx2;
        din[dnum][1]=r->door_y[dnum]+dy2;
        // 문 안쪽 칸 강제 바닥
        if (r->tiles[din[dnum][1]][din[dnum][0]]==T_WALL)
            r->tiles[din[dnum][1]][din[dnum][0]]=T_FLOOR;
    }

    // 도달 가능성 보장: 스폰에서 BFS → 각 문/보스중앙 못 가면 L자 통로 카브
    {
        static uint8_t reach[MAX_ROOM_H][MAX_ROOM_W];
        static int qx[MAX_ROOM_W*MAX_ROOM_H], qy[MAX_ROOM_W*MAX_ROOM_H];
        for (int attempt=0;attempt<2;attempt++){
            memset(reach,0,sizeof(reach));
            int head=0,tail=0;
            qx[tail]=stx; qy[tail]=sty; tail++;
            reach[sty][stx]=1;
            while (head<tail){
                int cx=qx[head], cy=qy[head]; head++;
                static const int dx4[4]={1,-1,0,0}, dy4[4]={0,0,1,-1};
                for (int d=0;d<4;d++){
                    int nx=cx+dx4[d], ny=cy+dy4[d];
                    if (nx<1||ny<1||nx>=W-1||ny>=H-1) continue;
                    if (reach[ny][nx]||r->tiles[ny][nx]==T_WALL) continue;
                    reach[ny][nx]=1; qx[tail]=nx; qy[tail]=ny; tail++;
                }
            }
            bool ok=true;
            for (int dnum=0; dnum<r->door_count; dnum++)
                if (!reach[din[dnum][1]][din[dnum][0]]) ok=false;
            if (r->is_boss && !reach[H/2][W/2]) ok=false;
            if (ok) break;
            // L자 2칸폭 통로: 스폰 → 각 문 안쪽
            for (int dnum=0; dnum<r->door_count; dnum++){
                int tx=din[dnum][0], ty=din[dnum][1];
                int x=stx, y=sty;
                while (x!=tx){
                    int s=x<tx?1:-1; x+=s;
                    if (x>0&&x<W-1){
                        if (y>0&&y<H-1) r->tiles[y][x]=T_FLOOR;
                        if (y+1>0&&y+1<H-1) r->tiles[y+1][x]=T_FLOOR;
                    }
                }
                while (y!=ty){
                    int s=y<ty?1:-1; y+=s;
                    if (y>0&&y<H-1){
                        if (x>0&&x<W-1) r->tiles[y][x]=T_FLOOR;
                        if (x+1>0&&x+1<W-1) r->tiles[y][x+1]=T_FLOOR;
                    }
                }
            }
        }
        // 닿을 수 없는 바닥은 벽으로 메움
        for (int y=1;y<H-1;y++) for (int x=1;x<W-1;x++)
            if (!reach[y][x] && r->tiles[y][x]==T_FLOOR) r->tiles[y][x]=T_WALL;
    }

    // 적 스폰
    memset(G.ents,0,sizeof(G.ents));
    memset(G.bullets,0,sizeof(G.bullets));
    if (r->is_boss){
        spawn_boss(biome);
        r->cleared=false;
    } else if (idx==0 && biome==0){
        r->cleared=true; open_doors(); // 튜토리얼 방 — 적 없음
    } else {
        // 스폰 후보 = 스폰에서 맨해튼 5칸 이상 떨어진 바닥
        static int fx[MAX_ROOM_W*MAX_ROOM_H], fy[MAX_ROOM_W*MAX_ROOM_H];
        int nf=0;
        for (int y=1;y<H-1;y++) for (int x=1;x<W-1;x++)
            if (r->tiles[y][x]==T_FLOOR &&
                (abs(x-stx)+abs(y-sty))>=5){ fx[nf]=x; fy[nf]=y; nf++; }
        int n = 4 + biome + idx/2 + G.difficulty + (G.ngplus?2:0);
        // 정예 확률 (튜토리얼 제외 — idx0/b0은 위 분기, 여기는 일반 방만)
        int elite_pct = 6 + (biome+G.difficulty)*6 + (G.ngplus?12:0);
        for (int i=0;i<n && nf>0;i++){
            int type = biome_enemies[biome][rng_i(&room_rng,6)];
            int k = rng_i(&room_rng,nf);
            // memset 직후 순차 스폰: i번째는 슬롯 i
            spawn_enemy(type, V2(fx[k]*TILE+8.0f, fy[k]*TILE+8.0f));
            Entity* e=&G.ents[i];
            if (e->active && type!=E_HIVE && (int)rng_i(&room_rng,100)<elite_pct){
                e->elite=true; e->hp*=2.2f; e->maxhp=e->hp; e->radius*=1.25f;
                uint32_t h=G.run_seed ^ 0x9E3779B9u;
                h^=(uint32_t)(biome+1)*0x85EBCA6Bu;
                h^=(uint32_t)(idx+1)*0xC2B2AE35u;
                h^=(uint32_t)(i+1)*0x27D4EB2Fu;
                h^=(h>>16); h*=0x7FEB352Du; h^=(h>>15);
                e->event_trait=(uint8_t)(ELITE_VOLATILE+(h&1u));
            }
        }
        event_assign_pending_trait();
        r->cleared=false;
    }

    // 픽업/파티클/범위공격 초기화
    memset(G.pickups,0,sizeof(G.pickups));
    memset(G.parts,0,sizeof(G.parts));
    memset(G.zones,0,sizeof(G.zones));
    G.pl.pos = spawn;
    G.pl.vel = V2(0,0);
    G.pl.glaive_out=false;
    G.room_t=0;
    story_emit_biome_entry_notice(biome,idx);
}

void open_doors(void){
    bool had=false;
    for (int y=0;y<G.room.h;y++) for (int x=0;x<G.room.w;x++)
        if (G.room.tiles[y][x]==T_DOOR_CLOSED){ G.room.tiles[y][x]=T_DOOR_OPEN; had=true; }
    if (had) sfx_play(SFX_DOOR);
}

// 열린 포탈(문/출구) 중심 좌표 나열 — 그리기/흡입 연출/글로우용. exit_flag는 NULL 허용
int portal_list(v2* out, int* exit_flag, int max){
    Room* r=&G.room;
    int n=0;
    for (int dn=0; dn<r->door_count && n<max; dn++){
        if (r->tiles[r->door_y[dn]][r->door_x[dn]]!=T_DOOR_OPEN) continue;
        if (exit_flag) exit_flag[n]=0;
        if (r->door_dir[dn]==DIR_L||r->door_dir[dn]==DIR_R)
            out[n++]=V2(r->door_x[dn]*TILE+8.0f, r->door_y[dn]*TILE+16.0f);
        else
            out[n++]=V2(r->door_x[dn]*TILE+16.0f, r->door_y[dn]*TILE+8.0f);
    }
    // 보스 출구: 둘레 스캔, 인접 2칸의 중간을 대표로
    for (int y=0;y<r->h && n<max;y++) for (int x=0;x<r->w && n<max;x++){
        if (x!=0&&x!=r->w-1&&y!=0&&y!=r->h-1) continue;
        if (r->tiles[y][x]!=T_EXIT) continue;
        if (x==0||x==r->w-1){
            if (y+1<r->h && r->tiles[y+1][x]==T_EXIT){ if(exit_flag)exit_flag[n]=1; out[n++]=V2(x*TILE+8.0f,y*TILE+16.0f); }
        } else {
            if (x+1<r->w && r->tiles[y][x+1]==T_EXIT){ if(exit_flag)exit_flag[n]=1; out[n++]=V2(x*TILE+16.0f,y*TILE+8.0f); }
        }
    }
    return n;
}

static Weapon random_weapon(void){
    Weapon w;
    w.type = rng_i(&room_rng,WPN_COUNT);
    int roll = rng_i(&room_rng,10);
    w.prefix = roll<4? PFX_NONE : (roll<6?PFX_HOT:(roll<8?PFX_COLD:(roll<9?PFX_BROKEN:PFX_COMPRESSED)));
    return w;
}
static void event_place(Room* r){
    static uint8_t reach[MAX_ROOM_H][MAX_ROOM_W];
    static int qx[MAX_ROOM_W*MAX_ROOM_H], qy[MAX_ROOM_W*MAX_ROOM_H];
    int sx=(int)(G.pl.pos.x/TILE), sy=(int)(G.pl.pos.y/TILE), head=0, tail=0;
    memset(reach,0,sizeof(reach));
    if(sx>=0&&sy>=0&&sx<r->w&&sy<r->h){
        uint8_t t=r->tiles[sy][sx];
        if(t==T_FLOOR||t==T_DOOR_OPEN||t==T_EXIT){
            reach[sy][sx]=1;
            qx[tail]=sx;
            qy[tail++]=sy;
        }
    }
    static const int dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    while(head<tail){
        int x=qx[head],y=qy[head++];
        for(int d=0;d<4;d++){
            int nx=x+dx[d],ny=y+dy[d];
            if(nx<0||ny<0||nx>=r->w||ny>=r->h||reach[ny][nx])continue;
            uint8_t t=r->tiles[ny][nx];
            if(t!=T_FLOOR&&t!=T_DOOR_OPEN&&t!=T_EXIT)continue;
            reach[ny][nx]=1;
            qx[tail]=nx;
            qy[tail++]=ny;
        }
    }
    int bx=-1,by=-1,selected_pass=4,best_score=0x7fffffff;
    for(int pass=1;pass<=3&&bx<0;pass++){
        for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++){
            if(!reach[y][x]||r->tiles[y][x]!=T_FLOOR)continue;
            int pd=abs(x-sx)+abs(y-sy);
            bool door_ok=true,pickup_ok=true;
            for(int d=0;d<r->door_count;d++){
                int ix=r->door_x[d],iy=r->door_y[d];
                if(r->door_dir[d]==DIR_L)ix++;
                else if(r->door_dir[d]==DIR_R)ix--;
                else if(r->door_dir[d]==DIR_U)iy++;
                else if(r->door_dir[d]==DIR_D)iy--;
                if(abs(x-ix)+abs(y-iy)<2)door_ok=false;
            }
            for(int i=0;i<MAX_PICKUPS;i++) if(G.pickups[i].active){
                float ax=x*TILE+TILE*0.5f-G.pickups[i].pos.x;
                float ay=y*TILE+TILE*0.5f-G.pickups[i].pos.y;
                if(ax*ax+ay*ay<576.0f)pickup_ok=false;
            }
            bool pass_ok = pass==3 || (pd>=3 && door_ok);
            if(pass==1 && !pickup_ok) pass_ok=false;
            if(!pass_ok)continue;
            int score=abs(2*x+1-r->w)+abs(2*y+1-r->h);
            if(score<best_score ||
               (score==best_score && (y<by || (y==by && x<bx)))){
                best_score=score;
                bx=x; by=y; selected_pass=pass;
            }
        }
    }
    if(bx<0 && sx>=0&&sy>=0&&sx<r->w&&sy<r->h&&reach[sy][sx] &&
       r->tiles[sy][sx]==T_FLOOR){
        bx=sx; by=sy;
        selected_pass=4;
    }
    if(bx<0){
        for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++){
            if(!reach[y][x])continue;
            int score=abs(2*x+1-r->w)+abs(2*y+1-r->h);
            if(score<best_score ||
               (score==best_score && (y<by || (y==by && x<bx)))){
                best_score=score;
                bx=x; by=y;
                selected_pass=4;
            }
        }
    }
    r->event_tile_x=bx;
    r->event_tile_y=by;
    r->event_pos=V2(bx*TILE+TILE*0.5f,by*TILE+TILE*0.5f);
    r->event_place_pass=(uint8_t)selected_pass;
    r->event_player_dist=v2len(v2sub(r->event_pos,G.pl.pos));
    r->event_min_pickup_dist=1e30f;
    for(int i=0;i<MAX_PICKUPS;i++) if(G.pickups[i].active){
        float d=v2len(v2sub(r->event_pos,G.pickups[i].pos));
        if(d<r->event_min_pickup_dist)r->event_min_pickup_dist=d;
    }
    r->event_min_door_dist=1e30f;
    for(int d=0;d<r->door_count;d++){
        int ix=r->door_x[d],iy=r->door_y[d];
        if(r->door_dir[d]==DIR_L)ix++;
        else if(r->door_dir[d]==DIR_R)ix--;
        else if(r->door_dir[d]==DIR_U)iy++;
        else if(r->door_dir[d]==DIR_D)iy--;
        float dx=(ix*TILE+TILE*0.5f)-r->event_pos.x;
        float dy=(iy*TILE+TILE*0.5f)-r->event_pos.y;
        float d2=dx*dx+dy*dy;
        if(d2<r->event_min_door_dist*r->event_min_door_dist)r->event_min_door_dist=sqrtf(d2);
    }
}
static void memory_log_append(int decision, int event_type, int tag){
    uint8_t code=(uint8_t)((decision<<4)|(event_type<<2)|tag);
    MemoryRunState* m=&G.memory;
    if (m->log_count<8){
        int slot=(m->log_head+m->log_count)%8;
        m->log[slot]=code;
        m->log_count++;
    } else {
        m->log[m->log_head]=code;
        m->log_head=(uint8_t)((m->log_head+1)%8);
    }
}

bool event_resolve_choice(int decision){
    Room* r=&G.room;
    if (decision!=MEM_DECISION_KEEP && decision!=MEM_DECISION_DISCARD) return false;
    if (r->event_state!=MEM_STATE_AVAILABLE) return false;
    if (r->event_type!=MEM_EVENT_ECHO && r->event_type!=MEM_EVENT_CORRUPTED) return false;
    if (r->event_tag<0 || r->event_tag>=MEM_TAG_COUNT) return false;

    float dx=r->event_pos.x-G.pl.pos.x;
    float dy=r->event_pos.y-G.pl.pos.y;
    if (dx*dx+dy*dy>24.0f*24.0f) return false;

    if (decision==MEM_DECISION_KEEP){
        if (r->event_type==MEM_EVENT_CORRUPTED &&
            G.memory.pending_trait!=ELITE_NONE) return false;
        if (player_used_kb()+item_kb(64)>player_capacity_kb()) return false;
    }

    int tag=r->event_tag;
    if (decision==MEM_DECISION_KEEP){
        if (G.memory.kept[tag]!=UINT8_MAX) G.memory.kept[tag]++;
        if (r->event_type==MEM_EVENT_CORRUPTED)
            G.memory.pending_trait=(uint8_t)r->event_trait;
    } else {
        if (G.memory.discarded[tag]!=UINT8_MAX) G.memory.discarded[tag]++;
        if (r->event_type==MEM_EVENT_ECHO) G.bytes_run+=3;
        else if (G.pl.hp<G.pl.maxhp) G.pl.hp=fminf(G.pl.hp+1.0f,(float)G.pl.maxhp);
    }
    memory_log_append(decision,r->event_type,tag);
    r->event_state=MEM_STATE_RESOLVED;
    return true;
}
void event_assign_pending_trait(void){
    if(!G.memory.pending_trait)return;
    for(int i=0;i<MAX_ENTITIES;i++){Entity*e=&G.ents[i];if(e->active&&e->type!=E_HIVE&&e->type<E_BOSS_ROT){if(!e->elite){e->elite=true;e->hp*=2.2f;e->maxhp=e->hp;e->radius*=1.25f;}e->event_trait=G.memory.pending_trait;e->event_bonus=1;G.memory.pending_trait=ELITE_NONE;break;}}
}
#ifdef DD_DEBUG
/*
 * Diagnostic fixtures must use the same assignment path as gameplay.  This
 * wrapper is intentionally tiny: it only seeds the pending value and then
 * delegates to the production helper, so its result/counters cannot drift.
 */
bool dd_debug_assign_pending_trait(uint8_t trait){
    G.memory.pending_trait = trait;
    event_assign_pending_trait();
    return G.memory.pending_trait == ELITE_NONE;
}
#endif

void on_room_cleared(void){
    Room* r=&G.room;
    if (r->cleared) return;
    r->cleared=true;
    open_doors();
    v2 c = V2(r->w*TILE*0.5f, r->h*TILE*0.5f);
    // 약속된 보상 드롭
    switch (r->promise){
        case PROMISE_WEAPON: spawn_pickup(PK_WEAPON,c,random_weapon(),0,0); break;
        case PROMISE_RELIC: {
            int rl=rng_i(&room_rng,RELIC_COUNT), guard=0;
            while (G.pl.relics[rl] && guard++<RELIC_COUNT) rl=(rl+1)%RELIC_COUNT;
            spawn_pickup(PK_RELIC,c,(Weapon){0,0},rl,0);
        } break;
        case PROMISE_HEART: spawn_pickup(PK_HEART,c,(Weapon){0,0},0,0); break;
        case PROMISE_SHARD: default:
            spawn_pickup(PK_SHARD,c,(Weapon){0,0},0,0); break;
    }
    // 추가: 추억 조각 확률 + 바이트
    int shard_drops=r->promise==PROMISE_SHARD?1:0;
    if (r->promise!=PROMISE_SHARD && rng_i(&room_rng,3)==0) shard_drops++;
    for (int i=0;i<shard_drops;i++){
        v2 offset=i==0?V2(0,0):V2(rng_range(&room_rng,-30,30),rng_range(&room_rng,-20,20));
        if (i>0 || r->promise!=PROMISE_SHARD)
            spawn_pickup(PK_SHARD,v2add(c,offset),(Weapon){0,0},0,0);
        if (rng_i(&room_rng,5)<3)
            spawn_pickup(PK_SHARD,v2add(c,v2add(offset,V2(20,10))),(Weapon){0,0},0,0);
    }
    for (int i=0;i<2+rng_i(&room_rng,3);i++)
        spawn_pickup(PK_BYTE,v2add(c,V2(rng_range(&room_rng,-40,40),rng_range(&room_rng,-30,30))),(Weapon){0,0},0,0);
    if (!r->is_boss && r->idx == 1 + (int)((G.run_seed >> (r->biome*3)) % 7u)){
        uint32_t seed=G.run_seed;
        r->event_type=1+(int)((seed+r->biome)&1u);
        r->event_tag=(int)(((seed/7u)+(uint32_t)r->biome)%3u);
        r->event_trait=1+(int)(((seed/21u)+(uint32_t)r->biome)&1u);
        r->event_state=MEM_STATE_AVAILABLE;
        event_place(r);
    }
}

static void start_run_with_seed_internal(uint32_t seed,bool save_meta){
    G.training_active=false;
    memset(&G.memory,0,sizeof(G.memory));
    memset(&G.pl,0,sizeof(G.pl));
    G.pl.wrelics[0]=G.pl.wrelics[1]=-1;
    memset(G.floaters,0,sizeof(G.floaters));
    G.pl.maxhp = 3 + (int)G.meta.upg[0]; // 기본 3칸, 영구 강화로 최대 8칸
    G.pl.shield_maxhp = G.pl.maxhp;
    G.pl.hp = (float)G.pl.maxhp;
    // 잠긴 무기 선택 시 기본 무기로
    if (!((G.meta.unlocked_weapons>>G.title_weapon)&1)) G.title_weapon=WPN_SWORD;
    G.pl.weapon.type = G.title_weapon;
    G.pl.weapon.prefix = PFX_NONE;
    G.pl.aim = V2(1,0);
    G.bytes_run = 0;
    G.run_time = 0;
    G.kills = 0;
    G.ambient_mul = 1.0f;
    G.light_mul = 1.0f;
    G.pl.shield = fminf(player_light_shield_limit(),G.meta.upg[3]*0.5f);
    G.pl.light_shield_cap = player_light_shield_limit();
    G.timescale = 1.0f;
    G.hitstop = 0; G.shake = 0;
    G.ending = -1;
    G.run_settled=false;
    G.hist_head = 0;
    G.cam = V2(0,0);
    if (seed) G.run_seed = seed;
    else G.run_seed = (uint32_t)(stm_now()&0xFFFFFFFFu) | 1u;
    G.meta.last_seed = G.run_seed;
    G.meta.runs++;
    story_entry_notice_mask=0;
    set_msg("W,A,S,D : 이동 · 마우스 : 공격 · Shift : 대시 · E : 줍기 · Q : 버리기 · Tab : 가방");
    room_generate(0,0,PROMISE_NONE,DIR_L);
    for (int i=0;i<256;i++) G.history[i]=G.pl.pos;
    music_set(1);
    if (save_meta) meta_save();
}
void start_run_with_seed(uint32_t seed){ start_run_with_seed_internal(seed,true); }
void start_run_after_intro(void){
    G.meta.intro_seen=1;
    G.meta.intro_replay_queued=0;
    start_run_with_seed_internal(G.title_seed,false);
    meta_save();
}
void start_run(void){ start_run_with_seed(G.title_seed); }

void start_training(void){
    static const float station_x[WPN_COUNT]={40,120,200,280,360,440};
    static const float module_dx[4]={-20,20,-20,20};
    static const float module_y[4]={194,194,230,230};
    Room* r=&G.room;
    Player* p=&G.pl;

    G.training_active=true;
    memset(&G.memory,0,sizeof(G.memory));
    memset(p,0,sizeof(*p));
    memset(G.ents,0,sizeof(G.ents));
    memset(G.bullets,0,sizeof(G.bullets));
    memset(G.parts,0,sizeof(G.parts));
    memset(G.pickups,0,sizeof(G.pickups));
    memset(G.floaters,0,sizeof(G.floaters));
    memset(G.enemy_feedback,0,sizeof(G.enemy_feedback));
    memset(G.zones,0,sizeof(G.zones));
    memset(r,0,sizeof(*r));

    p->wrelics[0]=p->wrelics[1]=-1;
    p->maxhp=3+(int)G.meta.upg[0];
    p->shield_maxhp=p->maxhp;
    p->hp=(float)p->maxhp;
    p->shield=fminf(player_light_shield_limit(),G.meta.upg[3]*0.5f);
    p->light_shield_cap=player_light_shield_limit();
    if (!((G.meta.unlocked_weapons>>G.title_weapon)&1)) G.title_weapon=WPN_SWORD;
    p->weapon=(Weapon){G.title_weapon,PFX_NONE};
    p->aim=V2(0,-1);
    p->pos=V2(VIRT_W*0.5f,132.0f);

    r->w=30; r->h=17; r->biome=0; r->idx=0; r->cleared=true;
    for (int y=0;y<r->h;y++) for (int x=0;x<r->w;x++)
        r->tiles[y][x]=(x==0||y==0||x==r->w-1||y==r->h-1)?T_WALL:T_FLOOR;

    G.run_seed=((uint32_t)(stm_now()*1000.0)&0xFFFFFFFFu)|1u;
    G.bytes_run=0; G.run_time=0; G.kills=0; G.room_t=0;
    G.ambient_mul=1.0f; G.light_mul=1.0f;
    G.timescale=1.0f; G.hitstop=0; G.shake=0; G.run_settled=true;
    G.hist_head=0; G.cam=V2(0,0);
    for (int i=0;i<256;i++) G.history[i]=p->pos;

    spawn_enemy(E_GOLEM,V2(VIRT_W*0.5f,88.0f));
    G.ents[0].hp=G.ents[0].maxhp=1000000000.0f;
    G.ents[0].spawn_t=0;

    for (int w=0;w<WPN_COUNT;w++){
        spawn_pickup(PK_WEAPON,V2(station_x[w],160.0f),(Weapon){w,PFX_NONE},0,0);
        for (int m=0;m<4;m++)
            spawn_pickup(PK_WRELIC,V2(station_x[w]+module_dx[m],module_y[m]),(Weapon){0,0},w*4+m,0);
    }
    set_msg("훈련장 — E로 무기와 무기 유물을 장착할 수 있다");
    music_set(1);
}

bool settle_run_once(int reason){
    if (G.run_settled) return false;
    G.run_settled=true;
    G.meta.bytes_currency += (uint32_t)G.bytes_run;
    if ((uint32_t)G.room.biome > G.meta.best_biome) G.meta.best_biome=(uint32_t)G.room.biome;
    meta_save();
    if (reason==SETTLE_DEATH){
        music_set(-1);
        fade_to(ST_DEAD,COL(0x160D24));
    } else {
        music_set(0);
        G.state=ST_TITLE;
        G.state_t=0;
    }
    return true;
}
#ifdef DD_DEBUG
/*
 * Route deterministic death fixtures through the production damage helper.
 * The implementation lives in combat.c; this declaration is available in the
 * unity build and keeps the fixture from duplicating death accounting.
 */
extern void player_take_damage(v2 from);

void dd_debug_trigger_player_death(v2 from){
    player_take_damage(from);
}
#endif
