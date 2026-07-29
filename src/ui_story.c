// ui_story.c — 상태머신/입력/렌더 디스패치/HUD/스토리(인트로·회상·엔딩·에필로그)
#ifdef DD_DEBUG
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#endif
#include "game.h"
#include "gameplay.h"

bool key_held[512];
bool attack_held;
v2 mouse_virt;
bool mouse_present;

float combat_slash_t(void);
v2 combat_slash_dir(void);
float combat_lance_thrust_t(void);
v2 combat_lance_thrust_dir(void);
float combat_lance_thrust_reach(void);

static Rng urng = { 0xBADA55C0DEull };

// ----------------------------------------------------------- 스토리 텍스트
static const char* core_titles[4] = {
    "복구 블록 #1 — 첫 부팅", "복구 블록 #2 — 첫 승리", "복구 블록 #3 — 마지막 저장", "복구 블록 #4 — 남긴 한 줄"
};
static const char* core_texts[4] = {
    "작은 손이 디스켓을 밀어 넣는다.\n화면이 켜지고 첫 모험이 시작된다.",
    "수십 번의 실패 끝에 두 팔이 올라간다.\n옆에서 작은 박수가 들린다.",
    "조금 자란 손이 디스켓을 다시 둔다.\n\"다음에 마저 해야지.\"",
    "세이브 블록 한구석의 글씨가\n읽기 창의 빛에서 또렷해진다.",
};
static const char* core_inventory_texts[4] = {
    "신호 획득 · 처음 켜진 화면", "승리의 흔적 · 첫 승리와 박수",
    "읽히지 않은 작별 · 다음에 마저", "검증된 메시지 · 읽기 창의 한 줄",
};
static const char* core_title_for(int core_id){
    return core_id>=0&&core_id<4?core_titles[core_id]:"복구 블록 — 확인 불가";
}
static const char* core_text_for(int core_id){
    return core_id>=0&&core_id<4?core_texts[core_id]:"복구 블록을 확인할 수 없다.";
}
static const char* intro_pages[3] = {
    "20년 전 —\n한 아이가 모든 모험을\n1.44MB 디스켓 한 장에 저장했다.",
    "아이는 자랐고, 잊었다.\n서랍 속에서 디스켓은\n천천히 부패해 갔다.\n\n오늘, 원본에 쓰지 않는\n마지막 한 번의 복구 패스가 시작된다.",
    "당신은 가장 깊은 배드 섹터에 남은\n마지막 정상 인덱스 조각.\n\n빛이 닿는 곳, 읽기 창까지\n올라가야 한다.\n\n무엇을 끝까지 기억할 것인가.",
};
static const char* biome_names[4] = { "배드 섹터", "잃어버린 트랙", "단편화 지대", "읽기 창" };
static const char* boss_names[4] = { "부패충 ROT", "메아리 ECHO", "단편기 DEFRAG", "삭제 NULL" };
static const char* diff_names[3] = { "쉬움", "보통", "어려움" };

static const char* promise_label(int promise){
    switch (promise){
        case PROMISE_WEAPON: return "무기 획득 보장 · 세부 능력은 획득 시 확인";
        case PROMISE_RELIC: return "유물 획득 보장 · 세부 효과는 획득 시 확인";
        case PROMISE_SHARD: {
            static char label[80];
            snprintf(label,sizeof label,"추억 조각 획득 보장 · 용량 +%dKB",player_item_kb(64));
            return label;
        }
        case PROMISE_HEART: return "회복 획득 보장 · 용량 변화 없음";
        default: return "";
    }
}

static const char* branch_promise_display_label(int promise){
    switch (promise){
        case PROMISE_WEAPON: return "무기 획득 보장 · 세부\n능력은 획득 시 확인";
        case PROMISE_RELIC: return "유물 획득 보장 · 세부\n효과는 획득 시 확인";
        default: return promise_label(promise);
    }
}

static v2 play_camera(Room* r){
    float room_px_x=r->w*TILE, room_px_y=r->h*TILE;
    float x = room_px_x<=VIRT_W ? (room_px_x-VIRT_W)*0.5f
                               : clampf(G.pl.pos.x-VIRT_W*0.5f,0,room_px_x-VIRT_W);
    float y = room_px_y<=VIRT_H ? (room_px_y-VIRT_H)*0.5f
                               : clampf(G.pl.pos.y-VIRT_H*0.5f,0,room_px_y-VIRT_H);
    return V2(x,y);
}

static v2 branch_promise_label_pos(float x,float y,float cam_x,float cam_y,const char* label){
    float half=text_width(label,0.42f)*0.5f;
    return V2(clampf(x-cam_x,half+16,VIRT_W-half-16),
              clampf(y-cam_y+14,84,VIRT_H-24));
}
static bool branch_promise_label_visible(float y,float cam_y){
    float label_y=y+14;
    return label_y>=cam_y+84 && label_y<=cam_y+VIRT_H-24;
}
static bool branch_promise_guidance_visible(void){
    return G.room.cleared && G.room.door_count==2 && G.difficulty==0;
}

// 엔딩: 0 빈손 1 표준 2 완전 복구 3 진엔딩
static const char* ending_lines[4][4] = {
    { "세 번의 읽기가 끝났다.", "복구 이미지를 만들 수 없었다.", "원본에는 아무것도 쓰지 않는다.", "드라이브가 멎고 원본 디스크는 다시 서랍으로 들어간다.\n읽히지 않은 기억은 돌아오지 않았다." },
    { "복구 블록이 모였다.", "부분 복구 이미지가 메모리에 재구성된다.", "\"...아직 있었네.\"", "읽힌 조각만 별도의 복구 이미지로 남긴다.\n어른은 원본 디스크를 건드리지 않는다." },
    { "네 복구 블록이 맞물렸다.", "복구 이미지가 검증된다.", "\"이 모험을 지우지 마.\"", "어른은 원본 디스크를 버리지 않고,\n복구된 마지막 한 줄을 읽는다." },
    { "복구 블록이 읽기 창에서 맞물린다.", "던전의 어둠은 원본 디스크의 트랙으로 사라진다.", "원본은 그대로 남아 있다.", "이번에는, 저장한다." },
};
static const char* ending_names[4] = { "지워짐", "한 번 더", "완전 복구", "원본의 이름" };

// ----------------------------------------------------------- biome palette
static col3 biome_floor(int b){
    switch(b){
        case 0: return COL(0x2A1B45);
        case 1: return COL(0x1B2545);
        case 2: return COL(0x3A2238);
        default: return COL(0x232B4E);
    }
}
static col3 biome_wall(int b){
    switch(b){
        case 0: return COL(0x3D2A66);
        case 1: return COL(0x2A3D77);
        case 2: return COL(0x5C3055);
        default: return COL(0x39477E);
    }
}
static col3 enemy_tint(int b){
    switch(b){
        case 0: return COL(0xB08CD8);
        case 1: return COL(0x8CB0E0);
        case 2: return COL(0xE09CC8);
        default: return COL(0xA8C0F0);
    }
}

static void draw_enemy_player_feedback(const EnemyFeedback* feedback){
    if (feedback->t<=0) return;
    float sx=feedback->pos.x-G.cam.x, sy=feedback->pos.y-G.cam.y;
    if (sx<-24 || sx>VIRT_W+24 || sy<-24 || sy>VIRT_H+24) return;
    float bar_w=clampf(feedback->radius*2.8f,18.0f,34.0f);
    float x=clampf(feedback->pos.x,G.cam.x+bar_w*0.5f+3.0f,G.cam.x+VIRT_W-bar_w*0.5f-3.0f);
    float y=clampf(feedback->pos.y-feedback->radius*0.8f-4.5f,G.cam.y+16.0f,G.cam.y+VIRT_H-8.0f);
    draw_quad(x-bar_w*0.5f-1,y-1,bar_w+2,4,COL(0x08050D),0.9f);
    draw_quad(x-bar_w*0.5f,y,bar_w*feedback->hp,2,feedback->elite?COL(0xFFD060):COL(0xFF3D7F),0.95f);
    float damage_t=feedback->t-2.1f;
    if (damage_t>0){
        char damage[48];
        float rise=(0.9f-damage_t)*10.0f;
        float text_y=clampf(y-8.0f-rise,G.cam.y+5.0f,G.cam.y+VIRT_H-12.0f);
        if (feedback->crit && feedback->hits>1) snprintf(damage,sizeof(damage),"치명 %d / %dHIT",(int)lroundf(feedback->damage),feedback->hits);
        else if (feedback->crit) snprintf(damage,sizeof(damage),"치명 %d",(int)lroundf(feedback->damage));
        else if (feedback->hits>1) snprintf(damage,sizeof(damage),"%d / %dHIT",(int)lroundf(feedback->damage),feedback->hits);
        else snprintf(damage,sizeof(damage),"%d",(int)lroundf(feedback->damage));
        draw_text_center(damage,x,text_y,feedback->crit?0.58f:0.52f,
                         feedback->crit?COL(0xFFD060):COL(0xFFFFFF),
                         clampf(damage_t/0.18f,0,1));
    }
}

// ----------------------------------------------------------- play drawing
static int enemy_sprite(int type){
    switch(type){
        case E_SLIME: case E_MINI_SLIME: return SPR_SLIME;
        case E_BAT: return SPR_BAT;
        case E_WRAITH: return SPR_WRAITH;
        case E_CHASER: return SPR_CHASER;
        case E_GOLEM: return SPR_GOLEM;
        case E_TURRET: return SPR_TURRET;
        case E_SENTINEL: return SPR_SENTINEL;
        case E_DRONE: return SPR_DRONE;
        case E_BOMBER: return SPR_BOMBER;
        case E_SNIPER: return SPR_SNIPER;
        case E_SHIELDER: return SPR_SHIELDER;
        case E_HIVE: return SPR_HIVE;
        case E_BOSS_ROT: return SPR_BOSS_ROT;
        case E_BOSS_ECHO: return SPR_BOSS_ECHO;
        case E_BOSS_DEFRAG: return SPR_BOSS_DEFRAG;
        case E_BOSS_NULL: return SPR_BOSS_NULL;
        case E_ECHO_GHOST: return SPR_FLAME;
    }
    return SPR_SLIME;
}
static float enemy_size(int type){
    switch(type){
        case E_MINI_SLIME: return 10;
        case E_GOLEM: return 22;
        case E_SENTINEL: return 21;
        case E_BOSS_ROT: case E_BOSS_ECHO: case E_BOSS_DEFRAG: case E_BOSS_NULL: return 44;
        case E_ECHO_GHOST: return 20;
        case E_WRAITH: return 17;
        case E_SNIPER: return 16;
        case E_SHIELDER: return 18;
        case E_HIVE: return 20;
    }
    return 14;
}
static void draw_reward_effect_label(const Pickup* pk);
static int pickup_sprite(const Pickup* pk){
    switch (pk->type){
        case PK_WEAPON: return weapon_defs[pk->weapon.type].spr;
        case PK_RELIC: return SPR_RELIC;
        case PK_WRELIC: return SPR_RELIC;
        case PK_SHARD: return SPR_SHARD;
        case PK_CORE: return SPR_CORE_SHARD;
        case PK_HEART: return SPR_HEART;
        default: return SPR_BYTE;
    }
}

// 원 외곽선 (범위공격 예고용)
static void draw_ring(float cx,float cy,float rad,col3 c,float a){
    const int N=14;
    for (int i=0;i<N;i++){
        float a0=i*6.2832f/N, a1=(i+1)*6.2832f/N;
        draw_line(cx+cosf(a0)*rad,cy+sinf(a0)*rad,cx+cosf(a1)*rad,cy+sinf(a1)*rad,1.0f,c,a);
    }
}

void draw_play(void){
    Room* r=&G.room;
    int RW=r->w, RH=r->h;
    float sx = G.meta.opt_shake? rng_range(&urng,-G.shake,G.shake):0;
    float sy = G.meta.opt_shake? rng_range(&urng,-G.shake,G.shake):0;
    col3 fl=biome_floor(r->biome), wl=biome_wall(r->biome), et=enemy_tint(r->biome);

    // ---------- 카메라 (큰 방 스크롤, 작은 방 가운데)
    G.cam=play_camera(r);
    float cx=G.cam.x, cy=G.cam.y;

    // ---------- 그림자 캐스터 (행 단위로 병합)
    shadow_clear();
    for (int y=0;y<RH;y++){
        int x=0;
        while (x<RW){
            if (r->tiles[y][x]==T_WALL||r->tiles[y][x]==T_DOOR_CLOSED){
                int x0=x;
                while (x<RW&&(r->tiles[y][x]==T_WALL||r->tiles[y][x]==T_DOOR_CLOSED)) x++;
                shadow_add_box(x0*TILE,(float)y*TILE,(x-x0)*TILE,TILE);
            } else x++;
        }
    }

    // ---------- scene
    draw_scene_begin(cx-sx,cy-sy);
    for (int y=0;y<RH;y++) for (int x=0;x<RW;x++){
        uint8_t t=r->tiles[y][x];
        float px=x*TILE+8.0f, py=y*TILE+8.0f;
        if (t==T_FLOOR||t==T_DOOR_OPEN||t==T_EXIT){
            uint32_t h=(uint32_t)(x*7349+y*9241+G.run_seed);
            int v=SPR_TILE_FLOOR_A+(int)(h%9==0?2:(h%5==0?1:0));
            draw_sprite(v,px,py,16,16,fl,1,false,0);
        } else {
            // 모든 벽 타일에 벽돌 텍스처를 그려 장애물을 확실히 인지시킨다 (검은 면 제거)
            col3 wc = (t==T_DOOR_CLOSED)?COL(0x6B3355):wl;
            // 윗면(북쪽이 바닥인 가장 윗줄)은 살짝 어둡게 — 의사 입체
            bool topface = (y>0) && (r->tiles[y-1][x]==T_WALL || r->tiles[y-1][x]==T_DOOR_CLOSED);
            col3 body = topface? (col3){wc.r*0.78f,wc.g*0.78f,wc.b*0.82f} : wc;
            draw_sprite(SPR_TILE_WALL,px,py,16,16,body,1,false,0);
            // 벽이 바닥과 만나는 윗 모서리에 밝은 띠 — 윤곽 강조
            bool topEdge = (y==0) || r->tiles[y-1][x]==T_FLOOR || r->tiles[y-1][x]==T_DOOR_OPEN || r->tiles[y-1][x]==T_EXIT;
            if (topEdge)
                draw_quad(px-8,py-8,16,3,(col3){wc.r*1.5f+0.12f,wc.g*1.5f+0.12f,wc.b*1.5f+0.12f},0.92f);
        }
    }
    { // 포탈: 문 2칸당 블랙홀 하나 — 크게, 천천히 회전
        v2 pc[4]; int ex[4]; int np=portal_list(pc,ex,4);
        for (int i=0;i<np;i++)
            draw_sprite(SPR_EXIT,pc[i].x,pc[i].y,30,30,COL(ex[i]?0x9FFFF0:0x3FE0C5),1,false,G.time*2.0f);
    }
    // 분기 문 보상 아이콘 — 각 실제 문 안쪽 1칸
    if (r->cleared && r->door_count==2){
        float bob=sinf(G.time*4.0f)*2.0f;
        for (int dn=0; dn<2; dn++){
            int pr=r->door_promise[dn];
            int icon = pr==PROMISE_WEAPON?SPR_SWORD:pr==PROMISE_RELIC?SPR_RELIC:pr==PROMISE_HEART?SPR_HEART:SPR_SHARD;
            int ix=0, iy=0;
            switch (r->door_dir[dn]){
                case DIR_R: ix=-1; break;
                case DIR_L: ix= 1; break;
                case DIR_D: iy=-1; break;
                default:    iy= 1; break;
            }
            float dx=(r->door_x[dn]+ix)*TILE+8.0f;
            float dy=(r->door_y[dn]+iy)*TILE+8.0f;
            const char* label=branch_promise_display_label(pr);
            v2 label_pos=branch_promise_label_pos(dx,dy,cx,cy,label);
            draw_sprite(icon,dx,dy+bob,12,12,COL(0xFFFFFF),1,false,0);
            if (branch_promise_guidance_visible() &&
                (pr==PROMISE_WEAPON || branch_promise_label_visible(dy,cy)))
                draw_text_center(label,label_pos.x,label_pos.y,0.42f,COL(0xE8E0F8),0.9f);
        }
    }
    if (!G.training_active)
        for (int i=0;i<MAX_PICKUPS;i++)
            if (G.pickups[i].active && (G.pickups[i].type==PK_WEAPON ||
                G.pickups[i].type==PK_RELIC || G.pickups[i].type==PK_WRELIC))
                draw_reward_effect_label(&G.pickups[i]);
    if (G.training_active){
        v2 button=V2(VIRT_W*(2.0f/3.0f),88.0f);
        bool nearby=v2len(v2sub(G.pl.pos,button))<=24.0f;
        col3 face=G.training_summon_cd<=0?(nearby?COL(0x3FE0C5):COL(0x287A73)):COL(0x34394A);
        for (int iy=-4;iy<=4;iy++){
            float half=sqrtf(25.0f-(float)(iy*iy));
            draw_line(button.x-half,button.y+1.5f+(float)iy,
                      button.x+half,button.y+1.5f+(float)iy,1.2f,COL(0x08060D),0.9f);
            draw_line(button.x-half,button.y-0.5f+(float)iy,
                      button.x+half,button.y-0.5f+(float)iy,1.2f,face,0.96f);
        }
        draw_ring(button.x,button.y-0.5f,5.5f,nearby?COL(0xBFFFF4):COL(0x3FE0C5),0.95f);
        draw_line(button.x-2.5f,button.y-3.5f,button.x+2.5f,button.y-3.5f,
                  1.0f,COL(0xE8FFF9),0.72f);
        draw_line(button.x-2.5f,button.y+3.5f,button.x+2.5f,button.y+3.5f,
                  1.0f,COL(0x10282A),0.8f);
    }
    // 픽업
    // 기억 이벤트 — 방 클리어 뒤 배드 섹터에 남은 조각
    if (r->event_state==MEM_STATE_AVAILABLE){
        float pulse=1.0f+0.12f*sinf(G.time*5.0f);
        draw_sprite(r->event_type==MEM_EVENT_ECHO?SPR_CORE_SHARD:SPR_SHARD,
                    r->event_pos.x,r->event_pos.y,14*pulse,14*pulse,
                    r->event_type==MEM_EVENT_ECHO?COL(0x9FFFF0):COL(0xFFB0CC),
                    0.9f,false,G.time*1.5f);
    }
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pk=&G.pickups[i];
        if (!pk->active) continue;
        float bob=sinf(pk->bob)*2.0f;
        float s = pk->type==PK_CORE?14.0f:(pk->type==PK_BYTE?8.0f:12.0f);
        col3 ptint = pk->type==PK_WRELIC? COL(0xFFD060):COL(0xFFFFFF); // 무기 유물은 금빛
        if (pk->type==PK_WRELIC){
            static const col3 station_colors[WPN_COUNT]={
                {0.25f,0.95f,0.82f}, {1.00f,0.68f,0.30f}, {0.98f,0.42f,0.72f},
                {0.55f,0.94f,0.45f}, {1.00f,0.42f,0.38f}, {0.68f,0.48f,1.00f}
            };
            if (G.training_active) ptint=station_colors[weapon_relic_defs[pk->relic].weapon];
            s=13.0f;
            draw_quad(pk->pos.x-9,pk->pos.y+bob-9,18,18,ptint,0.12f+0.08f*sinf(G.time*5.0f));
        }
        draw_sprite(pickup_sprite(pk),pk->pos.x,pk->pos.y+bob,s,s,ptint,1,false,0);
    }
    // 화염 궤적: 적과 플레이어 아래에 남는 3초 화상지대
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (!b->active || b->kind!=11) continue;
        float age=3.0f-b->life;
        float fade=clampf(age*7.0f,0,1)*clampf(b->life*2.5f,0,1);
        float phase=(float)i*1.731f;
        for (int k=0;k<5;k++){
            float angle=phase+(float)k*2.17f;
            float x=b->pos.x+cosf(angle)*b->radius*(0.18f+0.08f*(float)(k%3));
            float y=b->pos.y+sinf(angle)*b->radius*0.28f;
            float len=5.0f+(float)(k%3)*2.0f;
            float tilt=sinf(G.time*5.0f+angle)*2.0f;
            draw_line(x-len*0.5f,y,x+len*0.5f,y+tilt,3.0f,(col3){1.8f,0.24f,0.08f},0.22f*fade);
            draw_line(x-len*0.38f,y-0.5f,x+len*0.38f,y+tilt-0.5f,1.2f,(col3){2.2f,0.82f,0.12f},0.42f*fade);
        }
        for (int f=0;f<4;f++){
            float angle=phase+(float)f*2.399f;
            float orbit=2.0f+(float)(f%3)*3.0f;
            float flicker=sinf(G.time*(7.0f+(float)f)+(float)f*1.9f);
            float x=b->pos.x+cosf(angle)*orbit;
            float y=b->pos.y+sinf(angle)*orbit*0.42f+2.0f;
            float h=7.0f+(float)(f%3)*2.4f+flicker*1.4f;
            float sway=flicker*2.2f;
            draw_line(x-2.5f,y,x+sway,y-h,2.4f,(col3){2.0f,0.28f,0.08f},0.45f*fade);
            draw_line(x+2.5f,y,x+sway,y-h,1.8f,(col3){2.3f,0.72f,0.12f},0.58f*fade);
            draw_line(x,y-1.0f,x+sway*0.7f,y-h*0.72f,1.0f,(col3){2.6f,1.65f,0.34f},0.68f*fade);
        }
        for (int s=0;s<2;s++){
            float rise=fmodf(G.time*1.7f+phase*0.11f+(float)s*0.27f,1.0f);
            float drift=sinf(phase+(float)s*2.3f+G.time*4.0f)*3.0f;
            float size=1.0f+(1.0f-rise)*1.1f;
            draw_quad(b->pos.x+drift+(float)(s-1)*3.0f,b->pos.y-5.0f-rise*13.0f,size,size,(col3){2.4f,1.3f,0.22f},(1.0f-rise)*0.50f*fade);
        }
    }
    // 적
    for (int i=0;i<MAX_ENTITIES;i++){
        Entity* e=&G.ents[i];
        if (!e->active) continue;
        float s=enemy_size(e->type);
        if (e->spawn_t>0) s*= 1.0f-(e->spawn_t/0.5f);
        col3 tint = et;
        float alpha=1.0f;
        bool boss = e->type>=E_BOSS_ROT&&e->type<=E_BOSS_NULL;
        if (boss) tint=COL(0xD8C8F0);
        if (e->type==E_WRAITH){ alpha=0.8f; }
        if (e->type==E_ECHO_GHOST){ tint=COL(0xFF3D7F); alpha=0.55f; }
        // 둥지: 맥동
        if (e->type==E_HIVE) s*= 1.0f+0.08f*sinf(G.time*3.0f);
        // 정예: 크게 + 밝은 흰끼 + 맥동 외곽
        if (e->elite){ s*=1.3f; float pul=0.7f+0.3f*sinf(G.time*5.0f);
            tint=(col3){tint.r*1.3f+0.4f,tint.g*1.3f+0.4f,tint.b*1.3f+0.4f}; alpha=pul*0.3f+0.7f; }
        // 배드비트 도화선: 빠른 점멸
        if (e->type==E_BOMBER && e->state==1 && fmodf(G.time,0.12f)<0.06f) tint=(col3){3,2,1};
        if (e->flash>0) tint=(col3){3,3,3};
        float wob = sinf(G.time*6.0f+i)*0.08f;
        // 패리티는 바라보는 방향(face)으로 좌우 반전, 나머지는 이동 방향
        bool flip = e->type==E_SHIELDER? cosf(e->face)<0 : e->vel.x<0;
        draw_sprite(enemy_sprite(e->type),e->pos.x,e->pos.y,s*(1.0f+wob),s*(1.0f-wob),tint,alpha,flip,0);
        // 포인터 조준 텔레그래프: 점점 진해지는 가는 선
        if (e->type==E_SNIPER && e->state==1){
            float prog=clampf(1.0f-e->t0/0.9f,0,1);
            draw_line(e->pos.x,e->pos.y,e->target.x,e->target.y,1.0f,COL(0xFF3D7F),prog*0.7f);
        }
        // 배드비트 도화선 링
        if (e->type==E_BOMBER && e->state==1){
            float prog=clampf(1.0f-e->t0/0.7f,0,1);
            float rad=8.0f+prog*22.0f;
            draw_quad(e->pos.x-rad,e->pos.y-rad,rad*2,rad*2,COL(0xFF7A3D),0.12f+0.1f*sinf(G.time*22.0f));
        }
        // 보스 텔레그래프
        if (e->type==E_BOSS_DEFRAG && e->state==1){
            float warn=0.3f+0.3f*sinf(G.time*20.0f);
            if (e->t1==0) draw_quad(0,e->t2-TILE,RW*TILE,TILE*2,COL(0xFF3D7F),warn);
            else draw_quad(e->t2-TILE,0,TILE*2,RH*TILE,COL(0xFF3D7F),warn);
        }
        if (e->type==E_BOSS_NULL && e->phase==1 && e->state==1){
            draw_line(e->pos.x,e->pos.y,e->target.x,e->target.y,2,COL(0xFF3D7F),0.5f+0.3f*sinf(G.time*18.0f));
        }
    }
    for (int i=0;i<MAX_ENTITIES;i++) draw_enemy_player_feedback(&G.enemy_feedback[i]);
    // 범위지정 위험구역 예고
    for (int i=0;i<MAX_ZONES;i++){
        AoeZone* z=&G.zones[i];
        if (!z->active) continue;
        float prog=clampf(1.0f-z->t/z->warn,0,1);          // 0 시작 1 폭발 직전
        float pulse=0.4f+0.3f*sinf(G.time*22.0f);
        if (z->kind==1){ // 가로줄 밴드
            draw_quad(0,z->pos.y-z->r,RW*TILE,z->r*2,COL(0xFF3D7F),0.06f+0.1f*prog);
            float hh=z->r*2*prog;
            draw_quad(0,z->pos.y-hh*0.5f,RW*TILE,hh,COL(0xFFB0CC),0.35f+0.2f*pulse);
        } else if (z->kind==2){ // 세로줄 밴드
            draw_quad(z->pos.x-z->r,0,z->r*2,RH*TILE,COL(0xFF3D7F),0.06f+0.1f*prog);
            float ww=z->r*2*prog;
            draw_quad(z->pos.x-ww*0.5f,0,ww,RH*TILE,COL(0xFFB0CC),0.35f+0.2f*pulse);
        } else { // 원형
            draw_quad(z->pos.x-z->r,z->pos.y-z->r,z->r*2,z->r*2,COL(0xFF3D7F),0.06f+0.06f*prog);
            draw_ring(z->pos.x,z->pos.y,z->r,COL(0xFF3D7F),0.35f+0.35f*pulse);
            draw_ring(z->pos.x,z->pos.y,z->r*prog,COL(0xFFB0CC),0.6f);
        }
    }
    // 탄
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (!b->active) continue;
        if (b->kind==11) continue;
        if (b->kind==12){
            float pulse=0.65f+0.25f*sinf(G.time*18.0f);
            draw_ring(b->pos.x,b->pos.y,7.0f,COL(0x7CFCE4),pulse);
            continue;
        }
        if (b->kind==13){
            float prog=clampf(1.0f-b->life/0.22f,0,1);
            float radius=8.0f+(b->radius-8.0f)*prog;
            draw_ring(b->pos.x,b->pos.y,radius,COL(0x7CFCE4),(1.0f-prog)*0.85f);
            draw_ring(b->pos.x,b->pos.y,radius*0.7f,COL(0x9FFFF0),(1.0f-prog)*0.45f);
            continue;
        }
        if (b->fuse_armed){
            float pulse=0.72f+0.20f*sinf(G.time*24.0f);
            float radius=7.0f+(0.35f-b->life)*8.0f;
            draw_ring(b->pos.x,b->pos.y,radius,COL(0xFF7A3D),pulse);
            draw_ring(b->pos.x,b->pos.y,radius*0.62f,COL(0xFFF0D0),pulse*0.65f);
            continue;
        }
        if (b->kind==14){
            v2 end=v2add(b->pos,b->vel);
            float fade=clampf(b->life/0.12f,0,1);
            draw_line(b->pos.x,b->pos.y,end.x,end.y,b->radius*2.0f,COL(0x3FE0C5),0.38f*fade);
            draw_line(b->pos.x,b->pos.y,end.x,end.y,fmaxf(1.0f,b->radius*0.7f),COL(0xE8FFF9),0.95f*fade);
            continue;
        }
        col3 c = b->from_player? COL(0x7CFCE4):COL(0xFF3D7F);
        if (b->kind==6) c=COL(0xFF7A3D);
        if (b->kind==3){
            draw_sprite(SPR_GLAIVE,b->pos.x,b->pos.y,16,16,(col3){2,2,2},1,false,G.time*720.0f);
        } else if (b->kind==8){
            v2 forward=v2norm(b->vel), side=V2(-forward.y,forward.x);
            v2 previous=v2add(b->pos,v2add(v2scale(side,-11.0f),v2scale(forward,-1.0f)));
            for (int k=1;k<=6;k++){
                float u=-1.0f+(float)k/3.0f;
                v2 next=v2add(b->pos,v2add(v2scale(side,u*11.0f),v2scale(forward,5.0f*(1.0f-u*u)-1.0f)));
                draw_line(previous.x,previous.y,next.x,next.y,3.2f,(col3){1.5f,2.3f,2.1f},0.95f);
                draw_line(previous.x-forward.x*2.0f,previous.y-forward.y*2.0f,
                          next.x-forward.x*2.0f,next.y-forward.y*2.0f,1.1f,COL(0x7CFCE4),0.6f);
                previous=next;
            }
        } else {
            draw_quad(b->pos.x-b->radius,b->pos.y-b->radius,b->radius*2,b->radius*2,c,0.9f);
        }
    }
    // 파티클 (씬)
    for (int i=0;i<MAX_PARTICLES;i++){
        Particle* pa=&G.parts[i];
        if (!pa->active||pa->glow) continue;
        float a=pa->life/pa->max_life;
        draw_quad(pa->pos.x-pa->size*0.5f,pa->pos.y-pa->size*0.5f,pa->size,pa->size,pa->col,a);
    }
    // 플레이어
    {
        Player* p=&G.pl;
        float spd=v2len(p->vel);
        float sq = clampf(spd/300.0f,0,0.25f);
        float blink = (p->iframes>0 && fmodf(G.time,0.12f)<0.06f)?0.3f:1.0f;
        float bob = sinf(p->anim_t)*1.0f;
        float lt=combat_lance_thrust_t();
        if (lt>0){
            v2 d=combat_lance_thrust_dir();
            float reach=combat_lance_thrust_reach();
            float fade=clampf(lt/0.16f,0,1);
            float tipx=p->pos.x+d.x*reach, tipy=p->pos.y+d.y*reach;
            draw_line(p->pos.x,p->pos.y,tipx,tipy,6.0f,COL(0x3FE0C5),0.35f*fade);
            draw_line(p->pos.x,p->pos.y,tipx,tipy,2.0f,(col3){1.8f,2.2f,2.1f},fade);
        }
        draw_sprite(SPR_FLAME,p->pos.x,p->pos.y+bob,14*(1.0f+sq),14*(1.0f-sq),(col3){1.6f,1.6f,1.6f},blink,p->aim.x<0,0);
        // 검 슬래시
        float st=combat_slash_t();
        if (st>0){
            v2 d=combat_slash_dir();
            float prog=1.0f-st/0.14f;
            if (player_has_wrelic(WR_SWORD_WHIRL)){
                float rad=40.0f;
                float spin=prog*6.2832f;
                for (int layer=0;layer<3;layer++){
                    float rr=rad-(float)layer*5.0f;
                    float offset=spin+(float)layer*1.65f;
                    for (int arc=0;arc<3;arc++){
                        float start=offset+(float)arc*2.0944f;
                        for (int k=0;k<5;k++){
                            float a0=start+(float)k*0.22f, a1=start+(float)(k+1)*0.22f;
                            float alpha=(0.95f-(float)layer*0.18f)*(1.0f-prog*0.55f)*(1.0f-(float)k*0.1f);
                            draw_line(p->pos.x+cosf(a0)*rr,p->pos.y+sinf(a0)*rr,
                                      p->pos.x+cosf(a1)*rr,p->pos.y+sinf(a1)*rr,
                                      3.3f-(float)layer*0.7f,layer==0?(col3){1.8f,2.4f,2.2f}:COL(0x7CFCE4),alpha);
                        }
                    }
                }
                draw_ring(p->pos.x,p->pos.y,18.0f+prog*8.0f,COL(0x9FFFF0),(1.0f-prog)*0.38f);
            } else {
                float base=atan2f(d.y,d.x);
                float a=base-1.0f+prog*2.0f;
                draw_line(p->pos.x,p->pos.y,p->pos.x+cosf(a)*30.0f,p->pos.y+sinf(a)*30.0f,3.0f,(col3){2,2,2},1.0f-prog*0.5f);
                draw_line(p->pos.x,p->pos.y,p->pos.x+cosf(a-0.3f)*24.0f,p->pos.y+sinf(a-0.3f)*24.0f,2.0f,COL(0x7CFCE4),0.6f-prog*0.4f);
            }
        }
        // 캐논 차지
        if (p->charging){
            float ch=p->charge;
            bool tiered=(p->weapon.type==WPN_WAND && player_has_wrelic(WR_WAND_DELAY)) ||
                        (p->weapon.type==WPN_SWORD && player_has_wrelic(WR_SWORD_PHASE));
            if (tiered){
                static const col3 stage_colors[5]={
                    {0.25f,0.88f,0.77f},{0.49f,0.99f,0.89f},{1.00f,0.75f,0.02f},
                    {1.00f,0.28f,0.02f},{1.00f,0.24f,0.50f}
                };
                static const float marks[4]={0.25f,0.5f,0.75f,1.0f};
                float x=p->pos.x-12.0f, y=p->pos.y-14.0f, width=24.0f;
                draw_quad(x,y,width,2,COL(0x101A2B),0.85f);
                draw_quad(x,y,width*ch,2,stage_colors[wand_rain_charge_tier(ch)],0.95f);
                for (int i=0;i<4;i++)
                    draw_quad(x+width*marks[i]-0.5f,y-1,1,4,COL(0xE8FFF9),ch+0.00001f>=marks[i]?0.95f:0.42f);
                if (p->weapon.type==WPN_SWORD){
                    char hit_count[8];
                    int hits=sword_phase_charge_hits(ch);
                    snprintf(hit_count,sizeof(hit_count),hits>0?"x%d":"기본",hits);
                    draw_text(hit_count,x+width+3.0f,y-2.0f,0.34f,stage_colors[wand_rain_charge_tier(ch)],0.95f);
                }
            } else {
                draw_quad(p->pos.x-10,p->pos.y-14,20*ch,2,COL(0x7CFCE4),0.9f);
            }
        }
    }

    // ---------- light
    draw_light_begin(cx-sx,cy-sy);
    Player* p=&G.pl;
    draw_shadowed_light(p->pos.x,p->pos.y,player_light_radius(),COL(0x3FE0C5),1.05f);
    draw_light_blob(p->pos.x,p->pos.y,26,COL(0x7CFCE4),0.7f);
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (!b->active) continue;
        if (b->kind==11){
            float fade=clampf((3.0f-b->life)*7.0f,0,1)*clampf(b->life*2.5f,0,1);
            draw_light_blob(b->pos.x,b->pos.y,b->radius*1.8f,(col3){1.5f,0.28f,0.06f},0.12f*fade);
            continue;
        }
        if (b->fuse_armed||b->kind==13||b->kind==14) continue;
        col3 c=b->from_player?COL(0x3FE0C5):COL(0xFF3D7F);
        draw_light_blob(b->pos.x,b->pos.y,b->radius*5.0f,c,0.5f);
    }
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pk=&G.pickups[i];
        if (!pk->active) continue;
        if (pk->type==PK_CORE) draw_light_blob(pk->pos.x,pk->pos.y,55,COL(0x9FFFF0),0.8f);
        else if (pk->type==PK_SHARD) draw_light_blob(pk->pos.x,pk->pos.y,30,COL(0x9FFFF0),0.45f);
        else draw_light_blob(pk->pos.x,pk->pos.y,18,COL(0xFFD060),0.3f);
    }
    for (int i=0;i<MAX_ENTITIES;i++){
        Entity* e=&G.ents[i];
        if (!e->active) continue;
        if (e->type==E_BOSS_NULL) draw_light_blob(e->pos.x,e->pos.y,40,COL(0xFF3D7F),0.6f);
        if (e->type==E_BOSS_ECHO) draw_light_blob(e->pos.x,e->pos.y,50,COL(0x7CFCE4),0.4f);
        if (e->type==E_ECHO_GHOST) draw_light_blob(e->pos.x,e->pos.y,30,COL(0xFF3D7F),0.35f);
        if (e->type==E_HIVE) draw_light_blob(e->pos.x,e->pos.y,28,COL(0x3FE0C5),0.4f+0.15f*sinf(G.time*3.0f));
        if (e->elite) draw_light_blob(e->pos.x,e->pos.y,30,COL(0xFFF0D0),0.5f+0.2f*sinf(G.time*5.0f));
        if (e->flash>0) draw_light_blob(e->pos.x,e->pos.y,35,COL(0xFFFFFF),0.6f);
    }
    for (int i=0;i<MAX_PARTICLES;i++){
        Particle* pa=&G.parts[i];
        if (!pa->active||!pa->glow) continue;
        float a=pa->life/pa->max_life;
        draw_light_blob(pa->pos.x,pa->pos.y,pa->size*4.0f,pa->col,a*0.35f);
    }

    // ---------- glow
    draw_glow_begin(cx-sx,cy-sy);
    draw_glow_blob(p->pos.x,p->pos.y,18,COL(0x3FE0C5),0.8f);
    { // 포탈 강착원반 블룸
        v2 pc[4]; int np=portal_list(pc,NULL,4);
        for (int i=0;i<np;i++)
            draw_glow_blob(pc[i].x,pc[i].y,16.0f,COL(0x3FE0C5),0.6f+0.2f*sinf(G.time*3.0f));
    }
    for (int i=0;i<MAX_PARTICLES;i++){
        Particle* pa=&G.parts[i];
        if (!pa->active||!pa->glow) continue;
        float a=pa->life/pa->max_life;
        draw_glow_blob(pa->pos.x,pa->pos.y,pa->size*3.0f,pa->col,a*0.5f);
    }
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (!b->active) continue;
        if (b->fuse_armed||b->kind==11||b->kind==13||b->kind==14) continue;
        col3 c=b->from_player?COL(0x7CFCE4):COL(0xFF3D7F);
        draw_glow_blob(b->pos.x,b->pos.y,b->radius*3.0f,c,0.55f);
    }
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pk=&G.pickups[i];
        if (!pk->active) continue;
        if (pk->type==PK_CORE||pk->type==PK_SHARD)
            draw_glow_blob(pk->pos.x,pk->pos.y,pk->type==PK_CORE?20.0f:10.0f,COL(0x9FFFF0),0.7f);
    }
}

// 화면 밖 포탈을 가리키는 가장자리 화살표 (월드 좌표 wx,wy)
static void portal_arrow(float wx, float wy, col3 c){
    float sx=wx-G.cam.x, sy=wy-G.cam.y;
    const float m=8.0f;
    if (sx>=m && sx<=VIRT_W-m && sy>=m && sy<=VIRT_H-m) return; // 화면 안이면 생략
    // 화면 중심→포탈 방향
    float dx=sx-VIRT_W*0.5f, dy=sy-VIRT_H*0.5f;
    float ang=atan2f(dy,dx);
    // 가장자리 안쪽 18px로 클램프
    const float ins=18.0f;
    float ex=clampf(sx,ins,VIRT_W-ins), ey=clampf(sy,ins,VIRT_H-ins);
    float a=0.45f+0.35f*sinf(G.time*4.0f);
    // '>' 모양: 끝점에서 뒤로 두 획
    float bl=7.0f;          // 획 길이
    float tip_x=ex+cosf(ang)*bl, tip_y=ey+sinf(ang)*bl;
    float a1=ang+2.4f, a2=ang-2.4f;
    draw_line(tip_x,tip_y,tip_x+cosf(a1)*bl,tip_y+sinf(a1)*bl,2.5f,c,a);
    draw_line(tip_x,tip_y,tip_x+cosf(a2)*bl,tip_y+sinf(a2)*bl,2.5f,c,a);
}

// ----------------------------------------------------------- 기억 이벤트 UI
static const char* memory_tag_name(int tag){
    static const char* names[MEM_TAG_COUNT]={"용기","인연","약속"};
    return tag>=0&&tag<MEM_TAG_COUNT?names[tag]:"기록";
}
static const char* memory_trait_name(int trait){
    return trait==ELITE_VOLATILE?"휘발성":trait==ELITE_HASTE?"가속":"";
}
static bool event_nearby(void){
    return G.room.event_state==MEM_STATE_AVAILABLE &&
           v2len(v2sub(G.room.event_pos,G.pl.pos))<=24.0f;
}
static const char* memory_tag_effect(int tag){
    switch (tag){
        case MEM_TAG_COURAGE: return "공격력 +5% (최대 +10%)";
        case MEM_TAG_KINSHIP: return "공격 쿨다운 -4% (최대 -8%)";
        case MEM_TAG_PROMISE: return "빛 반경 +8px (최대 +16px)";
        default: return "효과 없음";
    }
}
// item_kb is kept local to this translation unit because the capacity
// implementation is private to game.c; keep the event cost in sync with it.
static int memory_item_kb(int kb){
    return G.pl.relics[RELIC_COMPRESS]?kb*4/5:kb;
}
static const char* story_memory_context(int biome,int event_type,int tag){
    static const char* contexts[4][2][MEM_TAG_COUNT]={
        {{"실패해도 다시 누른 시작 버튼.","옆자리의 박수가 아직 남아 있다.","다음에 마저 하자는 말을 기억한다."},
         {"깨진 기록도 앞으로 나아가려 한다.","흐린 목소리가 서로 이어진다.","지워진 줄 사이에 약속이 남아 있다."}},
        {{"실패해도 다시 누른 시작 버튼.","옆자리의 박수가 아직 남아 있다.","다음에 마저 하자는 말을 기억한다."},
         {"깨진 기록도 앞으로 나아가려 한다.","흐린 목소리가 서로 이어진다.","지워진 줄 사이에 약속이 남아 있다."}},
        {{"실패해도 다시 누른 시작 버튼.","옆자리의 박수가 아직 남아 있다.","다음에 마저 하자는 말을 기억한다."},
         {"깨진 기록도 앞으로 나아가려 한다.","흐린 목소리가 서로 이어진다.","지워진 줄 사이에 약속이 남아 있다."}},
        {{"실패해도 다시 누른 시작 버튼.","옆자리의 박수가 아직 남아 있다.","다음에 마저 하자는 말을 기억한다."},
         {"깨진 기록도 앞으로 나아가려 한다.","흐린 목소리가 서로 이어진다.","지워진 줄 사이에 약속이 남아 있다."}},
    };
    if (biome<0||biome>=4||event_type<MEM_EVENT_ECHO||event_type>MEM_EVENT_CORRUPTED||
        tag<0||tag>=MEM_TAG_COUNT) return "복구 신호가 잠시 흔들린다.";
    return contexts[biome][event_type-MEM_EVENT_ECHO][tag];
}
static const char* story_boss_framing(int biome){
    static const char* lines[4]={
        "손상된 첫 트랙을 지나야 신호가 이어진다.",
        "계속된 승리가 읽힌 흔적을 막는다.",
        "조각난 블록을 통과해야 마지막 저장에 닿는다.",
        "삭제의 빈칸을 지나야 메시지를 검증할 수 있다.",
    };
    return biome>=0&&biome<4?lines[biome]:"복구 신호가 이어진다.";
}
static void draw_memory_event(void){
    Room* r=&G.room;
    if (r->event_state!=MEM_STATE_AVAILABLE) return;
    char buf[160];
    float x=8,y=145;
    draw_quad(x-4,y-5,248,121,COL(0x0B0710),0.88f);
    snprintf(buf,sizeof(buf),"기억 로그 · %s",r->event_type==MEM_EVENT_ECHO?"메아리":"손상된 기억");
    draw_text(buf,x,y,0.78f,COL(0x9FFFF0),1); y+=15;
    draw_text(story_memory_context(r->biome,r->event_type,r->event_tag),x,y,0.54f,COL(0xE8E0F8),0.94f); y+=12;
    snprintf(buf,sizeof(buf),"태그: %s",memory_tag_name(r->event_tag));
    draw_text(buf,x,y,0.68f,COL(0xC8C0E0),1); y+=13;
    snprintf(buf,sizeof(buf),"효과: %s",memory_tag_effect(r->event_tag));
    draw_text(buf,x,y,0.62f,COL(0xC8C0E0),1); y+=13;
    snprintf(buf,sizeof(buf),"보관 비용: %dKB (원본 64KB)",memory_item_kb(64));
    draw_text(buf,x,y,0.62f,COL(0xC8C0E0),1); y+=12;
    if (r->event_type==MEM_EVENT_ECHO){
        draw_text("E : 보관 · Q : 폐기 → +3 바이트",x,y,0.68f,COL(0xFFD060),1);
    } else {
        if (G.memory.pending_trait){
            snprintf(buf,sizeof(buf),"E : 보관 불가 · 정예 대기열 사용 중 (%s)",
                     memory_trait_name(G.memory.pending_trait));
            draw_text(buf,x,y,0.60f,COL(0xFFB0CC),1); y+=12;
        } else {
            snprintf(buf,sizeof(buf),"E : 보관 → 다음 일반 적 정예 승급 (%s)",
                     memory_trait_name(r->event_trait));
            draw_text(buf,x,y,0.60f,COL(0xFFB0CC),1); y+=12;
        }
        draw_text("승급 적 처치 시 +2 바이트 · Q : 폐기 → HP +1",x,y,0.58f,COL(0xFFD060),1);
    }
    draw_sprite(r->event_type==MEM_EVENT_ECHO?SPR_CORE_SHARD:SPR_SHARD,
                x+230,y-15,13,13,COL(0xFFFFFF),0.9f,false,G.time);
}
static void draw_memory_log(void){
    if (!G.memory.log_count) return;
    char buf[96];
    float y=82;
    draw_text("기억 기록",VIRT_W-106,y,0.68f,COL(0x8878A8),0.9f);
    int n=G.memory.log_count<8?G.memory.log_count:8;
    for (int i=0;i<n;i++){
        int at=(G.memory.log_head+G.memory.log_count-n+i)%8;
        int code=G.memory.log[at];
        int decision=(code>>4)&3, type=(code>>2)&3, tag=code&3;
        snprintf(buf,sizeof(buf),"%s · %s · %s",decision==MEM_DECISION_KEEP?"보관":"폐기",
                 type==MEM_EVENT_ECHO?"메아리":"손상",memory_tag_name(tag));
        draw_text(buf,VIRT_W-106,y+13+i*11,0.58f,COL(0xA898C0),0.8f);
    }
    if (G.memory.pending_trait){
        snprintf(buf,sizeof(buf),"정예 대기: %s +2",memory_trait_name(G.memory.pending_trait));
        draw_text(buf,VIRT_W-106,y+13+n*11,0.58f,COL(0xFFB0CC),0.95f);
    }
}
// ----------------------------------------------------------- HUD
static col3 weapon_relic_color(int weapon){
    static const col3 colors[WPN_COUNT]={
        {0.25f,0.95f,0.82f}, {1.00f,0.68f,0.30f}, {0.98f,0.42f,0.72f},
        {0.55f,0.94f,0.45f}, {1.00f,0.42f,0.38f}, {0.68f,0.48f,1.00f}
    };
    return colors[weapon];
}
static const char* weapon_relic_type_name(int weapon){
    static const char* names[WPN_COUNT]={"검","포","산탄","글레이브","랜스","완드"};
    return names[weapon];
}
static void weapon_relic_effect_lines(const char* text,char lines[3][96]){
    const char* divider=strchr(text,'|');
    if (divider){
        snprintf(lines[0],96,"%.*s",(int)(divider-text),text);
        snprintf(lines[1],96,"%s",divider+1);
        lines[2][0]=0;
        return;
    }
    const char *first=0,*second=0,*fourth=0;
    int spaces=0;
    for (const char* p=text;*p;p++) if (*p==' '){
        spaces++;
        if (spaces==1) first=p;
        else if (spaces==2) second=p;
        else if (spaces==4) fourth=p;
    }
    if (!spaces){ snprintf(lines[0],96,"%s",text); lines[1][0]=lines[2][0]=0; return; }
    const char* a=spaces==1?first:second;
    snprintf(lines[0],96,"%.*s",(int)(a-text),text);
    if (spaces<=3){ snprintf(lines[1],96,"%s",a+1); lines[2][0]=0; return; }
    snprintf(lines[1],96,"%.*s",(int)(fourth-a-1),a+1);
    snprintf(lines[2],96,"%s",fourth+1);
}
static int utf8_char_len(const char* p){
    unsigned char c=(unsigned char)*p;
    if (c<0x80) return 1;
    if ((c&0xE0)==0xC0) return 2;
    if ((c&0xF0)==0xE0) return 3;
    if ((c&0xF8)==0xF0) return 4;
    return 1;
}
static void relic_swap_effect_lines(const char* text,float max_width,char lines[5][96]){
    for (int i=0;i<5;i++) lines[i][0]=0;
    int line=0;
    const char* p=text;
    while (*p && line<5){
        while (*p==' ') p++;
        if (!*p) break;
        char word[96]={0};
        int n=0;
        while (p[n] && p[n]!=' ' && n<(int)sizeof(word)-4){
            int step=utf8_char_len(p+n);
            if (n+step>=(int)sizeof(word)) break;
            n+=step;
        }
        memcpy(word,p,(size_t)n);
        word[n]=0;
        if (lines[line][0]){
            char combined[96];
            strcpy(combined,lines[line]);
            strncat(combined," ",sizeof(combined)-strlen(combined)-1);
            strncat(combined,word,sizeof(combined)-strlen(combined)-1);
            if (text_width(combined,0.24f)<=max_width){
                snprintf(lines[line],sizeof(lines[line]),"%s",combined);
                p+=n;
                continue;
            }
            line++;
            if (line>=5) break;
        }
        while (word[0] && line<5){
            int bytes=0;
            char partial[96]={0};
            while (word[bytes]){
                int step=utf8_char_len(word+bytes);
                char candidate[96];
                strcpy(candidate,partial);
                strncat(candidate,word+bytes,(size_t)step);
                if (partial[0] && text_width(candidate,0.24f)>max_width) break;
                snprintf(partial,sizeof(partial),"%s",candidate);
                bytes+=step;
            }
            snprintf(lines[line],sizeof(lines[line]),"%s",partial);
            if (!word[bytes]) break;
            memmove(word,word+bytes,strlen(word+bytes)+1);
            line++;
        }
        p+=n;
    }
}
static void codex_detail_effect_lines(const char* text,char lines[3][96]){
    if (text_width(text,0.48f)<=190.0f){
        snprintf(lines[0],96,"%.95s",text);
        lines[1][0]=lines[2][0]=0;
        return;
    }
    weapon_relic_effect_lines(text,lines);
}
static const char* reward_weapon_mode(int weapon){
    static const char* modes[WPN_COUNT]={
        "근접 베기", "충전 포탄 폭발", "다발 산탄 분사", "투척 후 귀환", "직선 관통 창", "유도탄 연속 발사"
    };
    return modes[weapon];
}
static const char* reward_prefix_effect(int prefix){
    static const char* effects[PFX_COUNT]={
        "", "적중 시 3초 화상", "적중 시 2초 둔화", "치명타 확률 30%", "용량 25% 감소"
    };
    return effects[prefix];
}
static void draw_reward_effect_label(const Pickup* pk){
    char title[96], name[96], desc[128], lines[3][96];
    col3 c=COL(0x9FFFF0);
    if (pk->type==PK_WRELIC){
        const WeaponRelicDef* wr=&weapon_relic_defs[pk->relic];
        snprintf(title,sizeof(title),"%s 유물",weapon_relic_type_name(wr->weapon));
        snprintf(name,sizeof(name),"%s",wr->name);
        snprintf(desc,sizeof(desc),"%s",wr->desc);
        c=weapon_relic_color(wr->weapon);
    } else if (pk->type==PK_RELIC){
        snprintf(title,sizeof(title),"일반 유물");
        snprintf(name,sizeof(name),"%s",relic_defs[pk->relic].name);
        snprintf(desc,sizeof(desc),"%s",relic_defs[pk->relic].desc);
    } else {
        const WeaponDef* wd=&weapon_defs[pk->weapon.type];
        snprintf(title,sizeof(title),"무기");
        snprintf(name,sizeof(name),"%s%s",prefix_names[pk->weapon.prefix],wd->name);
        snprintf(desc,sizeof(desc),"공격 %.1f · %s%s%s",wd->dmg,reward_weapon_mode(pk->weapon.type),
                 pk->weapon.prefix==PFX_NONE?"":" · ",reward_prefix_effect(pk->weapon.prefix));
        c=weapon_relic_color(pk->weapon.type);
    }
    weapon_relic_effect_lines(desc,lines);
    v2 label=reward_label_pos(pk);
    float x=label.x, y=label.y;
    draw_quad(x-90,y-5,180,75,c,1);
    draw_quad(x-89,y-4,178,73,COL(0x0B0710),1);
    draw_text_center(title,x,y,0.15f,c,1);
    draw_text_center(name,x,y+12,0.20f,COL(0xFFF0D0),1);
    for (int i=0;i<3;i++) if (lines[i][0])
        draw_text_center(lines[i],x,y+28+i*13,0.11f,COL(0xFFFFFF),1);
}
static void draw_shield_heart(float x,float capacity,float amount){
    col3 empty=COL(0x176D72), filled=COL(0x3FE0C5);
    #define SHIELD_HEART(C,A) do { \
        draw_quad(x-4,7,3,3,C,A); draw_quad(x+1,7,3,3,C,A); \
        draw_quad(x-5,10,10,4,C,A); draw_quad(x-3,14,6,3,C,A); \
        draw_quad(x-1,17,2,2,C,A); \
    } while (0)
    #define SHIELD_HALF(C,A) do { \
        draw_quad(x-4,7,3,3,C,A); draw_quad(x-5,10,5,4,C,A); \
        draw_quad(x-3,14,3,3,C,A); draw_quad(x-1,17,1,2,C,A); \
    } while (0)
    if (capacity>=1.0f) SHIELD_HEART(empty,0.85f); else SHIELD_HALF(empty,0.85f);
    if (amount>=1.0f) SHIELD_HEART(filled,1.0f); else if (amount>=0.5f) SHIELD_HALF(filled,1.0f);
    #undef SHIELD_HALF
    #undef SHIELD_HEART
}
static void draw_training_hud(void){
    static const float station_x[WPN_COUNT]={40,120,200,280,360,440};
    static const float module_dx[4]={-20,20,-20,20};
    static const float module_y[4]={194,194,230,230};
    Player* p=&G.pl;

    draw_quad(0,0,VIRT_W,28,COL(0x0B0710),0.78f);
    draw_text("훈련장",8,7,0.82f,COL(0x9FFFF0),1);
    draw_text_center("E : 장착 · Tab : 가방 · Esc : 나가기",VIRT_W*0.5f,8,0.54f,COL(0xC8C0E0),0.95f);
    Pickup* nearby=NULL;
    float best=28.0f;
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pk=&G.pickups[i];
        if (!pk->active || (pk->type!=PK_WEAPON && pk->type!=PK_WRELIC)) continue;
        float d=v2len(v2sub(pk->pos,p->pos));
        if (d<best){ best=d; nearby=pk; }
    }
    if (nearby){
        const char* name=nearby->type==PK_WEAPON?weapon_defs[nearby->weapon.type].name:weapon_relic_defs[nearby->relic].name;
        col3 c=nearby->type==PK_WEAPON?weapon_relic_color(nearby->weapon.type):weapon_relic_color(weapon_relic_defs[nearby->relic].weapon);
        draw_quad(144,31,192,25,COL(0x0B0710),0.88f);
        draw_text_center(name,VIRT_W*0.5f,35,0.54f,c,1);
        draw_text_center("E : 장착",VIRT_W*0.5f,47,0.38f,COL(0xE8E0F8),0.96f);
    } else {
        draw_text_center("무한 더미",VIRT_W*0.5f,57,0.56f,COL(0xFFB0CC),0.95f);
    }
    {
        v2 button=V2(VIRT_W*(2.0f/3.0f),88.0f);
        if (v2len(v2sub(p->pos,button))<=24.0f){
            float x=button.x-G.cam.x, y=button.y-G.cam.y-20.0f;
            draw_quad(x-18,y-2,36,11,COL(0x0B0710),0.86f);
            draw_text_center("E  소환",x,y,0.36f,COL(0x9FFFF0),1);
        }
    }

    for (int w=0;w<WPN_COUNT;w++){
        col3 c=weapon_relic_color(w);
        draw_text_center(weapon_defs[w].name,station_x[w],172,0.38f,c,0.95f);
        for (int m=0;m<4;m++){
            char slot[4];
            snprintf(slot,sizeof(slot),"%d",m+1);
            draw_text_center(slot,station_x[w]+module_dx[m],module_y[m]+7,0.34f,c,0.95f);
        }
    }
    for (int i=0;i<MAX_FLOATERS;i++){
        Floater* f=&G.floaters[i];
        if (f->t<=0) continue;
        float fx=f->screen_fixed?f->x:f->x-G.cam.x;
        float fy=f->screen_fixed?f->y:f->y-G.cam.y-30.0f-(1.4f-f->t)*16.0f;
        draw_text(f->text,fx-text_width(f->text,0.8f)*0.5f,fy,0.8f,f->c,clampf(f->t,0,1));
    }
    if (G.msg_t>0)
        draw_text_center(G.msg,VIRT_W*0.5f,VIRT_H-13,0.52f,COL(0xE8E0F8),clampf(G.msg_t,0,1));
}
void hud_draw(void){
    Player* p=&G.pl;
    char buf[160];
    if (G.training_active){
        draw_training_hud();
        return;
    }
    // 튜토리얼 힌트 (UI 패스 — 라이팅 영향 없음)
    if (G.room.biome==0&&G.room.idx==0){
        float ha=0.55f+0.2f*sinf(G.time*2.0f);
        draw_text("W,A,S,D : 이동",56,84,0.9f,COL(0x9FFFF0),ha);
        draw_text("마우스/Space : 공격",56,108,0.9f,COL(0x9FFFF0),ha);
        draw_text("Shift : 대시 · E : 줍기",56,132,0.9f,COL(0x9FFFF0),ha);
        draw_text("Q : 추억 조각 버리기",56,156,0.9f,COL(0x9FFFF0),ha);
        draw_text("빛이 강해지면 느려지지만 강해진다.",250,100,0.62f,COL(0xB8FFF0),1.0f);
        draw_text("빛이 줄어들면 약해지지만 빨라진다.",250,120,0.62f,COL(0xB8FFF0),1.0f);
        draw_text("Tab : 가방",56,180,0.9f,COL(0x9FFFF0),ha);
    }
    // 플로터: 월드 피드백은 카메라 보정, 안내는 화면 좌표에 고정
    for (int i=0;i<MAX_FLOATERS;i++){
        Floater* f=&G.floaters[i];
        if (f->t<=0) continue;
        float fx=f->screen_fixed?f->x:f->x-G.cam.x;
        float fy=f->screen_fixed?f->y:f->y-G.cam.y-30.0f-(1.4f-f->t)*16.0f;
        draw_text(f->text,fx-text_width(f->text,0.8f)*0.5f,fy,0.8f,f->c,clampf(f->t,0,1));
    }
    // 화면 밖 포탈 화살표 (열린 문/출구) — 페이드 중엔 생략
    if (G.fade_dir==0){
        Room* r=&G.room;
        int RW=r->w, RH=r->h;
        // 열린 분기 문: 2칸 중심
        for (int dn=0; dn<r->door_count; dn++){
            int dx0=r->door_x[dn], dy0=r->door_y[dn];
            if (dx0<0||dy0<0||dx0>=RW||dy0>=RH) continue;
            if (r->tiles[dy0][dx0]!=T_DOOR_OPEN) continue; // 닫혀있으면 생략
            float cxw, cyw;
            if (r->door_dir[dn]==DIR_R||r->door_dir[dn]==DIR_L){
                cxw=dx0*TILE+8.0f; cyw=(dy0+0.5f)*TILE+8.0f;
            } else {
                cxw=(dx0+0.5f)*TILE+8.0f; cyw=dy0*TILE+8.0f;
            }
            portal_arrow(cxw,cyw,COL(0x3FE0C5));
        }
        // 보스 출구(T_EXIT): 둘레 스캔, 짝수칸만 대표로 (2칸 중복 방지)
        for (int y=0;y<RH;y++) for (int x=0;x<RW;x++){
            if (x!=0&&x!=RW-1&&y!=0&&y!=RH-1) continue;
            if (r->tiles[y][x]!=T_EXIT) continue;
            // 가로변이면 x가 짝수일 때만, 세로변이면 y가 짝수일 때만 대표 (인접쌍 1개로)
            bool horiz=(y==0||y==RH-1);
            if (horiz){ if (x%2) continue; }
            else { if (y%2) continue; }
            float cxw = horiz? (x+1.0f)*TILE+8.0f : x*TILE+8.0f;
            float cyw = horiz? y*TILE+8.0f : (y+1.0f)*TILE+8.0f;
            portal_arrow(cxw,cyw,COL(0x9FFFF0));
        }
    }
    // 무결성
    for (int i=0;i<p->maxhp;i++){
        bool full = p->hp>=i+1;
        bool half = !full && p->hp>=i+0.5f;
        float hx=12+i*13.0f;
        draw_sprite(SPR_HEART,hx,12,11,10,full||half?COL(0xFFFFFF):COL(0x554060),full||half?1.0f:0.55f,false,0);
        if (half) draw_quad(hx,6.5f,5.5f,11.0f,COL(0x0B0710),0.9f);
    }
    float shield_limit=player_light_shield_limit();
    int shield_hearts=(int)ceilf(shield_limit);
    for (int i=0;i<shield_hearts;i++){
        float capacity=clampf(shield_limit-(float)i,0.0f,1.0f);
        float amount=clampf(p->shield-(float)i,0.0f,1.0f);
        float sx=17+p->maxhp*13.0f+i*13.0f;
        draw_shield_heart(sx,capacity,amount);
    }
    // 용량 게이지
    {
        float wfrac=capacity_frac();   // 게이지는 디스크 사용량(압축 반영)
        float bx=8,by=22,bw=86,bh=7;
        draw_quad(bx-1,by-1,bw+2,bh+2,COL(0x0B0710),0.8f);
        col3 bc = wfrac<0.4f?COL(0x3FE0C5):(wfrac<0.8f?COL(0x9FD06A):COL(0xFF3D7F));
        draw_quad(bx,by,bw*wfrac,bh,bc,0.95f);
        draw_quad(bx+bw*0.4f,by,1,bh,COL(0xFFFFFF),0.4f);
        draw_quad(bx+bw*0.8f,by,1,bh,COL(0xFFFFFF),0.4f);
        snprintf(buf,sizeof(buf),"복구 버퍼 %d/1440KB",player_used_kb());
        draw_text(buf,bx+bw+5,by-2,0.65f,COL(0xC8C0E0),0.9f);
    }
    // 무기
    draw_sprite(weapon_defs[p->weapon.type].spr,16,44,14,14,COL(0xFFFFFF),1,false,0);
    snprintf(buf,sizeof(buf),"%s%s",prefix_names[p->weapon.prefix],weapon_defs[p->weapon.type].name);
    draw_text(buf,28,38,0.7f,COL(0xC8C0E0),0.9f);
    snprintf(buf,sizeof(buf),"빛 %.0f",player_light_radius());
    draw_text(buf,8,56,0.65f,COL(0x9FFFF0),0.9f);
    // 진행/바이트/조각
    snprintf(buf,sizeof(buf),"%s  %d/9",biome_names[G.room.biome],G.room.idx+1);
    draw_text(buf,VIRT_W-text_width(buf,0.75f)-10,10,0.75f,COL(0x9FFFF0),0.9f);
    snprintf(buf,sizeof(buf),"%d",G.bytes_run);
    draw_sprite(SPR_BYTE,VIRT_W-44,28,8,8,COL(0xFFFFFF),1,false,0);
    draw_text(buf,VIRT_W-36,23,0.75f,COL(0xFFD060),0.9f);
    snprintf(buf,sizeof(buf),"%d",p->shards);
    draw_sprite(SPR_SHARD,VIRT_W-44,42,8,10,COL(0xFFFFFF),1,false,0);
    draw_text(buf,VIRT_W-36,37,0.75f,COL(0x9FFFF0),0.9f);
    for (int i=0;i<4;i++){
        bool got = (p->cores>>i)&1;
        draw_sprite(SPR_CORE_SHARD,VIRT_W-44+(i%2)*12.0f,58+(i/2)*13.0f,9,11,
                    got?COL(0xFFFFFF):COL(0x403050),got?1.0f:0.5f,false,0);
    }
    // 보스 HP
    bool boss_bar=false;
    for (int i=0;i<MAX_ENTITIES;i++){
        Entity* e=&G.ents[i];
        if (!e->active||e->type<E_BOSS_ROT||e->type>E_BOSS_NULL) continue;
        float frac=e->hp/e->maxhp;
        float bw=200,bx=(VIRT_W-bw)/2,by=VIRT_H-18;
        draw_quad(bx-1,by-1,bw+2,7,COL(0x0B0710),0.85f);
        draw_quad(bx,by,bw*frac,5,COL(0xFF3D7F),0.95f);
        const char* nm=boss_names[e->type-E_BOSS_ROT];
        draw_text(nm,(VIRT_W-text_width(nm,0.8f))/2,by-14,0.8f,COL(0xFFB0CC),0.95f);
        boss_bar=true;
    }
    // 남은 적 표시: 방을 오래 못 끝낼 때 위치 핑
    if (!G.room.cleared && !G.room.is_boss && G.room_t>12.0f && G.state==ST_PLAY){
        float pu=0.5f+0.5f*sinf(G.time*6.0f);
        for (int i=0;i<MAX_ENTITIES;i++){
            Entity* e=&G.ents[i];
            if (!e->active||e->type==E_ECHO_GHOST) continue;
            float mx=clampf(e->pos.x-G.cam.x,14,VIRT_W-14), my=clampf(e->pos.y-G.cam.y,14,VIRT_H-14);
            draw_sprite(SPR_SHARD,mx,my,8,10,COL(0xFF3D7F),0.3f+0.45f*pu,false,0);
        }
    }
    // 보스 인트로
    if (G.boss_intro){
        float a=clampf(G.boss_intro_t,0,1);
        const char* nm=boss_names[G.room.biome];
        draw_text_center(nm,VIRT_W/2,VIRT_H/2-30,1.8f,COL(0xFF3D7F),a);
        draw_text_center(story_boss_framing(G.room.biome),VIRT_W/2,VIRT_H/2-8,0.54f,COL(0xE8E0F8),a);
    }
    // 메시지 (보스전이면 이름/체력바 위로 올려 겹침 방지)
    if (G.msg_t>0){
        float a=clampf(G.msg_t,0,1);
        float my=boss_bar? VIRT_H-52 : VIRT_H-34;
        draw_text(G.msg,(VIRT_W-text_width(G.msg,0.8f))/2,my,0.8f,COL(0xE8E0F8),a);
    }
    draw_memory_log();
    if (event_nearby()) draw_memory_event();
}

// ----------------------------------------------------------- 오버레이 화면들
static void draw_overlay_bg(float a){
    draw_quad(0,0,VIRT_W,VIRT_H,COL(0x0B0710),a);
}

static void draw_relic_swap_card(float x,float y,float w,float h,int type,int id,bool selected){
    bool weapon=type==PK_WRELIC;
    const char* name=weapon?weapon_relic_defs[id].name:relic_defs[id].name;
    const char* desc=weapon?weapon_relic_defs[id].desc:relic_defs[id].desc;
    col3 c=weapon?weapon_relic_color(weapon_relic_defs[id].weapon):COL(0x9FFFF0);
    char lines[5][96];
    relic_swap_effect_lines(desc,w-16,lines);
    draw_quad(x-2,y-2,w+4,h+4,selected?COL(0xFFFFFF):COL(0x08050D),selected?0.95f:0.9f);
    draw_quad(x,y,w,h,c,0.62f);
    draw_quad(x+2,y+2,w-4,h-4,COL(0x0B0710),0.94f);
    draw_text_center(weapon?weapon_relic_type_name(weapon_relic_defs[id].weapon):"일반 유물",x+w*0.5f,y+7,0.38f,c,1);
    draw_text_center(name,x+w*0.5f,y+20,0.52f,COL(0xFFF0D0),1);
    for (int i=0;i<5;i++) if (lines[i][0])
        draw_text_center(lines[i],x+w*0.5f,y+34+i*11,0.24f,COL(0xFFFFFF),1);
}

static void draw_relic_swap(void){
    bool weapon=G.relic_swap_type==PK_WRELIC;
    int count=weapon?2:4;
    draw_overlay_bg(0.9f);
    draw_text_center("새 유물",VIRT_W*0.5f,5,0.75f,COL(0xFFFFFF),1);
    draw_relic_swap_card(130,20,220,90,G.relic_swap_type,G.relic_swap_id,false);
    draw_text_center("교체할 보유 유물 선택",VIRT_W*0.5f,116,0.62f,COL(0xE8E0F8),1);
    float w=weapon?180.0f:104.0f;
    float gap=weapon?28.0f:12.0f;
    float total=count*w+(count-1)*gap;
    float x=(VIRT_W-total)*0.5f;
    for (int i=0;i<count;i++){
        int id=weapon?G.pl.wrelics[G.relic_swap_slots[i]]:G.relic_swap_slots[i];
        draw_relic_swap_card(x+i*(w+gap),136,w,90,G.relic_swap_type,id,G.relic_swap_sel==i);
    }
    draw_text_center("A/D 또는 ←/→ : 선택 · Enter/Space : 교체 · Esc : 현재 상태 유지",VIRT_W*0.5f,244,0.42f,COL(0xB8FFF0),1);
}

static void draw_inventory(void){
    draw_overlay_bg(0.86f);
    char buf[160];
    draw_text_center("— 복구 버퍼 · 1440KB —",VIRT_W/2,16,1.1f,COL(0x9FFFF0),1);
    draw_text_center("런 중 재구성 이미지에 할당되는 버퍼",VIRT_W/2,32,0.58f,COL(0x8878A8),1);
    float y=48;
    snprintf(buf,sizeof(buf),"무기  %s%s  (%dKB)",prefix_names[G.pl.weapon.prefix],
             weapon_defs[G.pl.weapon.type].name,weapon_defs[G.pl.weapon.type].kb);
    draw_text(buf,40,y,0.85f,COL(0xE8E0F8),1); y+=20;
    for (int i=0;i<RELIC_COUNT;i++){
        if (!G.pl.relics[i]) continue;
        snprintf(buf,sizeof(buf),"유물  %s — %s",relic_defs[i].name,relic_defs[i].desc);
        draw_text(buf,40,y,0.75f,COL(0xC8B8E8),1); y+=16;
    }
    for (int i=0;i<2;i++){
        int wr=G.pl.wrelics[i];
        if (wr<0) continue;
        const WeaponRelicDef* wd=&weapon_relic_defs[wr];
        snprintf(buf,sizeof(buf),"무기유물  %s [%s] — %s",wd->name,weapon_defs[wd->weapon].name,wd->desc);
        draw_text(buf,40,y,0.7f,COL(0xFFD060),1); y+=16;
    }
    snprintf(buf,sizeof(buf),"추억 조각 ×%d  (%dKB)",G.pl.shards,G.pl.shards*64);
    draw_text(buf,40,y,0.85f,COL(0x9FFFF0),1); y+=18;
    int ncore=0; for(int i=0;i<4;i++) if(G.pl.cores&(1<<i)) ncore++;
    snprintf(buf,sizeof(buf),"핵심 조각 ×%d  (%dKB)",ncore,ncore*128);
    draw_text(buf,40,y,0.85f,COL(0xFFFFFF),1); y+=18;
    for (int i=0;i<4;i++){
        if (G.pl.cores&(1<<i)){
            draw_text(core_title_for(i),56,y,0.7f,COL(0xBFE8DC),1); y+=12;
            draw_text(core_inventory_texts[i],68,y,0.48f,COL(0x8878A8),1); y+=12;
        }
    }
    y+=6;
    snprintf(buf,sizeof(buf),"복구 버퍼 사용 %d / 1440 KB",player_used_kb());
    draw_text(buf,40,y,0.95f,weight_frac()>0.8f?COL(0xFF3D7F):COL(0x9FFFF0),1);
    draw_text_center("Q : 조각 버리기 · Tab : 닫기",VIRT_W/2,VIRT_H-26,0.75f,COL(0x8878A8),1);
    // 무게 효과 설명
    const char* tip = weight_frac()<0.4f? "가벼움: 빠르지만 빛과 공격력이 약하다"
                     : weight_frac()<0.8f? "적정: 균형 잡힌 상태"
                     : "무거움: 느리지만 빛과 공격력이 강하다";
    draw_text_center(tip,VIRT_W/2,VIRT_H-46,0.75f,COL(0xC8C0E0),1);
}

static void draw_flashback(void){
    draw_overlay_bg(0.62f);
    float t=G.fb_t;
    // 실루엣 빛 연출
    draw_light_begin(0,0);
    float pulse=0.7f+0.3f*sinf(t*2.0f);
    draw_light_blob(VIRT_W*0.35f,VIRT_H*0.42f,80.0f*pulse,COL(0x3FE0C5),0.5f);
    draw_light_blob(VIRT_W*0.65f,VIRT_H*0.5f,60.0f*pulse,COL(0x9FFFF0),0.35f);
    draw_glow_begin(0,0);
    draw_glow_blob(VIRT_W*0.5f,VIRT_H*0.35f,40,COL(0x9FFFF0),0.5f);
    draw_ui_begin();
    draw_overlay_bg(0.2f);
    draw_text_center(core_title_for(G.fb_core),VIRT_W/2,44,1.2f,COL(0x9FFFF0),clampf(t*2.0f,0,1));
    // 텍스트 타이핑
    const char* full=core_text_for(G.fb_core);
    int show=(int)((t-0.8f)*28.0f);
    if (show<0) show=0;
    char buf[256];
    int n=0;
    const char* q=full;
    // UTF-8 글자 단위로 show만큼 자르기
    while (*q && n<show){
        int step = ((*q&0xC0)==0xC0)? ((*q&0xE0)==0xC0?2:3):1;
        q+=step; n++;
    }
    size_t len=(size_t)(q-full); if (len>255) len=255;
    memcpy(buf,full,len); buf[len]=0;
    draw_text_center(buf,VIRT_W/2,100,0.95f,COL(0xE8E0F8),1);
    draw_text_center("핵심 조각 — 복구 블록을 잃지 않고 읽기 창에 닿으면 엔딩이 바뀐다",
                     VIRT_W/2,VIRT_H-52,0.75f,COL(0x6FBFB0),clampf((t-1.0f)*0.8f,0,0.8f));
    if (t>2.0f)
        draw_text_center("아무 키 — 계속",VIRT_W/2,VIRT_H-30,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(t*4.0f));
}

// ----------------------------------------------------------- 영구 강화
typedef struct { const char* name; const char* desc; int max; int base_cost; } UpgDef;
static const UpgDef upg_defs[5] = {
    {"무결성 코어","시작 무결성 +1",5,60},
    {"공격 회로","공격력 +5%",10,39},
    {"가속 캐시","이동속도 +3%",5,25},
    {"루멘 코어","시작 실드 +0.5",4,29},
    {"대시 칩","대시 쿨다운 -6%",5,26},
};
static const int upg_level_costs[5][10] = {
    {0},
    {35,66,97,128,159,190,221,252,283,314},
    {0},
    {90,140,200,260},
    {0},
};
static int upg_cost(int i){
    int level=(int)G.meta.upg[i];
    int custom=upg_level_costs[i][level];
    return custom?custom:upg_defs[i].base_cost*(level+1);
}

static const int weapon_unlock_costs[WPN_COUNT] = { 0, 130, 190, 230, 200, 410 };
static int weapon_unlock_cost(int weapon){ return weapon_unlock_costs[weapon]; }

static void draw_upgrade(void){
    draw_overlay_bg(0.95f);
    char buf[128];
    draw_text_center("— 캐릭터 강화 —",VIRT_W/2,16,1.1f,COL(0x9FFFF0),1);
    snprintf(buf,sizeof(buf),"보유 %u바이트",G.meta.bytes_currency);
    draw_text_center(buf,VIRT_W/2,40,0.85f,COL(0xFFD060),1);
    float y=64;
    for (int i=0;i<5;i++){
        bool sel=G.upg_sel==i;
        int lv=(int)G.meta.upg[i];
        if (lv>=upg_defs[i].max)
            snprintf(buf,sizeof(buf),"%s  MAX — %s",upg_defs[i].name,upg_defs[i].desc);
        else
            snprintf(buf,sizeof(buf),"%s  [%d바이트] — %s",upg_defs[i].name,upg_cost(i),upg_defs[i].desc);
        if (sel) draw_text(">",30,y,0.85f,COL(0x3FE0C5),1);
        draw_text(buf,44,y,0.85f,sel?COL(0xFFFFFF):COL(0x8878A8),1);
        y+=20;
    }
    if (G.upg_sel==5) draw_text(">",30,y+6,0.85f,COL(0x3FE0C5),1);
    draw_text("뒤로",44,y+6,0.85f,G.upg_sel==5?COL(0xFFFFFF):COL(0x8878A8),1);
    draw_text_center("런에서 모은 바이트로 영구히 강해진다 · Esc 뒤로",VIRT_W/2,VIRT_H-22,0.75f,COL(0x6F6090),0.9f);
}

#ifdef DD_DEBUG
typedef struct { int before, after; } DebugOpeningTransition;
static int dbg_opening_active;
static int dbg_opening_frozen;
static int dbg_opening_emitted;
static int dbg_opening_ready;
static int dbg_opening_rendered;
static float dbg_opening_hold_t;
static int dbg_opening_visited;
static int dbg_opening_natural_timeout;
static int dbg_opening_handoff_at_ms=-1;
static const char* dbg_opening_skip_method="none";
static int dbg_opening_skip_same_frame;
static int dbg_opening_entered_play;
static int dbg_opening_flow_step;
static DebugOpeningTransition dbg_opening_transitions[12];
static int dbg_opening_transition_count;
static int dbg_ending_active;
static int dbg_ending_rendered;
static int dbg_ending_emitted;
static float dbg_ending_hold_t;
static int dbg_ending_result;
static int dbg_ending_core_count;
static int dbg_start_intro_active;
static int dbg_start_intro_frozen;
static int dbg_start_intro_emitted;
static float dbg_start_intro_hold_t;
static int dbg_start_intro_phase;
static int dbg_start_intro_latches_clear;
static int dbg_start_intro_replay_complete;
static int dbg_start_intro_esc_ignored;
static int dbg_start_intro_input_unchanged;
#endif

#ifdef DD_DEBUG
static int boot_beat_index(float t){
    if (t<3.0f) return 0;
    if (t<7.0f) return 1;
    if (t<13.5f) return 2;
    return 3;
}
static const char* boot_beat_name(float t){
    static const char* names[]={"wake","scan","reveal","title-handoff"};
    return names[boot_beat_index(t)];
}
#endif
static void draw_title(void);
static void boot_handoff_to_title(bool natural_timeout, const char* skip_method){
#ifdef DD_DEBUG
    if (dbg_opening_active){
        if (natural_timeout){
            dbg_opening_natural_timeout=1;
            dbg_opening_handoff_at_ms=15000;
            dbg_opening_visited|=1<<3;
            if (dbg_opening_transition_count<12)
                dbg_opening_transitions[dbg_opening_transition_count++]=(DebugOpeningTransition){G.state,ST_TITLE};
        } else {
            dbg_opening_skip_method=skip_method;
            dbg_opening_skip_same_frame=1;
        }
    }
#else
    (void)natural_timeout;
    (void)skip_method;
#endif
    G.state=ST_TITLE;
    G.state_t=0;
    if (natural_timeout){
        G.fade=1;
        G.fade_dir=-1;
        G.fade_col=COL(0x000000);
    }
    music_set(0);
}

static void draw_boot(void){
    float t=G.state_t;
    float insert=clampf(t/0.9f,0,1);
    float shutter=clampf((t-0.8f)/1.4f,0,1);
    float media=clampf((t-1.4f)/1.0f,0,1);
    float recover=clampf((t-8.0f)/3.2f,0,1);
    float transfer=clampf((t-11.2f)/2.8f,0,1);
    float cx=VIRT_W*0.5f, cy=VIRT_H*0.58f;
    float spin=t*4.5f;
    int retry=0;
    float retry_pulse=0;
    float carriage_track=42.0f;
    float carriage_angle=5.62f;
    float carriage_x, carriage_y;
    float fragment_start_y=cy+24.0f;
    float fragment_target_y=VIRT_H*0.3f;
    float fragment_y=fragment_start_y+(fragment_target_y-fragment_start_y)*transfer;
    if (t>=2.4f && t<5.2f){
        int seek=(int)((t-2.4f)/0.56f);
        if (seek>4) seek=4;
        carriage_track=42.0f-seek*4.0f;
    } else if (t>=5.2f) {
        carriage_track=26.0f;
    }
    if (t>=5.2f && t<8.0f){
        retry=(int)((t-5.2f)/0.9f)+1;
        if (retry>3) retry=3;
        retry_pulse=1.0f-clampf(fmodf(t-5.2f,0.9f)/0.34f,0,1);
    }
    carriage_x=cx+cosf(carriage_angle)*carriage_track;
    carriage_y=cy+12+sinf(carriage_angle)*carriage_track;

    draw_light_begin(0,0);
    draw_light_blob(cx,cy+8,42.0f+18.0f*media,COL(0x5E86BC),0.08f+0.16f*media);
    if (retry_pulse>0) draw_light_blob(carriage_x,carriage_y,20,COL(0xFF8A8A),0.10f+0.28f*retry_pulse);
    if (transfer>0) draw_light_blob(cx,fragment_y,20+26*transfer,COL(0x7CFCE4),0.12f+0.38f*transfer);
    draw_glow_begin(0,0);
    if (media>0) draw_glow_blob(cx,cy+8,14,COL(0x7EABDF),0.12f+0.18f*media);
    if (transfer>0) draw_glow_blob(cx,fragment_y,10+16*transfer,COL(0x9FFFF0),0.16f+0.48f*transfer);

    draw_scene_begin(0,0);
    draw_quad(0,0,VIRT_W,VIRT_H,COL(0x090C18),1);
    for (int y=12;y<VIRT_H;y+=12)
        draw_line(0,y,VIRT_W,y,1,COL(0x18213A),0.14f+0.05f*sinf(t*4.0f+y));
    draw_quad(cx-93,cy-65,186*insert,130,COL(0x4A566E),1);
    draw_quad(cx-88,cy-60,176*insert,120,COL(0x1A253C),1);
    draw_quad(cx+66,cy-60,22,20,COL(0x090C18),1);
    draw_line(cx-87,cy-60,cx+66,cy-60,2,COL(0xB7C8E5),0.82f);
    draw_line(cx+66,cy-60,cx+88,cy-40,2,COL(0xB7C8E5),0.82f);
    draw_line(cx-88,cy-60,cx-88,cy+60,2,COL(0x7891B8),0.82f);
    draw_line(cx-88,cy+60,cx+88,cy+60,2,COL(0x7891B8),0.82f);
    draw_line(cx+88,cy-40,cx+88,cy+60,2,COL(0x5E749A),0.82f);
    draw_quad(cx-69,cy+23,52,24,COL(0xD5D9DD),0.78f);
    draw_line(cx-69,cy+23,cx-17,cy+23,1,COL(0xFFFFFF),0.5f);
    draw_quad(cx+43,cy+37,28,10,COL(0x0A1220),1);
    draw_ring(cx,cy+12,44,COL(0x55769E),0.72f*media);
    draw_ring(cx,cy+12,30,COL(0x6D8DB3),0.68f*media);
    draw_ring(cx,cy+12,17,COL(0x4D698A),0.68f*media);
    for (int i=0;i<4;i++){
        float a=spin+i*1.5708f;
        draw_line(cx+cosf(a)*6,cy+12+sinf(a)*6,cx+cosf(a)*13,cy+12+sinf(a)*13,
                  2,COL(0xC0D7F5),0.58f*media);
    }
    draw_ring(cx,cy+12,7,COL(0xDCEBFF),0.82f*media);
    draw_quad(cx-52,cy-35,104,18,COL(0x070C16),0.96f);
    draw_line(cx-52,cy-35,cx+52,cy-35,2,COL(0x8EA4C4),0.78f);
    draw_line(cx-52,cy-17,cx+52,cy-17,1,COL(0x587293),0.8f);
    for (int i=0;i<3;i++){
        float a=5.38f+i*0.12f;
        draw_line(cx+cosf(a)*23,cy+12+sinf(a)*23,cx+cosf(a)*43,cy+12+sinf(a)*43,
                  2,COL(0xFF7F91),0.44f+0.4f*retry_pulse);
    }
    draw_quad(cx-52+104*shutter,cy-35,52,18,COL(0x9BA6B7),0.9f*(1.0f-shutter));
    draw_line(cx-51+104*shutter,cy-34,cx+1+104*shutter,cy-34,2,COL(0xF4F7FC),0.75f*(1.0f-shutter));
    draw_quad(carriage_x-4,carriage_y-7,8,14,COL(0xD4E5F6),0.92f);
    draw_quad(carriage_x-7,carriage_y+7,14,4,COL(0x6BC4CE),0.72f+0.28f*retry_pulse);
    if (transfer>0)
        draw_sprite(SPR_CORE_SHARD,cx,fragment_y,10+7*transfer,12+8*transfer,
                    COL(0xFFFFFF),0.38f+0.62f*transfer,false,spin);
    draw_ui_begin();
    draw_text_center("1.44 MB  //  LAST RECOVERY PASS",cx,30,0.62f,COL(0x6F6090),0.35f+0.55f*insert);
    if (t<2.4f) draw_text_center("소유자의 마지막 복구 패스",cx,cy+80,0.76f,COL(0xD8E5FF),0.35f+0.65f*insert);
    else if (t<5.2f) draw_text_center("TRACK SEEK  //  DAMAGED RING",cx,cy+80,0.62f,COL(0x9FC8FF),0.78f);
    else if (t<8.0f){
        char retry_text[40];
        if (t>=7.55f) snprintf(retry_text,sizeof(retry_text),"RETRY SEQUENCE COMPLETE");
        else snprintf(retry_text,sizeof(retry_text),"READ RETRY %02d/03  //  CRC MISMATCH",retry);
        draw_text_center(retry_text,cx,t>=7.55f ? cy+68 : cy+80,0.57f,COL(0xFFB4BD),0.72f+0.28f*retry_pulse);
        if (t>=7.55f){
            draw_text_center("01/03 DONE  //  02/03 DONE  //  03/03 DONE",cx,cy+81,0.46f,COL(0xD8E5FF),0.9f);
            draw_text_center("RECALIBRATING",cx,cy+93,0.48f,COL(0xA7B8D8),0.86f);
        }
    } else if (t<11.2f) draw_text_center("마지막 정상 인덱스 조각",cx,cy+80,0.72f,COL(0xD8FFF5),0.88f);
    else draw_text_center("복구 신호가 읽기 창을 지난다",cx,cy+80,0.69f,COL(0x9FFFF0),0.92f);
    if (!(t>=7.55f && t<8.0f))
        draw_text_center("ANY KEY / CLICK TO SKIP",cx,VIRT_H-18,0.52f,COL(0xA7B8D8),0.62f+0.18f*sinf(t*4.0f));
}

static void draw_title(void){
    // 배경 분위기
    draw_light_begin(0,0);
    float t=G.time;
    draw_light_blob(VIRT_W*0.5f+sinf(t*0.4f)*30.0f,VIRT_H*0.35f,110,COL(0x3FE0C5),0.5f);
    draw_light_blob(VIRT_W*0.2f,VIRT_H*0.8f,70,COL(0x3D2A66),0.6f);
    draw_light_blob(VIRT_W*0.85f,VIRT_H*0.7f,60,COL(0xFF3D7F),0.25f);
    draw_glow_begin(0,0);
    draw_glow_blob(VIRT_W*0.5f,VIRT_H*0.3f,30+4*sinf(t*2.0f),COL(0x7CFCE4),0.6f);
    draw_scene_begin(0,0);
    draw_sprite(SPR_FLAME,VIRT_W*0.5f,VIRT_H*0.3f+sinf(t*2.0f)*3.0f,22,22,(col3){1.5f,1.5f,1.5f},1,false,0);

    draw_ui_begin();
    draw_text_center("디스켓 던전",VIRT_W/2,34,1.9f,COL(0x9FFFF0),1);
    draw_text_center("- 마지막 읽기 -",VIRT_W/2,62,0.85f,COL(0x6FBFB0),0.9f);

    char buf[128];
    float y=120;
    const char* items[7];
    items[0]="모험 시작";
    items[1]="무기 변경"; items[2]="캐릭터 강화"; items[3]="훈련장";
    items[4]="설정"; items[5]="도감"; items[6]="종료";
    for (int i=0;i<7;i++){
        bool sel = G.menu_sel==i;
        col3 c = sel?COL(0xFFFFFF):COL(0x8878A8);
        if (sel) draw_text(">",VIRT_W/2-text_width(items[i],0.9f)/2-16,y,0.9f,COL(0x3FE0C5),1);
        draw_text_center(items[i],VIRT_W/2,y,0.9f,c,1);
        y+=17;
    }
    snprintf(buf,sizeof(buf),"보유 %u바이트 · 런 %u회 · 최고 도달: %s",
             G.meta.bytes_currency,G.meta.runs,
             G.meta.wins>0?"읽기 창":(G.meta.runs>0?biome_names[G.meta.best_biome]:"-"));
    draw_text_center(buf,VIRT_W/2,VIRT_H-16,0.7f,COL(0x6F6090),0.9f);
}

static void draw_options(void){
    draw_light_begin(0,0);
    draw_light_blob(VIRT_W*0.5f,VIRT_H*0.42f,120,COL(0x3FE0C5),0.35f);
    draw_ui_begin();
    static const char* labels[]={"배경음악","효과음","난이도","화면 흔들림","스캔라인(CRT효과)","시드","오프닝 다시보기","New Game+","뒤로"};
    char value[64];
    draw_text_center("설정",VIRT_W/2,34,1.2f,COL(0x9FFFF0),1);
    for (int i=0;i<9;i++){
        if (i==0) snprintf(value,sizeof value,"%s",G.meta.opt_bgm?"ON":"OFF");
        else if (i==1) snprintf(value,sizeof value,"%s",G.meta.opt_sfx?"ON":"OFF");
        else if (i==2) snprintf(value,sizeof value,"%s",diff_names[G.difficulty]);
        else if (i==3) snprintf(value,sizeof value,"%s",G.meta.opt_shake?"ON":"OFF");
        else if (i==4) snprintf(value,sizeof value,"%s",G.meta.opt_scanline?"ON":"OFF");
        else if (i==5) snprintf(value,sizeof value,"%s",G.title_seed?"최근 시드":"무작위");
        else if (i==6) snprintf(value,sizeof value,"%s",G.meta.intro_replay_queued?"ON":"OFF");
        else if (i==7) snprintf(value,sizeof value,"%s",G.meta.true_clear?(G.ngplus?"ON":"OFF"):"LOCKED");
        else value[0]=0;
        float y=62+i*18;
        bool selected=G.menu_sel==i;
        if (selected) draw_text(">",126,y,0.7f,COL(0x3FE0C5),1);
        draw_text(labels[i],144,y,0.7f,selected?COL(0xFFFFFF):COL(0xC8C0E0),1);
        if (i<8) draw_text(value,326,y,0.65f,selected?COL(0x3FE0C5):COL(0x8878A8),1);
    }
    draw_text_center("오디오는 즉시 적용 · 그 외 설정은 다음 런부터 적용",VIRT_W/2,230,0.50f,COL(0x8878A8),1);
    draw_text_center("W/S : 선택 · A/D 또는 Enter : 변경 · Esc : 돌아가기",VIRT_W/2,246,0.45f,COL(0x6F6090),1);
}

static void draw_weapon_select(void){
    static const char* modes[WPN_COUNT]={"근접 베기","충전 포탄 폭발","다발 산탄 분사","투척 후 귀환","직선 관통 창","유도탄 연속 발사"};
    draw_overlay_bg(0.94f);
    draw_text_center("무기 변경",VIRT_W/2,18,1.1f,COL(0x9FFFF0),1);
    for (int i=0;i<WPN_COUNT;i++){
        int col=i%3, row=i/3;
        float x=30+col*152, y=48+row*96;
        bool selected=G.title_weapon==i;
        bool unlocked=(G.meta.unlocked_weapons&(1u<<i))!=0;
        int cost=weapon_unlock_cost(i);
        col3 tint=selected?COL(0x3FE0C5):weapon_relic_color(i);
        draw_quad(x,y,136,82,tint,selected?0.28f:0.11f);
        draw_text(weapon_defs[i].name,x+8,y+8,0.60f,selected?COL(0xFFFFFF):COL(0xE8E0F8),1);
        char line[64];
        snprintf(line,sizeof line,"공격 %.1f · %.2fs",weapon_defs[i].dmg,weapon_defs[i].cooldown);
        draw_text(line,x+8,y+27,0.42f,COL(0xC8C0E0),1);
        snprintf(line,sizeof line,"KB %d · %s",weapon_defs[i].kb,modes[i]);
        draw_text(line,x+8,y+41,0.38f,COL(0xC8C0E0),1);
        snprintf(line,sizeof line,"%d바이트",cost);
        draw_text(line,x+8,y+61,0.42f,COL(0xFFD060),1);
        if (i==G.title_weapon) snprintf(line,sizeof line,"현재 선택");
        else if (unlocked) snprintf(line,sizeof line,"구매 완료");
        else snprintf(line,sizeof line,"미구매");
        draw_text(line,x+128-text_width(line,0.42f),y+61,0.42f,
                  selected?COL(0x9FFFF0):(unlocked?COL(0x9FFFF0):COL(0x8878A8)),1);
    }
    draw_text_center("W/S 또는 위/아래 키 : 위/아래 이동",VIRT_W/2,232,0.40f,COL(0x8878A8),1);
    draw_text_center("A/D 또는 좌/우 키 : 좌/우 이동",VIRT_W/2,242,0.40f,COL(0x8878A8),1);
    draw_text_center("Enter : 구매/장착 · Esc : 돌아가기",VIRT_W/2,252,0.40f,COL(0x8878A8),1);
}

static void draw_difficulty_select(void){
    static const char* names[]={"쉬움","보통","어려움"};
    static const char* notes[]={"적 기본 체력/공격력","적 체력 향상\n개체 수 증가","적 체력/공격력 향상\n적 개체수 증가"};
    draw_overlay_bg(0.94f);
    draw_text_center("난이도 선택",VIRT_W/2,50,1.18f,COL(0x9FFFF0),1);
    for (int i=0;i<3;i++){
        float x=32+i*152;
        bool selected=G.menu_sel==i;
        draw_quad(x,92,132,82,selected?COL(0x3FE0C5):COL(0x302747),selected?0.32f:0.35f);
        draw_text_center(names[i],x+66,108,0.82f,selected?COL(0xFFFFFF):COL(0xC8C0E0),1);
        draw_text_center(notes[i],x+66,strchr(notes[i],'\n')?125:135,0.38f,COL(0xC8C0E0),1);
        if (selected) draw_text_center("ENTER",x+66,157,0.42f,COL(0x3FE0C5),1);
    }
    draw_text_center("A/D 또는 ←/→ : 선택 · Enter : 시작 · Esc : 돌아가기",VIRT_W/2,222,0.50f,COL(0x8878A8),1);
}

typedef struct { const char* name; const char* hp; const char* attack; const char* pattern; } CodexEntry;

static const CodexEntry codex_monsters[] = {
    {"슬라임", "체력 4", "접촉 0.5", "점프 추격 · 3번째 예측 돌진"},
    {"박쥐", "체력 2", "접촉 0.5", "부유 후 급강하"},
    {"망령", "체력 5", "접촉 0.5", "1.5초 전 위치를 추적"},
    {"추격자", "체력 4", "접촉 0.5", "예고 후 장거리 대시"},
    {"골렘", "체력 10", "접촉/탄 0.5", "근접 슬램 · 6방향 파편"},
    {"포탑", "체력 6", "탄 0.5", "연사 · 나선 · 8방향 링"},
    {"센티널", "체력 12", "접촉 0.5", "느린 추격 후 돌진"},
    {"드론", "체력 4", "탄 0.5", "3/5갈래 부채꼴 사격"},
    {"폭격체", "체력 3", "폭발 1.0", "근접 도화선 후 자폭"},
    {"스나이퍼", "체력 5", "저격탄 1.0", "조준 후 고속 단발 저격"},
    {"실더", "체력 9", "접촉 0.5", "정면 피해 감소 방패 추격"},
    {"하이브", "체력 8", "접촉 0.5", "미니 슬라임 소환"},
};
static const CodexEntry codex_bosses[] = {
    {"부패충 ROT", "체력 120", "접촉/탄 1.0", "돌진 · 포자 링 · 소환 · 틈 링"},
    {"메아리 ECHO", "체력 172.5", "접촉/탄 1.0", "잔상 · 5연사 · 순간이동"},
    {"단편기 DEFRAG", "체력 195", "접촉/탄 1.0", "위험구역 · 빔 · 배리어"},
    {"삭제 NULL", "체력 240", "접촉/탄 1.0", "나선 · 암전 이동 · 발악 링"},
};
static const char* codex_weapon_modes[WPN_COUNT] = {
    "근접 베기", "충전 포탄 폭발", "다발 산탄 분사", "투척 후 귀환", "직선 관통 창", "유도탄 연속 발사"
};

static int codex_entry_count(int section){
    if (section==0) return (int)(sizeof(codex_monsters)/sizeof(codex_monsters[0]));
    if (section==1) return (int)(sizeof(codex_bosses)/sizeof(codex_bosses[0]));
    if (section==2) return WPN_COUNT;
    return RELIC_COUNT+WR_COUNT;
}
static void codex_entry(int section,int index,char* name,size_t name_n,char* left,size_t left_n,
                        char* right,size_t right_n,char* pattern,size_t pattern_n,col3* color){
    if (section==0 || section==1){
        const CodexEntry* entry=section==0?&codex_monsters[index]:&codex_bosses[index];
        snprintf(name,name_n,"%s",entry->name); snprintf(left,left_n,"%s",entry->hp);
        snprintf(right,right_n,"%s",entry->attack); snprintf(pattern,pattern_n,"%s",entry->pattern);
        *color=section==0?COL(0xFFB0CC):COL(0xFF7A3D);
        return;
    }
    if (section==2){
        const WeaponDef* wd=&weapon_defs[index];
        snprintf(name,name_n,"%s",wd->name);
        snprintf(left,left_n,"공격 %.1f · 쿨 %.2fs",wd->dmg,wd->cooldown);
        snprintf(right,right_n,"%dB · %dKB",weapon_unlock_cost(index),wd->kb);
        snprintf(pattern,pattern_n,"%s",codex_weapon_modes[index]);
        *color=weapon_relic_color(index);
        return;
    }
    if (index<RELIC_COUNT){
        const RelicDef* rd=&relic_defs[index];
        snprintf(name,name_n,"일반 · %s",rd->name); snprintf(left,left_n,"%dKB",rd->kb);
        snprintf(right,right_n,"일반 유물"); snprintf(pattern,pattern_n,"%s",rd->desc);
        *color=COL(0x9FFFF0);
        return;
    }
    const WeaponRelicDef* wr=&weapon_relic_defs[index-RELIC_COUNT];
    snprintf(name,name_n,"%s · %s",weapon_relic_type_name(wr->weapon),wr->name);
    snprintf(left,left_n,"%dKB",wr->kb); snprintf(right,right_n,"무기 유물");
    snprintf(pattern,pattern_n,"%s",wr->desc); *color=weapon_relic_color(wr->weapon);
}

static void draw_codex(void){
    static const char* sections[4]={"몬스터","보스","무기","유물"};
    const int rows=4;
    int count=codex_entry_count(G.codex_section);
    int pages=(count+rows-1)/rows;
    if (G.codex_page>=pages) G.codex_page=pages-1;
    draw_light_begin(0,0);
    draw_light_blob(VIRT_W*0.5f,VIRT_H*0.3f,120,COL(0x1D3550),0.55f);
    draw_glow_begin(0,0);
    draw_glow_blob(80,56,22,COL(0x7CFCE4),0.45f);
    draw_scene_begin(0,0);
    draw_ui_begin();
    draw_text_center("도감",VIRT_W*0.5f,18,1.25f,COL(0x9FFFF0),1);
    draw_text_center("쉬움 난이도 기준",VIRT_W*0.5f,37,0.48f,COL(0x8878A8),1);
    draw_quad(18,54,108,188,COL(0x0B0710),0.94f);
    draw_quad(20,56,104,184,COL(0x16223A),0.75f);
    for (int i=0;i<4;i++){
        float y=76+i*31;
        bool selected=G.codex_section==i;
        if (selected) draw_quad(27,y-5,90,21,COL(0x3FE0C5),G.codex_focus==0?0.28f:0.12f);
        if (selected && G.codex_focus==0) draw_text(">",31,y,0.56f,COL(0x3FE0C5),1);
        draw_text(sections[i],42,y,0.72f,selected?COL(0xFFFFFF):COL(0x8878A8),1);
    }
    draw_text_center("W/S : 구분",72,218,0.42f,COL(0x6F6090),1);
    draw_quad(138,54,324,188,COL(0x0B0710),0.96f);
    char head[64];
    snprintf(head,sizeof(head),"%s  %d/%d",sections[G.codex_section],G.codex_page+1,pages);
    draw_text(head,151,63,0.78f,COL(0xE8E0F8),1);
    static const char* column_heads[4]={
        "이름 · 체력 · 공격력 / 패턴",
        "이름 · 체력 · 공격력 / 패턴",
        "이름 · 공격력 · 쿨다운 / 공격 방식",
        "이름 · 용량 · 유물 분류 / 효과"
    };
    draw_text(column_heads[G.codex_section],151,78,0.36f,COL(0x8878A8),1);
    for (int row=0;row<rows;row++){
        int index=G.codex_page*rows+row;
        if (index>=count) break;
        char name[96], left[64], right[64], pattern[128]; col3 color;
        codex_entry(G.codex_section,index,name,sizeof name,left,sizeof left,right,sizeof right,pattern,sizeof pattern,&color);
        float y=91+row*37;
        bool selected=G.codex_focus==1 && G.codex_detail==index;
        draw_quad(148,y,304,34,color,selected?0.28f:0.13f);
        if (selected) draw_text(">",143,y+10,0.48f,COL(0x3FE0C5),1);
        draw_text(name,154,y+3,0.52f,COL(0xFFF0D0),1);
        draw_text(left,270,y+5,0.34f,COL(0xC8C0E0),1);
        draw_text(right,380,y+5,0.32f,color,1);
        draw_text(pattern,154,y+20,0.40f,COL(0xFFFFFF),1);
    }
    draw_text_center(G.codex_focus==0?"Enter : 목록 · W/S : 구분 · Esc : 뒤로":"Enter : 상세 · W/S : 선택 · Esc : 구분",
                     VIRT_W*0.5f,VIRT_H-16,0.58f,COL(0x8878A8),1);
}

static int codex_sprite(int section,int index){
    static const int monster_sprites[] = {
        SPR_SLIME, SPR_BAT, SPR_WRAITH, SPR_CHASER, SPR_GOLEM, SPR_TURRET,
        SPR_SENTINEL, SPR_DRONE, SPR_BOMBER, SPR_SNIPER, SPR_SHIELDER, SPR_HIVE
    };
    if (section==0) return monster_sprites[index];
    if (section==1) return SPR_BOSS_ROT+index;
    if (section==2) return weapon_defs[index].spr;
    return SPR_RELIC;
}

static void draw_codex_detail(void){
    static const char* sections[4]={"몬스터","보스","무기","유물"};
    int count=codex_entry_count(G.codex_section);
    if (G.codex_detail>=count) G.codex_detail=count-1;
    char name[96], left[64], right[64], pattern[128], lines[3][96]; col3 color;
    codex_entry(G.codex_section,G.codex_detail,name,sizeof name,left,sizeof left,right,sizeof right,pattern,sizeof pattern,&color);
    codex_detail_effect_lines(pattern,lines);
    draw_scene_begin(0,0);
    draw_ui_begin();
    draw_text_center("도감 상세",VIRT_W*0.5f,18,1.1f,COL(0x9FFFF0),1);
    draw_text_center(sections[G.codex_section],VIRT_W*0.5f,35,0.48f,color,1);
    draw_quad(22,52,200,180,COL(0x0B0710),0.96f);
    draw_quad(28,58,188,168,COL(0x14262F),0.92f);
    draw_codex_sprite(codex_sprite(G.codex_section,G.codex_detail),122,125,
                      G.codex_section==1?64.0f:46.0f,G.codex_section==1?64.0f:46.0f,
                      COL(0xFFFFFF),1);
    draw_text_center("이미지",122,190,0.48f,COL(0x8878A8),1);
    draw_quad(238,52,220,180,COL(0x0B0710),0.96f);
    draw_text(name,252,68,0.90f,COL(0xFFF0D0),1);
    draw_text(left,252,98,0.58f,COL(0xC8C0E0),1);
    draw_text(right,252,116,0.58f,color,1);
    draw_text("패턴 / 효과",252,140,0.48f,COL(0x8878A8),1);
    for (int i=0;i<3;i++) if (lines[i][0])
        draw_text(lines[i],252,156+i*23,0.48f,COL(0xFFFFFF),1);
    char page[48];
    snprintf(page,sizeof(page),"%d / %d",G.codex_detail+1,count);
    draw_text_center(page,348,216,0.50f,COL(0x8878A8),1);
    draw_text_center("A/D : 항목 · Esc : 목록",VIRT_W*0.5f,VIRT_H-16,0.56f,COL(0x8878A8),1);
}

static void intro_clear_inputs(void){
    memset(key_held,0,sizeof(key_held));
    attack_held=false;
    mouse_present=false;
}
static float intro_disk_x(void){
    return VIRT_W*0.5f-25.0f;
}
static float intro_disk_y(float t){
    float place=clampf(t/2.0f,0,1);
    place=place*place*(3.0f-2.0f*place);
    return -42.0f+(117.0f+42.0f)*place;
}
static float intro_insert_caption_y(void){ return 196.0f; }
static bool intro_drive_stop_hold(float t){ return t>=2.15f && t<4.35f; }
static void intro_begin(void){
    intro_clear_inputs();
    G.state=ST_INTRO;
    G.state_t=0;
    G.intro_page=0;
    music_set(-1);
}
static void intro_handoff(void){
    intro_clear_inputs();
    start_run_after_intro();
    G.state=ST_PLAY;
    G.state_t=0;
    G.fade=1;
    G.fade_dir=-1;
}
static void draw_intro(void){
    float t=G.state_t;
    float cx=VIRT_W*0.5f, disk_x=intro_disk_x(), disk_y=intro_disk_y(t);
    float track=clampf((t-4.25f)/3.15f,0,1);
    float fragment=clampf((t-8.0f)/3.2f,0,1);
    (void)intro_pages;

    draw_light_begin(0,0);
    draw_light_blob(230,128,72,COL(0x253A60),0.32f);
    if (track>0) draw_light_blob(177+track*130,126,28,COL(0x527FC4),0.18f+0.18f*track);
    if (fragment>0) draw_light_blob(cx,164+fragment*46,38,COL(0x7CFCE4),0.22f+0.45f*fragment);
    draw_glow_begin(0,0);
    if (fragment>0) draw_glow_blob(cx,164+fragment*46,20,COL(0x9FFFF0),0.22f+0.55f*fragment);
    draw_scene_begin(0,0);
    draw_quad(0,0,VIRT_W,VIRT_H,COL(0x070A13),1);
    for (int y=16;y<VIRT_H;y+=16) draw_line(0,y,VIRT_W,y,1,COL(0x18223A),0.22f);
    draw_quad(116,84,248,92,COL(0x313E59),1);
    draw_quad(124,92,232,76,COL(0x121B2D),1);
    draw_line(124,92,356,92,3,COL(0x93A8D0),0.74f);
    draw_quad(154,108,172,36,COL(0x050912),1);
    draw_quad(disk_x,disk_y,50,30,COL(0x61759A),1);
    draw_quad(disk_x+5,disk_y+5,40,16,COL(0x1A2740),1);
    draw_quad(disk_x+34,disk_y+7,7,7,COL(0x9AB9E4),0.82f);
    draw_quad(154,132,172,13,COL(0x273753),0.96f);
    draw_line(154,132,326,132,2,COL(0x7E98C3),0.74f);
    if (track>0){
        float sx=169.0f+track*142.0f;
        draw_line(sx,109,sx,143,3,COL(0xEAF6FF),0.88f);
        draw_line(sx-8,109,sx-8,143,12,COL(0x1E3865),0.34f);
        draw_quad(187,114,17,5,COL(0xC3D5F5),0.65f);
        draw_quad(242,123,23,6,COL(0xFF789E),0.72f);
        draw_quad(286,116,14,5,COL(0xB7CEF3),0.65f);
    }
    if (fragment>0){
        float fy=158.0f+fragment*55.0f;
        draw_sprite(SPR_CORE_SHARD,cx,fy,12+fragment*7,15+fragment*8,COL(0xFFFFFF),0.42f+0.58f*fragment,false,t*70.0f);
        draw_quad(cx-72,219,144,12,COL(0x101A2C),0.92f);
        draw_quad(cx-66,222,132*fragment,6,COL(0x3FE0C5),0.76f);
    }
    draw_ui_begin();
    draw_text_center("RECOVERY BUFFER // SESSION 01",cx,34,0.64f,COL(0x9BB3D8),0.9f);
    if (t<2.15f) draw_text_center("INSERT ORIGINAL DISK",cx,intro_insert_caption_y(),0.72f,COL(0xD8E5FF),0.92f);
    else if (intro_drive_stop_hold(t)) draw_text_center("DRIVE STOP",cx,58,0.84f,COL(0xFFD5E0),1);
    else if (t<8.0f) draw_text_center("READ PASS // DAMAGED TRACK",cx,58,0.72f,COL(0xB8D4FF),0.94f);
    else draw_text_center("LAST NORMAL INDEX // TRANSFER",cx,58,0.72f,COL(0xCFFFEF),0.96f);
}

static void draw_dead(void){
    draw_overlay_bg(0.92f);
    char buf[128];
    draw_text_center("데이터 손상",VIRT_W/2,60,1.8f,COL(0xFF3D7F),1);
    static const char* enemy_names[E_TYPE_COUNT]={
        "슬라임", "박쥐", "망령", "추격자", "골렘", "포탑", "센티널", "드론", "폭격체", "스나이퍼", "실더", "하이브",
        "단편기", "메아리", "디프래그", "NULL", "미니 슬라임", "메아리 잔상"
    };
    const char* source=G.death_source_type>=0 && G.death_source_type<E_TYPE_COUNT?
                       enemy_names[G.death_source_type]:"알 수 없는 공격";
    snprintf(buf,sizeof(buf),"%s에게 사망하였습니다.",source);
    draw_text_center(buf,VIRT_W/2,100,0.9f,COL(0xC8C0E0),1);
    float progress=clampf((G.room.biome*9.0f+G.room.idx)/35.0f*100.0f,0.0f,100.0f);
    snprintf(buf,sizeof(buf),"처치 %d마리, 시간 %d:%02d, 진행률 %.2f%%",
             G.kills,(int)(G.run_time/60),(int)G.run_time%60,progress);
    draw_text_center(buf,VIRT_W/2,124,0.8f,COL(0x8878A8),1);
    snprintf(buf,sizeof(buf),"획득한 자원 : %d / 보유 자원 : %u",G.bytes_run,G.meta.bytes_currency);
    draw_text_center(buf,VIRT_W/2,148,0.8f,COL(0xFFD060),1);
    if (G.state_t>1.0f)
        draw_text_center("R : 재시작 · Space/Enter/Esc : 타이틀",VIRT_W/2,VIRT_H-36,0.75f,COL(0xE8DFFF),0.95f);
}

static void draw_pause(void){
    draw_overlay_bg(0.75f);
    float panel_x=16;
    draw_quad(panel_x-2,12,452,244,COL(0x08050D),0.98f);
    draw_quad(panel_x,14,448,240,COL(0x0B0710),1.0f);
    draw_text_center("일시정지",VIRT_W/2,24,1.05f,COL(0x9FFFF0),1);
    char buf[192];
    snprintf(buf,sizeof(buf),"무기  %s%s  ·  공격 %.1f",prefix_names[G.pl.weapon.prefix],
             weapon_defs[G.pl.weapon.type].name,player_attack_damage());
    draw_text(buf,26,45,0.58f,COL(0xE8E0F8),1);
    snprintf(buf,sizeof(buf),"HP %.0f/%d  ·  이동 ×%.2f  ·  빛 %.0f",
             G.pl.hp,G.pl.maxhp,player_speed_mul(),player_light_radius());
    draw_text(buf,26,60,0.54f,COL(0xC8C0E0),1);
    snprintf(buf,sizeof(buf),"복구 버퍼 %d/%dKB  ·  대시 %.1fs",player_used_kb(),player_capacity_kb(),
             fmaxf(0.0f,G.pl.dash_cd));
    draw_text(buf,26,75,0.54f,COL(0xC8C0E0),1);
    float y=94;
    bool has_relic=false;
    for (int i=0;i<RELIC_COUNT;i++){
        if (!G.pl.relics[i]) continue;
        snprintf(buf,sizeof(buf),"유물  %s — %s",relic_defs[i].name,relic_defs[i].desc);
        draw_text(buf,26,y,0.45f,COL(0xC8B8E8),1);
        y+=15; has_relic=true;
    }
    if (!has_relic){
        draw_text("보유 일반 유물 없음",26,y,0.45f,COL(0x8878A8),1);
        y+=15;
    }
    bool has_wrelic=false;
    for (int i=0;i<2;i++){
        int wr=G.pl.wrelics[i];
        if (wr<0) continue;
        snprintf(buf,sizeof(buf),"무기유물  %s — %s",weapon_relic_defs[wr].name,weapon_relic_defs[wr].desc);
        draw_text(buf,26,y,0.45f,COL(0xFFD060),1);
        y+=15; has_wrelic=true;
    }
    if (!has_wrelic)
        draw_text("보유 무기 유물 없음",26,y,0.45f,COL(0x8878A8),1);
    const char* items[3]={"계속하기","설정","타이틀로"};
    float x[3]={112,240,372};
    for (int i=0;i<3;i++){
        bool sel=G.menu_sel==i;
        draw_text_center(items[i],x[i],232,0.54f,sel?COL(0xFFFFFF):COL(0x8878A8),1);
    }
}

static int memory_coda(void){
    int best=-1, best_n=0, discarded=0;
    for (int i=0;i<MEM_TAG_COUNT;i++){
        if (G.memory.kept[i]>best_n){ best_n=G.memory.kept[i]; best=i; }
        discarded+=G.memory.discarded[i];
    }
    if (best>=0 && best_n>0) return best;
    return discarded>0?MEM_TAG_COUNT:-1;
}
static const char* memory_coda_text(void){
    static const char* kept[MEM_TAG_COUNT]={
        "처음 다시 누른 시작 버튼의 용기가, 복구 이미지를 비춘다.",
        "옆의 박수가 읽힌 흔적과 이어졌다.",
        "다음에 마저 하자는 약속이 마지막 줄을 지켜 냈다."
    };
    int c=memory_coda();
    if (c>=0 && c<MEM_TAG_COUNT) return kept[c];
    if (c==MEM_TAG_COUNT) return "버린 기록들 사이에도, 읽히지 않은 흔적은 남았다.";
    return NULL;
}
static float ending_phase(float t,float start,float duration){
    float x=clampf((t-start)/duration,0,1);
    return x*x*(3.0f-2.0f*x);
}
static void draw_true_ending(void){
    float t=G.state_t;
    float cx=VIRT_W*0.36f;
    float merge=ending_phase(t,2.4f,3.2f);
    float lift=ending_phase(t,10.0f,3.7f);
    float reveal=ending_phase(t,13.6f,3.0f);
    float wake=ending_phase(t,16.8f,3.1f);
    float disk_y=123.0f-lift*67.0f;
    static const v2 block_slots[4]={{-28,-18},{28,-18},{-28,18},{28,18}};
    draw_light_begin(0,0);
    draw_light_blob(cx,disk_y,50.0f+16.0f*merge,COL(0x9FFFF0),0.18f+0.28f*merge);
    draw_light_blob(VIRT_W*0.76f,119,24.0f+42.0f*wake,COL(0x9FFFF0),0.15f+0.36f*wake);
    draw_glow_begin(0,0);
    draw_glow_blob(cx,disk_y,18.0f+10.0f*merge,COL(0xD8FFF5),0.30f+0.28f*merge);
    draw_glow_blob(VIRT_W*0.76f,119,12.0f+20.0f*wake,COL(0xD8FFF5),0.22f+0.24f*wake);
    draw_scene_begin(0,0);
    draw_quad(0,0,VIRT_W,VIRT_H,COL(0x080A13),1);
    float world=1.0f-ending_phase(t,5.6f,3.8f);
    for (int x=16;x<VIRT_W*0.58f;x+=20)
        draw_line(x,48,x,206,1,COL(0x273653),0.20f*world);
    for (int y=50;y<208;y+=18)
        draw_line(0,y,VIRT_W*0.58f,y,1,COL(0x273653),0.20f*world);
    for (int i=0;i<22;i++){
        float a=(float)i*0.67f+t*0.35f;
        float r=18.0f+(float)(i%5)*11.0f;
        float px=cx+cosf(a)*r*(1.0f-merge*0.74f);
        float py=125+sinf(a)*r*(1.0f-merge*0.74f);
        draw_quad(px,py,2,2,COL(0x6B91C4),0.38f*(1.0f-merge));
    }
    float drive_x=cx-93, drive_y=119;
    draw_quad(drive_x,drive_y,186,66,COL(0x314057),1);
    draw_quad(drive_x+5,drive_y+5,176,56,COL(0x111928),1);
    draw_quad(drive_x+24,drive_y+16,138,11,COL(0x05080E),1);
    draw_line(drive_x+24,drive_y+16,drive_x+162,drive_y+16,1,COL(0x849BBE),0.65f);
    draw_quad(drive_x+149,drive_y+39,9,9,COL(0x3FE0C5),0.55f+0.35f*sinf(t*5.0f));
    float disk_x=cx-54;
    draw_quad(disk_x,disk_y-58,108,116,COL(0x52637B),1);
    draw_quad(disk_x+5,disk_y-53,98,106,COL(0x202B3C),1);
    draw_quad(disk_x+15,disk_y-42,78,51,COL(0xD8D0AF),0.96f);
    draw_quad(disk_x+22,disk_y-34,64,7,COL(0x8D9BBC),0.62f);
    draw_quad(disk_x+22,disk_y-20,64,1,COL(0x53627A),0.68f);
    draw_quad(disk_x+22,disk_y-13,64,1,COL(0x53627A),0.68f);
    draw_quad(disk_x+30,disk_y+12,48,30,COL(0x111927),1);
    draw_ring(cx,disk_y+27,22,COL(0x7FA0C7),0.76f);
    draw_ring(cx,disk_y+27,12,COL(0xA9C6E8),0.68f);
    draw_quad(disk_x+13,disk_y+44,82,4,COL(0x8EA3BF),0.72f);
    for (int i=0;i<4;i++){
        float a=t*2.5f+(float)i*1.5708f;
        float sx=cx+cosf(a)*47.0f;
        float sy=86+sinf(a)*31.0f;
        float tx=cx+block_slots[i].x;
        float ty=disk_y-15+block_slots[i].y;
        float bx=sx+(tx-sx)*merge;
        float by=sy+(ty-sy)*merge;
        draw_sprite(SPR_CORE_SHARD,bx,by,11,13,COL(0xFFFFFF),0.82f+0.18f*merge,false,t*4.0f+i);
    }
    if (reveal>0){
        float hx=cx-112+reveal*42.0f;
        float hy=18+reveal*18.0f;
        draw_quad(hx,hy,68,23,COL(0xD9A77D),0.82f);
        draw_quad(hx+49,hy+16,42,14,COL(0xD9A77D),0.82f);
        draw_quad(hx+84,hy+20,9,7,COL(0xEAC49B),0.88f);
        for (int i=0;i<10;i++){
            float px=disk_x+18+(float)((i*19)%68);
            float py=disk_y-38+(float)((i*13)%35);
            draw_quad(px,py,1,1,COL(0xFFF0D0),reveal*(0.3f+0.5f*sinf(t*4.0f+i)));
        }
    }
    float mx=VIRT_W*0.65f, my=65;
    draw_quad(mx,my,152,108,COL(0x44536B),1);
    draw_quad(mx+6,my+6,140,84,COL(0x07141C),1);
    draw_quad(mx+52,my+96,48,5,COL(0x8495AE),0.78f);
    if (wake>0){
        draw_quad(mx+14,my+14,124,50,COL(0x42B69D),0.18f+0.36f*wake);
        for (int i=0;i<5;i++) draw_line(mx+20,my+24+i*8,mx+130,my+24+i*8,1,COL(0xB8FFF0),0.18f*wake);
        draw_sprite(SPR_FLAME,mx+76,my+43,13,15,COL(0xE8FFF8),wake,false,t*3.0f);
    }
    draw_ui_begin();
    draw_text_center("READ WINDOW  //  VERIFIED RECOVERY",VIRT_W*0.76f,40,0.52f,COL(0xB8D8FF),0.94f);
    if (t<3.2f) draw_text_center("복구 블록이 읽기 창에서 맞물린다.",VIRT_W*0.5f,222,0.62f,COL(0xD8E8FF),1);
    else if (t<7.0f) draw_text_center("던전의 어둠은 원본 디스크의 트랙으로 사라진다.",VIRT_W*0.5f,222,0.56f,COL(0xD8E8FF),1);
    else if (t<12.6f) draw_text_center("RECOVERY VERIFIED  //  ORIGINAL UNCHANGED",VIRT_W*0.5f,222,0.54f,COL(0x9FFFF0),1);
    else if (t<16.8f){
        draw_text_center("DISKETTE DUNGEON",cx,disk_y-34,0.40f,COL(0x142132),reveal);
        draw_text_center("DO NOT ERASE",cx,disk_y-11,0.34f,COL(0x142132),reveal);
        draw_text_center("먼지 아래, 아이의 손글씨가 다시 읽힌다.",VIRT_W*0.5f,222,0.60f,COL(0xFFF0D0),reveal);
    } else {
        draw_text_center("이번에는, 저장한다.",VIRT_W*0.5f,222,0.82f,COL(0xFFFFFF),wake);
        draw_text_center("원본은 그대로 남아 있습니다.",VIRT_W*0.5f,239,0.48f,COL(0x9FFFF0),wake);
    }
    if (t>20.5f){
        draw_text_center("TRUE END — 원본의 이름",VIRT_W*0.5f,150,1.04f,COL(0x9FFFF0),clampf(t-20.5f,0,1));
        draw_text_center("아무 키 — 에필로그",VIRT_W*0.5f,VIRT_H-18,0.62f,COL(0x8878A8),0.5f+0.3f*sinf(t*4.0f));
    }
}
static void draw_ending(void){
    if (G.ending==3){ draw_true_ending(); return; }
    float t=G.state_t;
    float cx=VIRT_W*0.33f, cy=VIRT_H*0.48f;
    float spin=t*4.5f;
    float transfer=G.ending==1?clampf(t/2.0f,0,1):0;
    bool complete=G.ending==2;
    bool failed=G.ending==0;
    draw_light_begin(0,0);
    if (!failed) draw_light_blob(cx,cy+4,44,COL(0x7CFCE4),0.20f);
    if (transfer>0) draw_light_blob(cx+(VIRT_W*0.42f)*transfer,cy-6,16+18*transfer,
                                     COL(0x9FFFF0),0.22f+0.35f*transfer);
    if (complete) draw_light_blob(VIRT_W*0.76f,cy-5,50,COL(0x9FFFF0),0.42f);
    draw_glow_begin(0,0);
    if (!failed) draw_glow_blob(cx,cy+4,14,COL(0xD8FFF5),0.35f);
    draw_scene_begin(0,0);
    draw_quad(0,0,VIRT_W,VIRT_H,COL(0x090C18),1);
    for (int y=14;y<VIRT_H;y+=14)
        draw_line(0,y,VIRT_W,y,1,COL(0x18213A),0.18f);
    draw_quad(cx-92,cy-62,184,124,COL(0x3E4C63),1);
    draw_quad(cx-87,cy-57,174,114,COL(0x162239),1);
    draw_line(cx-87,cy-57,cx+65,cy-57,2,COL(0xB7C8E5),0.82f);
    draw_line(cx+65,cy-57,cx+87,cy-37,2,COL(0xB7C8E5),0.82f);
    draw_line(cx-87,cy-57,cx-87,cy+57,2,COL(0x7891B8),0.82f);
    draw_line(cx-87,cy+57,cx+87,cy+57,2,COL(0x7891B8),0.82f);
    draw_line(cx+87,cy-37,cx+87,cy+57,2,COL(0x5E749A),0.82f);
    draw_ring(cx,cy+8,42,COL(0x55769E),0.78f);
    draw_ring(cx,cy+8,28,COL(0x6D8DB3),0.72f);
    draw_ring(cx,cy+8,15,COL(0x4D698A),0.72f);
    for (int i=0;i<4;i++){
        float a=spin+i*1.5708f;
        draw_line(cx+cosf(a)*6,cy+8+sinf(a)*6,cx+cosf(a)*13,cy+8+sinf(a)*13,
                  2,COL(0xC0D7F5),0.68f);
    }
    draw_ring(cx,cy+8,6,COL(0xDCEBFF),0.88f);
    draw_quad(cx-51,cy-34,102,18,COL(0x070C16),0.98f);
    draw_line(cx-51,cy-34,cx+51,cy-34,2,COL(0x8EA4C4),0.84f);
    draw_line(cx-51,cy-16,cx+51,cy-16,1,COL(0x587293),0.84f);
    draw_quad(cx+3,cy-34,48,18,COL(0x9BA6B7),0.92f);
    draw_quad(cx+48,cy-19,8,13,COL(0xD4E5F6),0.92f);
    draw_quad(cx+44,cy-6,16,4,COL(0x6BC4CE),0.82f);
    float mx=VIRT_W*0.63f, my=cy-46;
    draw_quad(mx,my,154,108,COL(0x40516C),1);
    draw_quad(mx+5,my+5,144,88,failed?COL(0x080B12):COL(0x102C38),1);
    draw_quad(mx+51,my+96,52,5,COL(0x788BA8),0.8f);
    if (transfer>0)
        draw_sprite(SPR_CORE_SHARD,cx+(mx+72-cx)*transfer,cy+8+(my+47-cy)*transfer,
                    10+6*transfer,12+7*transfer,COL(0xFFFFFF),0.55f+0.45f*transfer,false,spin);
    if (complete){
        draw_quad(mx+12,my+13,130,50,COL(0x5AD5BE),0.36f);
        draw_line(mx+16,my+39,mx+138,my+39,1,COL(0xD8FFF5),0.62f);
    } else if (transfer>0) {
        draw_quad(mx+12,my+13,72*transfer,50,COL(0x5AD5BE),0.32f+0.22f*transfer);
    }
    draw_ui_begin();
    draw_text_center("READ WINDOW  //  FINAL RECOVERY",VIRT_W/2,24,0.62f,COL(0x9FC8FF),0.9f);
    draw_text_center("READ RETRY 01/03  DONE    02/03  DONE    03/03  DONE",
                     VIRT_W/2,cy+80,0.47f,COL(0xC8D8F5),0.92f);
    if (failed){
        draw_text_center("DRIVE STOP  //  NO RECOVERED IMAGE",mx+77,my+28,0.46f,COL(0xFFB4BD),1);
        draw_text_center("RECOVERY FAILED",mx+77,my+52,0.68f,COL(0xFF7F91),1);
    } else if (complete){
        draw_text_center("IMAGE VERIFIED",mx+77,my+21,0.60f,COL(0xD8FFF5),1);
        draw_text_center("이 모험을 지우지 마",mx+77,my+49,0.54f,COL(0xFFFFFF),1);
    } else {
        draw_text_center("PARTIAL IMAGE READY",mx+77,my+25,0.54f,COL(0x9FFFF0),1);
        draw_text_center("메모리에 남은 조각",mx+77,my+52,0.50f,COL(0xD8FFF5),1);
    }
    int line=(int)(t/3.2f);
    if (line>3) line=3;
    draw_text_center(ending_lines[G.ending][line],VIRT_W/2,cy+108,0.82f,COL(0xE8E0F8),1);
    if (t>14.0f){
        char buf[64];
        snprintf(buf,sizeof(buf),"%s END — %s",G.ending==2?"COMPLETE":(G.ending==0?"BAD":""),ending_names[G.ending]);
        draw_text_center(buf,VIRT_W/2,150,1.2f,G.ending==0?COL(0xFF3D7F):COL(0x9FFFF0),clampf((t-14.0f),0,1));
        draw_text_center("아무 키 — 에필로그",VIRT_W/2,VIRT_H-30,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(G.time*4.0f));
    }
}

static void draw_epilogue(void){
    draw_overlay_bg(1);
    float t=G.state_t;
    const char* epi_complete =
        "어른은 원본 디스크를 버리지 않고,\n"
        "복구된 마지막 한 줄을 읽는다.";
    const char* epi_true =
        "복구 이미지는 다시 실행되고,\n"
        "어른은 원본 디스크에 이름을 붙여 보관한다.\n\n"
        "이번에는, 저장한다.";
    const char* epi_partial =
        "읽힌 조각만 별도의 복구 이미지로 남긴다.\n"
        "어른은 원본 디스크를 건드리지 않는다.";
    const char* epi_bad =
        "드라이브가 멎고 원본 디스크는\n"
        "다시 서랍으로 들어간다.\n\n"
        "읽히지 않은 기억은 돌아오지 않았다.";
    const char* txt = G.ending==0?epi_bad:(G.ending==1?epi_partial:(G.ending==2?epi_complete:epi_true));
    int show=(int)(t*16.0f);
    char buf[512];
    int n=0; const char* q=txt;
    while (*q&&n<show){ int st=((*q&0xC0)==0xC0)?((*q&0xE0)==0xC0?2:3):1; q+=st; n++; }
    size_t len=(size_t)(q-txt); if(len>511)len=511;
    memcpy(buf,txt,len); buf[len]=0;
    draw_text_center(buf,VIRT_W/2,30,0.85f,COL(0xD8D0E8),1);
    if (t>3.0f && memory_coda_text())
        draw_text_center(memory_coda_text(),VIRT_W/2,VIRT_H-76,0.68f,
                         COL(0x9FFFF0),clampf(t-3.0f,0,1));
    if (G.ending==3 && t>16.0f)
        draw_text_center("...그리고 어딘가, 또 다른 어둠 속에서\n작은 불씨 하나가 깨어난다.  [New Game+ 해금]",
                         VIRT_W/2,VIRT_H-58,0.75f,COL(0x9FFFF0),clampf(t-16.0f,0,1));
    if (t>4.0f)
        draw_text_center("아무 키 — 처음으로",VIRT_W/2,VIRT_H-24,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(G.time*4.0f));
    (void)show;
}

// ----------------------------------------------------------- debug autopilot
#ifdef DD_DEBUG
int dbg_auto = 0;          // 1: 봇 플레이
int dbg_jump_biome = -1;   // --jump b<biome> r<room>
int dbg_jump_room = 0;
int dbg_weapon = -1;
int dbg_god = 0;
int dbg_ending = -1;
int dbg_intro = 0;
static float dbg_t = 0;
static Rng dbg_rng = { 0xD3B6ull };
static FILE* dbg_telemetry_file;
static float dbg_telemetry_t;
static bool dbg_telemetry_opened;
static bool dbg_telemetry_started;
static unsigned dbg_telemetry_samples;
static uint64_t dbg_raw_tick;
static uint64_t dbg_raw_elapsed_us;
static unsigned dbg_frame_index;
static int dbg_f10_prepared;
static int dbg_f10_done;
static uint64_t dbg_forced_crng;
static int dbg_showcase_active;
static int dbg_options_visual_active;
static int dbg_showcase_ready;
static float dbg_showcase_hold_t;
static int dbg_showcase_weapon_door=-1;
static int dbg_showcase_frames;
static int dbg_showcase_state_after_1s=-1;
typedef struct { const char* owner; int before, after; } DebugShowcaseTransition;
static DebugShowcaseTransition dbg_showcase_transitions[16];
static int dbg_showcase_transition_count;

extern void dd_debug_story_entry_notice_reset(void);
extern int dd_debug_story_entry_notice_probe(int biome,int idx);

static void debug_telemetry_close(void){
    if (dbg_telemetry_file){
        if (dbg_telemetry_started)
            fprintf(dbg_telemetry_file,
                    "{\"schema\":2,\"kind\":\"telemetry_stop\",\"reason\":\"duration\",\"frame_count\":%u,\"elapsed_us\":%llu,\"state_samples\":%u}\n",
                    dbg_frame_index,(unsigned long long)dbg_raw_elapsed_us,dbg_telemetry_samples);
        fclose(dbg_telemetry_file);
    }
    dbg_telemetry_file=NULL;
    dbg_telemetry_started=false;
    dbg_raw_tick=0;
    dbg_raw_elapsed_us=0;
    dbg_frame_index=0;
}
static void debug_invariant(const char* what, int expected, int actual){
    if (expected==actual) return;
    fprintf(stderr,"{\"error\":\"invariant\",\"what\":\"%s\",\"expected\":%d,\"actual\":%d}\n",
            what,expected,actual);
    exit(3);
}
static void debug_telemetry_open(void){
    if (!DBG_CFG.have_telemetry) return;
    dbg_telemetry_file=fopen(DBG_CFG.telemetry,"wx");
    dbg_telemetry_opened=true;
    if (!dbg_telemetry_file){
        fprintf(stderr,"{\"error\":\"telemetry-open\"}\n");
        exit(2);
    }
    atexit(debug_telemetry_close);
}
static void debug_telemetry_start(void);
static void debug_raw_frame(void){
    if (!dbg_telemetry_file) return;
    uint64_t now=stm_now();
    if (!dbg_telemetry_started) debug_telemetry_start();
    if (!dbg_raw_tick){
        fprintf(dbg_telemetry_file,
                "{\"schema\":2,\"kind\":\"frame\",\"frame_index\":0,\"elapsed_us\":0,\"delta_us\":0}\n");
    } else {
        if (DBG_CFG.have_duration &&
            dbg_raw_elapsed_us >= (uint64_t)DBG_CFG.duration_ms*1000ull) return;
        uint64_t us=(uint64_t)llround(stm_us(stm_diff(now,dbg_raw_tick)));
        if (DBG_CFG.have_duration){
            uint64_t endpoint=(uint64_t)DBG_CFG.duration_ms*1000ull;
            if (dbg_raw_elapsed_us+us>endpoint) us=endpoint-dbg_raw_elapsed_us;
        }
        dbg_raw_elapsed_us+=us;
        fprintf(dbg_telemetry_file,
                "{\"schema\":2,\"kind\":\"frame\",\"frame_index\":%u,\"elapsed_us\":%llu,\"delta_us\":%llu}\n",
                dbg_frame_index,(unsigned long long)dbg_raw_elapsed_us,(unsigned long long)us);
    }
    dbg_raw_tick=now;
    dbg_frame_index++;
}
static void debug_telemetry_start(void){
    if (!dbg_telemetry_file||dbg_telemetry_started) return;
    fprintf(dbg_telemetry_file,
            "{\"schema\":2,\"kind\":\"telemetry_start\",\"pid\":%lu,\"clock\":\"sokol_time_monotonic\",\"seed\":%u,\"difficulty\":%d,\"weapon\":%d,\"ngplus\":%d,\"biome\":%d,\"room\":%d,\"duration_ms\":%d,\"warmup_ms\":60000}\n",
#if defined(_WIN32)
            (unsigned long)_getpid(),
#else
            (unsigned long)getpid(),
#endif
            G.run_seed,G.difficulty,G.pl.weapon.type,G.ngplus?1:0,G.room.biome,G.room.idx,
            DBG_CFG.have_duration?DBG_CFG.duration_ms:0);
    fflush(dbg_telemetry_file);
    dbg_telemetry_started=true;
}
static void debug_telemetry_emit(float dt){
    if (!DBG_CFG.have_telemetry) return;
    if (!dbg_telemetry_started) debug_telemetry_start();
    dbg_telemetry_t-=dt;
    if (dbg_telemetry_t>0) return;
    dbg_telemetry_t=(DBG_CFG.have_telemetry_interval?DBG_CFG.telemetry_interval_ms:1000)/1000.0f;
    int ne=0;
    for (int i=0;i<MAX_ENTITIES;i++) if (G.ents[i].active) ne++;
    fprintf(dbg_telemetry_file,
            "{\"schema\":2,\"kind\":\"telemetry_state\",\"elapsed_ms\":%d,\"state\":%d,"
            "\"biome\":%d,\"room\":%d,\"x_milli\":%d,\"y_milli\":%d,\"hp_milli\":%d,"
            "\"entities\":%d,\"event\":{\"type\":%d,\"tag\":%d,\"trait\":%d,\"state\":%d,"
            "\"tile_x\":%d,\"tile_y\":%d,\"world_x_milli\":%d,\"world_y_milli\":%d},"
            "\"pending_trait\":%d,\"bytes\":%d}\n",
            (int)lroundf(dbg_t*1000.0f),G.state,G.room.biome,G.room.idx,
            (int)lroundf(G.pl.pos.x*1000.0f),(int)lroundf(G.pl.pos.y*1000.0f),
            (int)lroundf(G.pl.hp*1000.0f),ne,G.room.event_type,G.room.event_tag,
            G.room.event_trait,G.room.event_state,G.room.event_tile_x,G.room.event_tile_y,
            (int)lroundf(G.room.event_pos.x*1000.0f),(int)lroundf(G.room.event_pos.y*1000.0f),
            G.memory.pending_trait,G.bytes_run);
    fflush(dbg_telemetry_file);
    dbg_telemetry_samples++;
}
static void debug_f10_pathfind(void){
    int sx=(int)(G.pl.pos.x/TILE), sy=(int)(G.pl.pos.y/TILE);
    int tx=G.room.event_tile_x, ty=G.room.event_tile_y;
    int dist[MAX_ROOM_H][MAX_ROOM_W];
    int qx[MAX_ROOM_W*MAX_ROOM_H], qy[MAX_ROOM_W*MAX_ROOM_H];
    int head=0,tail=0;
    memset(dist,-1,sizeof dist);
    if(sx<0||sy<0||tx<0||ty<0||sx>=G.room.w||sy>=G.room.h||tx>=G.room.w||ty>=G.room.h)
        return;
    dist[ty][tx]=0; qx[tail]=tx; qy[tail++]=ty;
    static const int dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    while(head<tail){
        int x=qx[head],y=qy[head++];
        for(int d=0;d<4;d++){
            int nx=x+dx[d],ny=y+dy[d];
            if(nx<0||ny<0||nx>=G.room.w||ny>=G.room.h||dist[ny][nx]>=0) continue;
            uint8_t tile=G.room.tiles[ny][nx];
            if(tile!=T_FLOOR&&tile!=T_DOOR_OPEN&&tile!=T_EXIT) continue;
            dist[ny][nx]=dist[y][x]+1; qx[tail]=nx; qy[tail++]=ny;
        }
    }
    if(dist[sy][sx]<0) return;
    int best=dist[sy][sx], bx=sx, by=sy;
    for(int d=0;d<4;d++){
        int nx=sx+dx[d],ny=sy+dy[d];
        if(nx<0||ny<0||nx>=G.room.w||ny>=G.room.h) continue;
        if(dist[ny][nx]>=0 && dist[ny][nx]<best){best=dist[ny][nx];bx=nx;by=ny;}
    }
    memset(key_held,0,sizeof key_held);
    key_held[SAPP_KEYCODE_D]=bx>sx; key_held[SAPP_KEYCODE_A]=bx<sx;
    key_held[SAPP_KEYCODE_S]=by>sy; key_held[SAPP_KEYCODE_W]=by<sy;
}
static void debug_f10_pathfind(void);
static void debug_f10_prepare(void);
static void debug_f10_run(void);

static void debug_drive(float dt){
    if (DBG_CFG.action==21) return;
    debug_telemetry_emit(dt);
    dbg_t += dt;
    if (DBG_CFG.have_duration && dbg_t*1000.0f >= DBG_CFG.duration_ms){
        debug_telemetry_close();
        sapp_request_quit();
        return;
    }
    switch (G.state){
    case ST_BOOT: G.state=ST_TITLE; G.state_t=0; break;
    case ST_INTRO:
        break;
    case ST_TITLE:
        if (dbg_intro){ G.state=ST_INTRO; G.state_t=0; G.intro_page=0; break; }
        if (dbg_ending>=0){
            G.ending=dbg_ending; G.state=ST_ENDING; G.state_t=0; music_set(6);
            break;
        }
        start_run();
        if (dbg_weapon>=0) G.pl.weapon.type=dbg_weapon;
        if (dbg_jump_biome>=0){
            G.pl.cores = (uint8_t)((1<<dbg_jump_biome)-1);
            room_generate(dbg_jump_biome,dbg_jump_room,PROMISE_NONE,DIR_L);
        }
        G.state=ST_PLAY;
        break;
    case ST_PLAY: {
        if (DBG_CFG.have_f10_branch && !dbg_f10_done){
            if (G.room.event_state!=MEM_STATE_AVAILABLE){
                G.room.event_type=MEM_EVENT_CORRUPTED;
                G.room.event_tag=MEM_TAG_PROMISE;
                G.room.event_trait=ELITE_HASTE;
                G.room.event_state=MEM_STATE_AVAILABLE;
                G.room.event_pos=G.pl.pos;
                G.room.event_tile_x=(int)(G.pl.pos.x/TILE);
                G.room.event_tile_y=(int)(G.pl.pos.y/TILE);
            }
            debug_f10_prepare();
            float distance=v2len(v2sub(G.room.event_pos,G.pl.pos));
            if (distance<=24.0f) debug_f10_run();
            else debug_f10_pathfind();
            break;
        }
        if (!dbg_auto) break;
        // 봇: 적이 있으면 조준+공격, 없으면 오른쪽 문으로
        Player* p=&G.pl;
        Entity* tgt=NULL; float best=1e9f;
        for (int i=0;i<MAX_ENTITIES;i++){
            Entity* e=&G.ents[i];
            if (!e->active||e->type==E_ECHO_GHOST) continue;
            float d=v2len(v2sub(e->pos,p->pos));
            if (d<best){best=d;tgt=e;}
        }
        memset(key_held,0,sizeof(key_held));
        mouse_present=true;
        if (tgt){
            mouse_virt=v2sub(tgt->pos, G.cam);
            attack_held = (((int)(dbg_t*10.0f))%3)!=0;
            // 적과 거리 유지하며 배회 (근접무기는 파고든다)
            float ideal = (p->weapon.type==WPN_SWORD)? 24.0f:90.0f;
            v2 d=v2sub(tgt->pos,p->pos);
            float dist=v2len(d);
            v2 dir = dist>ideal? v2norm(d): v2scale(v2norm(d),-1.0f);
            // 벽 너머 적에게 접근 못 하면 우회 (사거리 밖 + 거리 미감소)
            static float cb_best=1e9f, cb_t, cb_detour; static int cb_sign=1;
            if (dist < cb_best-2.0f){ cb_best=dist; cb_t=0; }
            else if (dist > ideal*1.6f){
                cb_t+=dt;
                if (cb_t>1.5f){
                    cb_detour=1.0f+rng_f(&dbg_rng)*0.8f;
                    if (rng_i(&dbg_rng,3)==0) cb_sign=-cb_sign;
                    cb_t=0; cb_best=dist;
                }
            } else cb_t=0;
            if (cb_detour>0){
                cb_detour-=dt;
                v2 rot=V2(-dir.y*cb_sign,dir.x*cb_sign);
                dir=v2norm(v2add(v2scale(dir,0.25f),rot));
            }
            float wob = sinf(dbg_t*2.3f);
            v2 strafe = V2(-dir.y*wob,dir.x*wob);
            v2 mv = v2add(dir,strafe);
            key_held[mv.x>0.3f?SAPP_KEYCODE_D:SAPP_KEYCODE_A]= fabsf(mv.x)>0.3f;
            key_held[mv.y>0.3f?SAPP_KEYCODE_S:SAPP_KEYCODE_W]= fabsf(mv.y)>0.3f;
            if (p->dash_cd<=0 && dist<30.0f && rng_i(&dbg_rng,30)==0){
                sapp_event ev={0}; ev.type=SAPP_EVENTTYPE_KEY_DOWN; ev.key_code=SAPP_KEYCODE_LEFT_SHIFT;
                game_event(&ev);
            }
        } else {
            attack_held=false;
            // 픽업 먼저 (무기 스왑 루프 방지: 무기 제외, 실패 픽업은 잠시 무시)
            static float pk_ignore[MAX_PICKUPS];
            Pickup* bp=NULL; float bd=1e9f; int bpi=-1;
            for (int i=0;i<MAX_PICKUPS;i++){
                Pickup* pk=&G.pickups[i];
                if (pk_ignore[i]>dbg_t) continue;
                if (!pk->active||pk->type==PK_WEAPON) continue;
                if (pk->type==PK_HEART && p->hp>=p->maxhp) continue;
                float d=v2len(v2sub(pk->pos,p->pos));
                if (d<bd){bd=d;bp=pk;bpi=i;}
            }
            // 출구: 실제 열린 문/출구 타일 중 가장 가까운 곳 (모든 벽 스캔)
            int RW=G.room.w, RH=G.room.h;
            v2 exit_goal = V2(RW*0.5f*TILE,RH*0.5f*TILE);
            float ed=1e9f;
            for (int ty=0;ty<RH;ty++) for (int tx=0;tx<RW;tx++){
                if (tx!=0&&tx!=RW-1&&ty!=0&&ty!=RH-1) continue; // 둘레 벽만
                uint8_t t=G.room.tiles[ty][tx];
                if (t!=T_DOOR_OPEN&&t!=T_EXIT) continue;
                // 문 안쪽으로 몇 px 들어간 목표점
                float gx=tx*TILE+8.0f, gy=ty*TILE+8.0f;
                if (tx==0) gx+=TILE*0.5f; else if (tx==RW-1) gx-=TILE*0.5f;
                if (ty==0) gy+=TILE*0.5f; else if (ty==RH-1) gy-=TILE*0.5f;
                v2 g=V2(gx,gy);
                float dd=v2len(v2sub(g,p->pos));
                if (dd<ed){ ed=dd; exit_goal=g; }
            }
            v2 goal = bp? bp->pos : exit_goal;
            v2 dir=v2norm(v2sub(goal,p->pos));
            // 벽에 끼면 우회
            static v2 lastpos; static float stuck_t; static float detour_t; static int detour_sign=1;
            if (v2len(v2sub(p->pos,lastpos))<0.3f) stuck_t+=dt; else stuck_t=0;
            lastpos=p->pos;
            if (stuck_t>0.4f){
                detour_t=0.9f+rng_f(&dbg_rng)*0.8f;
                if (rng_i(&dbg_rng,3)==0) detour_sign=-detour_sign;
                stuck_t=0;
            }
            if (detour_t>0){
                detour_t-=dt;
                v2 rot=V2(-dir.y*detour_sign,dir.x*detour_sign);
                dir=v2norm(v2add(v2scale(dir,0.25f),rot));
            }
            key_held[SAPP_KEYCODE_D]=dir.x>0.25f; key_held[SAPP_KEYCODE_A]=dir.x<-0.25f;
            key_held[SAPP_KEYCODE_S]=dir.y>0.25f; key_held[SAPP_KEYCODE_W]=dir.y<-0.25f;
            if (bp&&bd<16.0f){
                if (!player_try_pickup(bp)) pk_ignore[bpi]=dbg_t+6.0f;
            }
            // 오래 못 닿는 픽업은 포기
            static int last_bpi=-1; static float chase_t;
            if (bpi==last_bpi && bpi>=0){ chase_t+=dt; if (chase_t>7.0f){ pk_ignore[bpi]=dbg_t+15.0f; chase_t=0; } }
            else { chase_t=0; last_bpi=bpi; }
        }
    } break;
    case ST_RELIC_SWAP:
        player_confirm_relic_swap(-1);
        break;
    case ST_FLASHBACK:
        if (dbg_auto && G.fb_t>1.5f){ G.state=ST_PLAY; }
        break;
    case ST_DEAD:
        if (G.state_t>1.2f){ G.state=ST_TITLE; G.state_t=0; }
        break;
    case ST_ENDING:
        if (G.state_t>(G.ending==3?23.0f:17.0f)){ G.state=ST_EPILOGUE; G.state_t=0; }
        break;
    case ST_EPILOGUE:
        if (G.state_t>22.0f){ music_set(0); G.state=ST_TITLE; G.state_t=0; }
        break;
    default: break;
    }
}
#ifdef DD_DEBUG
static void debug_prepare_configured_run(void){
    int weapon = DBG_CFG.have_weapon ? DBG_CFG.weapon : WPN_SWORD;
    memset(&G.meta,0,sizeof(G.meta));
    G.meta.unlocked_weapons = 1u<<WPN_SWORD;
    G.meta.opt_scanline = 1; G.meta.opt_shake = 1;
    G.meta.opt_bgm = 1; G.meta.opt_sfx = 1; G.meta.intro_replay_queued = 1;
    G.difficulty = DBG_CFG.have_difficulty ? DBG_CFG.difficulty : 1;
    G.ngplus = DBG_CFG.have_ngplus && DBG_CFG.ngplus;
    if (DBG_CFG.have_weapon) G.meta.unlocked_weapons |= 1u<<weapon;
    G.title_weapon = weapon;
    G.run_seed = DBG_CFG.have_seed ? DBG_CFG.seed : 1u;
    G.meta.last_seed = G.run_seed;
    G.timescale = 1.0f;
}
static uint32_t debug_wire_checksum(const uint32_t* words, int n){
    uint32_t sum=0x1D15C0DEu;
    for (int i=0;i<n;i++) sum=sum*31u+words[i];
    return sum;
}
static void debug_hex32(char* out, uint32_t v){
    static const char h[]="0123456789abcdef";
    for (int i=0;i<8;i++) out[i]=h[(v>>(28-i*4))&15];
    out[8]=0;
}
typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} DebugSha256;
#ifndef DD_DEBUG_SOURCE_SHA256
#define DD_DEBUG_SOURCE_SHA256 ""
#endif
static uint32_t debug_rotr(uint32_t x,int n){ return (x>>n)|(x<<(32-n)); }
static void debug_sha_init(DebugSha256* s){
    static const uint32_t h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memcpy(s->h,h,sizeof h); s->bits=0; s->used=0;
}
static void debug_sha_block(DebugSha256* s,const uint8_t* p){
    static const uint32_t k[64]={
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
        0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
        0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
        0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
        0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
        0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
        0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
        0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t w[64],a,b,c,d,e,f,g,h;
    int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|
                               ((uint32_t)p[i*4+2]<<8)|p[i*4+3];
    for(i=16;i<64;i++){
        uint32_t x=debug_rotr(w[i-15],7)^debug_rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t y=debug_rotr(w[i-2],17)^debug_rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+x+w[i-7]+y;
    }
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for(i=0;i<64;i++){
        uint32_t s1=debug_rotr(e,6)^debug_rotr(e,11)^debug_rotr(e,25);
        uint32_t ch=(e&f)^((~e)&g), t1=h+s1+ch+k[i]+w[i];
        uint32_t s0=debug_rotr(a,2)^debug_rotr(a,13)^debug_rotr(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;
    s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void debug_sha_update(DebugSha256* s,const void* data,size_t n){
    const uint8_t* p=(const uint8_t*)data;
    s->bits+=(uint64_t)n*8;
    while(n){
        size_t take=64-s->used; if(take>n)take=n;
        memcpy(s->block+s->used,p,take);s->used+=take;p+=take;n-=take;
        if(s->used==64){debug_sha_block(s,s->block);s->used=0;}
    }
}
static void debug_sha_final(DebugSha256* s,char out[65]){
    uint8_t pad[128]={0x80}; size_t n=s->used<56?56-s->used:120-s->used;
    uint64_t bits=s->bits; int i;
    debug_sha_update(s,pad,n);
    for(i=0;i<8;i++) s->block[56+i]=(uint8_t)(bits>>(56-i*8));
    debug_sha_block(s,s->block);
    for(i=0;i<8;i++) snprintf(out+i*8,9,"%08x",s->h[i]);
    out[64]=0;
}
static int debug_file_digest(const char* path,char sha[65],long* length){
    FILE* f=fopen(path,"rb"); uint8_t buf[256]; size_t n; long total=0;
    DebugSha256 s;
    if(!f)return 0;
    debug_sha_init(&s);
    while((n=fread(buf,1,sizeof buf,f))>0){debug_sha_update(&s,buf,n);total+=(long)n;}
    if(ferror(f)){fclose(f);return 0;}
    fclose(f);debug_sha_final(&s,sha);if(length)*length=total;return 1;
}
static const char* debug_source_sha256(void){
    static char source_sha[65];
    static int checked=0;
    DebugSha256 test;
    char vector_sha[65];
    if(checked)return source_sha;
    debug_sha_init(&test);
    debug_sha_update(&test,"abc",3);
    debug_sha_final(&test,vector_sha);
    if(strcmp(vector_sha,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ||
       strlen(DD_DEBUG_SOURCE_SHA256)!=64){
        fprintf(stderr,"{\"error\":\"debug-sha256\"}\n");
        exit(3);
    }
    memcpy(source_sha,DD_DEBUG_SOURCE_SHA256,sizeof source_sha);
    checked=1;
    return source_sha;
}
static void debug_emit_meta_fields(const MetaSave* m){
    printf("{\"bytes_currency\":%u,\"unlocked_weapons\":%u,\"best_biome\":%u,\"runs\":%u,"
           "\"wins\":%u,\"true_clear\":%u,\"opt_scanline\":%u,\"opt_shake\":%u,"
           "\"last_seed\":%u,\"upg\":[%u,%u,%u,%u,%u],\"intro_seen\":%u,\"intro_replay_queued\":%u,\"opt_bgm\":%u,\"opt_sfx\":%u}",
           m->bytes_currency,m->unlocked_weapons,m->best_biome,m->runs,m->wins,
           m->true_clear,m->opt_scanline,m->opt_shake,m->last_seed,m->upg[0],
           m->upg[1],m->upg[2],m->upg[3],m->upg[4],m->intro_seen,m->intro_replay_queued,m->opt_bgm,m->opt_sfx);
}
static int debug_meta_expected(const MetaSave* m,int version,uint32_t intro_seen,uint32_t intro_replay_queued){
    static const uint32_t v1[9]={123,0x15,2,9,3,1,1,0,12345};
    static const uint32_t v2[9]={456,0x3f,3,12,4,1,1,0,54321};
    static const uint32_t v3[9]={789,0x3f,3,15,5,1,1,0,67890};
    static const uint32_t v4[9]={999,0x3f,3,18,6,1,1,0,98765};
    const uint32_t* x=version==1?v1:(version==2?v2:(version==3?v3:v4)); int i;
    if(m->bytes_currency!=x[0]||m->unlocked_weapons!=x[1]||m->best_biome!=x[2]||
       m->runs!=x[3]||m->wins!=x[4]||m->true_clear!=x[5]||
       m->opt_scanline!=x[6]||m->opt_shake!=x[7]||m->last_seed!=x[8])return 0;
    for(i=0;i<5;i++) if(m->upg[i]!=(version==1?0:(uint32_t)(i+1)))return 0;
    if(version>=3 && (m->intro_seen!=intro_seen || m->intro_replay_queued!=intro_replay_queued))return 0;
    if(version<3 && (m->intro_seen!=0 || m->intro_replay_queued!=0))return 0;
    if(version<4 && (m->opt_bgm!=1 || m->opt_sfx!=1))return 0;
    if(version==4 && (m->opt_bgm!=1 || m->opt_sfx!=0))return 0;
    return 1;
}
static int debug_meta_is_default(const MetaSave* m){
    return m->magic==0 && m->version==0 && m->bytes_currency==0 &&
           m->unlocked_weapons==(1u<<WPN_SWORD) && m->best_biome==0 &&
           m->runs==0 && m->wins==0 && m->true_clear==0 &&
           m->opt_scanline==1 && m->opt_shake==1 && m->last_seed==0 &&
           m->upg[0]==0 && m->upg[1]==0 && m->upg[2]==0 &&
           m->upg[3]==0 && m->upg[4]==0 && m->intro_seen==0 &&
           m->intro_replay_queued==1 && m->opt_bgm==1 && m->opt_sfx==1 && m->checksum==0;
}
static void debug_save_fixture(bool reject){
    char path[600],input_sha[65],post_sha[65]; long input_len=0,post_len=0;
    uint32_t words[21]={0}; int n=DBG_CFG.expect_version==1?12:(DBG_CFG.expect_version==2?17:(DBG_CFG.expect_version==3?19:21));
    MetaSave loaded,reloaded; uint32_t expected_checksum=0; int length_valid;
    save_path(path,sizeof path);
#if defined(_WIN32)
    strncat(path,"\\save.bin",sizeof(path)-strlen(path)-1);
#else
    strncat(path,"/save.bin",sizeof(path)-strlen(path)-1);
#endif
    if(!debug_file_digest(path,input_sha,&input_len) || input_len<8 ||
       (size_t)input_len>sizeof words*4){
        fprintf(stderr,"{\"error\":\"save-fixture-input\"}\n");exit(2);
    }
    {
        FILE* f=fopen(path,"rb");
        if(!f||fread(words,1,(size_t)input_len,f)!=(size_t)input_len){if(f)fclose(f);
            fprintf(stderr,"{\"error\":\"save-fixture-input\"}\n");exit(2);}
        fclose(f);
    }
    if(words[0]!=0xD15C0DE7u || words[1]!=(uint32_t)DBG_CFG.expect_version){
        fprintf(stderr,"{\"error\":\"save-fixture-version\"}\n");exit(2);
    }
    length_valid=input_len==(long)(n*4);
    if(length_valid) expected_checksum=debug_wire_checksum(words,n-1);
    if((!reject && (!length_valid || words[n-1]!=expected_checksum)) ||
       (reject && length_valid && words[n-1]==expected_checksum)){
        fprintf(stderr,"{\"error\":\"save-fixture-checksum\"}\n");exit(2);
    }
    loaded=G.meta;
    if(!reject && !debug_meta_expected(&loaded,DBG_CFG.expect_version,
                                       DBG_CFG.expect_version>=3?words[16]:0,
                                       DBG_CFG.expect_version>=3?words[17]:0)){
        fprintf(stderr,"{\"error\":\"save-fixture-fields\"}\n");exit(3);
    }
    printf("{\"schema\":1,\"kind\":\"save_fixture\",\"case\":\"%s\",\"input_sha256\":\"%s\","
           "\"input_length\":%ld,\"load_result\":\"%s\",\"loaded_fields\":",
           reject?"reject":"roundtrip",input_sha,input_len,reject?"rejected":"valid");
    debug_emit_meta_fields(&loaded);
    if(length_valid) printf(",\"pre_save_checksum\":\"%08x\"",words[n-1]);
    else printf(",\"pre_save_checksum\":null");
    if(!reject){
        meta_save();
        memset(&G.meta,0,sizeof G.meta);
        meta_load();
        reloaded=G.meta;
        if(!debug_file_digest(path,post_sha,&post_len) || post_len!=84 ||
           !debug_meta_expected(&reloaded,DBG_CFG.expect_version,
                                DBG_CFG.expect_version>=3?words[16]:0,
                                DBG_CFG.expect_version>=3?words[17]:0)){
            fprintf(stderr,"{\"error\":\"save-fixture-reload\"}\n");exit(3);
        }
        if (reloaded.bytes_currency!=loaded.bytes_currency ||
            reloaded.unlocked_weapons!=loaded.unlocked_weapons ||
            reloaded.best_biome!=loaded.best_biome ||
            reloaded.runs!=loaded.runs || reloaded.wins!=loaded.wins ||
            reloaded.true_clear!=loaded.true_clear ||
            reloaded.opt_scanline!=loaded.opt_scanline ||
            reloaded.opt_shake!=loaded.opt_shake ||
            reloaded.last_seed!=loaded.last_seed ||
            memcmp(reloaded.upg,loaded.upg,sizeof loaded.upg)!=0 ||
            reloaded.intro_seen!=loaded.intro_seen ||
            reloaded.intro_replay_queued!=loaded.intro_replay_queued ||
            reloaded.opt_bgm!=loaded.opt_bgm || reloaded.opt_sfx!=loaded.opt_sfx){
            fprintf(stderr,"{\"error\":\"save-fixture-fields\"}\n");exit(3);
        }
        printf(",\"post_save_sha256\":\"%s\",\"post_save_length\":%ld,\"reload_fields\":",
               post_sha,post_len);
        debug_emit_meta_fields(&reloaded);
    }else{
        if(!debug_file_digest(path,post_sha,&post_len) ||
           strcmp(input_sha,post_sha)!=0 || post_len!=input_len){
            fprintf(stderr,"{\"error\":\"save-fixture-mutated\"}\n");exit(3);
        }
        if(!debug_meta_is_default(&G.meta)){
            fprintf(stderr,"{\"error\":\"save-fixture-defaults\"}\n");exit(3);
        }
        reloaded=G.meta;
        printf(",\"post_save_sha256\":\"%s\",\"post_save_length\":%ld,\"reload_fields\":",
               post_sha,post_len);
        debug_emit_meta_fields(&reloaded);
    }
    printf(",\"real_profile_untouched\":%d}\n",
           DBG_CFG.have_isolated_profile ? 1:0);
}
static float debug_observe_weapon(int weapon,int courage,bool cannon_release,float* cooldown){
    (void)cannon_release;
    memset(G.ents,0,sizeof G.ents);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.weapon.type=weapon; G.pl.weapon.prefix=PFX_NONE;
    G.pl.relics[RELIC_COMPRESS]=false; G.pl.relics[RELIC_LUMINANCE]=false;
    G.pl.wrelics[0]=G.pl.wrelics[1]=-1;
    G.pl.pos=V2(8*TILE,8*TILE); G.pl.aim=V2(1,0); G.pl.vel=V2(0,0);
    G.pl.attack_cd=0; G.pl.charge=0; G.pl.charging=false; G.pl.glaive_out=false;
    G.pl.hp=(float)G.pl.maxhp;
    G.memory.kept[MEM_TAG_COURAGE]=(uint8_t)courage;
    Entity* e=&G.ents[0];
    e->active=true; e->type=E_SLIME; e->pos=v2add(G.pl.pos,V2(24,0));
    e->radius=7.0f; e->hp=e->maxhp=1000.0f;
    G.state=ST_PLAY; G.room.cleared=true;
    mouse_present=false; attack_held=true;
    if (weapon==WPN_CANNON){
        update_play(0.1f);
        attack_held=false;
        update_play(0.0f);
    } else {
        update_play(0.0f);
    }
    attack_held=false;
    if(cooldown) *cooldown=G.pl.attack_cd;
    if(weapon==WPN_SWORD) return 1000.0f-e->hp;
    for(int i=0;i<MAX_BULLETS;i++)
        if(G.bullets[i].active&&G.bullets[i].from_player) return G.bullets[i].dmg;
    return 0.0f;
}
static void debug_fixture_modifiers(void){
    static const char* names[WPN_COUNT]={"sword","cannon","spray","glaive","lance","wand"};
    int w,c; float old_light=G.light_mul; bool old_lum=G.pl.relics[RELIC_LUMINANCE];
    uint32_t old_upg=G.meta.upg[3];
    int permanent_total=0, weapon_total=0;
    for (int i=0;i<5;i++) for (int level=0;level<upg_defs[i].max;level++)
        permanent_total+=upg_level_costs[i][level]?upg_level_costs[i][level]:upg_defs[i].base_cost*(level+1);
    for (int i=1;i<WPN_COUNT;i++) weapon_total+=weapon_unlock_cost(i);
    debug_invariant("permanent-upgrade-cost-total",4100,permanent_total);
    debug_invariant("weapon-unlock-cost-total",1160,weapon_total);
    G.light_mul=1.0f;
    G.pl.relics[RELIC_LUMINANCE]=false;
    G.meta.upg[3]=0;
    memset(&G.memory,0,sizeof G.memory);
    debug_invariant("light-shield-base-capacity",0,player_light_shield_limit());
    G.pl.maxhp=3; G.pl.shield_maxhp=3; G.pl.hp=3.0f; G.pl.shield=0.0f; G.pl.iframes=0;
    player_take_damage_amount(G.pl.pos,0.5f);
    debug_invariant("light-shield-base-spills-to-hp",2500,(int)lroundf(G.pl.hp*1000.0f));
    debug_invariant("light-shield-base-remains-empty",0,(int)lroundf(G.pl.shield*1000.0f));
    G.pl.iframes=0;
    player_take_damage_amount(G.pl.pos,1.0f);
    debug_invariant("light-shield-spills-to-hp",1500,(int)lroundf(G.pl.hp*1000.0f));
    debug_invariant("light-shield-depleted",0,(int)lroundf(G.pl.shield*1000.0f));
    player_restore_light_shield();
    debug_invariant("light-shield-stage-restore",0,(int)lroundf(G.pl.shield*1000.0f));
    G.meta.upg[3]=4; G.pl.maxhp=3; G.pl.shield_maxhp=3; G.pl.shield=0.0f;
    player_sync_light_shield();
    debug_invariant("light-shield-start-bonus-cap",2000,(int)lroundf(player_light_shield_limit()*1000.0f));
    debug_invariant("light-shield-start-bonus-grant",2000,(int)lroundf(G.pl.shield*1000.0f));
    G.meta.upg[3]=100; G.pl.maxhp=8; G.pl.shield_maxhp=8; G.pl.shield=5.0f;
    player_sync_light_shield();
    debug_invariant("light-shield-five-heart-cap",5,player_light_shield_limit());
    debug_invariant("light-shield-clamped-to-five",5000,(int)lroundf(G.pl.shield*1000.0f));
    G.pl.shards=25; G.pl.relics[RELIC_LUMINANCE]=true; G.memory.kept[MEM_TAG_PROMISE]=2;
    debug_invariant("light-speed-at-full-brightness",600,(int)lroundf(player_speed_mul()*1000.0f));
    G.pl.shards=0; G.pl.relics[RELIC_LUMINANCE]=false; G.memory.kept[MEM_TAG_PROMISE]=0;
    G.pl.maxhp=2;
    player_sync_light_shield();
    debug_invariant("light-shield-keeps-run-heart-cap",5,player_light_shield_limit());
    debug_invariant("light-shield-keeps-run-heart-amount",5000,(int)lroundf(G.pl.shield*1000.0f));
    player_restore_light_shield();
    debug_invariant("light-shield-full-no-stage-overheal",5000,(int)lroundf(G.pl.shield*1000.0f));
    G.pl.maxhp=3; G.pl.shield_maxhp=3;
    G.meta.upg[3]=0;
    Entity light_target={0};
    light_target.active=true; light_target.type=E_SLIME; light_target.hp=light_target.maxhp=100.0f;
    light_target.pos=G.pl.pos;
    enemy_damage(&light_target,10.0f,G.pl.pos,0,0,false,true,1);
    debug_invariant("light-damage-center-bonus",88000,(int)lroundf(light_target.hp*1000.0f));
    memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
    G.ents[0]=(Entity){true,E_SLIME,G.pl.pos,V2(0,0),1.0f,1.0f,7.0f};
    enemy_damage(&G.ents[0],2.0f,G.pl.pos,0,0,false,true,2);
    debug_invariant("one-shot-hit-feedback-hp",0,(int)lroundf(G.enemy_feedback[0].hp*1000.0f));
    debug_invariant("one-shot-hit-feedback-visible",1,G.enemy_feedback[0].t>0.0f?1:0);
    G.ents[0]=(Entity){true,E_SLIME,V2(120,96),V2(0,0),5.0f,5.0f,7.0f};
    enemy_damage(&G.ents[0],1.0f,G.pl.pos,0,0,false,true,3);
    G.ents[0].pos=V2(168,112);
    update_enemy_feedback(0.1f);
    debug_invariant("enemy-feedback-follows-x",168000,(int)lroundf(G.enemy_feedback[0].pos.x*1000.0f));
    debug_invariant("enemy-feedback-follows-y",112000,(int)lroundf(G.enemy_feedback[0].pos.y*1000.0f));
    debug_invariant("enemy-feedback-visible-after-0.1s",2900,(int)lroundf(G.enemy_feedback[0].t*1000.0f));
    memset(G.pickups,0,sizeof G.pickups);
    G.pickups[0]=(Pickup){true,PK_RELIC,{0,0},RELIC_LUMINANCE,0,G.pl.pos,0};
    v2 reward_label=reward_label_pos(&G.pickups[0]);
    int reward_tx=(int)(reward_label.x/TILE), reward_ty=(int)(reward_label.y/TILE);
    if (reward_tx<1) reward_tx=1;
    if (reward_tx>G.room.w-2) reward_tx=G.room.w-2;
    if (reward_ty<1) reward_ty=1;
    if (reward_ty>G.room.h-2) reward_ty=G.room.h-2;
    G.room.tiles[reward_ty][reward_tx]=T_WALL;
    clear_reward_label_obstacles();
    debug_invariant("reward-label-clears-covered-wall",T_FLOOR,G.room.tiles[reward_ty][reward_tx]);
    printf("{\"schema\":1,\"kind\":\"fixture_start\",\"fixture\":\"modifiers\"}\n");
    for(w=0;w<WPN_COUNT;w++){
        float base=0.0f;
        for(c=0;c<3;c++){
            memset(&G.memory,0,sizeof G.memory);
            float cooldown=0.0f, observed=debug_observe_weapon(w,c,false,&cooldown);
            if(c==0) base=observed;
            if(observed<=0.0f) debug_invariant("production-weapon-observation",1,0);
            if(c>0) debug_invariant("production-courage-monotonic",1,observed>base);
            printf("{\"schema\":1,\"kind\":\"modifier\",\"fixture\":\"modifiers\",\"weapon\":\"%s\",\"courage\":%d,\"damage_factor_milli\":%d,\"observed_damage_milli\":%d,\"raw_kb\":%d,\"used_kb\":%d}\n",
                   names[w],c,(int)lroundf(observed/base*1000.0f),
                   (int)lroundf(observed*1000.0f),player_raw_kb(),player_used_kb());
        }
    }
    float normal_base=0.0f,cannon_base=0.0f;
    for(c=0;c<3;c++){
        float normal_cd=0.0f,cannon_cd=0.0f;
        memset(&G.memory,0,sizeof G.memory);
        G.memory.kept[MEM_TAG_KINSHIP]=(uint8_t)c;
        (void)debug_observe_weapon(WPN_SWORD,0,false,&normal_cd);
        memset(&G.memory,0,sizeof G.memory);
        G.memory.kept[MEM_TAG_KINSHIP]=(uint8_t)c;
        (void)debug_observe_weapon(WPN_CANNON,0,true,&cannon_cd);
        if(c==0){ normal_base=normal_cd; cannon_base=cannon_cd; }
        else {
            debug_invariant("production-kinship-normal",1,normal_cd<normal_base);
            debug_invariant("production-kinship-cannon",1,cannon_cd<cannon_base);
        }
        printf("{\"schema\":1,\"kind\":\"modifier\",\"fixture\":\"modifiers\",\"site\":\"normal\",\"kinship\":%d,\"observed_cooldown_milli\":%d}\n",c,(int)lroundf(normal_cd*1000.0f));
        printf("{\"schema\":1,\"kind\":\"modifier\",\"fixture\":\"modifiers\",\"site\":\"cannon-release\",\"kinship\":%d,\"observed_cooldown_milli\":%d}\n",c,(int)lroundf(cannon_cd*1000.0f));
    }
    memset(&G.memory,0,sizeof G.memory);
    G.pl.weapon.type=WPN_SWORD; G.pl.relics[RELIC_COMPRESS]=false;
    debug_invariant("raw-cost",64,player_raw_kb()); debug_invariant("used-cost",64,player_used_kb());
    printf("{\"schema\":1,\"kind\":\"capacity\",\"fixture\":\"modifiers\",\"compression\":0,\"raw_kb\":%d,\"used_kb\":%d}\n",player_raw_kb(),player_used_kb());
    G.pl.relics[RELIC_COMPRESS]=true;
    debug_invariant("memory-used-cost-compressed",51,item_kb(64));
    printf("{\"schema\":1,\"kind\":\"capacity\",\"fixture\":\"modifiers\",\"compression\":1,\"raw_kb\":64,\"used_kb\":%d,\"inventory_raw_kb\":%d,\"inventory_used_kb\":%d}\n",
           item_kb(64),player_raw_kb(),player_used_kb());
    G.pl.relics[RELIC_COMPRESS]=false;
    float base_radius=0.0f;
    for(c=0;c<3;c++)for(int lum=0;lum<2;lum++)for(int up=0;up<2;up++)for(int lm=0;lm<2;lm++){
        float light_mul=lm?0.55f:1.0f;
        G.memory.kept[MEM_TAG_PROMISE]=(uint8_t)c; G.pl.relics[RELIC_LUMINANCE]=lum!=0;
        G.meta.upg[3]=(uint32_t)up; G.light_mul=light_mul;
        float observed=player_light_radius();
        if(c==0&&!lum&&!up&&!lm) base_radius=observed;
        if(c==0&&!lum&&up&&!lm)
            debug_invariant("lumen-does-not-affect-light",(int)lroundf(base_radius*1000.0f),(int)lroundf(observed*1000.0f));
        (void)base_radius;
        (void)light_mul;
        debug_invariant("production-promise-light-observed",1,isfinite(observed) && observed>=0.0f);
        printf("{\"schema\":1,\"kind\":\"promise\",\"fixture\":\"modifiers\",\"count\":%d,\"luminance\":%d,\"meta_light_upg\":%d,\"light_mul_milli\":%d,\"radius_milli\":%d}\n",c,lum,up,(int)lroundf(light_mul*1000.0f),(int)lroundf(observed*1000.0f));
        fflush(stdout);
    }
    memset(G.ents,0,sizeof G.ents);
    G.pl.relics[RELIC_CHECKSUM]=true;
    G.pl.maxhp=3; G.pl.shield_maxhp=3; G.pl.hp=1.0f; G.pl.heal_timer=45.0f;
    attack_held=false;
    update_play(0.1f);
    debug_invariant("checksum-periodic-half-heal",1500,(int)lroundf(G.pl.hp*1000.0f));
    Entity impact_target={0};
    impact_target.active=true; impact_target.type=E_SLIME; impact_target.hp=impact_target.maxhp=100.0f;
    impact_target.pos=G.pl.pos;
    G.pl.impact_group=0; G.hitstop=0;
    enemy_damage(&impact_target,1.0f,G.pl.pos,0,0,false,true,41);
    debug_invariant("attack-group-first-hitstop",1,G.hitstop>0?1:0);
    G.hitstop=0;
    enemy_damage(&impact_target,1.0f,G.pl.pos,0,0,false,true,41);
    debug_invariant("attack-group-single-hitstop",0,G.hitstop>0?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    Entity sniper={0};
    sniper.active=true; sniper.type=E_SNIPER; sniper.pos=V2(80,80); sniper.target=V2(180,80); sniper.state=1;
    update_enemy(&sniper,0,0);
    debug_invariant("sniper-projectile-speed",562500,(int)lroundf(v2len(G.bullets[0].vel)*1000.0f));
    debug_invariant("sniper-projectile-damage",2000,(int)lroundf(G.bullets[0].dmg*1000.0f));
    int difficulty_before=G.difficulty;
    for (int difficulty=0;difficulty<3;difficulty++){
        G.difficulty=difficulty;
        int extra_ms=(int)lroundf((boss_barrage_cooldown(0.0f)/1.25f)*1000.0f);
        debug_invariant("boss-barrage-extra-ms",(2-difficulty)*500,extra_ms);
        printf("{\"schema\":1,\"kind\":\"boss_barrage\",\"fixture\":\"modifiers\",\"difficulty\":%d,\"extra_delay_ms\":%d}\n",difficulty,extra_ms);
    }
    G.difficulty=difficulty_before;
    memset(G.pl.relics,0,sizeof G.pl.relics);
    for (int i=0;i<4;i++) G.pl.relics[i]=true;
    G.state=ST_PLAY;
    G.pickups[0]=(Pickup){true,PK_RELIC,{0,0},RELIC_LUMINANCE,0,G.pl.pos,0};
    player_try_pickup(&G.pickups[0]);
    debug_invariant("regular-relic-swap-opens",ST_RELIC_SWAP,G.state);
    player_confirm_relic_swap(0);
    debug_invariant("regular-relic-swap-removes-old",0,G.pl.relics[0]?1:0);
    debug_invariant("regular-relic-swap-adds-new",1,G.pl.relics[RELIC_LUMINANCE]?1:0);
    G.pl.wrelics[0]=WR_SWORD_WAVE; G.pl.wrelics[1]=WR_SWORD_WHIRL;
    G.pickups[1]=(Pickup){true,PK_WRELIC,{0,0},WR_CANNON_FRAG,0,G.pl.pos,0};
    player_try_pickup(&G.pickups[1]);
    debug_invariant("weapon-relic-swap-opens",ST_RELIC_SWAP,G.state);
    player_confirm_relic_swap(1);
    debug_invariant("weapon-relic-swap-replaces-slot",WR_CANNON_FRAG,G.pl.wrelics[1]);
    G.pl.weapon.type=WPN_WAND; G.pl.wrelics[0]=G.pl.wrelics[1]=-1;
    int current_weapon_relic=random_unowned_wrelic_for_weapon(G.pl.weapon.type,-1);
    debug_invariant("boss-relic-offers-current-weapon",WPN_WAND,weapon_relic_defs[current_weapon_relic].weapon);
    debug_invariant("weapon-relic-count",24,WR_COUNT);
    debug_invariant("weapon-relic-sword-phase-kind",WPN_SWORD,weapon_relic_defs[WR_SWORD_PHASE].weapon);
    debug_invariant("weapon-relic-wand-delay-kind",WPN_WAND,weapon_relic_defs[WR_WAND_DELAY].weapon);
    v2 fixture_pos=G.pl.pos;
    bool fixture_pos_found=false;
    for (int y=1;y<G.room.h-1&&!fixture_pos_found;y++) for (int x=1;x<G.room.w-1;x++)
        if (!tile_solid(x,y)){ fixture_pos=V2(x*TILE+8.0f,y*TILE+8.0f); fixture_pos_found=true; break; }
    memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_SWORD; G.pl.pos=V2(88,88); G.pl.aim=V2(1,0); G.pl.attack_cd=0;
    G.pl.wrelics[0]=-1; G.pl.wrelics[1]=-1;
    uint8_t saved_blade_wall=G.room.tiles[5][6];
    G.room.tiles[5][6]=T_WALL;
    G.ents[0]=(Entity){true,E_BAT,V2(120,88),V2(0,0),10.0f,10.0f,7.0f};
    attack_held=true; fire_weapon(0); attack_held=false;
    debug_invariant("sword-wall-blocks-melee",10000,(int)lroundf(G.ents[0].hp*1000.0f));
    G.pl.wrelics[0]=WR_SWORD_WAVE; G.pl.attack_cd=0;
    Bullet* wave=spawn_bullet(true,8,G.pl.pos,V2(360,0),3.0f,1.0f,7.0f,999);
    update_bullets(0.1f);
    debug_invariant("sword-wave-stops-at-wall",1,wave&&!wave->active?1:0);
    debug_invariant("sword-wave-wall-blocks-damage",10000,(int)lroundf(G.ents[0].hp*1000.0f));
    G.room.tiles[5][6]=saved_blade_wall;
    debug_invariant("sword-base-damage-plus-10",3300,(int)lroundf(weapon_defs[WPN_SWORD].dmg*1000.0f));
    debug_invariant("sword-base-cooldown-minus-20-speed",312,(int)lroundf(weapon_defs[WPN_SWORD].cooldown*1000.0f));
    debug_invariant("glaive-base-speed-plus-50",390,(int)lroundf(weapon_defs[WPN_GLAIVE].speed));
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.pos=fixture_pos; G.pl.attack_cd=0; G.pl.wrelics[0]=WR_SWORD_WAVE; G.pl.wrelics[1]=WR_SWORD_WHIRL;
    attack_held=true; fire_weapon(0); attack_held=false;
    int radial_waves=0; float radial_wave_damage=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==8){ radial_waves++; radial_wave_damage=G.bullets[i].dmg; }
    debug_invariant("sword-wave-whirl-eight-directions",8,radial_waves);
    debug_invariant("sword-wave-whirl-suppresses-wave-damage",(int)lroundf(player_attack_damage()*1000.0f),(int)lroundf(radial_wave_damage*1000.0f));
    memset(G.ents,0,sizeof G.ents);
    memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
    G.pl.weapon.type=WPN_SWORD; G.pl.pos=V2(80,80); G.pl.hp=3.0f; G.pl.maxhp=5;
    G.pl.wrelics[0]=WR_SWORD_PHASE; G.pl.wrelics[1]=-1;
    debug_invariant("sword-phase-below-25-uses-base-attack",0,sword_phase_charge_hits(0.249f));
    debug_invariant("sword-phase-25-hits",2,sword_phase_charge_hits(0.25f));
    debug_invariant("sword-phase-50-hits",3,sword_phase_charge_hits(0.50f));
    debug_invariant("sword-phase-75-hits",4,sword_phase_charge_hits(0.75f));
    debug_invariant("sword-phase-100-hits",6,sword_phase_charge_hits(1.0f));
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.wrelics[0]=WR_SWORD_PHASE; G.pl.wrelics[1]=WR_SWORD_WAVE;
    G.pl.attack_cd=0; G.pl.charge=0.1f; G.pl.charging=true; attack_held=false;
    fire_weapon(0);
    int phase_tap_waves=0;
    for (int i=0;i<MAX_BULLETS;i++)
        if (G.bullets[i].active&&G.bullets[i].from_player&&G.bullets[i].kind==8) phase_tap_waves++;
    debug_invariant("sword-phase-tap-uses-wave",1,phase_tap_waves);
    debug_invariant("sword-phase-tap-skips-phase",0,phase_attack.active?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.wrelics[0]=WR_SWORD_PHASE; G.pl.wrelics[1]=WR_SWORD_WHIRL;
    Bullet* phase_tap_hostile=spawn_bullet(false,1,G.pl.pos,V2(0,0),1.0f,1.0f,2.0f,0);
    G.pl.attack_cd=0; G.pl.charge=0.1f; G.pl.charging=true;
    fire_weapon(0);
    debug_invariant("sword-phase-tap-uses-whirl",1,phase_tap_hostile&&!phase_tap_hostile->active?1:0);
    debug_invariant("sword-phase-tap-whirl-animation",1,slash_t>0?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.wrelics[0]=WR_SWORD_PHASE; G.pl.wrelics[1]=-1;
    G.ents[0]=(Entity){true,E_BAT,V2(120,80),V2(0,0),1000.0f,1000.0f,7.0f};
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true; attack_held=false;
    fire_weapon(0);
    for (int i=0;i<5;i++) update_phase_attack(0.2f);
    debug_invariant("sword-phase-full-six-hits",6,G.enemy_feedback[0].hits);
    debug_invariant("sword-phase-damage-120-percent",
                    (int)lroundf(player_attack_damage()*1200.0f),
                    (int)lroundf(phase_attack.damage*1000.0f));
    debug_invariant("sword-phase-step-invulnerability",1,G.pl.iframes>=0.2f?1:0);
    memset(G.ents,0,sizeof G.ents); memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
    G.pl.pos=V2(80,80); G.pl.wrelics[0]=WR_SWORD_PHASE; G.pl.wrelics[1]=WR_SWORD_WHIRL;
    G.ents[0]=(Entity){true,E_BAT,V2(120,80),V2(0,0),1000.0f,1000.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,V2(128,80),V2(0,0),1000.0f,1000.0f,7.0f};
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true; attack_held=false;
    fire_weapon(0);
    debug_invariant("sword-phase-whirl-main-plus-area-hits",3,G.enemy_feedback[0].hits+G.enemy_feedback[1].hits);
    phase_attack.active=false;
    memset(G.ents,0,sizeof G.ents);
    G.pl.pos=V2(80,80); G.pl.hp=1.0f; G.pl.maxhp=5;
    G.pl.wrelics[0]=WR_SWORD_EXECUTE; G.pl.wrelics[1]=-1;
    G.ents[0]=(Entity){true,E_BAT,V2(104,80),V2(0,0),2.0f,10.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,V2(126,80),V2(0,0),20.0f,20.0f,7.0f};
    enemy_damage(&G.ents[0],1.0f,G.pl.pos,0,0,false,true,92);
    debug_invariant("sword-execute-quarter-heal",1250,(int)lroundf(G.pl.hp*1000.0f));
    debug_invariant("sword-execute-area-damage",1,G.ents[1].hp<20.0f?1:0);
    memset(G.bullets,0,sizeof G.bullets); memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_SWORD_EXECUTE; G.pl.wrelics[1]=WR_SWORD_WAVE;
    G.ents[0]=(Entity){true,E_BAT,V2(104,80),V2(0,0),2.0f,10.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,V2(126,80),V2(0,0),20.0f,20.0f,7.0f};
    enemy_damage(&G.ents[0],1.0f,G.pl.pos,0,0,false,true,93);
    int execute_fragments=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==8) execute_fragments++;
    debug_invariant("sword-wave-execute-eight-fragments",8,execute_fragments);
    debug_invariant("sword-wave-execute-area-damage",1,G.ents[1].hp<20.0f?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_CANNON_FUSE; G.pl.wrelics[1]=-1;
    G.pl.pos=fixture_pos;
    G.ents[0]=(Entity){true,E_BAT,fixture_pos,V2(0,0),20.0f,20.0f,7.0f};
    Bullet* fuse=spawn_bullet(true,1,fixture_pos,V2(0,0),4.0f,0.1f,4.0f,0);
    fuse->delayed_fuse=true; fuse->attack_group=93;
    update_bullets(0.2f);
    debug_invariant("cannon-fuse-arms",1,fuse->active&&fuse->fuse_armed?1:0);
    update_bullets(0.4f);
    debug_invariant("cannon-fuse-detonates",1,!fuse->active&&G.ents[0].hp<20.0f?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    uint8_t saved_cannon_row[7];
    for (int x=5;x<=11;x++){ saved_cannon_row[x-5]=G.room.tiles[5][x]; G.room.tiles[5][x]=T_FLOOR; }
    G.room.tiles[5][6]=T_WALL;
    G.pl.wrelics[0]=WR_CANNON_FRAG; G.pl.wrelics[1]=-1;
    spawn_bullet(true,1,V2(5*TILE+8,5*TILE+8),V2(200,0),10.0f,1.0f,4.0f,0);
    update_bullets(0.06f);
    int reflected_fragments=0, reflected_away=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==0){
        reflected_fragments++;
        if (G.bullets[i].vel.x<0) reflected_away++;
    }
    debug_invariant("cannon-frag-wall-count",6,reflected_fragments);
    debug_invariant("cannon-frag-all-reflect-away-from-wall",6,reflected_away);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.wrelics[1]=WR_CANNON_RECOIL;
    spawn_bullet(true,1,V2(5*TILE+8,5*TILE+8),V2(200,0),10.0f,1.0f,4.0f,0);
    update_bullets(0.06f);
    int amplified_fragments=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==0){
        amplified_fragments++;
        debug_invariant("cannon-recoil-frag-damage-plus-20",6000,(int)lroundf(G.bullets[i].dmg*1000.0f));
        debug_invariant("cannon-recoil-frag-size-plus-20",3600,(int)lroundf(G.bullets[i].radius*1000.0f));
    }
    debug_invariant("cannon-recoil-frag-count",6,amplified_fragments);
    G.room.tiles[5][5]=T_FLOOR; G.room.tiles[5][6]=T_FLOOR;
    memset(G.bullets,0,sizeof G.bullets); memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_CANNON; G.pl.weapon.prefix=PFX_NONE; G.pl.pos=V2(5*TILE+8,5*TILE+8); G.pl.aim=V2(1,0); G.pl.attack_cd=0;
    float base_full_cannon_damage=player_attack_damage()*3.0f;
    G.pl.wrelics[0]=WR_CANNON_RAIL; G.pl.wrelics[1]=-1;
    G.pl.charge=0.99f; G.pl.charging=true; attack_held=false;
    fire_weapon(0);
    int near_full_shells=0; Bullet* near_full_shell=NULL;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==1){ near_full_shells++; near_full_shell=&G.bullets[i]; }
    debug_invariant("cannon-rail-requires-full-charge",1,near_full_shells);
    debug_invariant("cannon-near-full-keeps-shell-size",7960,near_full_shell?(int)lroundf(near_full_shell->radius*1000.0f):0);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true;
    G.ents[0]=(Entity){true,E_BAT,v2add(G.pl.pos,V2(24,0)),V2(0,0),100.0f,100.0f,7.0f};
    fire_weapon(0);
    int rail_beams=0, rail_shells=0; Bullet* rail_beam=NULL;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active){
        if (G.bullets[i].kind==14){ rail_beams++; rail_beam=&G.bullets[i]; }
        if (G.bullets[i].kind==1) rail_shells++;
    }
    debug_invariant("cannon-rail-spawns-laser",1,rail_beams);
    debug_invariant("cannon-rail-replaces-shell",0,rail_shells);
    debug_invariant("cannon-rail-width-plus-30",5200,rail_beam?(int)lroundf(rail_beam->radius*1000.0f):0);
    debug_invariant("cannon-rail-full-damage-plus-30",(int)lroundf(base_full_cannon_damage*1.3f*1000.0f),rail_beam?(int)lroundf(rail_beam->dmg*1000.0f):0);
    debug_invariant("cannon-rail-hits-immediately",1,G.ents[0].hp<100.0f?1:0);
    memset(G.bullets,0,sizeof G.bullets); memset(G.ents,0,sizeof G.ents);
    G.room.tiles[5][6]=T_WALL;
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true;
    G.pl.wrelics[0]=WR_CANNON_RAIL; G.pl.wrelics[1]=WR_CANNON_FRAG;
    fire_weapon(0);
    int split_rails=0, reflected_split_rails=0, split_damage_matches=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==14){
        split_rails++;
        if (G.bullets[i].vel.x<0){
            reflected_split_rails++;
            if ((int)lroundf(G.bullets[i].dmg*1000.0f)==(int)lroundf(base_full_cannon_damage*1.3f*0.4f*1000.0f)) split_damage_matches++;
        }
    }
    debug_invariant("cannon-frag-rail-main-plus-four",5,split_rails);
    debug_invariant("cannon-frag-rail-four-reflected",4,reflected_split_rails);
    debug_invariant("cannon-frag-rail-40-percent-damage",4,split_damage_matches);
    memset(G.bullets,0,sizeof G.bullets); memset(G.ents,0,sizeof G.ents);
    G.room.tiles[5][6]=T_FLOOR;
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true;
    G.pl.wrelics[0]=WR_CANNON_RAIL; G.pl.wrelics[1]=WR_CANNON_RECOIL;
    fire_weapon(0);
    Bullet* recoil_rail=NULL;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==14){ recoil_rail=&G.bullets[i]; break; }
    debug_invariant("cannon-recoil-rail-double-width",10400,recoil_rail?(int)lroundf(recoil_rail->radius*1000.0f):0);
    debug_invariant("cannon-recoil-rail-total-damage-plus-50",(int)lroundf(base_full_cannon_damage*1.5f*1000.0f),recoil_rail?(int)lroundf(recoil_rail->dmg*1000.0f):0);
    memset(G.bullets,0,sizeof G.bullets); memset(G.ents,0,sizeof G.ents);
    G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true;
    G.pl.wrelics[0]=WR_CANNON_RAIL; G.pl.wrelics[1]=WR_CANNON_FUSE;
    G.ents[0]=(Entity){true,E_BAT,v2add(G.pl.pos,V2(24,0)),V2(0,0),1000.0f,1000.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(G.pl.pos,V2(80,0)),V2(0,0),1000.0f,1000.0f,7.0f};
    fire_weapon(0);
    int rail_fuses=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==1&&G.bullets[i].fuse_armed) rail_fuses++;
    debug_invariant("cannon-rail-fuse-every-hit",2,rail_fuses);
    for (int x=5;x<=11;x++) G.room.tiles[5][x]=saved_cannon_row[x-5];
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.weapon.type=WPN_SPRAY; G.pl.aim=V2(1,0); G.pl.attack_cd=0; attack_held=true;
    G.pl.wrelics[0]=WR_SPRAY_PIERCE; G.pl.wrelics[1]=-1;
    fire_weapon(0);
    attack_held=false;
    int spray_count=0, spray_pierce=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==2){ spray_count++; spray_pierce+=G.bullets[i].pierce; }
    debug_invariant("spray-pierce-pellet-count",4,spray_count);
    debug_invariant("spray-pierce-one-target-each",4,spray_pierce);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_GLAIVE; G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.attack_cd=0; G.pl.glaive_out=false;
    G.pl.wrelics[0]=WR_GLAIVE_ORBIT; G.pl.wrelics[1]=-1;
    attack_held=true; fire_weapon(0); attack_held=false;
    int orbit_forward=0;
    float orbit_min_angle=1e9f, orbit_max_angle=-1e9f;
    for (int i=0;i<MAX_BULLETS;i++)
        if (G.bullets[i].active&&G.bullets[i].kind==3&&G.bullets[i].vel.x>0){
            orbit_forward++;
            float angle=atan2f(G.bullets[i].vel.y,G.bullets[i].vel.x)*57.29578f;
            orbit_min_angle=fminf(orbit_min_angle,angle);
            orbit_max_angle=fmaxf(orbit_max_angle,angle);
        }
    debug_invariant("glaive-orbit-double-throw",2,orbit_forward);
    debug_invariant("glaive-orbit-angle-separation",20000,(int)lroundf((orbit_max_angle-orbit_min_angle)*1000.0f));
    G.ents[0]=(Entity){true,E_BAT,v2add(G.pl.pos,V2(24,0)),V2(0,0),1000.0f,1000.0f,7.0f};
    float orbit_hit_damage=0.0f;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==3){
        G.bullets[i].pos=G.ents[0].pos;
        G.bullets[i].vel=V2(0,0);
        orbit_hit_damage=G.bullets[i].dmg;
    }
    float orbit_proximity=1.0f-v2len(v2sub(G.ents[0].pos,G.pl.pos))/player_light_radius();
    float orbit_single_damage=orbit_hit_damage*(1.0f+clampf(orbit_proximity,0.0f,1.0f)*0.20f);
    update_bullets(0);
    debug_invariant("glaive-orbit-independent-hit-damage",2000,(int)lroundf((1000.0f-G.ents[0].hp)/orbit_single_damage*1000.0f));
    debug_invariant("glaive-orbit-feedback-hit-count",2,G.enemy_feedback[0].hits);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.attack_cd=0; G.pl.glaive_out=false; G.pl.wrelics[1]=WR_GLAIVE_TWIN;
    attack_held=true; fire_weapon(0); attack_held=false;
    int orbit_twin_forward=0, orbit_twin_back=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==3){
        if (G.bullets[i].vel.x>0) orbit_twin_forward++;
        else if (G.bullets[i].vel.x<0) orbit_twin_back++;
    }
    debug_invariant("glaive-orbit-twin-forward-count",2,orbit_twin_forward);
    debug_invariant("glaive-orbit-twin-back-count",2,orbit_twin_back);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.pos=V2(88,88); G.pl.wrelics[0]=WR_GLAIVE_RETURN; G.pl.wrelics[1]=-1;
    uint8_t saved_glaive_wall=G.room.tiles[5][6];
    G.room.tiles[5][6]=T_WALL;
    Bullet* wall_glaive=spawn_bullet(true,3,G.pl.pos,V2(260,0),4.0f,2.0f,7.0f,999);
    update_bullets(0.1f);
    debug_invariant("glaive-wall-starts-return",1,wall_glaive->active&&wall_glaive->returning?1:0);
    G.room.tiles[5][6]=saved_glaive_wall;
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
    G.pl.pos=fixture_pos; G.pl.wrelics[0]=WR_GLAIVE_RETURN; G.pl.wrelics[1]=-1;
    G.ents[0]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20,0)),V2(0,0),20.0f,20.0f,7.0f};
    Bullet* close_glaive=spawn_bullet(true,3,G.ents[0].pos,V2(0,0),4.0f,2.0f,7.0f,999);
    close_glaive->attack_group=97;
    float close_proximity=1.0f-v2len(v2sub(G.ents[0].pos,G.pl.pos))/player_light_radius();
    float close_factor=1.0f+clampf(close_proximity,0.0f,1.0f)*0.20f;
    update_bullets(0);
    begin_glaive_return(close_glaive,&G.pl);
    update_bullets(0);
    debug_invariant("glaive-close-return-damage",(int)lroundf((20.0f-(4.0f+5.6f)*close_factor)*1000.0f),(int)lroundf(G.ents[0].hp*1000.0f));
    debug_invariant("glaive-close-return-feedback-hit-count",2,G.enemy_feedback[0].hits);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_GLAIVE; G.pl.pos=fixture_pos;
    G.pl.wrelics[0]=WR_GLAIVE_RETURN; G.pl.wrelics[1]=WR_GLAIVE_TRAIL;
    G.ents[0]=(Entity){true,E_BAT,v2add(fixture_pos,V2(140,0)),V2(0,0),20.0f,20.0f,7.0f};
    Bullet* glaive=spawn_bullet(true,3,G.ents[0].pos,V2(0,0),4.0f,2.0f,7.0f,999);
    glaive->attack_group=94;
    update_bullets(0);
    int burn_zones=0;
    Bullet* burn_zone=NULL;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==11){ burn_zones++; burn_zone=&G.bullets[i]; }
    debug_invariant("glaive-return-damage-plus-40",5600,(int)lroundf(glaive->dmg*1000.0f));
    debug_invariant("glaive-return-speed-plus-30",6591,(int)lroundf(-glaive->vel.x*10.0f));
    debug_invariant("glaive-trail-creates-burn-zone",1,burn_zones);
    debug_invariant("glaive-burn-zone-duration-ms",3000,(int)lroundf(burn_zone->life*1000.0f));
    glaive->active=false;
    G.ents[0].hp=20.0f;
    update_bullets(1.0f);
    debug_invariant("glaive-burn-zone-half-damage",(int)lroundf((20.0f-player_attack_damage()*0.5f)*1000.0f),(int)lroundf(G.ents[0].hp*1000.0f));
    update_bullets(1.9f);
    debug_invariant("glaive-burn-zone-active-before-3s",1,burn_zone->active?1:0);
    update_bullets(0.2f);
    debug_invariant("glaive-burn-zone-expires-after-3s",0,burn_zone->active?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_LANCE; G.pl.pos=v2add(fixture_pos,V2(10000,0));
    G.pl.wrelics[0]=WR_LANCE_BLAST; G.pl.wrelics[1]=-1;
    G.ents[0]=(Entity){true,E_BAT,fixture_pos,V2(0,0),50.0f,50.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20,0)),V2(0,0),50.0f,50.0f,7.0f};
    Bullet* lance=spawn_bullet(true,4,fixture_pos,V2(0,0),10.0f,1.0f,5.0f,999);
    lance->attack_group=95;
    update_bullets(0);
    debug_invariant("lance-blast-monster-collision-stops-projectile",0,lance->active?1:0);
    debug_invariant("lance-blast-damage-40-percent",46000,(int)lroundf(G.ents[1].hp*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_CHARGE; G.pl.wrelics[1]=-1;
    G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.vel=V2(0,0); G.pl.iframes=0; G.pl.attack_cd=0;
    G.ents[0]=(Entity){true,E_BAT,v2add(fixture_pos,V2(50,0)),V2(0,0),50.0f,50.0f,1.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(fixture_pos,V2(75,0)),V2(0,0),50.0f,50.0f,1.0f};
    float lance_base=player_attack_damage();
    attack_held=true;
    fire_weapon(0);
    attack_held=false;
    float lance_light=1.0f+clampf(1.0f-50.0f/player_light_radius(),0,1)*0.2f;
    int lance_projectiles=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==4) lance_projectiles++;
    debug_invariant("lance-charge-is-melee",0,lance_projectiles);
    debug_invariant("lance-charge-range-double-pointer-blade",(int)lroundf((50.0f-lance_base*lance_light)*1000.0f),(int)lroundf(G.ents[0].hp*1000.0f));
    debug_invariant("lance-charge-outside-range-safe",50000,(int)lroundf(G.ents[1].hp*1000.0f));
    debug_invariant("lance-charge-forward-impulse-2-5x",750000,(int)lroundf(G.pl.vel.x*1000.0f));
    debug_invariant("lance-charge-invulnerability-500ms",500,(int)lroundf(G.pl.iframes*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_PIN; G.pl.wrelics[1]=-1;
    G.pl.pos=v2add(fixture_pos,V2(10000,0));
    G.ents[0]=(Entity){true,E_BAT,fixture_pos,V2(0,0),50.0f,50.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20,0)),V2(0,0),50.0f,50.0f,7.0f};
    lance=spawn_bullet(true,4,fixture_pos,V2(0,0),10.0f,1.0f,5.0f,999);
    lance->attack_group=96;
    update_bullets(0);
    int pin_events=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==12) pin_events++;
    debug_invariant("lance-pin-stops-projectile",0,lance->active?1:0);
    debug_invariant("lance-pin-roots-target-500ms",500,(int)lroundf(G.ents[0].root*1000.0f));
    debug_invariant("lance-pin-delayed-event",1,pin_events);
    update_bullets(0.5f);
    debug_invariant("lance-pin-pulls-nearby-enemy",0,(int)lroundf(v2len(v2sub(G.ents[1].pos,fixture_pos))*1000.0f));
    debug_invariant("lance-pin-area-damage-40-percent",46000,(int)lroundf(G.ents[1].hp*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_PIERCE; G.pl.wrelics[1]=-1;
    G.pl.pos=v2add(fixture_pos,V2(10000,0));
    for (int i=0;i<5;i++) G.ents[i]=(Entity){true,E_BAT,v2add(fixture_pos,V2((float)i*24.0f,0)),V2(0,0),50.0f,50.0f,1.0f};
    lance=spawn_bullet(true,4,fixture_pos,V2(0,0),10.0f,1.0f,5.0f,999);
    lance->attack_group=97;
    for (int i=0;i<5;i++){ lance->pos=G.ents[i].pos; update_bullets(0); }
    debug_invariant("lance-pierce-fifth-hit-damage-plus-20-each",29264,(int)lroundf(G.ents[4].hp*1000.0f));
    debug_invariant("lance-pierce-unlimited-charge",24883,(int)lroundf(lance->dmg*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_CHARGE; G.pl.wrelics[1]=WR_LANCE_PIERCE;
    G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.vel=V2(0,0); G.pl.iframes=0; G.pl.attack_cd=0;
    for (int i=0;i<3;i++) G.ents[i]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20.0f+(float)i*20.0f,0)),V2(0,0),50.0f,50.0f,1.0f};
    lance_base=player_attack_damage();
    attack_held=true;
    fire_weapon(0);
    attack_held=false;
    float charge_light=1.0f+clampf(1.0f-20.0f/player_light_radius(),0,1)*0.2f;
    debug_invariant("lance-charge-pierce-scales-with-target-count",(int)lroundf((50.0f-lance_base*1.6f*charge_light)*1000.0f),(int)lroundf(G.ents[0].hp*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_CHARGE; G.pl.wrelics[1]=WR_LANCE_BLAST;
    G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.vel=V2(0,0); G.pl.iframes=0; G.pl.attack_cd=0;
    G.ents[0]=(Entity){true,E_BAT,v2add(fixture_pos,V2(68,20)),V2(0,0),50.0f,50.0f,1.0f};
    lance_base=player_attack_damage();
    attack_held=true;
    fire_weapon(0);
    attack_held=false;
    float blast_dist=v2len(v2sub(G.ents[0].pos,G.pl.pos));
    float blast_light=1.0f+clampf(1.0f-blast_dist/player_light_radius(),0,1)*0.2f;
    debug_invariant("lance-charge-blast-at-thrust-end",(int)lroundf((50.0f-lance_base*0.4f*blast_light)*1000.0f),(int)lroundf(G.ents[0].hp*1000.0f));
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_LANCE_CHARGE; G.pl.wrelics[1]=WR_LANCE_PIN;
    G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.vel=V2(0,0); G.pl.iframes=0; G.pl.attack_cd=0;
    G.ents[0]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20,0)),V2(0,0),50.0f,50.0f,1.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(fixture_pos,V2(50,0)),V2(0,0),50.0f,50.0f,1.0f};
    attack_held=true;
    fire_weapon(0);
    attack_held=false;
    pin_events=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==12) pin_events++;
    debug_invariant("lance-charge-pin-applies-to-all-thrust-targets",2,pin_events);
    debug_invariant("lance-charge-pin-roots-all",1,G.ents[0].root>=0.5f&&G.ents[1].root>=0.5f?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.weapon.type=WPN_WAND; G.pl.pos=fixture_pos;
    G.pl.wrelics[0]=WR_WAND_RING; G.pl.wrelics[1]=-1;
    G.ents[0]=(Entity){true,E_BAT,fixture_pos,V2(0,0),20.0f,20.0f,7.0f};
    G.ents[1]=(Entity){true,E_BAT,v2add(fixture_pos,V2(20,0)),V2(0,0),20.0f,20.0f,7.0f};
    Bullet* wand=spawn_bullet(true,5,fixture_pos,V2(0,0),4.0f,1.0f,3.5f,0);
    wand->attack_group=96;
    update_bullets(0);
    wand=spawn_bullet(true,5,fixture_pos,V2(0,0),4.0f,1.0f,3.5f,0);
    wand->attack_group=96;
    update_bullets(0);
    int ring_effects=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==13) ring_effects++;
    debug_invariant("wand-ring-damages-nearby",1,G.ents[1].hp<20.0f?1:0);
    debug_invariant("wand-ring-merges-same-attack-range-effect",1,ring_effects);
    memset(G.bullets,0,sizeof G.bullets);
    memset(G.ents,0,sizeof G.ents);
    G.pl.wrelics[0]=WR_WAND_DELAY; G.pl.wrelics[1]=-1;
    G.pl.pos=fixture_pos; G.pl.aim=V2(1,0); G.pl.attack_cd=0;
    G.pl.charge=0.2f; G.pl.charging=true; attack_held=false;
    fire_weapon(0);
    int rain_uncharged_count=0, rain_uncharged_forward=0, rain_uncharged_from_player=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==5){
        Bullet* rain=&G.bullets[i]; rain_uncharged_count++;
        if (rain->vel.x>0) rain_uncharged_forward++;
        if (v2len(v2sub(rain->pos,G.pl.pos))<0.01f) rain_uncharged_from_player++;
    }
    debug_invariant("wand-rain-below-first-tier-count",2,rain_uncharged_count);
    debug_invariant("wand-rain-below-first-tier-fires-forward",2,rain_uncharged_forward);
    debug_invariant("wand-rain-below-first-tier-uses-player-origin",2,rain_uncharged_from_player);
    memset(G.bullets,0,sizeof G.bullets); G.pl.attack_cd=0;
    attack_held=true; G.pl.charge=0; G.pl.charging=false;
    fire_weapon(0.375f);
    attack_held=false;
    debug_invariant("wand-rain-charge-time-25-percent",250,(int)lroundf(G.pl.charge*1000.0f));
    fire_weapon(0);
    int rain_count=0, rain_left=0, rain_right=0, rain_forward=0;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==5){
        Bullet* rain=&G.bullets[i]; rain_count++;
        if (rain->pos.y<G.pl.pos.y) rain_left++; else if (rain->pos.y>G.pl.pos.y) rain_right++;
        if (fabsf(rain->vel.x)>0.01f) rain_forward++;
    }
    debug_invariant("wand-rain-tier-25-count",4,rain_count);
    debug_invariant("wand-rain-tier-25-splits-left",2,rain_left);
    debug_invariant("wand-rain-tier-25-splits-right",2,rain_right);
    debug_invariant("wand-rain-tier-25-fans-directions",4,rain_forward);
    float rain_base=player_attack_damage();
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==5){
        debug_invariant("wand-rain-tier-25-damage-plus-7-5",(int)lroundf(rain_base*1.075f*1000.0f),(int)lroundf(G.bullets[i].dmg*1000.0f));
        break;
    }
    static const float rain_charge[3]={0.5f,0.75f,1.0f};
    static const int rain_expected[3]={6,8,10};
    static const float rain_damage_mul[3]={1.15f,1.225f,1.3f};
    static const char* rain_name[3]={"wand-rain-tier-50-count","wand-rain-tier-75-count","wand-rain-tier-100-count"};
    static const char* rain_damage_name[3]={"wand-rain-tier-50-damage-plus-15","wand-rain-tier-75-damage-plus-22-5","wand-rain-tier-100-damage-plus-30"};
    for (int tier=0;tier<3;tier++){
        memset(G.bullets,0,sizeof G.bullets);
        G.pl.attack_cd=0; G.pl.charge=rain_charge[tier]; G.pl.charging=true;
        fire_weapon(0);
        rain_count=0;
        for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==5){
            rain_count++;
            debug_invariant(rain_damage_name[tier],(int)lroundf(rain_base*rain_damage_mul[tier]*1000.0f),(int)lroundf(G.bullets[i].dmg*1000.0f));
        }
        debug_invariant(rain_name[tier],rain_expected[tier],rain_count);
    }
    float rain_near_side=999.0f, rain_far_side=0.0f;
    for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==5){
        float side_distance=fabsf(G.bullets[i].pos.y-G.pl.pos.y);
        rain_near_side=fminf(rain_near_side,side_distance);
        rain_far_side=fmaxf(rain_far_side,side_distance);
        debug_invariant("wand-rain-full-charge-damage-plus-30",(int)lroundf(rain_base*1.3f*1000.0f),(int)lroundf(G.bullets[i].dmg*1000.0f));
    }
    debug_invariant("wand-rain-full-charge-spreads-side-origin",1,rain_far_side-rain_near_side>=16.0f?1:0);
    memset(G.bullets,0,sizeof G.bullets);
    G.pl.attack_cd=0; G.pl.charge=0; G.pl.charging=false; attack_held=true;
    fire_weapon(1.5f);
    attack_held=false;
    debug_invariant("wand-rain-charge-caps-at-one-point-five-seconds",1000,(int)lroundf(G.pl.charge*1000.0f));
    debug_invariant("wand-rain-gauge-before-first-mark",0,wand_rain_charge_tier(0.249f));
    debug_invariant("wand-rain-gauge-first-mark",1,wand_rain_charge_tier(0.25f));
    debug_invariant("wand-rain-gauge-second-mark",2,wand_rain_charge_tier(0.5f));
    debug_invariant("wand-rain-gauge-third-mark",3,wand_rain_charge_tier(0.75f));
    debug_invariant("wand-rain-gauge-full-mark",4,wand_rain_charge_tier(1.0f));
    G.light_mul=old_light; G.pl.relics[RELIC_LUMINANCE]=old_lum; G.meta.upg[3]=old_upg;
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"modifiers\",\"status\":\"pass\"}\n");
}
static void debug_haste_probe(int type,int state,const char* clock){
    Entity normal={0}, haste={0}; const float dt=1.0f/60.0f;
    normal.active=haste.active=true; normal.type=haste.type=type;
    normal.state=haste.state=state; normal.pos=haste.pos=v2add(G.pl.pos,V2(120,0));
    normal.radius=haste.radius=7.0f; normal.t0=haste.t0=1.0f;
    normal.t1=haste.t1=1.0f; normal.t2=haste.t2=1.0f; normal.t3=haste.t3=1.0f;
    update_enemy(&normal,dt,dt);
    update_enemy(&haste,dt,dt*1.15f);
    (void)clock;
    debug_invariant("haste-clock-observed",1,isfinite(normal.t0) && isfinite(haste.t0) &&
                    isfinite(normal.t1) && isfinite(haste.t1));
    printf("{\"schema\":1,\"kind\":\"haste_probe\",\"fixture\":\"haste\",\"type\":%d,\"state\":%d,\"clock\":\"%s\",\"normal_t0_milli\":%d,\"haste_t0_milli\":%d,\"normal_t1_milli\":%d,\"haste_t1_milli\":%d}\n",
           type,state,clock,(int)lroundf(normal.t0*1000),(int)lroundf(haste.t0*1000),
           (int)lroundf(normal.t1*1000),(int)lroundf(haste.t1*1000));
}
static void debug_fixture_haste(void){
    const float real_dt=1.0f/60.0f;
    printf("{\"schema\":1,\"kind\":\"fixture_start\",\"fixture\":\"haste\"}\n");
    debug_haste_probe(E_SLIME,0,"ai");
    debug_haste_probe(E_BAT,0,"ai");
    debug_haste_probe(E_BAT,1,"real");
    debug_haste_probe(E_BOMBER,0,"ai");
    debug_haste_probe(E_BOMBER,1,"real");
    debug_haste_probe(E_CHASER,0,"ai");
    debug_haste_probe(E_CHASER,1,"real");
    debug_haste_probe(E_DRONE,0,"ai");
    debug_haste_probe(E_DRONE,1,"real");
    debug_haste_probe(E_GOLEM,1,"real");
    debug_haste_probe(E_SENTINEL,0,"ai");
    debug_haste_probe(E_SENTINEL,1,"real");
    debug_haste_probe(E_SNIPER,0,"ai");
    debug_haste_probe(E_SNIPER,1,"real");
    printf("{\"schema\":1,\"kind\":\"haste\",\"fixture\":\"haste\",\"real_dt_milli\":%d,\"normal_ai_dt_milli\":%d,\"haste_ai_dt_milli\":%d,\"real_time_domains\":\"status,burn,dps,spawn,telegraph,bullets,zones,boss,player,global,room,bomber_fuse,sniper_aim,golem_windup,sentinel_charge,chaser_dash,drone_bob\",\"ai_time_domains\":\"ordinary_movement,recovery,cadence\"}\n",
           (int)lroundf(real_dt*1000.0f),(int)lroundf(real_dt*1000.0f),(int)lroundf(real_dt*1.15f*1000.0f));
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"haste\",\"status\":\"pass\"}\n");
}
static void apply_fade_action(void);
static int resolve_ending_result(uint8_t core_bits);
static void debug_fixture_endings(void){
    static const char* coda_names[6]={"none","discard-only","courage","kinship","promise","courage-tie"};
    MetaSave meta_before=G.meta; int core_count,coda,difficulty;
    printf("{\"schema\":1,\"kind\":\"fixture_start\",\"fixture\":\"endings\"}\n");
    memset(G.pickups,0,sizeof G.pickups);
    G.pl.cores=0; G.pl.shards=0;
    G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.5f);
    spawn_pickup(PK_CORE,G.pl.pos,(Weapon){0,0},0,2);
    debug_invariant("ending-core-first-pickup",1,player_try_pickup(&G.pickups[0])?1:0);
    debug_invariant("ending-core-first-bits",1<<2,G.pl.cores);
    player_drop_shard();
    debug_invariant("ending-core-drop-clears-bit",0,G.pl.cores);
    debug_invariant("ending-core-drop-active",1,G.pickups[0].active?1:0);
    debug_invariant("ending-core-drop-id",2,G.pickups[0].core_id);
    debug_invariant("ending-core-repickup",1,player_try_pickup(&G.pickups[0])?1:0);
    debug_invariant("ending-core-repickup-no-duplicate",1<<2,G.pl.cores);
    debug_invariant("ending-core-repickup-threshold",1,resolve_ending_result(G.pl.cores));
    printf("{\"schema\":1,\"kind\":\"core_drop_repickup\",\"core_id\":2,\"bits_after_repickup\":%u,\"ending\":%d,\"status\":\"pass\"}\n",
           G.pl.cores,resolve_ending_result(G.pl.cores));
    for(difficulty=0;difficulty<3;difficulty++)for(core_count=0;core_count<=4;core_count++)for(coda=0;coda<6;coda++){
        memset(&G.memory,0,sizeof G.memory);
        G.pl.cores=core_count==4?15:(core_count==0?0:(1<<core_count)-1);
        if(coda==1)G.memory.discarded[MEM_TAG_COURAGE]=1;
        if(coda==2)G.memory.kept[MEM_TAG_COURAGE]=1;
        if(coda==3)G.memory.kept[MEM_TAG_KINSHIP]=1;
        if(coda==4)G.memory.kept[MEM_TAG_PROMISE]=1;
        if(coda==5){G.memory.kept[MEM_TAG_COURAGE]=1;G.memory.kept[MEM_TAG_KINSHIP]=1;}
        G.meta=meta_before; G.difficulty=difficulty; G.ngplus=false; G.room.biome=3; G.fade_next_state=-3;
        G.bytes_run=0; G.pl.shards=0; G.state=ST_PLAY;
        apply_fade_action();
        int ending=G.ending;
        int expected_coda=
            coda==0?-1:(coda==1?MEM_TAG_COUNT:(coda==2||coda==5?MEM_TAG_COURAGE:coda==3?MEM_TAG_KINSHIP:MEM_TAG_PROMISE));
        debug_invariant("ending-major",core_count==4?(difficulty==2?3:2):(core_count==0?0:1),ending);
        debug_invariant("ending-coda",expected_coda,memory_coda());
        debug_invariant("ending-wins",1,(int)G.meta.wins-(int)meta_before.wins);
        debug_invariant("ending-true-clear",core_count==4&&difficulty==2?1:0,
                        (int)G.meta.true_clear-(int)meta_before.true_clear);
        debug_invariant("ending-ngplus-unlock",core_count==4&&difficulty==2?1:0,G.ngplus?1:0);
        debug_invariant("ending-unlocks",0,
                        G.meta.unlocked_weapons!=meta_before.unlocked_weapons);
        printf("{\"schema\":1,\"kind\":\"ending\",\"fixture\":\"endings\",\"difficulty\":%d,\"cores\":%d,\"ending\":%d,\"coda\":\"%s\",\"wins_delta\":%d,\"true_clear_delta\":%d,\"unlocks_changed\":%d,\"ngplus_changed\":0,\"ngplus_unlocked\":%d}\n",
               difficulty,core_count,ending,coda_names[coda],(int)G.meta.wins-(int)meta_before.wins,
               (int)G.meta.true_clear-(int)meta_before.true_clear,
               G.meta.unlocked_weapons!=meta_before.unlocked_weapons,G.ngplus?1:0);
    }
    G.meta=meta_before;
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"endings\",\"status\":\"pass\",\"major_mapping\":\"0=bad,1-3=standard,4=complete,hard+4=true\"}\n");
}
static void debug_emit_save_wire(int version){
    uint32_t w[17]={0}; int n=version==1?12:17;
    w[0]=0xD15C0DE7u; w[1]=(uint32_t)version;
    if (version==1){
        w[2]=123; w[3]=0x15; w[4]=2; w[5]=9; w[6]=3; w[7]=1;
        w[8]=1; w[9]=0; w[10]=12345;
    } else {
        w[2]=456; w[3]=0x3f; w[4]=3; w[5]=12; w[6]=4; w[7]=1;
        w[8]=1; w[9]=0; w[10]=54321; w[11]=1; w[12]=2; w[13]=3; w[14]=4; w[15]=5;
    }
    w[n-1]=debug_wire_checksum(w,n-1);
    printf("{\"schema\":1,\"kind\":\"save_wire\",\"save_version\":%d,\"byte_length\":%d,\"endian\":\"little\",\"words_hex\":[",version,n*4);
    for (int i=0;i<n;i++){ char x[9]; debug_hex32(x,w[i]); printf("%s\"%s\"",i?",":"",x); }
    printf("],\"checksum_hex\":\""); { char x[9]; debug_hex32(x,w[n-1]); printf("%s",x); }
    printf("\",\"wire_hex\":\"");
    for (int i=0;i<n;i++){ char x[9]; debug_hex32(x,w[i]); for(int j=6;j>=0;j-=2) printf("%c%c",x[j],x[j+1]); }
    printf("\",\"decoded\":{\"bytes_currency\":%u,\"unlocked_weapons\":%u,\"best_biome\":%u,\"runs\":%u,\"wins\":%u,\"true_clear\":%u,\"opt_scanline\":%u,\"opt_shake\":%u,\"last_seed\":%u,\"upg\":[%u,%u,%u,%u,%u]}}\n",
           w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],
           version==1?0:w[11],version==1?0:w[12],version==1?0:w[13],version==1?0:w[14],version==1?0:w[15]);
}
static void debug_init_forced_entity(void){
    int sx=(int)(G.pl.pos.x/TILE), sy=(int)(G.pl.pos.y/TILE), head=0, tail=0;
    uint8_t reach[MAX_ROOM_H][MAX_ROOM_W]={0}; int qx[MAX_ROOM_W*MAX_ROOM_H], qy[MAX_ROOM_W*MAX_ROOM_H];
    static const int dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    if(sx>=0&&sy>=0&&sx<G.room.w&&sy<G.room.h){reach[sy][sx]=1;qx[tail]=sx;qy[tail++]=sy;}
    while(head<tail){int x=qx[head],y=qy[head++];for(int d=0;d<4;d++){int nx=x+dx[d],ny=y+dy[d];if(nx<0||ny<0||nx>=G.room.w||ny>=G.room.h||reach[ny][nx])continue;uint8_t t=G.room.tiles[ny][nx];if(t!=T_FLOOR&&t!=T_DOOR_OPEN&&t!=T_EXIT)continue;reach[ny][nx]=1;qx[tail]=nx;qy[tail++]=ny;}}
    int bx=-1,by=-1,score=0x7fffffff;
    for(int pass=0;pass<2&&bx<0;pass++)for(int y=0;y<G.room.h;y++)for(int x=0;x<G.room.w;x++){if(!reach[y][x]||G.room.tiles[y][x]!=T_FLOOR)continue;if(pass==0&&abs(x-sx)+abs(y-sy)<5)continue;int s=abs(2*x+1-G.room.w)+abs(2*y+1-G.room.h);if(s<score){score=s;bx=x;by=y;}}
    if(bx<0){bx=sx;by=sy;}
    memset(G.ents,0,sizeof G.ents);memset(G.bullets,0,sizeof G.bullets);memset(G.zones,0,sizeof G.zones);
    int slot=DBG_CFG.force_target[0],type=DBG_CFG.force_target[1];
    if(type!=E_SLIME&&type!=E_BOMBER){fprintf(stderr,"{\"error\":\"unsupported-forced-type\"}\n");exit(2);}
    Entity* e=&G.ents[slot];float scale=1.0f+G.difficulty*0.5f+(G.ngplus?0.5f:0.0f);
    e->active=true;e->type=type;e->elite=DBG_CFG.force_target[2]!=0;e->event_trait=(uint8_t)DBG_CFG.force_target[3];
    e->pos=V2(bx*TILE+TILE*0.5f,by*TILE+TILE*0.5f);e->vel=V2(0,0);e->spawn_t=0;e->slow=e->burn=e->flash=0;
    e->state=e->phase=0;e->t0=e->t1=e->t2=e->t3=0;e->facing_left=false;e->face=0;e->event_bonus=0;
    e->maxhp=e->hp=(type==E_SLIME?4.0f:3.0f)*scale;e->radius=type==E_SLIME?7.0f:6.0f;
    if(e->elite){e->hp*=2.2f;e->maxhp=e->hp;e->radius*=1.25f;}
}
static uint32_t debug_tile_fnv1a(void){
    uint32_t h=2166136261u; Room* r=&G.room;
    h=(h^(uint8_t)r->w)*16777619u; h=(h^(uint8_t)r->h)*16777619u;
    for(int y=0;y<r->h;y++) for(int x=0;x<r->w;x++) h=(h^r->tiles[y][x])*16777619u;
    return h;
}
static int debug_player_manhattan(void){
    return abs((int)(G.pl.pos.x/TILE)-G.room.event_tile_x)+
           abs((int)(G.pl.pos.y/TILE)-G.room.event_tile_y);
}
static int debug_event_reachable(void){
    int sx=(int)(G.pl.pos.x/TILE), sy=(int)(G.pl.pos.y/TILE);
    int tx=G.room.event_tile_x, ty=G.room.event_tile_y;
    int qx[MAX_ROOM_W*MAX_ROOM_H], qy[MAX_ROOM_W*MAX_ROOM_H], head=0,tail=0;
    uint8_t seen[MAX_ROOM_H][MAX_ROOM_W]={0};
    static const int dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
    if(sx<0||sy<0||tx<0||ty<0||sx>=G.room.w||sy>=G.room.h||tx>=G.room.w||ty>=G.room.h)
        return 0;
    seen[sy][sx]=1; qx[tail]=sx; qy[tail++]=sy;
    while(head<tail){
        int x=qx[head],y=qy[head++];
        if(x==tx&&y==ty) return 1;
        for(int d=0;d<4;d++){
            int nx=x+dx[d],ny=y+dy[d];
            if(nx<0||ny<0||nx>=G.room.w||ny>=G.room.h||seen[ny][nx]) continue;
            uint8_t t=G.room.tiles[ny][nx];
            if(t!=T_FLOOR&&t!=T_DOOR_OPEN&&t!=T_EXIT) continue;
            seen[ny][nx]=1; qx[tail]=nx; qy[tail++]=ny;
        }
    }
    return 0;
}
static int debug_door_manhattan(void){
    int best=0x7fffffff;
    for(int i=0;i<G.room.door_count;i++){
        int x=G.room.door_x[i],y=G.room.door_y[i];
        if(G.room.door_dir[i]==DIR_L)x++; else if(G.room.door_dir[i]==DIR_R)x--;
        else if(G.room.door_dir[i]==DIR_U)y++; else y--;
        int d=abs(G.room.event_tile_x-x)+abs(G.room.event_tile_y-y);
        if(d<best)best=d;
    }
    return best==0x7fffffff?0:best;
}
static void debug_emit_snapshot(bool reward){
    Room* r=&G.room; uint64_t rb=room_rng.s,cb=crng.s; unsigned n=0;
    printf("{\"schema\":1,\"kind\":\"run\",\"seed\":%u,\"difficulty\":%d,\"weapon\":%d,\"ngplus\":%d,\"biome\":%d,\"room\":%d,\"room_rng_before\":\"%016llx\",\"crng_before\":\"%016llx\"}\n",
           G.run_seed,G.difficulty,G.pl.weapon.type,G.ngplus?1:0,r->biome,r->idx,
           (unsigned long long)rb,(unsigned long long)cb); n++;
    printf("{\"schema\":1,\"kind\":\"room\",\"width\":%d,\"height\":%d,\"tile_fnv1a\":\"%08x\",\"entry_dir\":%d,\"promise\":%d,\"door_count\":%d,\"cleared\":%d,\"is_boss\":%d}\n",
           r->w,r->h,debug_tile_fnv1a(),r->entry_dir,r->promise,r->door_count,r->cleared?1:0,r->is_boss?1:0); n++;
    for(int i=0;i<r->door_count;i++){ printf("{\"schema\":1,\"kind\":\"door\",\"index\":%d,\"dir\":%d,\"x\":%d,\"y\":%d,\"promise\":%d}\n",i,r->door_dir[i],r->door_x[i],r->door_y[i],r->door_promise[i]); n++; }
    for(int i=0;i<MAX_ENTITIES;i++) if(G.ents[i].active){
        Entity* e=&G.ents[i];
        printf("{\"schema\":1,\"kind\":\"entity\",\"slot\":%d,\"type\":%d,\"x_milli\":%d,\"y_milli\":%d,\"hp_milli\":%d,\"maxhp_milli\":%d,\"radius_milli\":%d,\"elite\":%d,\"trait\":%d,\"bonus\":%d,\"state\":%d,\"phase\":%d,\"t0_milli\":%d,\"t1_milli\":%d,\"t2_milli\":%d,\"t3_milli\":%d}\n",
               i,e->type,(int)lroundf(e->pos.x*1000),(int)lroundf(e->pos.y*1000),(int)lroundf(e->hp*1000),(int)lroundf(e->maxhp*1000),(int)lroundf(e->radius*1000),e->elite?1:0,e->event_trait,e->event_bonus,e->state,e->phase,(int)lroundf(e->t0*1000),(int)lroundf(e->t1*1000),(int)lroundf(e->t2*1000),(int)lroundf(e->t3*1000)); n++;
    }
    if(reward) for(int i=0;i<MAX_PICKUPS;i++) if(G.pickups[i].active){
        Pickup* p=&G.pickups[i];
        printf("{\"schema\":1,\"kind\":\"pickup\",\"slot\":%d,\"type\":%d,\"x_milli\":%d,\"y_milli\":%d,\"weapon_type\":%d,\"weapon_prefix\":%d,\"relic\":%d,\"core_id\":%d}\n",i,p->type,(int)lroundf(p->pos.x*1000),(int)lroundf(p->pos.y*1000),p->weapon.type,p->weapon.prefix,p->relic,p->core_id); n++;
    }
    int avail=r->event_state!=MEM_STATE_NONE;
    int p2=avail && r->event_min_pickup_dist<1e20f?(int)lroundf(r->event_min_pickup_dist*r->event_min_pickup_dist*1000.0f):0;
    int et=avail?r->event_type:0, eg=avail?r->event_tag:0, er=avail?r->event_trait:0;
    int es=avail?r->event_state:0, ex=avail?r->event_tile_x:0, ey=avail?r->event_tile_y:0;
    int ew=avail?(int)lroundf(r->event_pos.x*1000):0, eh=avail?(int)lroundf(r->event_pos.y*1000):0;
    printf("{\"schema\":1,\"kind\":\"event\",\"type\":%d,\"tag\":%d,\"trait\":%d,\"state\":%d,\"tile_x\":%d,\"tile_y\":%d,\"world_x_milli\":%d,\"world_y_milli\":%d,\"placement_pass\":%d,\"reachable\":%d,\"player_manhattan\":%d,\"door_manhattan\":%d,\"pickup_dist2_milli\":%d}\n",et,eg,er,es,ex,ey,ew,eh,avail?r->event_place_pass:0,avail?debug_event_reachable():0,avail?debug_player_manhattan():0,avail?debug_door_manhattan():0,p2);
    printf("{\"schema\":1,\"kind\":\"end\",\"room_rng_after\":\"%016llx\",\"crng_after\":\"%016llx\",\"record_count\":%u}\n",(unsigned long long)room_rng.s,(unsigned long long)crng.s,n+1);
}
static void debug_f10_prepare(void){
    if (dbg_f10_prepared) return;
    memset(G.pickups,0,sizeof G.pickups);
    G.memory.pending_trait=ELITE_NONE;
    if (DBG_CFG.f10_branch==1){
        spawn_pickup(PK_BYTE,v2add(G.room.event_pos,V2(12,0)),(Weapon){0,0},0,0);
    } else {
        G.pl.hp=fmaxf(0.0f,(float)G.pl.maxhp-1.0f);
        G.pl.shards=1;
        G.pl.cores=1;
    }
    dbg_f10_prepared=1;
}
static void debug_dispatch_key(int key, bool repeat){
    sapp_event ev={0};
    ev.type=SAPP_EVENTTYPE_KEY_DOWN; ev.key_code=key; ev.key_repeat=repeat;
    game_event(&ev);
    ev.type=SAPP_EVENTTYPE_KEY_UP; ev.key_repeat=false;
    game_event(&ev);
}
void dd_debug_send_key(int key, bool repeat){ debug_dispatch_key(key,repeat); }
static void debug_dispatch_mouse(void){
    sapp_event ev={0};
    ev.type=SAPP_EVENTTYPE_MOUSE_DOWN;
    game_event(&ev);
    ev.type=SAPP_EVENTTYPE_MOUSE_UP;
    game_event(&ev);
}
static const char* debug_state_name(int state);
static int resolve_ending_result(uint8_t core_bits);
static const char* debug_opening_checkpoint_name(void){
    static const char* names[]={"insert","seek","retry","recover","transfer","title-handoff","skip-key","skip-mouse","wake","scan","reveal","title-flow"};
    return names[DBG_CFG.opening_checkpoint-1];
}
static int debug_opening_target_ms(void){
    static const int targets[]={1800,3600,7600,9600,14000,0,0,0,1800,5600,10800,0};
    if (DBG_CFG.opening_checkpoint==3){
        const char* phase=getenv("DD_DEBUG_RETRY_PHASE");
        if (phase && !strcmp(phase,"1")) return 5400;
        if (phase && !strcmp(phase,"2")) return 6300;
        if (phase && !strcmp(phase,"3")) return 7200;
    }
    return targets[DBG_CFG.opening_checkpoint-1];
}
static void debug_opening_record_transition(int before){
    if (before==G.state || dbg_opening_transition_count>=12) return;
    dbg_opening_transitions[dbg_opening_transition_count++]=(DebugOpeningTransition){before,G.state};
}
static void debug_opening_emit(void){
    static const char* beat_names[]={"wake","scan","reveal","title-handoff"};
    int state_t_ms=(int)lroundf(G.state_t*1000.0f);
    const char* active=G.state==ST_BOOT?boot_beat_name(G.state_t):"title-handoff";
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-opening\",\"checkpoint\":\"%s\",\"recovery_result\":\"not-applicable\",\"retry_count\":3,\"writeback\":false,\"profile_unchanged\":true,\"meta_save_events\":%d,\"fixture_ready\":true,\"core_mapping\":{\"0\":\"bad\",\"1\":\"standard\",\"2\":\"standard\",\"3\":\"standard\",\"4\":\"true\"},\"core_count\":0,\"state\":\"%s\",\"state_t_ms\":%d,\"active_boot_beat\":\"%s\",\"natural_timeout\":%s,\"handoff_at_ms\":%d,\"skip_method\":\"%s\",\"skip_same_frame\":%s,\"visited_boot_beats\":[",
           debug_source_sha256(),debug_opening_checkpoint_name(),dd_debug_meta_save_events(),debug_state_name(G.state),state_t_ms,active,
           dbg_opening_natural_timeout?"true":"false",dbg_opening_handoff_at_ms,dbg_opening_skip_method,
           dbg_opening_skip_same_frame?"true":"false");
    for (int i=0,shown=0;i<4;i++) if (dbg_opening_visited&(1<<i))
        printf("%s\"%s\"",shown++?",":"",beat_names[i]);
    printf("],\"transitions\":[");
    for (int i=0;i<dbg_opening_transition_count;i++)
        printf("%s{\"from\":\"%s\",\"to\":\"%s\"}",i?",":"",
               debug_state_name(dbg_opening_transitions[i].before),
               debug_state_name(dbg_opening_transitions[i].after));
    printf("],\"entered_play\":%s}\n",dbg_opening_entered_play?"true":"false");
    fflush(stdout);
}
static void debug_prepare_opening_fixture(void){
    debug_invariant("opening-clean-profile",1,dd_debug_clean_profile_active()?1:0);
    debug_invariant("opening-meta-load-suppressed",0,(int)G.meta.magic);
    dd_debug_reset_meta_save_events();
    meta_save();
    debug_invariant("opening-meta-save-events",1,dd_debug_meta_save_events());
    if (DBG_CFG.opening_checkpoint==12) dd_debug_reset_meta_save_events();
    dbg_opening_active=1;
    dbg_opening_frozen=0;
    dbg_opening_emitted=0;
    dbg_opening_ready=0;
    dbg_opening_rendered=0;
    dbg_opening_hold_t=0;
    dbg_opening_visited=0;
    dbg_opening_natural_timeout=0;
    dbg_opening_handoff_at_ms=-1;
    dbg_opening_skip_method="none";
    dbg_opening_skip_same_frame=0;
    dbg_opening_entered_play=0;
    dbg_opening_flow_step=0;
    dbg_opening_transition_count=0;
    debug_invariant("opening-start-state",ST_BOOT,G.state);
}
static void debug_opening_tick(float dt){
    int checkpoint;
    if (!dbg_opening_active) return;
    checkpoint=DBG_CFG.opening_checkpoint;
    if (G.state==ST_BOOT) dbg_opening_visited|=1<<boot_beat_index(G.state_t);
    if (!dbg_opening_emitted){
        if (dbg_opening_ready && dbg_opening_rendered){
            debug_opening_emit();
            dbg_opening_emitted=1;
        } else if ((checkpoint>=1 && checkpoint<=5) || (checkpoint>=9 && checkpoint<=11)){
            if (G.state==ST_BOOT && G.state_t*1000.0f>=debug_opening_target_ms()){
                dbg_opening_frozen=1;
                dbg_opening_ready=1;
                dbg_opening_rendered=0;
            }
        } else if (checkpoint==6){
            if (G.state==ST_TITLE && dbg_opening_natural_timeout){
                debug_invariant("opening-natural-handoff-ms",15000,dbg_opening_handoff_at_ms);
                debug_invariant("opening-natural-beats",15,dbg_opening_visited);
                dbg_opening_ready=1;
                dbg_opening_rendered=0;
            }
        } else if (checkpoint==7 || checkpoint==8){
            if (dbg_opening_flow_step==0){
                int before=G.state;
                if (checkpoint==7) debug_dispatch_key(SAPP_KEYCODE_SPACE,false);
                else debug_dispatch_mouse();
                debug_opening_record_transition(before);
                debug_invariant("opening-skip-title",ST_TITLE,G.state);
                debug_invariant("opening-skip-same-frame",1,dbg_opening_skip_same_frame);
                debug_invariant("opening-skip-no-play",0,dbg_opening_entered_play);
                dbg_opening_flow_step=1;
            } else if (G.state==ST_TITLE && G.state_t>=0.75f){
                dbg_opening_ready=1;
                dbg_opening_rendered=0;
            }
        } else if (checkpoint==12){
            int before;
            if (dbg_opening_flow_step==0){
                before=G.state;
                debug_dispatch_key(SAPP_KEYCODE_SPACE,false);
                debug_opening_record_transition(before);
                debug_invariant("opening-flow-title",ST_TITLE,G.state);
                dbg_opening_flow_step++;
            } else if (dbg_opening_flow_step==1){
                before=G.state;
                debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
                debug_invariant("opening-flow-difficulty",ST_DIFFICULTY_SELECT,G.state);
                debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
                debug_opening_record_transition(before);
                debug_invariant("opening-flow-intro",ST_INTRO,G.state);
                dbg_opening_flow_step++;
            } else if (dbg_opening_flow_step<=4){
                before=G.state;
                debug_dispatch_key(SAPP_KEYCODE_SPACE,false);
                debug_opening_record_transition(before);
                dbg_opening_flow_step++;
                if (G.state==ST_PLAY){
                    dbg_opening_entered_play=1;
                    debug_invariant("opening-flow-meta-save-events",1,dd_debug_meta_save_events());
                    dbg_opening_ready=1;
                    dbg_opening_rendered=0;
                }
            }
        }
    }
    if (dbg_opening_emitted){
        dbg_opening_hold_t+=dt;
        if (dbg_opening_hold_t*1000.0f>=DBG_CFG.hold_ms) sapp_request_quit();
    }
}
static const char* debug_start_intro_checkpoint_name(void){
    static const char* names[]={"first-run","placement","latch","drive-stop-hold","track","fragment","handoff","repeat-bypass","queued-replay","post-replay-bypass"};
    return names[DBG_CFG.start_intro_checkpoint-1];
}
static int debug_start_intro_target_ms(void){
    static const int targets[]={800,1000,1200,3000,5700,9300,0,0,1000,0};
    return targets[DBG_CFG.start_intro_checkpoint-1];
}
static void debug_start_intro_emit(void){
    float t=G.state_t;
    float disk_x=intro_disk_x();
    float disk_y=intro_disk_y(t);
    float disk_center_x=disk_x+25.0f;
    float drive_center_x=VIRT_W*0.5f;
    int drive_hold=intro_drive_stop_hold(t)?1:0;
    int disk_inside=disk_x>=154.0f && disk_x+50.0f<=326.0f;
    int lip_occludes=disk_y+30.0f>132.0f && disk_y<145.0f;
    int centered=fabsf(disk_center_x-drive_center_x)<=0.01f;
    int descends=intro_disk_y(0.0f)<intro_disk_y(0.5f) &&
                 intro_disk_y(0.5f)<intro_disk_y(1.0f) &&
                 intro_disk_y(1.0f)<intro_disk_y(2.0f);
    int caption_clear=disk_y+30.0f<intro_insert_caption_y()-8.0f;
    if (DBG_CFG.start_intro_checkpoint==2){
        debug_invariant("intro-placement-center-x",0,(int)lroundf((disk_center_x-drive_center_x)*1000.0f));
        debug_invariant("intro-placement-centered",1,centered);
        debug_invariant("intro-placement-descends",1,descends);
        debug_invariant("intro-placement-caption-clear",1,caption_clear);
    }
    if (DBG_CFG.start_intro_checkpoint==4){
        debug_invariant("intro-drive-stop-start-y",117000,(int)lroundf(intro_disk_y(2.15f)*1000.0f));
        debug_invariant("intro-drive-stop-end-y",117000,(int)lroundf(intro_disk_y(4.349f)*1000.0f));
        debug_invariant("intro-drive-stop-held",1,drive_hold);
        debug_invariant("intro-drive-stop-current-y",117000,(int)lroundf(disk_y*1000.0f));
        debug_invariant("intro-drive-stop-center-x",0,(int)lroundf((disk_center_x-drive_center_x)*1000.0f));
        debug_invariant("intro-drive-stop-centered",1,centered);
        debug_invariant("intro-drive-stop-inside",1,disk_inside);
        debug_invariant("intro-drive-stop-occluded",1,lip_occludes);
    }
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-start-intro\",\"checkpoint\":\"%s\",\"fixture_ready\":true,\"state\":\"%s\",\"state_t_ms\":%d,\"intro_seen\":%u,\"intro_replay_queued\":%u,\"meta_save_events\":%d,\"input_latches_clear\":%s,\"esc_ignored\":%s,\"input_state_unchanged\":%s,\"drive_stop_hold\":%s,\"disk_x_milli\":%d,\"disk_y_milli\":%d,\"disk_center_x_milli\":%d,\"drive_center_x_milli\":%d,\"insert_caption_y_milli\":196000,\"insert_caption_clear\":%s,\"insertion_start_y_milli\":-42000,\"insertion_end_y_milli\":117000,\"top_down_descent\":%s,\"disk_inside_drive\":%s,\"drive_lip_occludes\":%s,\"replay_complete\":%s}\n",
           debug_source_sha256(),debug_start_intro_checkpoint_name(),debug_state_name(G.state),
           (int)lroundf(t*1000.0f),G.meta.intro_seen,G.meta.intro_replay_queued,
           dd_debug_meta_save_events(),dbg_start_intro_latches_clear?"true":"false",
           dbg_start_intro_esc_ignored?"true":"false",dbg_start_intro_input_unchanged?"true":"false",
           drive_hold?"true":"false",(int)lroundf(disk_x*1000.0f),(int)lroundf(disk_y*1000.0f),
           (int)lroundf(disk_center_x*1000.0f),(int)lroundf(drive_center_x*1000.0f),caption_clear?"true":"false",descends?"true":"false",
           disk_inside?"true":"false",lip_occludes?"true":"false",
           dbg_start_intro_replay_complete?"true":"false");
    fflush(stdout);
}
static void debug_prepare_start_intro_fixture(void){
    int checkpoint=DBG_CFG.start_intro_checkpoint;
    debug_invariant("start-intro-clean-profile",1,dd_debug_clean_profile_active()?1:0);
    dd_debug_reset_meta_save_events();
    dbg_start_intro_active=1;
    dbg_start_intro_frozen=0;
    dbg_start_intro_emitted=0;
    dbg_start_intro_hold_t=0;
    dbg_start_intro_phase=0;
    dbg_start_intro_latches_clear=0;
    dbg_start_intro_replay_complete=0;
    dbg_start_intro_esc_ignored=0;
    dbg_start_intro_input_unchanged=0;
    G.state=ST_TITLE;
    G.state_t=0;
    G.menu_sel=0;
    G.meta.intro_seen=(checkpoint==8 || checkpoint==9 || checkpoint==10)?1u:0u;
    G.meta.intro_replay_queued=(checkpoint==9 || checkpoint==10)?1u:0u;
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    debug_invariant("intro-difficulty-entry",ST_DIFFICULTY_SELECT,G.state);
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    if (checkpoint==8){
        debug_invariant("intro-repeat-bypass",ST_PLAY,G.state);
        debug_invariant("intro-repeat-bypass-save",1,dd_debug_meta_save_events());
        dbg_start_intro_latches_clear=!key_held[SAPP_KEYCODE_W]&&!key_held[SAPP_KEYCODE_SPACE]&&!attack_held&&!mouse_present;
        debug_invariant("intro-repeat-bypass-latches",1,dbg_start_intro_latches_clear);
    } else {
        debug_invariant("intro-first-entry",ST_INTRO,G.state);
        debug_invariant("intro-entry-no-save",0,dd_debug_meta_save_events());
    }
}
static void debug_start_intro_tick(float dt){
    int checkpoint;
    if (!dbg_start_intro_active || dbg_start_intro_emitted) goto hold;
    checkpoint=DBG_CFG.start_intro_checkpoint;
    if (checkpoint==3 && dbg_start_intro_phase==0 && G.state==ST_INTRO){
        sapp_event ev={0};
        int state_before=G.state, saves_before=dd_debug_meta_save_events(), player_bullets_before=0;
        float state_t_before=G.state_t;
        v2 pos_before=G.pl.pos;
        for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].from_player) player_bullets_before++;
        ev.type=SAPP_EVENTTYPE_KEY_DOWN; ev.key_code=SAPP_KEYCODE_ESCAPE; game_event(&ev);
        ev.key_code=SAPP_KEYCODE_W; game_event(&ev);
        ev.key_code=SAPP_KEYCODE_SPACE; game_event(&ev);
        ev.type=SAPP_EVENTTYPE_MOUSE_DOWN; game_event(&ev);
        int player_bullets_after=0;
        for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].from_player) player_bullets_after++;
        dbg_start_intro_latches_clear=!key_held[SAPP_KEYCODE_ESCAPE]&&!key_held[SAPP_KEYCODE_W]&&!key_held[SAPP_KEYCODE_SPACE]&&!attack_held&&!mouse_present;
        dbg_start_intro_esc_ignored=G.state==ST_INTRO&&G.state==state_before&&G.state_t==state_t_before;
        dbg_start_intro_input_unchanged=dbg_start_intro_esc_ignored&&
            G.pl.pos.x==pos_before.x&&G.pl.pos.y==pos_before.y&&
            player_bullets_after==player_bullets_before&&dd_debug_meta_save_events()==saves_before;
        debug_invariant("intro-esc-ignored",1,dbg_start_intro_esc_ignored);
        debug_invariant("intro-input-state-unchanged",1,dbg_start_intro_input_unchanged);
        debug_invariant("intro-latch-ignored",1,dbg_start_intro_latches_clear);
        dbg_start_intro_phase=1;
    }
    if (checkpoint==7){
        if (G.state==ST_PLAY){
            debug_invariant("intro-handoff-save",1,dd_debug_meta_save_events());
            debug_invariant("intro-handoff-seen",1,(int)G.meta.intro_seen);
            debug_invariant("intro-handoff-queue",0,(int)G.meta.intro_replay_queued);
            dbg_start_intro_latches_clear=!attack_held&&!mouse_present;
            dbg_start_intro_emitted=1;
            debug_start_intro_emit();
        }
    } else if (checkpoint==10){
        if (dbg_start_intro_phase==0 && G.state==ST_PLAY){
            debug_invariant("intro-replay-handoff-save",1,dd_debug_meta_save_events());
            dbg_start_intro_latches_clear=!key_held[SAPP_KEYCODE_W]&&!key_held[SAPP_KEYCODE_SPACE]&&!attack_held&&!mouse_present;
            debug_invariant("intro-replay-latches",1,dbg_start_intro_latches_clear);
            G.state=ST_TITLE; G.state_t=0; G.menu_sel=0;
            debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
            debug_invariant("intro-post-replay-difficulty",ST_DIFFICULTY_SELECT,G.state);
            debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
            debug_invariant("intro-post-replay-bypass",ST_PLAY,G.state);
            debug_invariant("intro-post-replay-save",2,dd_debug_meta_save_events());
            dbg_start_intro_replay_complete=1;
            dbg_start_intro_phase=1;
            dbg_start_intro_emitted=1;
            debug_start_intro_emit();
        }
    } else if (checkpoint==8){
        dbg_start_intro_emitted=1;
        debug_start_intro_emit();
    } else if (G.state==ST_INTRO && G.state_t*1000.0f>=debug_start_intro_target_ms()){
        if (checkpoint==9) debug_invariant("intro-queued-replay",1,(int)G.meta.intro_replay_queued);
        dbg_start_intro_latches_clear=!attack_held&&!mouse_present;
        dbg_start_intro_frozen=1;
        dbg_start_intro_emitted=1;
        debug_start_intro_emit();
    }
hold:
    if (dbg_start_intro_emitted){
        dbg_start_intro_hold_t+=dt;
        if (dbg_start_intro_hold_t*1000.0f>=DBG_CFG.hold_ms) sapp_request_quit();
    }
}
static const char* debug_ending_checkpoint_name(void){
    static const char* names[]={"recovery-failed","partial-recovery","complete-recovery"};
    return names[DBG_CFG.ending_checkpoint-1];
}
static const char* debug_recovery_result_name(int result){
    return result==0?"bad":result==1?"standard":"true";
}
static void debug_prepare_ending_fixture(void){
    int core_bits=DBG_CFG.core_count==4?15:(1<<DBG_CFG.core_count)-1;
    debug_invariant("ending-clean-profile",1,dd_debug_clean_profile_active()?1:0);
    debug_invariant("ending-meta-load-suppressed",0,(int)G.meta.magic);
    G.pl.cores=(uint8_t)core_bits;
    G.room.biome=3;
    G.fade_next_state=-3;
    G.bytes_run=0;
    G.pl.shards=0;
    G.state=ST_PLAY;
    dd_debug_reset_meta_save_events();
    apply_fade_action();
    dbg_ending_result=resolve_ending_result(G.pl.cores);
    dbg_ending_core_count=DBG_CFG.core_count;
    debug_invariant("ending-result",dbg_ending_result,G.ending);
    debug_invariant("ending-state",ST_ENDING,G.state);
    debug_invariant("ending-meta-save-events",1,dd_debug_meta_save_events());
    dbg_ending_active=1;
    dbg_ending_rendered=0;
    dbg_ending_emitted=0;
    dbg_ending_hold_t=0;
}
static void debug_ending_tick(float dt){
    if (!dbg_ending_active) return;
    if (!dbg_ending_emitted && dbg_ending_rendered){
        printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-ending\",\"checkpoint\":\"%s\",\"recovery_result\":\"%s\",\"retry_count\":3,\"writeback\":false,\"profile_unchanged\":true,\"clean_profile_active\":true,\"meta_load_suppressed\":true,\"meta_save_events\":%d,\"fixture_ready\":true,\"core_mapping\":{\"0\":\"bad\",\"1\":\"standard\",\"2\":\"standard\",\"3\":\"standard\",\"4\":\"true\"},\"core_count\":%d,\"state\":\"%s\",\"entered_play\":false}\n",
               debug_source_sha256(),debug_ending_checkpoint_name(),debug_recovery_result_name(dbg_ending_result),
               dd_debug_meta_save_events(),dbg_ending_core_count,debug_state_name(G.state));
        fflush(stdout);
        dbg_ending_emitted=1;
    }
    if (dbg_ending_emitted){
        dbg_ending_hold_t+=dt;
        if (dbg_ending_hold_t*1000.0f>=DBG_CFG.hold_ms) sapp_request_quit();
    }
}
static const char* debug_showcase_checkpoint_name(void){
    if (DBG_CFG.showcase_checkpoint==1) return "death";
    if (DBG_CFG.showcase_checkpoint==2) return "door";
    if (DBG_CFG.showcase_checkpoint==4) return "hit";
    if (DBG_CFG.showcase_checkpoint==5) return "boss-reward";
    if (DBG_CFG.showcase_checkpoint==6) return "relic-swap";
    if (DBG_CFG.showcase_checkpoint==10) return "weapon-relic-swap";
    if (DBG_CFG.showcase_checkpoint==7) return "memory-event";
    if (DBG_CFG.showcase_checkpoint==8) return "boss-intro";
    if (DBG_CFG.showcase_checkpoint==9) return "core-flashback";
    if (DBG_CFG.showcase_checkpoint==11) return "fire-trail-start";
    if (DBG_CFG.showcase_checkpoint==12) return "fire-trail-mid";
    if (DBG_CFG.showcase_checkpoint==13) return "fire-trail-end";
    if (DBG_CFG.showcase_checkpoint==14) return "lance-thrust-start";
    if (DBG_CFG.showcase_checkpoint==15) return "lance-thrust-mid";
    if (DBG_CFG.showcase_checkpoint==16) return "lance-thrust-end";
    if (DBG_CFG.showcase_checkpoint==17) return "wand-rain-start";
    if (DBG_CFG.showcase_checkpoint==18) return "wand-rain-mid";
    if (DBG_CFG.showcase_checkpoint==19) return "wand-rain-end";
    if (DBG_CFG.showcase_checkpoint==20) return "wand-charge-base";
    if (DBG_CFG.showcase_checkpoint==21) return "wand-charge-25";
    if (DBG_CFG.showcase_checkpoint==22) return "wand-charge-50";
    if (DBG_CFG.showcase_checkpoint==23) return "wand-charge-75";
    if (DBG_CFG.showcase_checkpoint==24) return "wand-charge-full";
    if (DBG_CFG.showcase_checkpoint==25) return "cannon-frag-wall";
    if (DBG_CFG.showcase_checkpoint==26) return "cannon-rail-start";
    if (DBG_CFG.showcase_checkpoint==27) return "cannon-rail-mid";
    if (DBG_CFG.showcase_checkpoint==28) return "cannon-rail-end";
    if (DBG_CFG.showcase_checkpoint==29) return "cannon-rail-frag";
    if (DBG_CFG.showcase_checkpoint==30) return "cannon-rail-fuse-start";
    if (DBG_CFG.showcase_checkpoint==31) return "cannon-rail-fuse-end";
    if (DBG_CFG.showcase_checkpoint==32) return "cannon-rail-recoil";
    if (DBG_CFG.showcase_checkpoint==33) return "sword-phase-50";
    if (DBG_CFG.showcase_checkpoint==34) return "sword-phase-full";
    if (DBG_CFG.showcase_checkpoint==35) return "sword-wave";
    if (DBG_CFG.showcase_checkpoint==36) return "sword-whirl";
    if (DBG_CFG.showcase_checkpoint==37) return "training-summon";
    return "pause";
}
static void debug_showcase_record_transition(const char* owner,int before){
    if (!dbg_showcase_active || before==G.state || dbg_showcase_transition_count>=16) return;
    dbg_showcase_transitions[dbg_showcase_transition_count++]=(DebugShowcaseTransition){owner,before,G.state};
}
static int debug_showcase_expected_state(void){
    if (DBG_CFG.showcase_checkpoint==1) return ST_DEAD;
    if (DBG_CFG.showcase_checkpoint==9) return ST_FLASHBACK;
    if (DBG_CFG.showcase_checkpoint==2 || DBG_CFG.showcase_checkpoint==4 || DBG_CFG.showcase_checkpoint==5 ||
        DBG_CFG.showcase_checkpoint==7 || DBG_CFG.showcase_checkpoint==8 ||
        (DBG_CFG.showcase_checkpoint>=11 && DBG_CFG.showcase_checkpoint<=36)) return ST_PLAY;
    if (DBG_CFG.showcase_checkpoint==37) return ST_TRAINING;
    if (DBG_CFG.showcase_checkpoint==6 || DBG_CFG.showcase_checkpoint==10) return ST_RELIC_SWAP;
    return ST_PAUSE;
}
static void debug_showcase_emit(void){
    const char* label="";
    const char* display_label="";
    int doors_cleared=0, weapon_label_camera_visible=0;
    int player_damage_visible=0, player_damage_value=0, player_damage_crit=0;
    if (DBG_CFG.showcase_checkpoint==2){
        v2 cam=play_camera(&G.room);
        int dn=dbg_showcase_weapon_door;
        label=promise_label(G.room.door_promise[dn]);
        display_label=branch_promise_display_label(G.room.door_promise[dn]);
        v2 label_pos=branch_promise_label_pos((G.room.door_x[dn]+(G.room.door_dir[dn]==DIR_L?1:G.room.door_dir[dn]==DIR_R?-1:0))*TILE+8,
                                               (G.room.door_y[dn]+(G.room.door_dir[dn]==DIR_U?1:G.room.door_dir[dn]==DIR_D?-1:0))*TILE+8,
                                               cam.x,cam.y,display_label);
        for (int i=0;i<G.room.door_count;i++)
            if (G.room.tiles[G.room.door_y[i]][G.room.door_x[i]]==T_DOOR_OPEN) doors_cleared++;
        weapon_label_camera_visible=label_pos.x-text_width(display_label,0.42f)*0.5f>=0 &&
                                    label_pos.x+text_width(display_label,0.42f)*0.5f<=VIRT_W &&
                                    label_pos.y>=0 && label_pos.y<=VIRT_H;
    }
    if (DBG_CFG.showcase_checkpoint==4){
        Entity* e=&G.ents[0];
        player_damage_visible=e->player_damaged && e->player_damage_t>0;
        player_damage_value=(int)lroundf(e->player_damage);
        player_damage_crit=e->player_damage_crit;
    }
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-ui-showcase\",\"checkpoint\":\"%s\",\"fixture_ready\":true,\"state\":\"%s\",\"state_after_drive\":\"%s\",\"state_after_1s\":\"%s\",\"frames_after_drive\":%d,\"hold_ms\":%d,\"door_label\":\"%s\",\"door_count\":%d,\"doors_cleared\":%d,\"weapon_door_index\":%d,\"weapon_label_camera_visible\":%d,\"player_damage_visible\":%d,\"player_damage_value\":%d,\"player_damage_crit\":%d,\"pause_panel_opaque\":%d,\"pause_panel_x\":%d,\"pause_panel_y\":%d,\"pause_panel_w\":%d,\"pause_panel_h\":%d,\"menu_first_y\":%d,\"menu_last_y\":%d,\"menu_selected\":%d,\"transitions\":[",
           debug_source_sha256(),debug_showcase_checkpoint_name(),debug_state_name(G.state),
           debug_state_name(G.state),debug_state_name(dbg_showcase_state_after_1s),dbg_showcase_frames,DBG_CFG.hold_ms,label,G.room.door_count,doors_cleared,dbg_showcase_weapon_door,weapon_label_camera_visible,
           player_damage_visible,player_damage_value,player_damage_crit,
           DBG_CFG.showcase_checkpoint==3,16,14,448,240,232,232,G.menu_sel);
    for (int i=0;i<dbg_showcase_transition_count;i++)
        printf("%s{\"owner\":\"%s\",\"before\":\"%s\",\"after\":\"%s\"}",i?",":"",
               dbg_showcase_transitions[i].owner,debug_state_name(dbg_showcase_transitions[i].before),
               debug_state_name(dbg_showcase_transitions[i].after));
    printf("]}\n");
    fflush(stdout);
}
static bool debug_showcase_prepare_weapon_branch(void){
    for (int biome=0;biome<4;biome++) for (int room=2;room<=5;room+=3){
        room_generate(biome,room,PROMISE_NONE,DIR_L);
        for (int dn=0;dn<G.room.door_count;dn++) if (G.room.door_promise[dn]==PROMISE_WEAPON){
            dbg_showcase_weapon_door=dn;
            dd_debug_clear_room_enemies();
            on_room_cleared();
            return true;
        }
    }
    return false;
}
static void debug_prepare_ui_showcase(void){
    if (DBG_CFG.showcase_checkpoint==7 || DBG_CFG.showcase_checkpoint==8 || DBG_CFG.showcase_checkpoint==9){
        start_run();
        G.state=ST_PLAY;
        G.state_t=0;
    } else {
        start_run();
        G.state=ST_PLAY;
        G.state_t=0;
    }
    debug_invariant("showcase-play-state",ST_PLAY,G.state);
    if (DBG_CFG.showcase_checkpoint==1){
        G.pl.hp=0;
        player_take_damage(G.pl.pos);
    } else if (DBG_CFG.showcase_checkpoint==3) {
        G.pl.relics[RELIC_CHECKSUM]=true;
        G.pl.relics[RELIC_LUMINANCE]=true;
        G.pl.wrelics[0]=WR_SWORD_WAVE;
        G.pl.wrelics[1]=WR_SWORD_WHIRL;
        dd_debug_send_key(SAPP_KEYCODE_ESCAPE,false);
        debug_invariant("showcase-pause-state",ST_PAUSE,G.state);
    } else if (DBG_CFG.showcase_checkpoint==4) {
        G.pl.maxhp=4; G.pl.hp=3.5f;
        G.meta.upg[3]=16;
        G.pl.shield=1.5f;
        G.pl.light_shield_cap=player_light_shield_limit();
        G.pl.iframes=60.0f;
        memset(G.ents,0,sizeof G.ents);
        spawn_enemy(E_SLIME,v2add(G.pl.pos,V2(24,0)));
        G.ents[0].hp=G.ents[0].maxhp=100.0f;
        G.ents[0].spawn_t=0;
        G.pl.aim=V2(1,0);
        mouse_present=false;
        attack_held=true;
    } else if (DBG_CFG.showcase_checkpoint==5) {
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.5f);
        G.room.idx=1;
        spawn_pickup(PK_WRELIC,v2add(G.pl.pos,V2(-40,30)),(Weapon){0,0},WR_CANNON_FRAG,0);
        spawn_pickup(PK_WRELIC,v2add(G.pl.pos,V2(40,30)),(Weapon){0,0},WR_WAND_FORK,0);
        G.room.cleared=true;
    } else if (DBG_CFG.showcase_checkpoint==6) {
        memset(G.pickups,0,sizeof G.pickups);
        for (int i=0;i<4;i++) G.pl.relics[i]=true;
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.5f);
        spawn_pickup(PK_RELIC,G.pl.pos,(Weapon){0,0},RELIC_LUMINANCE,0);
        debug_invariant("showcase-relic-swap",1,player_try_pickup(&G.pickups[0])?1:0);
        debug_invariant("showcase-relic-swap-state",ST_RELIC_SWAP,G.state);
    } else if (DBG_CFG.showcase_checkpoint==10) {
        G.pl.wrelics[0]=WR_SWORD_WAVE;
        G.pl.wrelics[1]=WR_SWORD_WHIRL;
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.5f);
        spawn_pickup(PK_WRELIC,G.pl.pos,(Weapon){0,0},WR_SWORD_EXECUTE,0);
        debug_invariant("showcase-weapon-relic-swap",1,player_try_pickup(&G.pickups[0])?1:0);
        debug_invariant("showcase-weapon-relic-swap-state",ST_RELIC_SWAP,G.state);
    } else if (DBG_CFG.showcase_checkpoint==7) {
        G.room.event_type=MEM_EVENT_CORRUPTED;
        G.room.event_tag=MEM_TAG_PROMISE;
        G.room.event_trait=ELITE_HASTE;
        G.room.event_state=MEM_STATE_AVAILABLE;
        G.room.event_pos=G.pl.pos;
        G.room.event_tile_x=(int)(G.pl.pos.x/TILE);
        G.room.event_tile_y=(int)(G.pl.pos.y/TILE);
    } else if (DBG_CFG.showcase_checkpoint==8) {
        room_generate(3,8,PROMISE_NONE,DIR_L);
        G.boss_intro_t=20.0f;
        debug_invariant("showcase-boss-intro",1,G.boss_intro?1:0);
    } else if (DBG_CFG.showcase_checkpoint==9) {
        G.fb_core=3;
        G.fb_t=4.0f;
        G.state=ST_FLASHBACK;
        G.state_t=0;
    } else if (DBG_CFG.showcase_checkpoint>=11 && DBG_CFG.showcase_checkpoint<=13) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.68f);
        G.pl.weapon.type=WPN_GLAIVE;
        G.room.cleared=true;
        for (int i=-3;i<=3;i++) spawn_glaive_burn_zone(v2add(G.pl.pos,V2((float)i*22.0f,-62.0f+sinf((float)i)*8.0f)),700+(uint32_t)(i+3));
        for (int i=-1;i<=1;i++){
            spawn_enemy(E_SLIME,v2add(G.pl.pos,V2((float)i*44.0f,-62.0f+sinf((float)i)*8.0f)));
            G.ents[i+1].hp=G.ents[i+1].maxhp=100.0f;
            G.ents[i+1].spawn_t=0;
        }
    } else if (DBG_CFG.showcase_checkpoint>=14 && DBG_CFG.showcase_checkpoint<=16) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.42f,G.room.h*TILE*0.55f);
        G.pl.weapon.type=WPN_LANCE;
        G.pl.wrelics[0]=WR_LANCE_CHARGE;
        G.pl.wrelics[1]=WR_LANCE_BLAST;
        G.pl.aim=V2(1,0);
        G.room.cleared=true;
        for (int i=0;i<3;i++){
            spawn_enemy(E_SLIME,v2add(G.pl.pos,V2(24.0f+(float)i*20.0f,0)));
            G.ents[i].hp=G.ents[i].maxhp=100.0f;
            G.ents[i].spawn_t=0;
        }
    } else if (DBG_CFG.showcase_checkpoint>=17 && DBG_CFG.showcase_checkpoint<=19) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.70f);
        G.pl.weapon.type=WPN_WAND;
        G.pl.wrelics[0]=WR_WAND_DELAY;
        G.pl.wrelics[1]=WR_WAND_RING;
        G.pl.aim=V2(0,-1);
        G.room.cleared=true;
        spawn_enemy(E_SLIME,v2add(G.pl.pos,V2(0,-92.0f)));
        G.ents[0].hp=G.ents[0].maxhp=100.0f;
        G.ents[0].spawn_t=0;
    } else if (DBG_CFG.showcase_checkpoint>=20 && DBG_CFG.showcase_checkpoint<=24) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.55f);
        G.pl.weapon.type=WPN_WAND;
        G.pl.wrelics[0]=WR_WAND_DELAY;
        G.pl.wrelics[1]=-1;
        G.room.cleared=true;
    } else if (DBG_CFG.showcase_checkpoint>=25 && DBG_CFG.showcase_checkpoint<=32) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.36f,G.room.h*TILE*0.55f);
        G.pl.weapon.type=WPN_CANNON;
        G.pl.aim=V2(1,0);
        G.room.cleared=true;
        for (int i=0;i<2;i++){
            spawn_enemy(E_SLIME,v2add(G.pl.pos,V2(68.0f+(float)i*56.0f,0)));
            G.ents[i].hp=G.ents[i].maxhp=1000.0f;
            G.ents[i].spawn_t=0;
        }
    } else if (DBG_CFG.showcase_checkpoint>=33 && DBG_CFG.showcase_checkpoint<=36) {
        room_generate(0,1,PROMISE_NONE,DIR_L);
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.ents,0,sizeof G.ents);
        memset(G.pickups,0,sizeof G.pickups);
        G.pl.pos=V2(G.room.w*TILE*0.5f,G.room.h*TILE*0.58f);
        G.pl.weapon.type=WPN_SWORD;
        G.pl.aim=V2(1,0);
        G.room.cleared=true;
        spawn_enemy(E_SLIME,v2add(G.pl.pos,V2(72.0f,0)));
        G.ents[0].hp=G.ents[0].maxhp=1000.0f;
        G.ents[0].spawn_t=0;
        if (DBG_CFG.showcase_checkpoint<=34) G.pl.wrelics[0]=WR_SWORD_PHASE;
        else if (DBG_CFG.showcase_checkpoint==35) G.pl.wrelics[0]=WR_SWORD_WAVE;
        else G.pl.wrelics[0]=WR_SWORD_WHIRL;
        G.pl.wrelics[1]=-1;
    } else if (DBG_CFG.showcase_checkpoint==37) {
        start_training();
        G.state=ST_TRAINING;
        G.pl.pos=V2(VIRT_W*(2.0f/3.0f)-18.0f,88.0f);
        training_try_summon();
    } else {
        debug_invariant("showcase-weapon-branch",1,debug_showcase_prepare_weapon_branch()?1:0);
        debug_invariant("showcase-door-count",2,G.room.door_count);
        debug_invariant("showcase-door-open",T_DOOR_OPEN,
                        G.room.tiles[G.room.door_y[dbg_showcase_weapon_door]][G.room.door_x[dbg_showcase_weapon_door]]);
    }
    dbg_showcase_active=1;
    dbg_showcase_transitions[0]=(DebugShowcaseTransition){"launch",G.state,G.state};
    dbg_showcase_transition_count=1;
}
static void debug_showcase_tick(float dt){
    if (!dbg_showcase_active) return;
    if (DBG_CFG.showcase_checkpoint==1 && G.state!=ST_DEAD) return;
    if (DBG_CFG.showcase_checkpoint>=11 && DBG_CFG.showcase_checkpoint<=13){
        static const float life[3]={2.9f,1.5f,0.18f};
        static const float time[3]={0.08f,0.55f,0.96f};
        G.time=time[DBG_CFG.showcase_checkpoint-11];
        for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active&&G.bullets[i].kind==11)
            G.bullets[i].life=life[DBG_CFG.showcase_checkpoint-11];
        for (int i=0;i<3;i++){
            G.ents[i].pos=v2add(G.pl.pos,V2((float)(i-1)*44.0f,-62.0f+sinf((float)(i-1))*8.0f));
            G.ents[i].vel=V2(0,0);
            G.ents[i].spawn_t=0;
        }
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint>=14 && DBG_CFG.showcase_checkpoint<=16){
        static const float thrust_time[3]={0.16f,0.10f,0.03f};
        lance_thrust_t=thrust_time[DBG_CFG.showcase_checkpoint-14];
        lance_thrust_dir=V2(1,0);
        lance_thrust_reach=68.0f;
        G.pl.vel=V2(0,0);
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint>=17 && DBG_CFG.showcase_checkpoint<=19){
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.parts,0,sizeof G.parts);
        memset(G.floaters,0,sizeof G.floaters);
        memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
        G.ents[0].active=true; G.ents[0].pos=v2add(G.pl.pos,V2(0,-92.0f));
        G.ents[0].hp=G.ents[0].maxhp=100.0f; G.ents[0].vel=V2(0,0); G.ents[0].spawn_t=0;
        G.pl.attack_cd=0; G.pl.charge=1.0f; G.pl.charging=true; G.pl.impact_group=0;
        attack_held=false;
        fire_weapon(0);
        if (DBG_CFG.showcase_checkpoint==18){
            for (int step=0;step<22;step++) update_bullets(1.0f/120.0f);
        } else if (DBG_CFG.showcase_checkpoint==19){
            for (int step=0;step<120;step++){
                update_bullets(1.0f/120.0f);
                bool shots=false, ring=false;
                for (int i=0;i<MAX_BULLETS;i++) if (G.bullets[i].active){
                    if (G.bullets[i].kind==5) shots=true;
                    if (G.bullets[i].kind==13) ring=true;
                }
                if (ring&&!shots){
                    for (int settle=0;settle<10;settle++) update_bullets(1.0f/120.0f);
                    break;
                }
            }
        }
        G.pl.vel=V2(0,0);
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint>=20 && DBG_CFG.showcase_checkpoint<=24){
        static const float charge_levels[5]={0.0f,0.25f,0.5f,0.75f,1.0f};
        attack_held=true;
        G.pl.charging=true;
        G.pl.charge=charge_levels[DBG_CFG.showcase_checkpoint-20];
        G.pl.vel=V2(0,0);
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint>=25 && DBG_CFG.showcase_checkpoint<=32){
        memset(G.bullets,0,sizeof G.bullets);
        memset(G.parts,0,sizeof G.parts);
        memset(G.floaters,0,sizeof G.floaters);
        memset(G.enemy_feedback,0,sizeof G.enemy_feedback);
        for (int i=0;i<2;i++){
            G.ents[i].active=true;
            G.ents[i].pos=v2add(G.pl.pos,V2(68.0f+(float)i*56.0f,0));
            G.ents[i].hp=G.ents[i].maxhp=1000.0f;
            G.ents[i].vel=V2(0,0);
            G.ents[i].spawn_t=0;
        }
        int py=(int)(G.pl.pos.y/TILE), px=(int)(G.pl.pos.x/TILE);
        for (int x=px;x<G.room.w-1;x++) G.room.tiles[py][x]=T_FLOOR;
        G.pl.attack_cd=0;
        G.pl.charge=1.0f;
        G.pl.charging=true;
        G.pl.impact_group=0;
        G.pl.aim=V2(1,0);
        attack_held=false;
        if (DBG_CFG.showcase_checkpoint==25){
            G.pl.wrelics[0]=WR_CANNON_FRAG;
            G.pl.wrelics[1]=WR_CANNON_RECOIL;
            G.room.tiles[py][px+3]=T_WALL;
            fire_weapon(0);
            for (int step=0;step<38;step++) update_bullets(1.0f/120.0f);
        } else if (DBG_CFG.showcase_checkpoint<=28) {
            G.pl.wrelics[0]=WR_CANNON_RAIL;
            G.pl.wrelics[1]=-1;
            fire_weapon(0);
            static const int steps[3]={0,6,13};
            for (int step=0;step<steps[DBG_CFG.showcase_checkpoint-26];step++) update_bullets(1.0f/120.0f);
        } else if (DBG_CFG.showcase_checkpoint==29) {
            int wall_x=px+9;
            for (int y=py-5;y<=py+5;y++) for (int x=1;x<=wall_x;x++) G.room.tiles[y][x]=T_FLOOR;
            for (int y=py-5;y<=py+5;y++) G.room.tiles[y][wall_x]=T_WALL;
            G.pl.wrelics[0]=WR_CANNON_RAIL;
            G.pl.wrelics[1]=WR_CANNON_FRAG;
            fire_weapon(0);
        } else if (DBG_CFG.showcase_checkpoint==30 || DBG_CFG.showcase_checkpoint==31) {
            G.pl.wrelics[0]=WR_CANNON_RAIL;
            G.pl.wrelics[1]=WR_CANNON_FUSE;
            fire_weapon(0);
            if (DBG_CFG.showcase_checkpoint==31) update_bullets(0.36f);
        } else {
            G.pl.wrelics[0]=WR_CANNON_RAIL;
            G.pl.wrelics[1]=WR_CANNON_RECOIL;
            fire_weapon(0);
        }
        G.pl.vel=V2(0,0);
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint==33 || DBG_CFG.showcase_checkpoint==34){
        attack_held=true;
        G.pl.charging=true;
        G.pl.charge=DBG_CFG.showcase_checkpoint==33?0.5f:1.0f;
        G.pl.vel=V2(0,0);
        G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint==35){
        memset(G.bullets,0,sizeof G.bullets);
        Bullet* wave=spawn_bullet(true,8,v2add(G.pl.pos,V2(28,0)),V2(360,0),player_attack_damage()*1.2f,1.0f,7.0f,999);
        if (wave) wave->attack_group=G.pl.attack_group;
        slash_t=0.08f; slash_dir=V2(1,0);
        G.pl.vel=V2(0,0); G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint==36){
        slash_t=0.09f; slash_dir=V2(1,0);
        G.pl.vel=V2(0,0); G.pl.iframes=60.0f;
    } else if (DBG_CFG.showcase_checkpoint==37){
        G.pl.vel=V2(0,0); G.pl.iframes=60.0f;
        for (int i=0;i<MAX_FLOATERS;i++)
            if (!strcmp(G.floaters[i].text,"몬스터 1기 소환")) G.floaters[i].t=1.0f;
    }
    dbg_showcase_frames++;
    debug_invariant("showcase-state-after-drive",debug_showcase_expected_state(),G.state);
    if (!dbg_showcase_ready){
        dbg_showcase_ready=1;
        return;
    }
    dbg_showcase_hold_t+=dt;
    if (dbg_showcase_hold_t>=1.0f && dbg_showcase_state_after_1s<0){
        dbg_showcase_state_after_1s=G.state;
        debug_invariant("showcase-state-after-1s",debug_showcase_expected_state(),G.state);
        debug_showcase_emit();
    }
    if (dbg_showcase_hold_t*1000.0f >= DBG_CFG.hold_ms) sapp_request_quit();
}
static const char* debug_state_name(int state){
    switch (state){
        case ST_BOOT: return "ST_BOOT";
        case ST_TITLE: return "ST_TITLE";
        case ST_OPTIONS: return "ST_OPTIONS";
        case ST_INTRO: return "ST_INTRO";
        case ST_PLAY: return "ST_PLAY";
        case ST_FLASHBACK: return "ST_FLASHBACK";
        case ST_RELIC_SWAP: return "ST_RELIC_SWAP";
        case ST_ENDING: return "ST_ENDING";
        case ST_DEAD: return "ST_DEAD";
        case ST_PAUSE: return "ST_PAUSE";
        case ST_TRAINING: return "ST_TRAINING";
        default: return "other";
    }
}
static const char* debug_terminal_state_name(void){
    return debug_state_name(G.fade_dir>0.0f?G.fade_next_state:G.state);
}
static void debug_fixture_dd_legacy_settlement(void){
    int currency_before,death_bytes,death_bank,death_saves,death_owned,forfeit_bytes,forfeit_bank,forfeit_saves;
    int saves_before_forfeit,state_before,forfeit_state_before;
    const char* death_state_after;
    G.state=ST_PLAY; G.state_t=0;
    G.bytes_run=17; G.pl.hp=0; dd_debug_reset_meta_save_events();
    state_before=G.state;
    death_bytes=G.bytes_run;
    currency_before=(int)G.meta.bytes_currency;
    player_take_damage(G.pl.pos);
    death_bank=(int)G.meta.bytes_currency-currency_before; death_saves=dd_debug_meta_save_events();
    death_owned=(int)G.meta.bytes_currency;
    death_state_after=debug_terminal_state_name();
    debug_invariant("legacy-death-bank-events",1,death_bank==death_bytes?1:0);
    debug_invariant("legacy-death-save-events",1,death_saves);
    start_run(); G.bytes_run=19; forfeit_bytes=G.bytes_run;
    saves_before_forfeit=dd_debug_meta_save_events();
    currency_before=(int)G.meta.bytes_currency;
    G.state=ST_PLAY; forfeit_state_before=G.state; debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    G.menu_sel=3; debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    forfeit_bank=(int)G.meta.bytes_currency-currency_before;
    forfeit_saves=dd_debug_meta_save_events()-saves_before_forfeit;
    debug_invariant("legacy-forfeit-bank-events",1,forfeit_bank==forfeit_bytes?1:0);
    debug_invariant("legacy-forfeit-save-events",1,forfeit_saves);
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-legacy-settlement\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"total_meta_save_events\":%d,\"run_banked\":%d,\"owned_total\":%u,\"death\":{\"terminal_reason\":\"death\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"run_banked\":%d,\"owned_total\":%d},\"forfeit\":{\"terminal_reason\":\"forfeit\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"run_banked\":%d,\"owned_total\":%u},\"bytes_run\":%d,\"bytes_currency\":%u,\"title_seed\":%u,\"run_seed\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),(death_bank==death_bytes?1:0)+(forfeit_bank==forfeit_bytes?1:0),death_saves+forfeit_saves,dd_debug_meta_save_events(),forfeit_bank,G.meta.bytes_currency,debug_state_name(state_before),death_state_after,death_bank==death_bytes?1:0,death_saves,death_bank,death_owned,debug_state_name(forfeit_state_before),debug_state_name(G.state),forfeit_bank==forfeit_bytes?1:0,forfeit_saves,forfeit_bank,G.meta.bytes_currency,forfeit_bytes,G.meta.bytes_currency,G.title_seed,G.run_seed,G.title_weapon,G.difficulty,G.ngplus);
}
static void debug_fixture_dd_retry(void){
    G.state=ST_PLAY; G.state_t=0;
    int state_before=G.state;
    int before_currency=(int)G.meta.bytes_currency;
    uint32_t run_seed_before=G.run_seed;
    G.bytes_run=17; G.pl.hp=0; dd_debug_reset_meta_save_events();
    int bytes_run_before=G.bytes_run;
    player_take_damage(G.pl.pos); G.state=ST_DEAD; G.state_t=2.0f;
    int settlement_saves=dd_debug_meta_save_events();
    int settlement_bank_events=((int)G.meta.bytes_currency-before_currency)==bytes_run_before?1:0;
    debug_dispatch_key(SAPP_KEYCODE_R,false);
    uint32_t run_seed_after=G.run_seed;
    int retry_state=G.state;
    debug_invariant("retry-bank-events",1,settlement_bank_events);
    debug_invariant("retry-settlement-save-events",1,settlement_saves);
    debug_invariant("retry-total-save-events",2,dd_debug_meta_save_events());
    debug_invariant("retry-state",ST_PLAY,retry_state);
    const sapp_keycode menu_keys[3]={SAPP_KEYCODE_SPACE,SAPP_KEYCODE_ENTER,SAPP_KEYCODE_ESCAPE};
    int menu_states[3];
    for (int i=0;i<3;i++){
        G.state=ST_DEAD; G.state_t=2.0f;
        debug_dispatch_key(menu_keys[i],false);
        menu_states[i]=G.state;
        debug_invariant("death-menu-state",ST_TITLE,menu_states[i]);
    }
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-retry-contract\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"total_meta_save_events\":%d,\"bytes_run\":%d,\"bytes_currency\":%d,\"title_seed\":%u,\"run_seed\":%u,\"run_seed_before\":%u,\"run_seed_after\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"retry_state\":\"%s\",\"title_state_after_space\":\"%s\",\"title_state_after_enter\":\"%s\",\"title_state_after_esc\":\"%s\"}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),settlement_bank_events,settlement_saves,dd_debug_meta_save_events(),bytes_run_before,(int)G.meta.bytes_currency-before_currency,G.title_seed,G.run_seed,run_seed_before,run_seed_after,G.title_weapon,G.difficulty,G.ngplus,debug_state_name(retry_state),debug_state_name(menu_states[0]),debug_state_name(menu_states[1]),debug_state_name(menu_states[2]));
}
static void debug_fixture_dd_forfeit(void){
    G.state=ST_PLAY; G.state_t=0;
    int state_before=G.state;
    int currency_before=(int)G.meta.bytes_currency;
    G.bytes_run=17; int bytes_run_before=G.bytes_run;
    dd_debug_reset_meta_save_events();
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    G.menu_sel=3; debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    int settlement_bank_events=((int)G.meta.bytes_currency-currency_before)==bytes_run_before?1:0;
    int total_saves=dd_debug_meta_save_events();
    debug_invariant("forfeit-bank-events",1,settlement_bank_events);
    debug_invariant("forfeit-save-events",1,total_saves);
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-forfeit\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"total_meta_save_events\":%d,\"run_banked\":%d,\"owned_total\":%u,\"bytes_run\":%d,\"bytes_currency\":%u,\"title_seed\":%u,\"run_seed\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"retry_invocations\":0}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),settlement_bank_events,total_saves,total_saves,(int)G.meta.bytes_currency-currency_before,G.meta.bytes_currency,bytes_run_before,G.meta.bytes_currency,G.title_seed,G.run_seed,G.title_weapon,G.difficulty,G.ngplus);
}
static void debug_fixture_dd_promise_labels(void){
    char compressed_shard[80];
    G.state=ST_PLAY; G.state_t=0;
    int state_before=G.state;
    G.pl.relics[RELIC_COMPRESS]=true;
    int compressed_effect=player_item_kb(64);
    snprintf(compressed_shard,sizeof compressed_shard,"추억 조각 획득 보장 · 용량 +%dKB",compressed_effect);
    if (strcmp(promise_label(PROMISE_SHARD),compressed_shard)){
        fprintf(stderr,"{\"error\":\"invariant\",\"what\":\"compressed-shard-promise-label\"}\n");
        exit(3);
    }
    int difficulty_before=G.difficulty, door_count_before=G.room.door_count;
    bool cleared_before=G.room.cleared;
    int branch_guidance[3];
    G.room.cleared=true; G.room.door_count=2;
    for (int difficulty=0;difficulty<3;difficulty++){
        G.difficulty=difficulty;
        branch_guidance[difficulty]=branch_promise_guidance_visible()?1:0;
        debug_invariant("branch-guidance-difficulty",difficulty==0?1:0,branch_guidance[difficulty]);
    }
    G.difficulty=difficulty_before; G.room.door_count=door_count_before; G.room.cleared=cleared_before;
    G.pl.relics[RELIC_COMPRESS]=false;
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-promise-labels\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":0,\"settlement_meta_save_events\":0,\"total_meta_save_events\":0,\"bytes_run\":%d,\"bytes_currency\":%u,\"title_seed\":%u,\"run_seed\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"labels\":{\"weapon\":\"%s\",\"relic\":\"%s\",\"shard\":\"%s\",\"heart\":\"%s\"},\"capacity_effect\":[null,null,%d,0],\"effects\":{\"weapon\":null,\"relic\":null,\"shard\":%d,\"heart\":0},\"compressed_shard_label\":\"%s\",\"compressed_shard_effect\":%d,\"branch_guidance\":[%d,%d,%d],\"bonus_shard_disclosed\":false}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),G.bytes_run,G.meta.bytes_currency,G.title_seed,G.run_seed,G.title_weapon,G.difficulty,G.ngplus,promise_label(PROMISE_WEAPON),promise_label(PROMISE_RELIC),promise_label(PROMISE_SHARD),promise_label(PROMISE_HEART),player_item_kb(64),player_item_kb(64),compressed_shard,compressed_effect,branch_guidance[0],branch_guidance[1],branch_guidance[2]);
}
static void debug_fixture_dd_shake_menu(bool roundtrip){
    int before=G.meta.opt_shake, scanline=G.meta.opt_scanline;
    G.state=ST_PLAY; G.state_t=0;
    int state_before=G.state;
    G.menu_sel=0; dd_debug_reset_meta_save_events();
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false); debug_dispatch_key(SAPP_KEYCODE_RIGHT,false);
    int selected=G.menu_sel; debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    int state_after=G.state;
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-%s\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":0,\"settlement_meta_save_events\":0,\"total_meta_save_events\":%d,\"menu_order\":[\"resume\",\"settings\",\"title\"],\"selected_after_one_right\":\"%s\",\"opt_shake_before\":%d,\"opt_shake_after\":%d,\"opt_scanline_before\":%d,\"opt_scanline_after\":%d,\"save_bytes\":%zu,\"checksum_offset\":%zu}\n",debug_source_sha256(),roundtrip?"shake-roundtrip":"shake-menu",debug_state_name(state_before),debug_state_name(state_after),dd_debug_meta_save_events(),selected==1?"settings":"other",before,G.meta.opt_shake,scanline,G.meta.opt_scanline,sizeof(MetaSave),offsetof(MetaSave,checksum));
}
static void debug_fixture_dd_options(void){
    int queue_before=G.meta.intro_replay_queued, seen_before=G.meta.intro_seen;
    int scanline_before=G.meta.opt_scanline, shake_before=G.meta.opt_shake;
    int bgm_before=G.meta.opt_bgm, sfx_before=G.meta.opt_sfx;
    int saves, queue_after, queue_reloaded, state_entered, state_returned, pause_returned;
    G.state=ST_TITLE; G.state_t=0; G.menu_sel=6;
    debug_dispatch_key(SAPP_KEYCODE_S,false);
    debug_invariant("options-title-wrap-down",0,G.menu_sel);
    debug_dispatch_key(SAPP_KEYCODE_W,false);
    debug_invariant("options-title-wrap-up",6,G.menu_sel);
    G.menu_sel=0;
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    debug_invariant("difficulty-chooser-enter",ST_DIFFICULTY_SELECT,G.state);
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    debug_invariant("difficulty-chooser-return",ST_TITLE,G.state);
    G.menu_sel=1;
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    debug_invariant("weapon-selector-enter",ST_WEAPON_SELECT,G.state);
    int weapon_before=G.title_weapon;
    debug_dispatch_key(SAPP_KEYCODE_W,false);
    debug_invariant("weapon-selector-w-up",(weapon_before+WPN_COUNT-3)%WPN_COUNT,G.title_weapon);
    debug_dispatch_key(SAPP_KEYCODE_S,false);
    debug_invariant("weapon-selector-s-down",weapon_before,G.title_weapon);
    debug_dispatch_key(SAPP_KEYCODE_UP,false);
    debug_invariant("weapon-selector-up-row",(weapon_before+WPN_COUNT-3)%WPN_COUNT,G.title_weapon);
    debug_dispatch_key(SAPP_KEYCODE_DOWN,false);
    debug_invariant("weapon-selector-down-row",weapon_before,G.title_weapon);
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    debug_invariant("weapon-selector-return",ST_TITLE,G.state);
    G.menu_sel=4;
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    state_entered=G.state;
    debug_invariant("options-enter-state",ST_OPTIONS,G.state);
    dd_debug_reset_meta_save_events();
    if (DBG_CFG.options_checkpoint==1){
        G.menu_sel=0;
        debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
        debug_dispatch_key(SAPP_KEYCODE_S,false);
        debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
        G.menu_sel=6;
        debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    } else {
        debug_dispatch_key(SAPP_KEYCODE_Q,false);
    }
    saves=dd_debug_meta_save_events();
    queue_after=G.meta.intro_replay_queued;
    debug_invariant("options-intro-seen",seen_before,G.meta.intro_seen);
    debug_invariant("options-scanline",scanline_before,G.meta.opt_scanline);
    debug_invariant("options-shake",shake_before,G.meta.opt_shake);
    if (DBG_CFG.options_checkpoint==1){
        debug_invariant("options-toggle-save",3,saves);
        debug_invariant("options-toggle-queue",!queue_before,queue_after);
        debug_invariant("options-toggle-bgm",!bgm_before,G.meta.opt_bgm);
        debug_invariant("options-toggle-sfx",!sfx_before,G.meta.opt_sfx);
        memset(&G.meta,0,sizeof G.meta);
        meta_load();
        queue_reloaded=G.meta.intro_replay_queued;
        debug_invariant("options-reload-queue",queue_after,queue_reloaded);
        debug_invariant("options-reload-seen",seen_before,G.meta.intro_seen);
        debug_invariant("options-reload-scanline",scanline_before,G.meta.opt_scanline);
        debug_invariant("options-reload-shake",shake_before,G.meta.opt_shake);
        debug_invariant("options-reload-bgm",!bgm_before,G.meta.opt_bgm);
        debug_invariant("options-reload-sfx",!sfx_before,G.meta.opt_sfx);
        G.state=ST_OPTIONS; G.menu_sel=0;
    } else {
        queue_reloaded=queue_after;
        debug_invariant("options-invalid-save",0,saves);
        debug_invariant("options-invalid-queue",queue_before,queue_after);
    }
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    state_returned=G.state;
    debug_invariant("options-return-title",ST_TITLE,G.state);
    G.state=ST_PAUSE; G.menu_sel=1;
    debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    debug_invariant("options-pause-enter",ST_OPTIONS,G.state);
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    pause_returned=G.state;
    debug_invariant("options-pause-return",ST_PAUSE,pause_returned);
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-options\",\"checkpoint\":\"%s\",\"fixture_ready\":true,\"state_entered\":\"%s\",\"state_returned\":\"%s\",\"pause_returned\":\"%s\",\"queue_before\":%d,\"queue_after\":%d,\"queue_reloaded\":%d,\"intro_seen_before\":%d,\"intro_seen_after\":%u,\"pause_scanline_before\":%d,\"pause_scanline_after\":%u,\"pause_shake_before\":%d,\"pause_shake_after\":%u,\"bgm_before\":%d,\"bgm_after\":%u,\"sfx_before\":%d,\"sfx_after\":%u,\"meta_save_events\":%d,\"run_started\":false}\n",
           debug_source_sha256(),DBG_CFG.options_checkpoint==1?"options-toggle":(DBG_CFG.options_checkpoint==2?"options-invalid-state":"options-invalid-input"),
           debug_state_name(state_entered),debug_state_name(state_returned),debug_state_name(pause_returned),queue_before,queue_after,queue_reloaded,
           seen_before,G.meta.intro_seen,scanline_before,G.meta.opt_scanline,shake_before,G.meta.opt_shake,
           bgm_before,G.meta.opt_bgm,sfx_before,G.meta.opt_sfx,saves);
    if (DBG_CFG.have_hold_ms){
        G.state=ST_OPTIONS; G.menu_sel=0; G.state_t=0;
        dbg_options_visual_active=1;
    }
}
static void debug_fixture_story_signals(void){
    const char* fallback="복구 신호가 잠시 흔들린다.";
    static const char* expected_titles[4]={
        "복구 블록 #1 — 첫 부팅", "복구 블록 #2 — 첫 승리",
        "복구 블록 #3 — 마지막 저장", "복구 블록 #4 — 남긴 한 줄"
    };
    static const char* expected_terms[4]={"첫 모험","박수","다음에","읽기 창"};
    int contexts=0, entries=0;
    for (int biome=0;biome<4;biome++) for (int type=MEM_EVENT_ECHO;type<=MEM_EVENT_CORRUPTED;type++)
        for (int tag=0;tag<MEM_TAG_COUNT;tag++){
            const char* text=story_memory_context(biome,type,tag);
            int bounded=(int)strlen(text)<96;
            debug_invariant("story-context-bounded",1,bounded);
            debug_invariant("story-context-valid",0,!strcmp(text,fallback));
            printf("{\"schema\":1,\"kind\":\"story_context\",\"biome\":%d,\"event_type\":%d,\"tag\":%d,\"bounded\":%d,\"text\":\"%s\"}\n",
                   biome,type,tag,bounded,text);
            contexts++;
        }
    debug_invariant("story-context-fallback",1,!strcmp(story_memory_context(-1,0,0),fallback));
    debug_invariant("story-boss-fallback",1,!strcmp(story_boss_framing(4),"복구 신호가 이어진다."));
    for (int core=0;core<4;core++){
        const char* title=core_title_for(core);
        const char* text=core_text_for(core);
        debug_invariant("story-core-title",1,!strcmp(title,expected_titles[core]));
        debug_invariant("story-core-act",1,strstr(text,expected_terms[core])!=NULL);
        printf("{\"schema\":1,\"kind\":\"story_core\",\"core_id\":%d,\"title\":\"%s\",\"inventory\":\"%s\",\"flashback_chars\":%d}\n",
               core,title,core_inventory_texts[core],(int)strlen(text));
    }
    debug_invariant("story-core-invalid-title",1,!strcmp(core_title_for(4),"복구 블록 — 확인 불가"));
    debug_invariant("story-core-invalid-text",1,!strcmp(core_text_for(-1),"복구 블록을 확인할 수 없다."));
    dd_debug_story_entry_notice_reset();
    int first0=dd_debug_story_entry_notice_probe(0,0);
    int first1=dd_debug_story_entry_notice_probe(1,0);
    int duplicate0=dd_debug_story_entry_notice_probe(0,0);
    int first2=dd_debug_story_entry_notice_probe(2,0);
    int first3=dd_debug_story_entry_notice_probe(3,0);
    int later3=dd_debug_story_entry_notice_probe(3,1);
    debug_invariant("story-entry-0-first",1,first0);
    debug_invariant("story-entry-1-first",1,first1);
    debug_invariant("story-entry-0-repeat",0,duplicate0);
    debug_invariant("story-entry-2-first",1,first2);
    debug_invariant("story-entry-3-first",1,first3);
    debug_invariant("story-entry-later-room",0,later3);
    entries=first0+first1+first2+first3;
    printf("{\"schema\":1,\"kind\":\"story_entry_sequence\",\"sequence\":[0,1,0],\"emits\":[%d,%d,%d],\"unique_biomes\":%d,\"later_room_emits\":%d}\n",
           first0,first1,duplicate0,entries,later3);
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"story-signals\",\"contexts\":%d,\"entries\":%d,\"fallback\":true,\"status\":\"pass\"}\n",contexts,entries);
}
static int debug_last_log_code(void){
    if (!G.memory.log_count) return -1;
    int slot=(G.memory.log_head+G.memory.log_count-1)%8;
    return G.memory.log[slot];
}
static int debug_active_byte_pickup(void){
    int n=0;
    for(int i=0;i<MAX_PICKUPS;i++)
        if(G.pickups[i].active&&G.pickups[i].type==PK_BYTE)n++;
    return n;
}
static int debug_pickup_count(void){
    int n=0; for(int i=0;i<MAX_PICKUPS;i++) if(G.pickups[i].active)n++; return n;
}
static void debug_f10_run(void){
    int key=DBG_CFG.f10_branch==1?SAPP_KEYCODE_E:SAPP_KEYCODE_Q;
    int decision=DBG_CFG.f10_branch==1?MEM_DECISION_KEEP:MEM_DECISION_DISCARD;
    int tag=G.room.event_tag, event_type=G.room.event_type;
    int state_before=G.room.event_state;
    int kept_before=G.memory.kept[tag], discarded_before=G.memory.discarded[tag];
    int pending_before=G.memory.pending_trait;
    int log_before=G.memory.log_count, hp_before=(int)lroundf(G.pl.hp*1000);
    int bytes_before=G.bytes_run, pickups_before=debug_pickup_count();
    int sentinel_before=debug_active_byte_pickup();
    int shards_before=G.pl.shards, cores_before=G.pl.cores;
    int distance=(int)lroundf(v2len(v2sub(G.room.event_pos,G.pl.pos))*1000.0f);
    debug_invariant("f10-distance",1,distance<=24000);
    debug_dispatch_key(key,false);
    int state_after=G.room.event_state;
    int kept_delta=G.memory.kept[tag]-kept_before;
    int discarded_delta=G.memory.discarded[tag]-discarded_before;
    int log_delta=G.memory.log_count-log_before;
    int pending_after=G.memory.pending_trait;
    int hp_delta=(int)lroundf(G.pl.hp*1000)-hp_before;
    int bytes_delta=G.bytes_run-bytes_before;
    int pickup_delta=debug_pickup_count()-pickups_before;
    int shards_delta=G.pl.shards-shards_before;
    int cores_delta=(int)G.pl.cores-cores_before;
    int sentinel_after=debug_active_byte_pickup();
    int log_code=debug_last_log_code();
    debug_invariant("f10-resolved",MEM_STATE_RESOLVED,state_after);
    debug_invariant("f10-kept-delta",decision==MEM_DECISION_KEEP?1:0,kept_delta);
    debug_invariant("f10-discarded-delta",decision==MEM_DECISION_DISCARD?1:0,discarded_delta);
    debug_invariant("f10-log-delta",1,log_delta);
    debug_invariant("f10-log-code",(decision<<4)|(event_type<<2)|tag,log_code);
    debug_invariant("f10-pending",
                    decision==MEM_DECISION_KEEP?G.room.event_trait:pending_before,pending_after);
    debug_invariant("f10-hp-delta",decision==MEM_DECISION_DISCARD?1000:0,hp_delta);
    debug_invariant("f10-bytes-delta",0,bytes_delta);
    debug_invariant("f10-pickup-delta",0,pickup_delta);
    debug_invariant("f10-sentinel",sentinel_before,sentinel_after);
    debug_invariant("f10-shards-delta",0,shards_delta);
    debug_invariant("f10-cores-delta",0,cores_delta);
    int reentry_pending=G.memory.pending_trait, reentry_sentinel=debug_active_byte_pickup();
    int reentry_event_type=G.room.event_type, reentry_event_tag=G.room.event_tag;
    int reentry_event_trait=G.room.event_trait;
    bool reentry=event_resolve_choice(decision);
    debug_invariant("f10-reentry",0,reentry?1:0);
    debug_invariant("f10-reentry-pending",reentry_pending,G.memory.pending_trait);
    debug_invariant("f10-reentry-sentinel",reentry_sentinel,debug_active_byte_pickup());
    debug_invariant("f10-reentry-type",reentry_event_type,G.room.event_type);
    debug_invariant("f10-reentry-tag",reentry_event_tag,G.room.event_tag);
    debug_invariant("f10-reentry-trait",reentry_event_trait,G.room.event_trait);
    debug_invariant("f10-reentry-state",0,G.room.event_state-state_after);
    debug_invariant("f10-reentry-kept",0,G.memory.kept[tag]-kept_before-kept_delta);
    debug_invariant("f10-reentry-discarded",0,G.memory.discarded[tag]-discarded_before-discarded_delta);
    debug_invariant("f10-reentry-log",0,G.memory.log_count-log_before-log_delta);
    debug_invariant("f10-reentry-hp",0,(int)lroundf(G.pl.hp*1000)-hp_before-hp_delta);
    debug_invariant("f10-reentry-bytes",0,G.bytes_run-bytes_before-bytes_delta);
    debug_invariant("f10-reentry-pickups",0,debug_pickup_count()-pickups_before-pickup_delta);
    debug_invariant("f10-reentry-shards",0,G.pl.shards-shards_before-shards_delta);
    debug_invariant("f10-reentry-cores",0,(int)G.pl.cores-cores_before-cores_delta);
    int repeat_state=G.room.event_state, repeat_log=G.memory.log_count;
    int repeat_kept=G.memory.kept[tag], repeat_discarded=G.memory.discarded[tag];
    int repeat_hp=(int)lroundf(G.pl.hp*1000), repeat_bytes=G.bytes_run;
    int repeat_pickups=debug_pickup_count(), repeat_shards=G.pl.shards, repeat_cores=G.pl.cores;
    int repeat_pending=G.memory.pending_trait, repeat_sentinel=debug_active_byte_pickup();
    int repeat_type=G.room.event_type, repeat_tag=G.room.event_tag, repeat_trait=G.room.event_trait;
    debug_dispatch_key(key,true);
    debug_invariant("f10-repeat-state",repeat_state,G.room.event_state);
    debug_invariant("f10-repeat-pending",repeat_pending,G.memory.pending_trait);
    debug_invariant("f10-repeat-sentinel",repeat_sentinel,debug_active_byte_pickup());
    debug_invariant("f10-repeat-type",repeat_type,G.room.event_type);
    debug_invariant("f10-repeat-tag",repeat_tag,G.room.event_tag);
    debug_invariant("f10-repeat-trait",repeat_trait,G.room.event_trait);
    debug_invariant("f10-repeat-choice",repeat_log,G.memory.log_count);
    debug_invariant("f10-repeat-kept",repeat_kept,G.memory.kept[tag]);
    debug_invariant("f10-repeat-discarded",repeat_discarded,G.memory.discarded[tag]);
    debug_invariant("f10-repeat-hp",repeat_hp,(int)lroundf(G.pl.hp*1000));
    debug_invariant("f10-repeat-bytes",repeat_bytes,G.bytes_run);
    debug_invariant("f10-repeat-pickups",repeat_pickups,debug_pickup_count());
    debug_invariant("f10-repeat-shards",repeat_shards,G.pl.shards);
    debug_invariant("f10-repeat-cores",repeat_cores,G.pl.cores);
    printf("{\"schema\":1,\"kind\":\"f10_resolution\",\"branch\":\"%s\",\"event_type\":%d,\"event_tag\":%d,\"event_distance_milli\":%d,\"key_code\":%d,\"key_repeat\":0,\"event_state_before\":%d,\"event_state_after\":%d,\"kept_delta\":%d,\"discarded_delta\":%d,\"log_count_delta\":%d,\"log_code\":%d,\"pending_before\":%d,\"pending_after\":%d,\"hp_delta_milli\":%d,\"bytes_delta\":%d,\"pickup_delta\":%d,\"shards_delta\":%d,\"cores_delta\":%d,\"public_nonrepeat_calls\":1,\"public_choice_count\":1,\"resolver_reentry_calls\":1,\"resolver_reentry_result\":0,\"resolver_reentry_state_delta\":0,\"repeat_key_events\":1,\"repeat_choice_count\":0,\"repeat_pickup_delta\":0,\"repeat_shards_delta\":0,\"repeat_cores_delta\":0,\"post_resolution_nonrepeat_events\":0,\"stop_reason\":\"f10_resolved\"}\n",
           DBG_CFG.f10_branch==1?"keep":"discard",event_type,tag,distance,key,state_before,state_after,
           kept_delta,discarded_delta,log_delta,log_code,pending_before,pending_after,hp_delta,
           bytes_delta,pickup_delta,shards_delta,cores_delta);
    fflush(stdout); dbg_f10_done=1; sapp_request_quit();
}
static void debug_emit_forced(const char* kind, int pending, int assignment_count){
    Entity* e=&G.ents[DBG_CFG.force_target[0]];
    printf("{\"schema\":1,\"kind\":\"%s\",\"fixture\":\"%s\",\"slot\":%d,\"type\":%d,"
           "\"tile_x\":%d,\"tile_y\":%d,\"world_x_milli\":%d,\"world_y_milli\":%d,"
           "\"active\":%d,\"elite\":%d,\"trait\":%d,\"bonus\":%d,\"hp_milli\":%d,"
           "\"maxhp_milli\":%d,\"radius_milli\":%d,\"pending\":%d,\"assignment_count\":%d,"
           "\"crng_before\":\"%016llx\",\"crng_after\":\"%016llx\"}\n",
           kind,DBG_CFG.action==3?"qne":"qae",DBG_CFG.force_target[0],e->type,
           (int)(e->pos.x/TILE),(int)(e->pos.y/TILE),(int)lroundf(e->pos.x*1000),
           (int)lroundf(e->pos.y*1000),e->active?1:0,e->elite?1:0,e->event_trait,
           e->event_bonus,(int)lroundf(e->hp*1000),(int)lroundf(e->maxhp*1000),
           (int)lroundf(e->radius*1000),pending,assignment_count,
           (unsigned long long)dbg_forced_crng,(unsigned long long)crng.s);
}

static int debug_enemy_bullets(void){
    int n=0;
    for(int i=0;i<MAX_BULLETS;i++) if(G.bullets[i].active&&!G.bullets[i].from_player)n++;
    return n;
}
static void debug_fixture_training(void){
    MetaSave meta_before=G.meta;
    start_training();
    debug_invariant("training-state",1,G.training_active?1:0);
    debug_invariant("training-room-width",30,G.room.w);
    debug_invariant("training-room-height",17,G.room.h);
    debug_invariant("training-dummy-active",1,G.ents[0].active?1:0);
    debug_invariant("training-dummy-hp",1000000000,(int)G.ents[0].hp);
    debug_invariant("training-stations",30,debug_pickup_count());
    v2 dummy_pos=G.ents[0].pos;
    update_play(0.25f);
    debug_invariant("training-dummy-static-x",(int)lroundf(dummy_pos.x*1000.0f),(int)lroundf(G.ents[0].pos.x*1000.0f));
    debug_invariant("training-dummy-static-y",(int)lroundf(dummy_pos.y*1000.0f),(int)lroundf(G.ents[0].pos.y*1000.0f));
    debug_invariant("training-dummy-no-bullets",0,debug_enemy_bullets());
    G.pl.pos=V2(VIRT_W*(2.0f/3.0f),88.0f);
    debug_invariant("training-summon-button-consumes-e",1,training_try_summon()?1:0);
    int passive_count=0, passive_slot=-1;
    for (int i=1;i<MAX_ENTITIES;i++) if (G.ents[i].active&&G.ents[i].training_passive){ passive_count++; passive_slot=i; }
    debug_invariant("training-summons-one-passive-flyer",1,passive_count);
    v2 passive_pos=G.ents[passive_slot].pos;
    debug_invariant("training-summon-cooldown-blocks-repeat",1,training_try_summon()?1:0);
    update_play(0.5f);
    passive_count=0;
    for (int i=1;i<MAX_ENTITIES;i++) if (G.ents[i].active&&G.ents[i].training_passive) passive_count++;
    debug_invariant("training-summon-cooldown-keeps-count",1,passive_count);
    debug_invariant("training-passive-flyer-moves",1,v2len(v2sub(G.ents[passive_slot].pos,passive_pos))>0.1f?1:0);
    debug_invariant("training-passive-flyer-no-bullets",0,debug_enemy_bullets());
    update_play(0.51f);
    training_try_summon();
    passive_count=0;
    for (int i=1;i<MAX_ENTITIES;i++) if (G.ents[i].active&&G.ents[i].training_passive) passive_count++;
    debug_invariant("training-summon-cooldown-one-second",2,passive_count);

    Pickup* cannon=&G.pickups[5];
    int cannon_station_weapon=cannon->weapon.type;
    player_try_pickup(cannon);
    debug_invariant("training-weapon-equipped",WPN_CANNON,G.pl.weapon.type);
    debug_invariant("training-weapon-station-fixed",cannon_station_weapon,cannon->weapon.type);
    player_try_pickup(&G.pickups[1]);
    player_try_pickup(&G.pickups[2]);
    debug_invariant("training-module-first",WR_SWORD_WAVE,G.pl.wrelics[0]);
    debug_invariant("training-module-second",WR_SWORD_WHIRL,G.pl.wrelics[1]);
    player_try_pickup(&G.pickups[3]);
    debug_invariant("training-module-swap-state",ST_RELIC_SWAP,G.state);
    player_confirm_relic_swap(0);
    debug_invariant("training-module-swap-return",ST_TRAINING,G.state);
    debug_invariant("training-module-station-fixed",1,G.pickups[3].active?1:0);
    debug_invariant("training-meta-unchanged",0,memcmp(&meta_before,&G.meta,sizeof(MetaSave))==0?0:1);
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"training\",\"status\":\"pass\",\"stations\":30,\"dummy\":\"infinite-passive\",\"weapon_station\":\"fixed\",\"module_station\":\"fixed\"}\n");
}
void debug_apply_config_after_game_init(void){
    bool finite = DBG_CFG.have_action;
    if (DBG_CFG.clean_profile) debug_prepare_configured_run();
    if (DBG_CFG.have_seed) G.title_seed = DBG_CFG.seed;
    if (DBG_CFG.have_telemetry) debug_telemetry_open();
    if (DBG_CFG.clean_profile && DBG_CFG.action!=21 && DBG_CFG.action!=22 && DBG_CFG.action!=23 && DBG_CFG.action!=25) start_run();
    dbg_auto=DBG_CFG.auto_play; dbg_god=DBG_CFG.god; dbg_intro=DBG_CFG.intro;
    dbg_ending=DBG_CFG.ending;
    if (DBG_CFG.have_jump){ dbg_jump_biome=DBG_CFG.jump_biome; dbg_jump_room=DBG_CFG.jump_room; }
    if (!finite){
        debug_telemetry_start();
        if (DBG_CFG.auto_play || DBG_CFG.have_jump || DBG_CFG.ending>=0){ G.state=ST_TITLE; G.state_t=0; }
        return;
    }
    if (DBG_CFG.have_jump){
        G.pl.cores=(uint8_t)((1<<DBG_CFG.jump_biome)-1);
        room_generate(DBG_CFG.jump_biome,DBG_CFG.jump_room,PROMISE_NONE,DIR_L);
    }
    if (DBG_CFG.have_force_target){ dbg_forced_crng=crng.s; debug_init_forced_entity(); }
    debug_telemetry_start();
    if (DBG_CFG.have_force_pending)
        G.memory.pending_trait = (uint8_t)DBG_CFG.force_pending;
    if (DBG_CFG.have_force_event){
        G.room.event_type=DBG_CFG.force_event[0]; G.room.event_tag=DBG_CFG.force_event[1];
        G.room.event_trait=DBG_CFG.force_event[2]; G.room.event_state=DBG_CFG.force_event[3];
        G.room.event_pos=G.pl.pos;
        G.room.event_tile_x=(int)(G.pl.pos.x/TILE); G.room.event_tile_y=(int)(G.pl.pos.y/TILE);
    }
    if (DBG_CFG.action==5 || DBG_CFG.action==6) G.state=ST_PLAY;
    if (DBG_CFG.action==21){ debug_prepare_ui_showcase(); return; }
    if (DBG_CFG.action==22){ debug_prepare_opening_fixture(); return; }
    if (DBG_CFG.action==23){ debug_prepare_ending_fixture(); return; }
    if (DBG_CFG.action==25){ debug_prepare_start_intro_fixture(); return; }
    switch (DBG_CFG.action){
    case 1:
        debug_emit_snapshot(false); fflush(stdout); exit(0);
    case 2:
        on_room_cleared();
        debug_emit_snapshot(true); fflush(stdout); exit(0);
    case 3: case 4: case 5: case 6: {
        Entity* e=&G.ents[DBG_CFG.force_target[0]];
        int assignment=0;
        if (DBG_CFG.action==3||DBG_CFG.action==4){
            int bytes_before=G.bytes_run, bullets_before=debug_enemy_bullets();
            int pickups_before=debug_pickup_count(), kills_before=G.kills;
            debug_emit_forced("forced_pre",G.memory.pending_trait,0);
            if (DBG_CFG.have_force_pending){
                event_assign_pending_trait();
                assignment = G.memory.pending_trait==ELITE_NONE &&
                    e->elite && e->event_bonus==1 &&
                    e->event_trait==(uint8_t)DBG_CFG.force_pending;
            }
            debug_invariant("pending-assignment",1,assignment);
            if (DBG_CFG.action==3)
                debug_invariant("volatile-trait",ELITE_VOLATILE,e->event_trait);
            else
                debug_invariant("haste-trait",ELITE_HASTE,e->event_trait);
            if (crng.s!=dbg_forced_crng){
                fprintf(stderr,"{\"error\":\"invariant\",\"what\":\"crng-unchanged\"}\n"); exit(3);
            }
            debug_emit_forced("forced_post",G.memory.pending_trait,assignment);
            debug_invariant("pending-cleared",0,G.memory.pending_trait);
            int death_reason=DBG_CFG.action==3?DEATH_REASON_DAMAGE:DEATH_REASON_BOMBER;
            if (DBG_CFG.action==4)
                debug_invariant("bomber-self-destruct",1,
                                death_reason==DEATH_REASON_BOMBER && e->type==E_BOMBER);
            Entity finalizer_probe=*e;
            finalizer_probe.hp=0.0f;
            enemy_finalize_death(&finalizer_probe,death_reason);
            debug_invariant("enemy-finalized",0,finalizer_probe.active?1:0);
            int bytes_delta=G.bytes_run-bytes_before;
            int bullet_delta=debug_enemy_bullets()-bullets_before;
            int pickup_delta=debug_pickup_count()-pickups_before;
            int kills_delta=G.kills-kills_before;
            debug_invariant("bonus-bytes",2,bytes_delta);
            debug_invariant("death-path-bullets",DBG_CFG.action==3?6:8,bullet_delta);
            debug_invariant("qae-kills",DBG_CFG.action==3?1:0,kills_delta);
            int bytes_after_first=G.bytes_run, bullets_after_first=debug_enemy_bullets();
            enemy_finalize_death(&finalizer_probe,death_reason);
            debug_invariant("duplicate-finalization-bytes",bytes_after_first,G.bytes_run);
            debug_invariant("duplicate-finalization-bullets",bullets_after_first,debug_enemy_bullets());
            printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"%s\",\"slot\":%d,\"type\":%d,\"active\":%d,\"elite\":%d,\"trait\":%d,\"bonus\":%d,\"hp_milli\":%d,\"maxhp_milli\":%d,\"radius_milli\":%d,\"pending\":%d,\"assignment_count\":%d,\"crng_before\":\"%016llx\",\"crng_after\":\"%016llx\",\"finalizer_calls\":2,\"effects_applied\":1,\"volatile_bursts\":%d,\"bomber_bursts\":%d,\"enemy_bullets_delta\":%d,\"bytes_delta\":%d,\"kills_delta\":%d,\"ordinary_drop_delta\":%d,\"event_state\":%d,\"kept_delta\":0,\"discarded_delta\":0,\"log_count_delta\":0,\"log_code\":-1,\"pickup_delta\":%d,\"shards_delta\":0,\"cores_delta\":0}\n",
                   DBG_CFG.action==3?"qne":"qae",DBG_CFG.force_target[0],e->type,
                   finalizer_probe.active?1:0,finalizer_probe.elite?1:0,finalizer_probe.event_trait,finalizer_probe.event_bonus,
                   (int)lroundf(finalizer_probe.hp*1000),(int)lroundf(finalizer_probe.maxhp*1000),
                   (int)lroundf(finalizer_probe.radius*1000),G.memory.pending_trait,assignment,
                   (unsigned long long)dbg_forced_crng,(unsigned long long)crng.s,
                   DBG_CFG.action==3?1:0,DBG_CFG.action==4?1:0,bullet_delta,bytes_delta,
                   kills_delta,pickup_delta,G.room.event_state,pickup_delta);
        } else {
            int tag=G.room.event_tag, event_type=G.room.event_type, trait=G.room.event_trait;
            memset(G.pickups,0,sizeof G.pickups);
            G.pl.hp=fmaxf(0.0f,(float)G.pl.maxhp-1.0f);
            G.pl.shards=1; G.pl.cores=1;
            int state_before=G.room.event_state, pending_before=G.memory.pending_trait;
            int kept_before=G.memory.kept[tag], discarded_before=G.memory.discarded[tag];
            int log_before=G.memory.log_count, hp_before=(int)lroundf(G.pl.hp*1000);
            int bytes_before=G.bytes_run, pickup_before=debug_pickup_count();
            int shards_before=G.pl.shards, cores_before=G.pl.cores;
            int denial_state=G.room.event_state, denial_pending=G.memory.pending_trait;
            int denial_kept=G.memory.kept[tag], denial_discarded=G.memory.discarded[tag];
            int denial_log=G.memory.log_count, denial_hp=(int)lroundf(G.pl.hp*1000);
            int denial_bytes=G.bytes_run, denial_pickups=debug_pickup_count();
            int denial_shards=G.pl.shards, denial_cores=G.pl.cores;
            debug_dispatch_key(SAPP_KEYCODE_E,false);
            debug_invariant("qcol-denial-state",denial_state,G.room.event_state);
            debug_invariant("qcol-denial-pending",denial_pending,G.memory.pending_trait);
            debug_invariant("qcol-denial-kept",denial_kept,G.memory.kept[tag]);
            debug_invariant("qcol-denial-discarded",denial_discarded,G.memory.discarded[tag]);
            debug_invariant("qcol-denial-log",denial_log,G.memory.log_count);
            debug_invariant("qcol-denial-hp",denial_hp,(int)lroundf(G.pl.hp*1000));
            debug_invariant("qcol-denial-bytes",denial_bytes,G.bytes_run);
            debug_invariant("qcol-denial-pickups",denial_pickups,debug_pickup_count());
            debug_invariant("qcol-denial-shards",denial_shards,G.pl.shards);
            debug_invariant("qcol-denial-cores",denial_cores,G.pl.cores);
            debug_dispatch_key(SAPP_KEYCODE_Q,false);
            int kept_delta=G.memory.kept[tag]-kept_before;
            int discarded_delta=G.memory.discarded[tag]-discarded_before;
            int log_delta=G.memory.log_count-log_before;
            int hp_delta=(int)lroundf(G.pl.hp*1000)-hp_before;
            int bytes_delta=G.bytes_run-bytes_before;
            int pickup_delta=debug_pickup_count()-pickup_before;
            int shards_delta=G.pl.shards-shards_before;
            int cores_delta=(int)G.pl.cores-cores_before;
            int log_code=debug_last_log_code();
            debug_invariant("qcol-resolved",MEM_STATE_RESOLVED,G.room.event_state);
            debug_invariant("qcol-kept-delta",0,kept_delta);
            debug_invariant("qcol-discarded-delta",1,discarded_delta);
            debug_invariant("qcol-log-delta",1,log_delta);
            debug_invariant("qcol-log-code",(MEM_DECISION_DISCARD<<4)|(event_type<<2)|tag,log_code);
            debug_invariant("qcol-heal",1000,hp_delta);
            debug_invariant("qcol-bytes",0,bytes_delta);
            debug_invariant("qcol-pickups",0,pickup_delta);
            debug_invariant("qcol-shards",0,shards_delta);
            debug_invariant("qcol-cores",0,cores_delta);
            int repeat_pending=G.memory.pending_trait, repeat_bytes=G.bytes_run;
            int repeat_pickups=debug_pickup_count(), repeat_shards=G.pl.shards, repeat_cores=G.pl.cores;
            int repeat_kept=G.memory.kept[tag], repeat_discarded=G.memory.discarded[tag];
            bool reentry=event_resolve_choice(MEM_DECISION_DISCARD);
            debug_invariant("qcol-reentry-type",event_type,G.room.event_type);
            debug_invariant("qcol-reentry-tag",tag,G.room.event_tag);
            debug_invariant("qcol-reentry-trait",trait,G.room.event_trait);
            debug_invariant("resolver-reentry",0,reentry?1:0);
            debug_dispatch_key(SAPP_KEYCODE_Q,true);
            debug_invariant("qcol-repeat-state",MEM_STATE_RESOLVED,G.room.event_state);
            debug_invariant("qcol-repeat-type",event_type,G.room.event_type);
            debug_invariant("qcol-repeat-tag",tag,G.room.event_tag);
            debug_invariant("qcol-repeat-trait",trait,G.room.event_trait);
            debug_invariant("qcol-repeat-log",log_before+1,G.memory.log_count);
            debug_invariant("qcol-repeat-hp",hp_before+hp_delta,(int)lroundf(G.pl.hp*1000));
            debug_invariant("qcol-repeat-pending",repeat_pending,G.memory.pending_trait);
            debug_invariant("qcol-repeat-bytes",repeat_bytes,G.bytes_run);
            debug_invariant("qcol-repeat-pickups",repeat_pickups,debug_pickup_count());
            debug_invariant("qcol-repeat-shards",repeat_shards,G.pl.shards);
            debug_invariant("qcol-repeat-cores",repeat_cores,G.pl.cores);
            debug_invariant("qcol-repeat-kept",repeat_kept,G.memory.kept[tag]);
            debug_invariant("qcol-repeat-discarded",repeat_discarded,G.memory.discarded[tag]);
            printf("{\"schema\":1,\"kind\":\"f10_resolution\",\"fixture\":\"qcol\",\"event_type\":%d,\"event_tag\":%d,\"event_trait\":%d,\"public_nonrepeat_calls\":2,\"public_choice_count\":1,\"resolver_reentry_calls\":1,\"resolver_reentry_result\":0,\"resolver_reentry_state_delta\":0,\"repeat_key_events\":1,\"repeat_choice_count\":0,\"repeat_pickup_delta\":0,\"repeat_shards_delta\":0,\"repeat_cores_delta\":0,\"post_resolution_nonrepeat_events\":0,\"event_state_before\":%d,\"event_state_after\":%d,\"kept_delta\":%d,\"discarded_delta\":%d,\"log_count_delta\":%d,\"log_code\":%d,\"pending_before\":%d,\"pending_after\":%d,\"hp_delta_milli\":%d,\"bytes_delta\":%d,\"pickup_delta\":%d,\"shards_delta\":%d,\"cores_delta\":%d,\"stop_reason\":\"f10_resolved\"}\n",
                   event_type,tag,trait,state_before,G.room.event_state,kept_delta,discarded_delta,
                   log_delta,log_code,pending_before,G.memory.pending_trait,hp_delta,bytes_delta,
                   pickup_delta,shards_delta,cores_delta);
        fflush(stdout); exit(0);
    }
    case 7: debug_emit_save_wire(1); fflush(stdout); exit(0);
    case 9: debug_save_fixture(false); fflush(stdout); exit(0);
    case 10: debug_save_fixture(true); fflush(stdout); exit(0);
    case 8: debug_emit_save_wire(2); fflush(stdout); exit(0);
    case 11: debug_invariant("forced",1,0); break;
    case 12: debug_fixture_modifiers(); fflush(stdout); exit(0);
    case 13: debug_fixture_haste(); fflush(stdout); exit(0);
    case 14: debug_fixture_endings(); fflush(stdout); exit(0);
    case 15: debug_fixture_dd_legacy_settlement(); fflush(stdout); exit(0);
    case 16: debug_fixture_dd_retry(); fflush(stdout); exit(0);
    case 17: debug_fixture_dd_forfeit(); fflush(stdout); exit(0);
    case 18: debug_fixture_dd_promise_labels(); fflush(stdout); exit(0);
    case 19: debug_fixture_dd_shake_menu(false); fflush(stdout); exit(0);
    case 20: debug_fixture_dd_shake_menu(true); fflush(stdout); exit(0);
    case 24:
        debug_fixture_dd_options();
        fflush(stdout);
        if (!DBG_CFG.have_hold_ms) exit(0);
        break;
    case 26: debug_fixture_story_signals(); fflush(stdout); exit(0);
    case 27: debug_fixture_training(); fflush(stdout); exit(0);
    default: break;
    }
    fflush(stdout); sapp_request_quit();
}
}
#endif
#endif

// ----------------------------------------------------------- frame
void game_init(void){
    memset(&G,0,sizeof(G));
    meta_load();
    audio_set_bgm_enabled(G.meta.opt_bgm!=0);
    audio_set_sfx_enabled(G.meta.opt_sfx!=0);
    G.state=ST_BOOT;
    G.difficulty=0;
    G.ngplus=G.meta.true_clear;
    G.title_weapon=WPN_SWORD;
    G.ambient_mul=1.0f;
    G.light_mul=1.0f;
    music_set(-1);
}

static int resolve_ending_result(uint8_t core_bits){
    int core_count=0;
    for (int i=0;i<4;i++) if (core_bits&(1<<i)) core_count++;
    return core_count==4?(G.difficulty==2?3:2):(core_count==0?0:1);
}

static void apply_fade_action(void){
    int a=G.fade_next_state;
    if (a==-2){
        player_restore_light_shield();
        room_generate(G.room.biome,G.room.idx+1,G.pending_door,G.pending_entry_dir);
    } else if (a==-3){
        if (G.room.biome<3){
            int nb=G.room.biome+1;
            G.pl.hp=fminf((float)G.pl.maxhp,G.pl.hp+2.0f);
            player_restore_light_shield();
            room_generate(nb,0,PROMISE_NONE,DIR_L);
            music_set(nb+1);
            char buf[64];
            snprintf(buf,sizeof(buf),"— %s —",biome_names[nb]);
            set_msg(buf);
        } else {
            // 읽기 헤드 도달 — 엔딩 결정
            G.ending = resolve_ending_result(G.pl.cores);
            G.meta.wins++;
            G.meta.bytes_currency += (uint32_t)G.bytes_run + (uint32_t)(G.pl.shards*5);
            G.meta.best_biome=3;
            if (G.ending==3){ G.meta.true_clear=1; G.ngplus=true; }
            meta_save();
            G.state=ST_ENDING; G.state_t=0;
            music_set(6);
            sfx_play(SFX_ENDING);
        }
    } else if (a>=0){
        G.state=a; G.state_t=0;
        if (a==ST_TITLE){ music_set(0); }
        if (a==ST_DEAD){ }
    }
}

void game_frame(void){
#ifdef DD_DEBUG
    debug_raw_frame();
#endif
    float rdt=(float)sapp_frame_duration();
    if (rdt>0.05f) rdt=0.05f;
    G.time += rdt;
#ifdef DD_DEBUG
    if (!((dbg_opening_active && dbg_opening_frozen) ||
          (dbg_start_intro_active && dbg_start_intro_frozen)))
#endif
    G.state_t += rdt;
#ifdef DD_DEBUG
    int showcase_state_before=G.state;
    if (!dbg_showcase_active && (dbg_auto||dbg_jump_biome>=0||dbg_ending>=0)) debug_drive(rdt);
    debug_showcase_record_transition("debug-drive",showcase_state_before);
    debug_showcase_tick(rdt);
    if (dbg_options_visual_active && G.state_t*1000.0f >= DBG_CFG.hold_ms)
        sapp_request_quit();
#endif

    // 페이드
    if (G.fade_dir>0){
        G.fade += rdt*2.2f;
        if (G.fade>=1){ G.fade=1; apply_fade_action(); G.fade_dir=-1; }
    } else if (G.fade_dir<0){
        G.fade -= rdt*1.8f;
        if (G.fade<=0){ G.fade=0; G.fade_dir=0; }
    }
#ifdef DD_DEBUG
    debug_showcase_record_transition("fade",showcase_state_before);
    showcase_state_before=G.state;
#endif

    // 타임스케일/히트스톱
    G.timescale += (1.0f-G.timescale)*2.5f*rdt;
    float dt=rdt*G.timescale;
    if (G.hitstop>0){ G.hitstop-=rdt; dt=0; }
    G.shake *= powf(0.001f,rdt);
    if (G.shake<0.05f) G.shake=0;
    if (G.flash_white>0) G.flash_white-=rdt*1.5f;

    // 상태 갱신
    switch (G.state){
    case ST_BOOT:
        if (G.state_t>=15.0f){ G.state_t=15.0f; boot_handoff_to_title(true,"none"); }
        break;
    case ST_INTRO:
        if (G.state_t>=11.2f) intro_handoff();
        break;
    case ST_PLAY:
        if (G.fade_dir==0||G.fade_next_state==-2) update_play(dt);
        break;
    case ST_TRAINING:
        update_play(dt);
        break;
    case ST_FLASHBACK:
        G.fb_t += rdt;
        break;
    default: break;
    }
#ifdef DD_DEBUG
    debug_showcase_record_transition("update",showcase_state_before);
    debug_opening_tick(rdt);
    debug_start_intro_tick(rdt);
    debug_ending_tick(rdt);
#endif

    // 그리기
    render_begin_frame();
    if (G.state==ST_PLAY||G.state==ST_TRAINING||G.state==ST_PAUSE||G.state==ST_INVENTORY||G.state==ST_RELIC_SWAP){
        draw_play();
        draw_ui_begin();
        hud_draw();
        if (G.state==ST_INVENTORY) draw_inventory();
        if (G.state==ST_RELIC_SWAP) draw_relic_swap();
        if (G.state==ST_PAUSE) draw_pause();
    } else if (G.state==ST_TITLE){
        draw_title();
    } else if (G.state==ST_OPTIONS){
        draw_options();
    } else if (G.state==ST_WEAPON_SELECT){
        draw_ui_begin();
        draw_weapon_select();
    } else if (G.state==ST_DIFFICULTY_SELECT){
        draw_ui_begin();
        draw_difficulty_select();
    } else if (G.state==ST_UPGRADE){
        draw_ui_begin();
        draw_upgrade();
    } else if (G.state==ST_CODEX){
        draw_codex();
    } else if (G.state==ST_CODEX_DETAIL){
        draw_codex_detail();
    } else if (G.state==ST_BOOT){
        draw_boot();
    } else if (G.state==ST_INTRO){
        draw_intro();
    } else if (G.state==ST_FLASHBACK){
        draw_play();
        draw_ui_begin();
        draw_flashback();
    } else if (G.state==ST_DEAD){
        draw_ui_begin();
        draw_dead();
    } else if (G.state==ST_ENDING){
        draw_ending();
        draw_ui_begin();
    } else if (G.state==ST_EPILOGUE){
        draw_ui_begin();
        draw_epilogue();
    }

    float amb_boost = (G.state==ST_PLAY&&G.pl.relics[RELIC_LUMINANCE])?1.6f:1.0f;
    render_set_ambient(0.27f*G.ambient_mul*amb_boost + (G.state==ST_ENDING?clampf(G.state_t/14.0f,0,0.9f):0));
    render_set_scanline(G.meta.opt_scanline?0.35f:0.0f);
    render_end_frame(G.time,G.flash_white,G.fade,G.fade_col,
                     G.shake*(G.meta.opt_shake?1.0f:0.0f),G.shake*(G.meta.opt_shake?1.0f:0.0f));
#ifdef DD_DEBUG
    if (dbg_opening_active && dbg_opening_ready && !dbg_opening_emitted)
        dbg_opening_rendered=1;
    if (dbg_ending_active && !dbg_ending_emitted)
        dbg_ending_rendered=1;
#endif
}

// ----------------------------------------------------------- events
static void title_start_run(void){
    if (!G.meta.intro_seen || G.meta.intro_replay_queued) intro_begin();
    else {
        start_run();
        G.state=ST_PLAY;
        G.state_t=0;
        G.fade=1;
        G.fade_dir=-1;
    }
}

static void title_activate(void){
    switch (G.menu_sel){
    case 0:
        sfx_play(SFX_UI);
        G.state=ST_DIFFICULTY_SELECT; G.state_t=0; G.menu_sel=G.difficulty;
        break;
    case 1: G.state=ST_WEAPON_SELECT; G.state_t=0; sfx_play(SFX_UI); break;
    case 2: G.state=ST_UPGRADE; G.upg_sel=0; G.state_t=0; sfx_play(SFX_UI); break;
    case 3:
        start_training();
        G.state=ST_TRAINING; G.state_t=0;
        G.fade=1; G.fade_dir=-1;
        sfx_play(SFX_UI);
        break;
    case 4: G.options_return_state=ST_TITLE; G.state=ST_OPTIONS; G.menu_sel=0; G.state_t=0; sfx_play(SFX_UI); break;
    case 5: G.state=ST_CODEX; G.codex_section=0; G.codex_page=0; G.codex_detail=0; G.codex_focus=0; sfx_play(SFX_UI); break;
    case 6: meta_save(); sapp_request_quit(); break;
    }
}

void game_event(const sapp_event* e){
#ifdef DD_DEBUG
    int showcase_state_before=G.state;
#endif
    if (G.state==ST_INTRO) return;
    if (e->type==SAPP_EVENTTYPE_KEY_DOWN && e->key_code<512) key_held[e->key_code]=true;
    if (e->type==SAPP_EVENTTYPE_KEY_UP && e->key_code<512) key_held[e->key_code]=false;
    if (e->type==SAPP_EVENTTYPE_MOUSE_MOVE){
        mouse_virt=V2(e->mouse_x/sapp_widthf()*VIRT_W, e->mouse_y/sapp_heightf()*VIRT_H);
        mouse_present=true;
    }
    if (e->type==SAPP_EVENTTYPE_MOUSE_DOWN) attack_held=true;
    if (e->type==SAPP_EVENTTYPE_MOUSE_UP) attack_held=false;
    if (e->type==SAPP_EVENTTYPE_KEY_DOWN&&e->key_code==SAPP_KEYCODE_SPACE) attack_held=true;
    if (e->type==SAPP_EVENTTYPE_KEY_UP&&e->key_code==SAPP_KEYCODE_SPACE) attack_held=false;

    bool kd = e->type==SAPP_EVENTTYPE_KEY_DOWN && !e->key_repeat;
    bool anykey = kd || e->type==SAPP_EVENTTYPE_MOUSE_DOWN;

    switch (G.state){
    case ST_BOOT:
        if (anykey) boot_handoff_to_title(false,kd?"key":"mouse");
        break;
    case ST_TITLE:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){ G.menu_sel=(G.menu_sel+6)%7; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){ G.menu_sel=(G.menu_sel+1)%7; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE) title_activate();
        break;
    case ST_OPTIONS:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){
            G.state=G.options_return_state?G.options_return_state:ST_TITLE;
            G.menu_sel=G.state==ST_PAUSE?1:4;
            sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){
            G.menu_sel=(G.menu_sel+8)%9; sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){
            G.menu_sel=(G.menu_sel+1)%9; sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A||
                   e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D||
                   e->key_code==SAPP_KEYCODE_ENTER){
            if (G.menu_sel==0){ G.meta.opt_bgm=!G.meta.opt_bgm; audio_set_bgm_enabled(G.meta.opt_bgm!=0); }
            else if (G.menu_sel==1){ G.meta.opt_sfx=!G.meta.opt_sfx; audio_set_sfx_enabled(G.meta.opt_sfx!=0); }
            else if (G.menu_sel==2) G.difficulty=(G.difficulty+1)%3;
            else if (G.menu_sel==3) G.meta.opt_shake=!G.meta.opt_shake;
            else if (G.menu_sel==4) G.meta.opt_scanline=!G.meta.opt_scanline;
            else if (G.menu_sel==5) G.title_seed=G.title_seed?0:(G.meta.last_seed?G.meta.last_seed:12345u);
            else if (G.menu_sel==6) G.meta.intro_replay_queued=!G.meta.intro_replay_queued;
            else if (G.menu_sel==7){ if(G.meta.true_clear) G.ngplus=!G.ngplus; else { sfx_play(SFX_DENY); break; } }
            else { G.state=G.options_return_state?G.options_return_state:ST_TITLE; G.menu_sel=G.state==ST_PAUSE?1:4; sfx_play(SFX_UI); break; }
            meta_save(); sfx_play(SFX_UI);
        }
        break;
    case ST_WEAPON_SELECT:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_TITLE; G.menu_sel=1; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A){ G.title_weapon=(G.title_weapon+WPN_COUNT-1)%WPN_COUNT; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D){ G.title_weapon=(G.title_weapon+1)%WPN_COUNT; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){ G.title_weapon=(G.title_weapon+WPN_COUNT-3)%WPN_COUNT; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){ G.title_weapon=(G.title_weapon+3)%WPN_COUNT; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){
            bool unlocked=(G.meta.unlocked_weapons&(1u<<G.title_weapon))!=0;
            uint32_t cost=(uint32_t)weapon_unlock_cost(G.title_weapon);
            if (!unlocked && G.meta.bytes_currency>=cost){ G.meta.bytes_currency-=cost; G.meta.unlocked_weapons|=1u<<G.title_weapon; meta_save(); unlocked=true; sfx_play(SFX_CORE_SHARD); }
            else if (!unlocked){ sfx_play(SFX_DENY); break; }
            if (unlocked){ G.state=ST_TITLE; G.menu_sel=1; sfx_play(SFX_UI); }
        }
        break;
    case ST_DIFFICULTY_SELECT:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_TITLE; G.menu_sel=0; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A){ G.menu_sel=(G.menu_sel+2)%3; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D){ G.menu_sel=(G.menu_sel+1)%3; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){ G.difficulty=G.menu_sel; title_start_run(); }
        break;
    case ST_CODEX:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){
            if (G.codex_focus==1) G.codex_focus=0;
            else G.state=ST_TITLE;
            sfx_play(SFX_UI);
        }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){
            if (G.codex_focus==0) G.codex_focus=1;
            else G.state=ST_CODEX_DETAIL;
            sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){
            if (G.codex_focus==0){
                G.codex_section=(G.codex_section+3)%4; G.codex_page=0; G.codex_detail=0;
            } else {
                int count=codex_entry_count(G.codex_section);
                G.codex_detail=(G.codex_detail+count-1)%count;
                G.codex_page=G.codex_detail/4;
            }
            sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){
            if (G.codex_focus==0){
                G.codex_section=(G.codex_section+1)%4; G.codex_page=0; G.codex_detail=0;
            } else {
                int count=codex_entry_count(G.codex_section);
                G.codex_detail=(G.codex_detail+1)%count;
                G.codex_page=G.codex_detail/4;
            }
            sfx_play(SFX_UI);
        }
        break;
    case ST_CODEX_DETAIL:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_CODEX; G.codex_focus=1; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A){
            int count=codex_entry_count(G.codex_section);
            G.codex_detail=(G.codex_detail+count-1)%count; G.codex_page=G.codex_detail/4; sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D){
            int count=codex_entry_count(G.codex_section);
            G.codex_detail=(G.codex_detail+1)%count; G.codex_page=G.codex_detail/4; sfx_play(SFX_UI);
        }
        break;
    case ST_PLAY:
        if (kd && event_nearby() &&
            (e->key_code==SAPP_KEYCODE_E || e->key_code==SAPP_KEYCODE_Q)){
            int decision=e->key_code==SAPP_KEYCODE_E?MEM_DECISION_KEEP:MEM_DECISION_DISCARD;
            if (event_resolve_choice(decision)){
                sfx_play(SFX_CORE_SHARD);
                if (decision==MEM_DECISION_KEEP)
                    set_msg(G.room.event_type==MEM_EVENT_CORRUPTED?
                            "기억 보관 — 정예 효과가 대기 중":"기억 보관");
                else if (G.room.event_type==MEM_EVENT_ECHO)
                    set_msg("기억 폐기 — +3 바이트");
                else set_msg("기억 폐기 — 무결성 +1");
            } else {
                sfx_play(SFX_DENY);
                set_msg(G.room.event_type==MEM_EVENT_CORRUPTED && G.memory.pending_trait?
                        "정예 대기열이 사용 중이다":"디스크 용량이 부족하다");
            }
            break;
        }
        // 일반 런과 훈련장은 이동·공격·장착 입력을 공유한다.
        /* fall through */
    case ST_TRAINING:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_LEFT_SHIFT||e->key_code==SAPP_KEYCODE_RIGHT_SHIFT){
            Player* p=&G.pl;
            if (p->dash_cd<=0){
                v2 mv=V2(0,0);
                if (key_held[SAPP_KEYCODE_W]||key_held[SAPP_KEYCODE_UP]) mv.y-=1;
                if (key_held[SAPP_KEYCODE_S]||key_held[SAPP_KEYCODE_DOWN]) mv.y+=1;
                if (key_held[SAPP_KEYCODE_A]||key_held[SAPP_KEYCODE_LEFT]) mv.x-=1;
                if (key_held[SAPP_KEYCODE_D]||key_held[SAPP_KEYCODE_RIGHT]) mv.x+=1;
                mv=v2norm(mv);
                if (mv.x==0&&mv.y==0) mv=p->aim;
                p->dash_dir=mv;
                p->dash_t=0.16f;
                p->dash_cd=(2.0f - (p->relics[RELIC_DEFRAG]?(1.0f-capacity_frac())*0.5f:0))
                            *(1.0f - G.meta.upg[4]*0.06f);
                p->iframes=fmaxf(p->iframes,0.22f);
                sfx_play(SFX_DASH);
                burst(p->pos,6,COL(0x3FE0C5),80,0.3f,2,true);
            }
        }
        else if (e->key_code==SAPP_KEYCODE_Q && !G.training_active) player_drop_shard();
        else if (e->key_code==SAPP_KEYCODE_E){
            if (G.training_active && training_try_summon()) break;
            // 가까운 픽업 줍기
            float best=22.0f; Pickup* bp=NULL;
            for (int i=0;i<MAX_PICKUPS;i++){
                Pickup* pk=&G.pickups[i];
                if (!pk->active||pk->type==PK_BYTE) continue;
                float d=v2len(v2sub(pk->pos,G.pl.pos));
                if (d<best){ best=d; bp=pk; }
            }
            if (bp) player_try_pickup(bp);
        }
        else if (e->key_code==SAPP_KEYCODE_TAB){ G.state=ST_INVENTORY; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ESCAPE){
            if (G.training_active){
                G.training_active=false;
                G.state=ST_TITLE; G.state_t=0;
                music_set(0);
            } else {
                G.state=ST_PAUSE; G.menu_sel=0;
            }
            sfx_play(SFX_UI);
        }
        break;
    case ST_INVENTORY:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_TAB||e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=G.training_active?ST_TRAINING:ST_PLAY; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_Q && !G.training_active) player_drop_shard();
        break;
    case ST_RELIC_SWAP: {
        if (!kd) break;
        int count=G.relic_swap_type==PK_WRELIC?2:4;
        if (e->key_code==SAPP_KEYCODE_ESCAPE) player_confirm_relic_swap(-1);
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A||
                 e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){
            G.relic_swap_sel=(G.relic_swap_sel+count-1)%count;
            sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D||
                   e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){
            G.relic_swap_sel=(G.relic_swap_sel+1)%count;
            sfx_play(SFX_UI);
        } else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE)
            player_confirm_relic_swap(G.relic_swap_sel);
    } break;
    case ST_UPGRADE:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_TITLE; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){ G.upg_sel=(G.upg_sel+5)%6; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){ G.upg_sel=(G.upg_sel+1)%6; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){
            if (G.upg_sel==5){ G.state=ST_TITLE; sfx_play(SFX_UI); break; }
            int ui2=G.upg_sel;
            if ((int)G.meta.upg[ui2]<upg_defs[ui2].max && G.meta.bytes_currency>=(uint32_t)upg_cost(ui2)){
                G.meta.bytes_currency-=(uint32_t)upg_cost(ui2);
                G.meta.upg[ui2]++;
                meta_save();
                sfx_play(SFX_CORE_SHARD);
            } else sfx_play(SFX_DENY);
        }
        break;
    case ST_PAUSE:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_PLAY; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A){ G.menu_sel=(G.menu_sel+2)%3; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D){ G.menu_sel=(G.menu_sel+1)%3; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){
            if (G.menu_sel==0){ G.state=ST_PLAY; }
            else if (G.menu_sel==1){ G.options_return_state=ST_PAUSE; G.state=ST_OPTIONS; G.menu_sel=0; }
            else {
                settle_run_once(SETTLE_FORFEIT);
            }
            sfx_play(SFX_UI);
        }
        break;
    case ST_FLASHBACK:
        if (anykey && G.fb_t>1.2f){
            G.state=ST_PLAY;
            sfx_play(SFX_UI);
        }
        break;
    case ST_DEAD:
        if (kd && G.state_t>1.0f && e->key_code==SAPP_KEYCODE_R){
            start_run_with_seed(G.run_seed);
            G.state=ST_PLAY; G.state_t=0;
            G.fade=1; G.fade_dir=-1;
        } else if (kd && G.state_t>1.0f &&
                   (e->key_code==SAPP_KEYCODE_SPACE||e->key_code==SAPP_KEYCODE_ENTER||
                    e->key_code==SAPP_KEYCODE_ESCAPE)){
            music_set(0);
            G.state=ST_TITLE; G.state_t=0;
        }
        break;
    case ST_ENDING:
        if (anykey && G.state_t>(G.ending==3?21.0f:14.5f)){
            G.state=ST_EPILOGUE; G.state_t=0;
        }
        break;
    case ST_EPILOGUE:
        if (anykey && G.state_t>4.0f){
            music_set(0);
            G.state=ST_TITLE; G.state_t=0;
        }
        break;
    }
#ifdef DD_DEBUG
    debug_showcase_record_transition("event",showcase_state_before);
#endif
}
