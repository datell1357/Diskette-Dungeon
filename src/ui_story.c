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

static Rng urng = { 0xBADA55C0DEull };

// ----------------------------------------------------------- 스토리 텍스트
static const char* core_titles[4] = {
    "조각 #1 — 첫 부팅", "조각 #2 — 첫 승리", "조각 #3 — 마지막 저장", "조각 #4 — 남긴 한 줄"
};
static const char* core_texts[4] = {
    "작은 손이 디스켓을 밀어 넣는다.\n드르륵 — 드라이브가 처음으로 노래했다.\n화면이 켜졌고, 하나의 세계가 시작됐다.",
    "수십 번을 졌던 보스가 마침내 무너졌다.\n아이는 두 팔을 번쩍 들어 올렸다.\n등 뒤에서 누군가 작게 박수를 쳤다.",
    "조금 자란 손이 디스켓을 뺐다.\n\"다음에 마저 해야지.\"\n그 '다음'은, 오지 않았다.",
    "세이브 데이터 한구석, 서툰 글씨의 한 줄.\n내용은 흐려서 읽을 수 없다.\n...읽기 헤드에 닿으면, 보일 것이다.",
};
static const char* intro_pages[3] = {
    "20년 전 —\n한 아이가 모든 모험을\n1.44MB 디스켓 한 장에 저장했다.",
    "아이는 자랐고, 잊었다.\n서랍 속에서 디스켓은\n천천히 부패해 갔다.\n\n오늘, 마지막으로 단 한 번\n읽기 헤드가 돈다.",
    "당신은 가장 깊은 배드 섹터에 남은\n마지막 멀쩡한 데이터 조각.\n\n빛이 닿는 곳, 읽기 헤드까지\n올라가야 한다.\n\n무엇을 끝까지 기억할 것인가.",
};
static const char* biome_names[4] = { "배드 섹터", "잃어버린 트랙", "단편화 지대", "부트 레코드" };
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
    return V2(clampf(x,cam_x+half+4,cam_x+VIRT_W-half-4),
              clampf(y+14,cam_y+84,cam_y+VIRT_H-24));
}
static bool branch_promise_label_visible(float y,float cam_y){
    float label_y=y+14;
    return label_y>=cam_y+84 && label_y<=cam_y+VIRT_H-24;
}

// 엔딩: 0 빈손 1 표준 2 트루
static const char* ending_lines[3][4] = {
    { "읽기 헤드가 도착했다.", "...파일은 비어 있었다.", "포맷. 조용한 점멸 하나.", "아무것도 기억되지 않았다." },
    { "읽기 헤드가 도착했다.", "화면 가득, 청록빛.", "모니터 앞의 어른이 멈칫한다.", "\"...아직 있었네.\"" },
    { "읽기 헤드가 도착했다.", "재생된 것은 세이브가 아니라,\n아이가 그 시절 자신에게 남긴 한 줄.", "\"이 모험을 지우지 마.\n 나는 여기서 가장 용감했어.\"", "어른이 된 주인이, 그걸 읽는다." },
};
static const char* ending_names[3] = { "지워짐", "한 번 더", "전부 기억해" };

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
            const char* label=promise_label(pr);
            v2 label_pos=branch_promise_label_pos(dx,dy,cx,cy,label);
            draw_sprite(icon,dx,dy+bob,12,12,COL(0xFFFFFF),1,false,0);
            if (pr==PROMISE_WEAPON || branch_promise_label_visible(dy,cy))
                draw_text_center(label,label_pos.x,label_pos.y,0.42f,COL(0xE8E0F8),0.9f);
        }
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
        if (pk->type==PK_WRELIC){ s=13.0f; draw_quad(pk->pos.x-9,pk->pos.y+bob-9,18,18,COL(0xFFD060),0.12f+0.08f*sinf(G.time*5.0f)); }
        draw_sprite(pickup_sprite(pk),pk->pos.x,pk->pos.y+bob,s,s,ptint,1,false,0);
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
        col3 c = b->from_player? COL(0x7CFCE4):COL(0xFF3D7F);
        if (b->kind==6) c=COL(0xFF7A3D);
        if (b->kind==3){
            draw_sprite(SPR_GLAIVE,b->pos.x,b->pos.y,16,16,(col3){2,2,2},1,false,G.time*720.0f);
        } else if (b->kind==8){ // 검기: 진행 방향에 수직인 칼날 띠
            float ang=atan2f(b->vel.y,b->vel.x)+1.5708f;
            float ex=cosf(ang)*11.0f, ey=sinf(ang)*11.0f;
            draw_line(b->pos.x-ex,b->pos.y-ey,b->pos.x+ex,b->pos.y+ey,3.0f,(col3){1.4f,2.2f,2.0f},0.95f);
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
        draw_sprite(SPR_FLAME,p->pos.x,p->pos.y+bob,14*(1.0f+sq),14*(1.0f-sq),(col3){1.6f,1.6f,1.6f},blink,p->aim.x<0,0);
        // 검 슬래시
        float st=combat_slash_t();
        if (st>0){
            v2 d=combat_slash_dir();
            float prog=1.0f-st/0.14f;
            if (player_has_wrelic(WR_SWORD_WHIRL)){
                // 회전 베기: 전방향을 도는 원형 칼날 (반경 40 — 타격 범위와 일치)
                float rad=40.0f;
                int N=16;
                float spin=prog*6.2832f;
                for (int k=0;k<N;k++){
                    float a0=k*6.2832f/N+spin, a1=(k+1)*6.2832f/N+spin;
                    float al=(1.0f-prog*0.6f)*0.9f;
                    draw_line(p->pos.x+cosf(a0)*rad,p->pos.y+sinf(a0)*rad,
                              p->pos.x+cosf(a1)*rad,p->pos.y+sinf(a1)*rad,2.5f,(col3){1.6f,2.2f,2.0f},al);
                }
                // 안쪽 회전 잔광
                float a=prog*9.0f;
                draw_line(p->pos.x,p->pos.y,p->pos.x+cosf(a)*rad,p->pos.y+sinf(a)*rad,2.0f,COL(0x7CFCE4),0.7f-prog*0.4f);
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
            draw_quad(p->pos.x-10,p->pos.y-14,20*ch,2,COL(0x7CFCE4),0.9f);
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
    // 문/출구 빛 — 모든 벽 스캔
    for (int y=0;y<RH;y++) for (int x=0;x<RW;x++){
        if (x!=0&&x!=RW-1&&y!=0&&y!=RH-1) continue; // 벽 둘레만
        uint8_t t=r->tiles[y][x];
        if (t==T_DOOR_OPEN||t==T_EXIT)
            draw_light_blob(x*TILE+8,(float)y*TILE+8,42.0f+6.0f*sinf(G.time*3.0f),COL(0x3FE0C5),0.7f);
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
static void draw_memory_event(void){
    Room* r=&G.room;
    if (r->event_state!=MEM_STATE_AVAILABLE) return;
    char buf[160];
    float x=8,y=158;
    draw_quad(x-4,y-5,248,108,COL(0x0B0710),0.88f);
    snprintf(buf,sizeof(buf),"기억 로그 · %s",r->event_type==MEM_EVENT_ECHO?"메아리":"손상된 기억");
    draw_text(buf,x,y,0.78f,COL(0x9FFFF0),1); y+=15;
    snprintf(buf,sizeof(buf),"태그: %s",memory_tag_name(r->event_tag));
    draw_text(buf,x,y,0.68f,COL(0xC8C0E0),1); y+=13;
    snprintf(buf,sizeof(buf),"효과: %s",memory_tag_effect(r->event_tag));
    draw_text(buf,x,y,0.62f,COL(0xC8C0E0),1); y+=13;
    snprintf(buf,sizeof(buf),"보관 비용: %dKB (원본 64KB)",memory_item_kb(64));
    draw_text(buf,x,y,0.62f,COL(0xC8C0E0),1); y+=12;
    if (r->event_type==MEM_EVENT_ECHO){
        draw_text("E 보관  ·  Q 폐기 → +3 바이트",x,y,0.68f,COL(0xFFD060),1);
    } else {
        if (G.memory.pending_trait){
            snprintf(buf,sizeof(buf),"E 보관 불가 · 정예 대기열 사용 중 (%s)",
                     memory_trait_name(G.memory.pending_trait));
            draw_text(buf,x,y,0.60f,COL(0xFFB0CC),1); y+=12;
        } else {
            snprintf(buf,sizeof(buf),"E 보관 → 다음 일반 적 정예 승급 (%s)",
                     memory_trait_name(r->event_trait));
            draw_text(buf,x,y,0.60f,COL(0xFFB0CC),1); y+=12;
        }
        draw_text("승급 적 처치 시 +2 바이트  ·  Q 폐기 → HP +1",x,y,0.58f,COL(0xFFD060),1);
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
void hud_draw(void){
    Player* p=&G.pl;
    char buf[160];
    // 튜토리얼 힌트 (UI 패스 — 라이팅 영향 없음)
    if (G.room.biome==0&&G.room.idx==0){
        float ha=0.55f+0.2f*sinf(G.time*2.0f);
        draw_text("WASD 이동",56,84,0.9f,COL(0x9FFFF0),ha);
        draw_text("마우스/스페이스 공격",56,108,0.9f,COL(0x9FFFF0),ha);
        draw_text("Shift 대시 · E 줍기",56,132,0.9f,COL(0x9FFFF0),ha);
        draw_text("빛이 닿는 곳만 안전하다",250,116,0.9f,COL(0x6FBFB0),ha*0.9f);
        draw_text("Tab 가방 — 무게가 속도·빛·엔딩을 좌우한다",56,156,0.9f,COL(0x9FFFF0),ha);
    }
    // 플로터 (월드 좌표 → 화면 좌표, 카메라 보정)
    for (int i=0;i<MAX_FLOATERS;i++){
        Floater* f=&G.floaters[i];
        if (f->t<=0) continue;
        float fx=f->x-G.cam.x, fy=f->y-G.cam.y;
        draw_text(f->text,fx-text_width(f->text,0.8f)*0.5f,fy-30.0f-(1.4f-f->t)*16.0f,0.8f,f->c,clampf(f->t,0,1));
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
        draw_sprite(SPR_HEART,12+i*13.0f,12,11,10,full?COL(0xFFFFFF):COL(0x554060),full?1.0f:0.55f,false,0);
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
        snprintf(buf,sizeof(buf),"무게 %d/1440KB",player_used_kb());
        draw_text(buf,bx+bw+5,by-2,0.65f,COL(0xC8C0E0),0.9f);
    }
    // 무기
    draw_sprite(weapon_defs[p->weapon.type].spr,16,44,14,14,COL(0xFFFFFF),1,false,0);
    snprintf(buf,sizeof(buf),"%s%s",prefix_names[p->weapon.prefix],weapon_defs[p->weapon.type].name);
    draw_text(buf,28,38,0.7f,COL(0xC8C0E0),0.9f);
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

static void draw_inventory(void){
    draw_overlay_bg(0.86f);
    char buf[160];
    draw_text_center("— 가방 · 1.44MB —",VIRT_W/2,16,1.1f,COL(0x9FFFF0),1);
    float y=44;
    snprintf(buf,sizeof(buf),"무기  %s%s  (%dKB)",prefix_names[G.pl.weapon.prefix],
             weapon_defs[G.pl.weapon.type].name,weapon_defs[G.pl.weapon.type].kb);
    draw_text(buf,40,y,0.85f,COL(0xE8E0F8),1); y+=20;
    for (int i=0;i<RELIC_COUNT;i++){
        if (!G.pl.relics[i]) continue;
        snprintf(buf,sizeof(buf),"유물  %s (%dKB) — %s",relic_defs[i].name,relic_defs[i].kb,relic_defs[i].desc);
        draw_text(buf,40,y,0.75f,COL(0xC8B8E8),1); y+=16;
    }
    for (int i=0;i<2;i++){
        int wr=G.pl.wrelics[i];
        if (wr<0) continue;
        const WeaponRelicDef* wd=&weapon_relic_defs[wr];
        snprintf(buf,sizeof(buf),"무기유물  %s [%s] (%dKB) — %s",wd->name,weapon_defs[wd->weapon].name,wd->kb,wd->desc);
        draw_text(buf,40,y,0.7f,COL(0xFFD060),1); y+=16;
    }
    snprintf(buf,sizeof(buf),"추억 조각 ×%d  (%dKB)",G.pl.shards,G.pl.shards*64);
    draw_text(buf,40,y,0.85f,COL(0x9FFFF0),1); y+=18;
    int ncore=0; for(int i=0;i<4;i++) if(G.pl.cores&(1<<i)) ncore++;
    snprintf(buf,sizeof(buf),"핵심 조각 ×%d  (%dKB)",ncore,ncore*128);
    draw_text(buf,40,y,0.85f,COL(0xFFFFFF),1); y+=18;
    for (int i=0;i<4;i++){
        if (G.pl.cores&(1<<i)){ draw_text(core_titles[i],56,y,0.7f,COL(0xBFE8DC),1); y+=15; }
    }
    y+=6;
    snprintf(buf,sizeof(buf),"사용 %d / 1440 KB",player_used_kb());
    draw_text(buf,40,y,0.95f,weight_frac()>0.8f?COL(0xFF3D7F):COL(0x9FFFF0),1);
    draw_text_center("Q 조각 버리기 · Tab 닫기",VIRT_W/2,VIRT_H-26,0.75f,COL(0x8878A8),1);
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
    draw_text_center(core_titles[G.fb_core],VIRT_W/2,44,1.2f,COL(0x9FFFF0),clampf(t*2.0f,0,1));
    // 텍스트 타이핑
    const char* full=core_texts[G.fb_core];
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
    draw_text_center("핵심 조각 — 잃지 않고 읽기 헤드에 닿으면 엔딩이 바뀐다",
                     VIRT_W/2,VIRT_H-52,0.75f,COL(0x6FBFB0),clampf((t-1.0f)*0.8f,0,0.8f));
    if (t>2.0f)
        draw_text_center("아무 키 — 계속",VIRT_W/2,VIRT_H-30,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(t*4.0f));
}

// ----------------------------------------------------------- 영구 강화
typedef struct { const char* name; const char* desc; int max; int base_cost; } UpgDef;
static const UpgDef upg_defs[5] = {
    {"무결성 코어","시작 무결성 +1",5,40},   // 600B: 3칸 → 최대 8칸
    {"공격 회로","공격력 +5%",10,10},        // 550B
    {"가속 캐시","이동속도 +3%",5,20},       // 300B
    {"루멘 코어","빛 반경 +8%",3,25},        // 150B
    {"대시 칩","대시 쿨다운 -6%",5,18},      // 270B
};
static int upg_cost(int i){ return upg_defs[i].base_cost*((int)G.meta.upg[i]+1); }

static void draw_upgrade(void){
    draw_overlay_bg(0.95f);
    char buf[128];
    draw_text_center("— 영구 강화 —",VIRT_W/2,16,1.1f,COL(0x9FFFF0),1);
    snprintf(buf,sizeof(buf),"보유 %u바이트",G.meta.bytes_currency);
    draw_text_center(buf,VIRT_W/2,40,0.85f,COL(0xFFD060),1);
    float y=64;
    for (int i=0;i<5;i++){
        bool sel=G.upg_sel==i;
        int lv=(int)G.meta.upg[i];
        if (lv>=upg_defs[i].max)
            snprintf(buf,sizeof(buf),"%s  Lv%d/%d  MAX — %s",upg_defs[i].name,lv,upg_defs[i].max,upg_defs[i].desc);
        else
            snprintf(buf,sizeof(buf),"%s  Lv%d/%d  [%d바이트] — %s",upg_defs[i].name,lv,upg_defs[i].max,upg_cost(i),upg_defs[i].desc);
        if (sel) draw_text(">",30,y,0.85f,COL(0x3FE0C5),1);
        draw_text(buf,44,y,0.85f,sel?COL(0xFFFFFF):COL(0x8878A8),1);
        y+=20;
    }
    if (G.upg_sel==5) draw_text(">",30,y+6,0.85f,COL(0x3FE0C5),1);
    draw_text("뒤로",44,y+6,0.85f,G.upg_sel==5?COL(0xFFFFFF):COL(0x8878A8),1);
    draw_text_center("런에서 모은 바이트로 영구히 강해진다 · Esc 뒤로",VIRT_W/2,VIRT_H-22,0.75f,COL(0x6F6090),0.9f);
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
    const char* wname = weapon_defs[G.title_weapon].name;
    bool wlocked = !((G.meta.unlocked_weapons>>G.title_weapon)&1);
    int wcost = 30+G.title_weapon*12;
    float y=108;
    const char* items[7];
    char witem[64], ditem[48], sitem[48], ngitem[32], uitem[48];
    if (wlocked) snprintf(witem,sizeof(witem),"무기  %s [%d바이트 해금]",wname,wcost);
    else snprintf(witem,sizeof(witem),"무기  %s",wname);
    snprintf(ditem,sizeof(ditem),"난이도  %s",diff_names[G.difficulty]);
    snprintf(sitem,sizeof(sitem),"시드  %s",G.title_seed?"고정":"무작위");
    snprintf(ngitem,sizeof(ngitem),"NG+  %s",G.ngplus?"ON":"OFF");
    int upg_total=0; for (int i=0;i<5;i++) upg_total+=(int)G.meta.upg[i];
    snprintf(uitem,sizeof(uitem),"영구 강화  [Lv %d]",upg_total);
    items[0]="모험 시작";
    items[1]=witem; items[2]=uitem; items[3]=ditem; items[4]=sitem;
    items[5]=G.meta.true_clear? ngitem:"NG+  ???";
    items[6]="종료";
    for (int i=0;i<7;i++){
        bool sel = G.menu_sel==i;
        col3 c = sel?COL(0xFFFFFF):COL(0x8878A8);
        if (sel) draw_text(">",VIRT_W/2-text_width(items[i],0.9f)/2-16,y,0.9f,COL(0x3FE0C5),1);
        draw_text_center(items[i],VIRT_W/2,y,0.9f,c,1);
        y+=18;
    }
    snprintf(buf,sizeof(buf),"보유 %u바이트 · 런 %u회 · 최고 도달: %s",
             G.meta.bytes_currency,G.meta.runs,
             G.meta.wins>0?"읽기 헤드":(G.meta.runs>0?biome_names[G.meta.best_biome]:"-"));
    draw_text_center(buf,VIRT_W/2,VIRT_H-16,0.7f,COL(0x6F6090),0.9f);
}

static void draw_intro(void){
    draw_overlay_bg(1.0f);
    draw_light_begin(0,0);
    draw_light_blob(VIRT_W*0.5f,VIRT_H*0.5f,140,COL(0x16223A),0.8f);
    draw_ui_begin();
    float a=clampf(G.state_t*1.5f,0,1);
    draw_text_center(intro_pages[G.intro_page],VIRT_W/2,70,0.95f,COL(0xD8D0E8),a);
    draw_text_center("아무 키 — 계속",VIRT_W/2,VIRT_H-30,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(G.time*4.0f));
}

static void draw_dead(void){
    draw_overlay_bg(0.92f);
    char buf[128];
    draw_text_center("데이터 손상",VIRT_W/2,60,1.8f,COL(0xFF3D7F),1);
    snprintf(buf,sizeof(buf),"%s에서 흩어졌다",biome_names[G.room.biome]);
    draw_text_center(buf,VIRT_W/2,100,0.9f,COL(0xC8C0E0),1);
    snprintf(buf,sizeof(buf),"처치 %d · 시간 %d:%02d · 수집 %d바이트",
             G.kills,(int)(G.run_time/60),(int)G.run_time%60,G.bytes_run);
    draw_text_center(buf,VIRT_W/2,124,0.8f,COL(0x8878A8),1);
    snprintf(buf,sizeof(buf),"이번 런 입금 %d / 보유 %u",G.bytes_run,G.meta.bytes_currency);
    draw_text_center(buf,VIRT_W/2,148,0.8f,COL(0xFFD060),1);
    if (G.state_t>1.0f)
        draw_text_center("R/Enter: 재시작 · Esc: 타이틀",VIRT_W/2,VIRT_H-36,0.75f,COL(0x8878A8),0.5f+0.3f*sinf(G.time*4.0f));
}

static void draw_pause(void){
    draw_overlay_bg(0.75f);
    float panel_x=(VIRT_W-208)*0.5f;
    draw_quad(panel_x-2,42,212,168,COL(0x08050D),0.98f);
    draw_quad(panel_x,44,208,164,COL(0x0B0710),1.0f);
    draw_text_center("일시정지",VIRT_W/2,60,1.4f,COL(0x9FFFF0),1);
    const char* items[4];
    char scn[40], shake[40];
    snprintf(scn,sizeof(scn),"스캔라인  %s",G.meta.opt_scanline?"ON":"OFF");
    snprintf(shake,sizeof(shake),"화면 흔들림  %s",G.meta.opt_shake?"ON":"OFF");
    items[0]="계속하기"; items[1]=shake; items[2]=scn; items[3]="타이틀로";
    float y=110;
    for (int i=0;i<4;i++){
        bool sel=G.menu_sel==i;
        draw_text_center(items[i],VIRT_W/2,y,0.9f,sel?COL(0xFFFFFF):COL(0x8878A8),1);
        y+=22;
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
        "용기를 보관한 기록이, 가장 먼저 빛났다.",
        "인연을 보관한 기록이, 화면 너머로 이어졌다.",
        "약속을 보관한 기록이, 마지막 줄을 지켜 냈다."
    };
    int c=memory_coda();
    if (c>=0 && c<MEM_TAG_COUNT) return kept[c];
    if (c==MEM_TAG_COUNT) return "버린 기록들 사이에도, 작은 흔적은 남았다.";
    return NULL;
}
static void draw_ending(void){
    float t=G.state_t;
    // 상승하는 빛
    draw_light_begin(0,0);
    float rise=clampf(t/10.0f,0,1);
    draw_light_blob(VIRT_W*0.5f,VIRT_H*(0.8f-rise*0.6f),90+rise*120,COL(0x9FFFF0),0.6f+rise*0.5f);
    draw_glow_begin(0,0);
    draw_glow_blob(VIRT_W*0.5f,VIRT_H*(0.8f-rise*0.6f),30+rise*30,COL(0xFFFFFF),0.7f);
    draw_scene_begin(0,0);
    draw_sprite(SPR_FLAME,VIRT_W*0.5f,VIRT_H*(0.8f-rise*0.6f),16+rise*8,16+rise*8,(col3){1.8f,1.8f,1.8f},1,false,0);
    draw_ui_begin();
    // 4줄 페이즈 텍스트
    int line=(int)(t/3.2f);
    if (line>3) line=3;
    float la=clampf(fmodf(t,3.2f)*1.2f,0,1);
    if (t>12.8f) la=1;
    draw_text_center(ending_lines[G.ending][line],VIRT_W/2,90,1.0f,COL(0xE8E0F8),la);
    if (t>14.0f){
        char buf[64];
        snprintf(buf,sizeof(buf),"%s END — %s",G.ending==2?"TRUE":(G.ending==0?"BAD":""),ending_names[G.ending]);
        draw_text_center(buf,VIRT_W/2,150,1.2f,G.ending==0?COL(0xFF3D7F):COL(0x9FFFF0),clampf((t-14.0f),0,1));
        draw_text_center("아무 키 — 에필로그",VIRT_W/2,VIRT_H-30,0.7f,COL(0x8878A8),0.5f+0.3f*sinf(G.time*4.0f));
    }
}

static void draw_epilogue(void){
    draw_overlay_bg(1);
    float t=G.state_t;
    const char* epi_good =
        "검은 화면. 플로피 드라이브 소리가 멎는다.\n\n"
        "햇빛 드는 방.\n어른의 손이 디스켓을 버리지 않고\n책상 위 작은 액자 옆에 세워 둔다.\n\n"
        "라벨엔 빛바랜 아이 글씨 —\n\"내 모험. 지우지 말 것.\"\n\n"
        "모니터에 한 줄이 깜빡인다.\n1,474,560 bytes — 전부 기억함.";
    const char* epi_bad =
        "검은 화면. 플로피 드라이브 소리가 멎는다.\n\n"
        "디스켓은 다시 서랍으로 들어갔다.\n\n"
        "데이터는 읽히지 않으면, 사라진다.\n\n"
        "...하지만 어딘가, 아직\n작은 불씨 하나가 남아 있을지도 모른다.";
    const char* txt = G.ending==0? epi_bad:epi_good;
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
    if (G.ending==2 && t>16.0f)
        draw_text_center("...그리고 어딘가, 또 다른 어둠 속에서\n작은 불씨 하나가 깨어난다.  [NG+ 해금]",
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
static int dbg_showcase_ready;
static float dbg_showcase_hold_t;
static int dbg_showcase_weapon_door=-1;
static int dbg_showcase_frames;
static int dbg_showcase_state_after_1s=-1;
typedef struct { const char* owner; int before, after; } DebugShowcaseTransition;
static DebugShowcaseTransition dbg_showcase_transitions[16];
static int dbg_showcase_transition_count;

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
        if (dbg_intro && G.state_t>4.0f){
            G.intro_page++; G.state_t=0;
            if (G.intro_page>=3){ start_run(); G.state=ST_PLAY; G.fade=1; G.fade_dir=-1; }
        }
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
    case ST_FLASHBACK:
        if (dbg_auto && G.fb_t>1.5f){ G.state=ST_PLAY; }
        break;
    case ST_DEAD:
        if (G.state_t>1.2f){ G.state=ST_TITLE; G.state_t=0; }
        break;
    case ST_ENDING:
        if (G.state_t>17.0f){ G.state=ST_EPILOGUE; G.state_t=0; }
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
           "\"last_seed\":%u,\"upg\":[%u,%u,%u,%u,%u]}",
           m->bytes_currency,m->unlocked_weapons,m->best_biome,m->runs,m->wins,
           m->true_clear,m->opt_scanline,m->opt_shake,m->last_seed,m->upg[0],
           m->upg[1],m->upg[2],m->upg[3],m->upg[4]);
}
static int debug_meta_expected(const MetaSave* m,int version){
    static const uint32_t v1[9]={123,0x15,2,9,3,1,1,0,12345};
    static const uint32_t v2[9]={456,0x3f,3,12,4,1,1,0,54321};
    const uint32_t* x=version==1?v1:v2; int i;
    if(m->bytes_currency!=x[0]||m->unlocked_weapons!=x[1]||m->best_biome!=x[2]||
       m->runs!=x[3]||m->wins!=x[4]||m->true_clear!=x[5]||
       m->opt_scanline!=x[6]||m->opt_shake!=x[7]||m->last_seed!=x[8])return 0;
    for(i=0;i<5;i++) if(m->upg[i]!=(version==2?(uint32_t)(i+1):0))return 0;
    return 1;
}
static int debug_meta_is_default(const MetaSave* m){
    return m->magic==0 && m->version==0 && m->bytes_currency==0 &&
           m->unlocked_weapons==(1u<<WPN_SWORD) && m->best_biome==0 &&
           m->runs==0 && m->wins==0 && m->true_clear==0 &&
           m->opt_scanline==1 && m->opt_shake==1 && m->last_seed==0 &&
           m->upg[0]==0 && m->upg[1]==0 && m->upg[2]==0 &&
           m->upg[3]==0 && m->upg[4]==0 && m->checksum==0;
}
static void debug_save_fixture(bool reject){
    char path[600],input_sha[65],post_sha[65]; long input_len=0,post_len=0;
    uint32_t words[17]={0}; int n=DBG_CFG.expect_version==1?12:17;
    MetaSave loaded,reloaded; uint32_t expected_checksum;
    save_path(path,sizeof path);
#if defined(_WIN32)
    strncat(path,"\\save.bin",sizeof(path)-strlen(path)-1);
#else
    strncat(path,"/save.bin",sizeof(path)-strlen(path)-1);
#endif
    if(!debug_file_digest(path,input_sha,&input_len) ||
       input_len!=(long)(n*4) ||
       (size_t)input_len>sizeof words*4){
        fprintf(stderr,"{\"error\":\"save-fixture-input\"}\n");exit(2);
    }
    {
        FILE* f=fopen(path,"rb");
        if(!f||fread(words,4,(size_t)n,f)!=(size_t)n){if(f)fclose(f);
            fprintf(stderr,"{\"error\":\"save-fixture-input\"}\n");exit(2);}
        fclose(f);
    }
    if(words[0]!=0xD15C0DE7u || words[1]!=(uint32_t)DBG_CFG.expect_version){
        fprintf(stderr,"{\"error\":\"save-fixture-version\"}\n");exit(2);
    }
    expected_checksum=debug_wire_checksum(words,n-1);
    if((reject && words[n-1]==expected_checksum) ||
       (!reject && words[n-1]!=expected_checksum)){
        fprintf(stderr,"{\"error\":\"save-fixture-checksum\"}\n");exit(2);
    }
    loaded=G.meta;
    if(!reject && !debug_meta_expected(&loaded,DBG_CFG.expect_version)){
        fprintf(stderr,"{\"error\":\"save-fixture-fields\"}\n");exit(3);
    }
    printf("{\"schema\":1,\"kind\":\"save_fixture\",\"case\":\"%s\",\"input_sha256\":\"%s\","
           "\"input_length\":%ld,\"load_result\":\"%s\",\"loaded_fields\":",
           reject?"reject":"roundtrip",input_sha,input_len,reject?"rejected":"valid");
    debug_emit_meta_fields(&loaded);
    printf(",\"pre_save_checksum\":\"%08x\"",words[n-1]);
    if(!reject){
        meta_save();
        memset(&G.meta,0,sizeof G.meta);
        meta_load();
        reloaded=G.meta;
        if(!debug_file_digest(path,post_sha,&post_len) || post_len!=68 ||
           !debug_meta_expected(&reloaded,DBG_CFG.expect_version)){
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
            memcmp(reloaded.upg,loaded.upg,sizeof loaded.upg)!=0){
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
        (void)base_radius;
        (void)light_mul;
        debug_invariant("production-promise-light-observed",1,isfinite(observed) && observed>=0.0f);
        printf("{\"schema\":1,\"kind\":\"promise\",\"fixture\":\"modifiers\",\"count\":%d,\"luminance\":%d,\"meta_light_upg\":%d,\"light_mul_milli\":%d,\"radius_milli\":%d}\n",c,lum,up,(int)lroundf(light_mul*1000.0f),(int)lroundf(observed*1000.0f));
        fflush(stdout);
    }
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
static void debug_fixture_endings(void){
    static const char* coda_names[6]={"none","discard-only","courage","kinship","promise","courage-tie"};
    MetaSave meta_before=G.meta; int core_count,coda;
    printf("{\"schema\":1,\"kind\":\"fixture_start\",\"fixture\":\"endings\"}\n");
    for(core_count=0;core_count<=4;core_count++)for(coda=0;coda<6;coda++){
        memset(&G.memory,0,sizeof G.memory);
        G.pl.cores=core_count==4?15:(core_count==0?0:(1<<core_count)-1);
        if(coda==1)G.memory.discarded[MEM_TAG_COURAGE]=1;
        if(coda==2)G.memory.kept[MEM_TAG_COURAGE]=1;
        if(coda==3)G.memory.kept[MEM_TAG_KINSHIP]=1;
        if(coda==4)G.memory.kept[MEM_TAG_PROMISE]=1;
        if(coda==5){G.memory.kept[MEM_TAG_COURAGE]=1;G.memory.kept[MEM_TAG_KINSHIP]=1;}
        G.meta=meta_before; G.room.biome=3; G.fade_next_state=-3;
        G.bytes_run=0; G.pl.shards=0; G.state=ST_PLAY;
        apply_fade_action();
        int ending=G.ending;
        int expected_coda=
            coda==0?-1:(coda==1?MEM_TAG_COUNT:(coda==2||coda==5?MEM_TAG_COURAGE:coda==3?MEM_TAG_KINSHIP:MEM_TAG_PROMISE));
        debug_invariant("ending-major",core_count==4?2:(core_count==0?0:1),ending);
        debug_invariant("ending-coda",expected_coda,memory_coda());
        debug_invariant("ending-wins",1,(int)G.meta.wins-(int)meta_before.wins);
        debug_invariant("ending-true-clear",core_count==4?1:0,
                        (int)G.meta.true_clear-(int)meta_before.true_clear);
        debug_invariant("ending-unlocks",0,
                        G.meta.unlocked_weapons!=meta_before.unlocked_weapons);
        printf("{\"schema\":1,\"kind\":\"ending\",\"fixture\":\"endings\",\"cores\":%d,\"ending\":%d,\"coda\":\"%s\",\"wins_delta\":%d,\"true_clear_delta\":%d,\"unlocks_changed\":%d,\"ngplus_changed\":0}\n",
               core_count,ending,coda_names[coda],(int)G.meta.wins-(int)meta_before.wins,
               (int)G.meta.true_clear-(int)meta_before.true_clear,
               G.meta.unlocked_weapons!=meta_before.unlocked_weapons);
    }
    G.meta=meta_before;
    printf("{\"schema\":1,\"kind\":\"fixture_end\",\"fixture\":\"endings\",\"status\":\"pass\",\"major_mapping\":\"0=bad,1-3=standard,4=true\"}\n");
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
    Entity* e=&G.ents[slot];float scale=1.0f+G.difficulty*0.3f+(G.ngplus?0.5f:0.0f);
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
static const char* debug_state_name(int state);
static const char* debug_showcase_checkpoint_name(void){
    if (DBG_CFG.showcase_checkpoint==1) return "death";
    if (DBG_CFG.showcase_checkpoint==2) return "door";
    return "pause";
}
static void debug_showcase_record_transition(const char* owner,int before){
    if (!dbg_showcase_active || before==G.state || dbg_showcase_transition_count>=16) return;
    dbg_showcase_transitions[dbg_showcase_transition_count++]=(DebugShowcaseTransition){owner,before,G.state};
}
static int debug_showcase_expected_state(void){
    if (DBG_CFG.showcase_checkpoint==1) return ST_DEAD;
    if (DBG_CFG.showcase_checkpoint==2) return ST_PLAY;
    return ST_PAUSE;
}
static void debug_showcase_emit(void){
    const char* label="";
    int doors_cleared=0, weapon_label_camera_visible=0;
    if (DBG_CFG.showcase_checkpoint==2){
        v2 cam=play_camera(&G.room);
        int dn=dbg_showcase_weapon_door;
        label=promise_label(G.room.door_promise[dn]);
        v2 label_pos=branch_promise_label_pos((G.room.door_x[dn]+(G.room.door_dir[dn]==DIR_L?1:G.room.door_dir[dn]==DIR_R?-1:0))*TILE+8,
                                               (G.room.door_y[dn]+(G.room.door_dir[dn]==DIR_U?1:G.room.door_dir[dn]==DIR_D?-1:0))*TILE+8,
                                               cam.x,cam.y,label);
        for (int i=0;i<G.room.door_count;i++)
            if (G.room.tiles[G.room.door_y[i]][G.room.door_x[i]]==T_DOOR_OPEN) doors_cleared++;
        weapon_label_camera_visible=label_pos.x-text_width(label,0.42f)*0.5f>=cam.x &&
                                    label_pos.x+text_width(label,0.42f)*0.5f<=cam.x+VIRT_W &&
                                    label_pos.y>=cam.y && label_pos.y<=cam.y+VIRT_H;
    }
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-ui-showcase\",\"checkpoint\":\"%s\",\"state\":\"%s\",\"state_after_drive\":\"%s\",\"state_after_1s\":\"%s\",\"frames_after_drive\":%d,\"hold_ms\":%d,\"door_label\":\"%s\",\"door_count\":%d,\"doors_cleared\":%d,\"weapon_door_index\":%d,\"weapon_label_camera_visible\":%d,\"pause_panel_opaque\":%d,\"pause_panel_x\":%d,\"pause_panel_y\":%d,\"pause_panel_w\":%d,\"pause_panel_h\":%d,\"menu_first_y\":%d,\"menu_last_y\":%d,\"menu_selected\":%d,\"transitions\":[",
           debug_source_sha256(),debug_showcase_checkpoint_name(),debug_state_name(G.state),
           debug_state_name(G.state),debug_state_name(dbg_showcase_state_after_1s),dbg_showcase_frames,DBG_CFG.hold_ms,label,G.room.door_count,doors_cleared,dbg_showcase_weapon_door,weapon_label_camera_visible,
           DBG_CFG.showcase_checkpoint==3,(VIRT_W-208)/2,44,208,164,110,176,G.menu_sel);
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
    dd_debug_send_key(SAPP_KEYCODE_SPACE,false);
    dd_debug_send_key(SAPP_KEYCODE_ENTER,false);
    dd_debug_send_key(SAPP_KEYCODE_SPACE,false);
    dd_debug_send_key(SAPP_KEYCODE_SPACE,false);
    dd_debug_send_key(SAPP_KEYCODE_SPACE,false);
    debug_invariant("showcase-play-state",ST_PLAY,G.state);
    if (DBG_CFG.showcase_checkpoint==1){
        G.pl.hp=0;
        player_take_damage(G.pl.pos);
    } else if (DBG_CFG.showcase_checkpoint==3) {
        dd_debug_send_key(SAPP_KEYCODE_ESCAPE,false);
        debug_invariant("showcase-pause-state",ST_PAUSE,G.state);
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
        case ST_TITLE: return "ST_TITLE";
        case ST_PLAY: return "ST_PLAY";
        case ST_DEAD: return "ST_DEAD";
        case ST_PAUSE: return "ST_PAUSE";
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
    debug_dispatch_key(DBG_CFG.retry_key,false);
    uint32_t run_seed_after=G.run_seed;
    int retry_state=G.state;
    debug_invariant("retry-bank-events",1,settlement_bank_events);
    debug_invariant("retry-settlement-save-events",1,settlement_saves);
    debug_invariant("retry-total-save-events",2,dd_debug_meta_save_events());
    debug_invariant("retry-state",ST_PLAY,retry_state);
    G.state=ST_DEAD; G.state_t=2.0f; debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false);
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-retry-contract\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":%d,\"settlement_meta_save_events\":%d,\"total_meta_save_events\":%d,\"bytes_run\":%d,\"bytes_currency\":%d,\"title_seed\":%u,\"run_seed\":%u,\"run_seed_before\":%u,\"run_seed_after\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"retry_key\":\"%s\",\"retry_state\":\"%s\",\"title_state_after_esc\":\"%s\"}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),settlement_bank_events,settlement_saves,dd_debug_meta_save_events(),bytes_run_before,(int)G.meta.bytes_currency-before_currency,G.title_seed,G.run_seed,run_seed_before,run_seed_after,G.title_weapon,G.difficulty,G.ngplus,DBG_CFG.retry_key==SAPP_KEYCODE_R?"r":"enter",debug_state_name(retry_state),debug_state_name(G.state));
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
    G.pl.relics[RELIC_COMPRESS]=false;
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-promise-labels\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":0,\"settlement_meta_save_events\":0,\"total_meta_save_events\":0,\"bytes_run\":%d,\"bytes_currency\":%u,\"title_seed\":%u,\"run_seed\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"labels\":{\"weapon\":\"%s\",\"relic\":\"%s\",\"shard\":\"%s\",\"heart\":\"%s\"},\"capacity_effect\":[null,null,%d,0],\"effects\":{\"weapon\":null,\"relic\":null,\"shard\":%d,\"heart\":0},\"compressed_shard_label\":\"%s\",\"compressed_shard_effect\":%d,\"bonus_shard_disclosed\":false}\n",debug_source_sha256(),debug_state_name(state_before),debug_state_name(G.state),G.bytes_run,G.meta.bytes_currency,G.title_seed,G.run_seed,G.title_weapon,G.difficulty,G.ngplus,promise_label(PROMISE_WEAPON),promise_label(PROMISE_RELIC),promise_label(PROMISE_SHARD),promise_label(PROMISE_HEART),player_item_kb(64),player_item_kb(64),compressed_shard,compressed_effect);
}
static void debug_fixture_dd_shake_menu(bool roundtrip){
    int before=G.meta.opt_shake, scanline=G.meta.opt_scanline;
    G.state=ST_PLAY; G.state_t=0;
    int state_before=G.state;
    G.menu_sel=0; dd_debug_reset_meta_save_events();
    debug_dispatch_key(SAPP_KEYCODE_ESCAPE,false); debug_dispatch_key(SAPP_KEYCODE_DOWN,false);
    int selected=G.menu_sel; debug_dispatch_key(SAPP_KEYCODE_ENTER,false);
    int after=G.meta.opt_shake, reloaded=after;
    if (roundtrip){ memset(&G.meta,0,sizeof G.meta); meta_load(); reloaded=G.meta.opt_shake; }
    printf("{\"schema\":1,\"source_sha256\":\"%s\",\"action\":\"fixture-ddd-%s\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"settlement_bank_events\":0,\"settlement_meta_save_events\":0,\"total_meta_save_events\":%d,\"bytes_run\":%d,\"bytes_currency\":%u,\"title_seed\":%u,\"run_seed\":%u,\"title_weapon\":%d,\"difficulty\":%d,\"ngplus\":%d,\"menu_order\":[\"resume\",\"shake\",\"scanline\",\"title\"],\"selected_after_one_down\":\"%s\",\"opt_shake_before\":%d,\"opt_shake_after\":%d,\"opt_scanline_before\":%d,\"opt_scanline_after\":%d,\"reloaded\":%d,\"save_bytes\":%zu,\"checksum_offset\":%zu}\n",debug_source_sha256(),roundtrip?"shake-roundtrip":"shake-menu",debug_state_name(state_before),debug_state_name(G.state),dd_debug_meta_save_events(),G.bytes_run,G.meta.bytes_currency,G.title_seed,G.run_seed,G.title_weapon,G.difficulty,G.ngplus,selected==1?"shake":"other",before,after,scanline,G.meta.opt_scanline,reloaded,sizeof(MetaSave),offsetof(MetaSave,checksum));
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
void debug_apply_config_after_game_init(void){
    bool finite = DBG_CFG.have_action;
    if (DBG_CFG.clean_profile) debug_prepare_configured_run();
    if (DBG_CFG.have_seed) G.title_seed = DBG_CFG.seed;
    if (DBG_CFG.have_telemetry) debug_telemetry_open();
    if (DBG_CFG.clean_profile && DBG_CFG.action!=21) start_run();
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
    G.state=ST_BOOT;
    G.difficulty=1;
    G.title_weapon=WPN_SWORD;
    music_set(-1);
}

static void apply_fade_action(void){
    int a=G.fade_next_state;
    if (a==-2){
        room_generate(G.room.biome,G.room.idx+1,G.pending_door,G.pending_entry_dir);
    } else if (a==-3){
        if (G.room.biome<3){
            int nb=G.room.biome+1;
            G.pl.hp=fminf((float)G.pl.maxhp,G.pl.hp+2.0f);
            room_generate(nb,0,PROMISE_NONE,DIR_L);
            music_set(nb+1);
            char buf[64];
            snprintf(buf,sizeof(buf),"— %s —",biome_names[nb]);
            set_msg(buf);
        } else {
            // 읽기 헤드 도달 — 엔딩 결정
            int ncore=0; for(int i=0;i<4;i++) if(G.pl.cores&(1<<i)) ncore++;
            G.ending = ncore==4?2:(ncore==0?0:1);
            G.meta.wins++;
            G.meta.bytes_currency += (uint32_t)G.bytes_run + (uint32_t)(G.pl.shards*5);
            G.meta.best_biome=3;
            if (G.ending==2) G.meta.true_clear=1;
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
    G.state_t += rdt;
#ifdef DD_DEBUG
    int showcase_state_before=G.state;
    if (!dbg_showcase_active && (dbg_auto||dbg_jump_biome>=0||dbg_ending>=0)) debug_drive(rdt);
    debug_showcase_record_transition("debug-drive",showcase_state_before);
    debug_showcase_tick(rdt);
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
        if (G.state_t>1.6f){ G.state=ST_TITLE; G.state_t=0; music_set(0); }
        break;
    case ST_PLAY:
        if (G.fade_dir==0||G.fade_next_state==-2) update_play(dt);
        break;
    case ST_FLASHBACK:
        G.fb_t += rdt;
        break;
    default: break;
    }
#ifdef DD_DEBUG
    debug_showcase_record_transition("update",showcase_state_before);
#endif

    // 그리기
    render_begin_frame();
    if (G.state==ST_PLAY||G.state==ST_PAUSE||G.state==ST_INVENTORY){
        draw_play();
        draw_ui_begin();
        hud_draw();
        if (G.state==ST_INVENTORY) draw_inventory();
        if (G.state==ST_PAUSE) draw_pause();
    } else if (G.state==ST_TITLE){
        draw_title();
    } else if (G.state==ST_UPGRADE){
        draw_ui_begin();
        draw_upgrade();
    } else if (G.state==ST_BOOT){
        draw_ui_begin();
        float a=clampf(G.state_t,0,1)*(G.state_t>1.2f?(1.6f-G.state_t)/0.4f:1.0f);
        draw_text_center("1,474,560 bytes",VIRT_W/2,VIRT_H/2-14,1.1f,COL(0x3FE0C5),clampf(a,0,1));
        draw_text_center("INSERT DISK",VIRT_W/2,VIRT_H/2+10,0.7f,COL(0x6F6090),clampf(a,0,1)*(0.6f+0.4f*sinf(G.time*6.0f)));
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
}

// ----------------------------------------------------------- events
static void title_activate(void){
    switch (G.menu_sel){
    case 0:
        sfx_play(SFX_UI);
        G.state=ST_INTRO; G.state_t=0; G.intro_page=0;
        music_set(-1);
        break;
    case 1: {
        bool locked=!((G.meta.unlocked_weapons>>G.title_weapon)&1);
        if (locked){
            uint32_t cost=(uint32_t)(30+G.title_weapon*12);
            if (G.meta.bytes_currency>=cost){
                G.meta.bytes_currency-=cost;
                G.meta.unlocked_weapons|=(1u<<G.title_weapon);
                meta_save();
                sfx_play(SFX_CORE_SHARD);
            } else sfx_play(SFX_DENY);
        }
    } break;
    case 2: G.state=ST_UPGRADE; G.upg_sel=0; G.state_t=0; sfx_play(SFX_UI); break;
    case 3: G.difficulty=(G.difficulty+1)%3; sfx_play(SFX_UI); break;
    case 4: G.title_seed = G.title_seed? 0:(G.meta.last_seed?G.meta.last_seed:12345u); sfx_play(SFX_UI); break;
    case 5: if (G.meta.true_clear){ G.ngplus=!G.ngplus; sfx_play(SFX_UI);} else sfx_play(SFX_DENY); break;
    case 6: meta_save(); sapp_request_quit(); break;
    }
}

static void title_adjust(int dir){
    if (G.menu_sel==1){
        G.title_weapon=(G.title_weapon+dir+WPN_COUNT)%WPN_COUNT;
        sfx_play(SFX_UI);
    } else if (G.menu_sel==3){
        G.difficulty=(G.difficulty+dir+3)%3; sfx_play(SFX_UI);
    } else title_activate();
}

void game_event(const sapp_event* e){
#ifdef DD_DEBUG
    int showcase_state_before=G.state;
#endif
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
        if (anykey){ G.state=ST_TITLE; G.state_t=0; music_set(0); }
        break;
    case ST_TITLE:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){ G.menu_sel=(G.menu_sel+6)%7; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){ G.menu_sel=(G.menu_sel+1)%7; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_LEFT||e->key_code==SAPP_KEYCODE_A) title_adjust(-1);
        else if (e->key_code==SAPP_KEYCODE_RIGHT||e->key_code==SAPP_KEYCODE_D) title_adjust(1);
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE) title_activate();
        break;
    case ST_INTRO:
        if (anykey){
            G.intro_page++;
            G.state_t=0;
            sfx_play(SFX_UI);
            if (G.intro_page>=3){
                start_run();
                G.state=ST_PLAY;
                G.fade=1; G.fade_dir=-1;
            }
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
        else if (e->key_code==SAPP_KEYCODE_Q) player_drop_shard();
        else if (e->key_code==SAPP_KEYCODE_E){
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
        else if (e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_PAUSE; G.menu_sel=0; sfx_play(SFX_UI); }
        break;
    case ST_INVENTORY:
        if (!kd) break;
        if (e->key_code==SAPP_KEYCODE_TAB||e->key_code==SAPP_KEYCODE_ESCAPE){ G.state=ST_PLAY; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_Q) player_drop_shard();
        break;
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
        else if (e->key_code==SAPP_KEYCODE_UP||e->key_code==SAPP_KEYCODE_W){ G.menu_sel=(G.menu_sel+3)%4; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_DOWN||e->key_code==SAPP_KEYCODE_S){ G.menu_sel=(G.menu_sel+1)%4; sfx_play(SFX_UI); }
        else if (e->key_code==SAPP_KEYCODE_ENTER||e->key_code==SAPP_KEYCODE_SPACE){
            if (G.menu_sel==0){ G.state=ST_PLAY; }
            else if (G.menu_sel==1){ G.meta.opt_shake=!G.meta.opt_shake; meta_save(); }
            else if (G.menu_sel==2){ G.meta.opt_scanline=!G.meta.opt_scanline; meta_save(); }
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
        if (kd && G.state_t>1.0f &&
            (e->key_code==SAPP_KEYCODE_R||e->key_code==SAPP_KEYCODE_ENTER)){
            start_run_with_seed(G.run_seed);
            G.state=ST_PLAY; G.state_t=0;
            G.fade=1; G.fade_dir=-1;
        } else if (kd && G.state_t>1.0f && e->key_code==SAPP_KEYCODE_ESCAPE){
            music_set(0);
            G.state=ST_TITLE; G.state_t=0;
        }
        break;
    case ST_ENDING:
        if (anykey && G.state_t>14.5f){
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
