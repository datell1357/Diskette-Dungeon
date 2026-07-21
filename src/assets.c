// assets.c — 절차 생성 스프라이트 아틀라스 (데이터 파일 0바이트)
// 스프라이트는 대부분 회색조로 그려서 draw 시 tint로 바이옴 색을 입힌다.
#include "game.h"

#define ATLAS 256
static uint32_t atlas_px[ATLAS*ATLAS];
float spr_uv[SPR_COUNT][4];
sg_view atlas_view;
static int spr_rect[SPR_COUNT][4];

static inline uint32_t PACK(uint8_t r,uint8_t g,uint8_t b,uint8_t a){
    return ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)g<<8)|r;
}
static inline uint32_t GRAY(int v,int a){ v=v<0?0:(v>255?255:v); return PACK((uint8_t)v,(uint8_t)v,(uint8_t)v,(uint8_t)a); }
static inline uint32_t RGB(int r,int g,int b){ return PACK((uint8_t)r,(uint8_t)g,(uint8_t)b,255); }

// current sprite rect
static int cur_x, cur_y, cur_w, cur_h, pen_x, pen_y, row_h;
static Rng arng = { 0xC0FFEE123456789ull };

static void px_set(int x,int y,uint32_t c){
    if (x<0||y<0||x>=cur_w||y>=cur_h) return;
    atlas_px[(cur_y+y)*ATLAS + cur_x+x] = c;
}
static uint32_t px_get(int x,int y){
    if (x<0||y<0||x>=cur_w||y>=cur_h) return 0;
    return atlas_px[(cur_y+y)*ATLAS + cur_x+x];
}
static void fill_rect(int x,int y,int w,int h,uint32_t c){
    for (int j=y;j<y+h;j++) for (int i=x;i<x+w;i++) px_set(i,j,c);
}
static void disc(float cx,float cy,float r,uint32_t c){
    for (int j=(int)(cy-r)-1;j<=(int)(cy+r)+1;j++)
        for (int i=(int)(cx-r)-1;i<=(int)(cx+r)+1;i++){
            float dx=i+0.5f-cx, dy=j+0.5f-cy;
            if (dx*dx+dy*dy<=r*r) px_set(i,j,c);
        }
}
static void aline(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){ px_set(x0,y0,c); if(x0==x1&&y0==y1)break;
        int e2=2*err; if(e2>=dy){err+=dy;x0+=sx;} if(e2<=dx){err+=dx;y0+=sy;} }
}
// 외곽선 + 윗면 하이라이트 패스
static void finish_sprite(bool do_outline){
    if (do_outline){
        for (int y=0;y<cur_h;y++) for (int x=0;x<cur_w;x++){
            if ((px_get(x,y)>>24)!=0) continue;
            bool adj = (px_get(x+1,y)>>24)>100 || (px_get(x-1,y)>>24)>100 ||
                       (px_get(x,y+1)>>24)>100 || (px_get(x,y-1)>>24)>100;
            if (adj) px_set(x,y,PACK(14,8,20,255));
        }
    }
}
static void begin_sprite(int id,int w,int h){
    if (pen_x + w + 2 > ATLAS){ pen_x = 0; pen_y += row_h + 2; row_h = 0; }
    if (h > row_h) row_h = h;
    cur_x = pen_x+1; cur_y = pen_y+1; cur_w = w; cur_h = h;
    spr_rect[id][0]=cur_x; spr_rect[id][1]=cur_y;
    spr_rect[id][2]=w; spr_rect[id][3]=h;
    pen_x += w + 2;
    spr_uv[id][0]=(cur_x)/(float)ATLAS;     spr_uv[id][1]=(cur_y)/(float)ATLAS;
    spr_uv[id][2]=(cur_x+w)/(float)ATLAS;   spr_uv[id][3]=(cur_y+h)/(float)ATLAS;
}

// ------------------------------------------------------------- generators
static void gen_flame(void){
    begin_sprite(SPR_FLAME,16,16);
    // 청록 불씨: 아래 둥글고 위로 갈수록 좁아지는 불꽃
    for (int y=0;y<16;y++){
        float t=y/15.0f; // 0 top 1 bottom
        float w = t<0.25f ? t*2.2f : (t<0.75f ? 0.55f+ (t-0.25f)*0.5f : 0.8f-(t-0.75f)*1.6f);
        w *= 7.0f;
        for (int x=0;x<16;x++){
            float dx=fabsf(x+0.5f-8.0f);
            if (dx<w){
                float core = 1.0f-dx/(w+0.001f);
                if (core>0.55f && t>0.3f && t<0.85f) px_set(x,y,RGB(220,255,245));
                else px_set(x,y,RGB(63,224,197));
            }
        }
    }
    finish_sprite(false);
}
static void gen_slime(void){
    begin_sprite(SPR_SLIME,16,16);
    for (int y=4;y<15;y++){
        float t=(y-4)/10.0f;
        float w=7.0f*sqrtf(1.0f-(1.0f-t)*(1.0f-t)*0.8f);
        for (int x=0;x<16;x++){
            float dx=fabsf(x+0.5f-8.0f);
            if (dx<w) px_set(x,y,GRAY(150+(int)(40*(1.0f-t)),255));
        }
    }
    disc(5.5f,6.5f,1.6f,GRAY(235,255)); // gloss
    px_set(5,9,GRAY(25,255)); px_set(6,9,GRAY(25,255));
    px_set(10,9,GRAY(25,255)); px_set(11,9,GRAY(25,255));
    finish_sprite(true);
}
static void gen_bat(void){
    begin_sprite(SPR_BAT,16,12);
    disc(8,6,2.6f,GRAY(160,255));
    // wings
    aline(6,6,1,2,GRAY(140,255)); aline(6,7,1,4,GRAY(140,255)); aline(6,8,2,7,GRAY(120,255));
    aline(10,6,15,2,GRAY(140,255)); aline(10,7,15,4,GRAY(140,255)); aline(10,8,14,7,GRAY(120,255));
    fill_rect(2,3,4,3,GRAY(135,255)); fill_rect(10,3,4,3,GRAY(135,255));
    px_set(7,5,RGB(255,61,127)); px_set(9,5,RGB(255,61,127)); // eyes accent
    finish_sprite(true);
}
static void gen_wraith(void){
    begin_sprite(SPR_WRAITH,16,20);
    for (int y=2;y<19;y++){
        float t=(y-2)/17.0f;
        float w = t<0.4f ? 5.5f*sqrtf(t/0.4f+0.15f) : 5.8f-(t-0.4f)*2.0f;
        float wob = sinf(y*1.7f)*(t>0.6f?1.5f:0.0f);
        for (int x=0;x<16;x++){
            float dx=fabsf(x+0.5f-8.0f-wob);
            if (dx<w){
                if (t>0.85f && ((x+y)&1)) continue; // 너덜한 밑단
                px_set(x,y,GRAY(120+(int)(60*(1.0f-t)),255));
            }
        }
    }
    fill_rect(5,6,6,4,GRAY(20,255)); // hollow face
    px_set(6,7,GRAY(220,255)); px_set(9,7,GRAY(220,255));
    finish_sprite(true);
}
static void gen_chaser(void){
    begin_sprite(SPR_CHASER,16,12);
    for (int y=0;y<12;y++){
        int half=abs(y-6);
        int len=12-half*2; if(len<0)len=0;
        for (int x=0;x<len;x++){
            int v=170-x*8; px_set(15-x-half/2,y,GRAY(v<60?60:v,255));
        }
    }
    px_set(13,5,RGB(255,61,127)); px_set(13,7,RGB(255,61,127));
    finish_sprite(true);
}
static void gen_golem(void){
    begin_sprite(SPR_GOLEM,24,24);
    // 떠 있는 조각 덩어리들
    fill_rect(4,2,16,7,GRAY(150,255));
    fill_rect(2,11,8,8,GRAY(130,255));
    fill_rect(13,11,9,8,GRAY(140,255));
    fill_rect(7,20,10,3,GRAY(110,255));
    for (int i=0;i<26;i++){ int x=rng_i(&arng,24),y=rng_i(&arng,24);
        if((px_get(x,y)>>24)) px_set(x,y,GRAY(95+rng_i(&arng,80),255)); }
    fill_rect(9,9,6,2,RGB(63,224,197)); // core glow
    px_set(8,5,GRAY(25,255)); px_set(9,5,GRAY(25,255));
    px_set(14,5,GRAY(25,255)); px_set(15,5,GRAY(25,255));
    finish_sprite(true);
}
static void gen_turret(void){
    begin_sprite(SPR_TURRET,16,16);
    fill_rect(2,11,12,4,GRAY(110,255));
    fill_rect(4,8,8,4,GRAY(150,255));
    fill_rect(7,3,2,6,GRAY(180,255)); // barrel
    px_set(7,3,RGB(255,122,61)); px_set(8,3,RGB(255,122,61));
    finish_sprite(true);
}
static void gen_sentinel(void){
    begin_sprite(SPR_SENTINEL,20,24);
    fill_rect(5,2,10,18,GRAY(140,255));
    fill_rect(3,4,2,14,GRAY(170,255)); fill_rect(15,4,2,14,GRAY(170,255)); // shoulder plates
    fill_rect(6,6,8,2,RGB(63,224,197)); // visor
    fill_rect(6,20,3,3,GRAY(110,255)); fill_rect(11,20,3,3,GRAY(110,255));
    for (int y=9;y<18;y+=3) fill_rect(6,y,8,1,GRAY(115,255));
    finish_sprite(true);
}
static void gen_drone(void){
    begin_sprite(SPR_DRONE,16,16);
    for (int i=0;i<7;i++){
        px_set(1+i,1+i,GRAY(150,255)); px_set(14-i,1+i,GRAY(150,255));
        px_set(1+i,14-i,GRAY(150,255)); px_set(14-i,14-i,GRAY(150,255));
        px_set(2+i,1+i,GRAY(120,255)); px_set(13-i,1+i,GRAY(120,255));
    }
    disc(8,8,3.2f,GRAY(170,255));
    disc(8,8,1.4f,RGB(255,61,127));
    finish_sprite(true);
}
static void gen_bomber(void){
    begin_sprite(SPR_BOMBER,16,16);
    disc(8,9,5.5f,GRAY(120,255));        // 둥근 폭탄 몸체
    disc(6.5f,7.5f,1.6f,GRAY(200,255));  // 광택
    px_set(7,11,GRAY(30,255)); px_set(9,11,GRAY(30,255)); // 눈
    fill_rect(7,2,2,3,GRAY(150,255));    // 도화선
    px_set(8,1,RGB(255,122,61)); px_set(7,1,RGB(255,122,61)); // 불꽃
    finish_sprite(true);
}
static void gen_sniper(void){
    begin_sprite(SPR_SNIPER,12,18);
    fill_rect(4,2,4,14,GRAY(140,255));   // 가느다란 센서 기둥
    fill_rect(3,14,6,3,GRAY(110,255));   // 받침
    fill_rect(2,4,8,1,GRAY(170,255));
    disc(6,6,1.8f,RGB(255,61,127));      // 단안 조준 눈
    px_set(6,6,GRAY(255,255));
    finish_sprite(true);
}
static void gen_shielder(void){
    begin_sprite(SPR_SHIELDER,18,18);
    fill_rect(7,3,8,12,GRAY(130,255));   // 몸체 (오른쪽)
    px_set(9,6,GRAY(25,255)); px_set(12,6,GRAY(25,255)); // 눈
    fill_rect(2,2,3,14,GRAY(195,255));   // 정면 방패판 (밝은 회색)
    fill_rect(1,4,1,10,GRAY(160,255));
    px_set(3,8,RGB(63,224,197));         // 방패 룬
    finish_sprite(true);
}
static void gen_hive(void){
    begin_sprite(SPR_HIVE,20,20);
    disc(10,11,8.0f,GRAY(110,255));      // 울퉁불퉁 덩어리
    disc(6,7,3.5f,GRAY(125,255)); disc(14,8,3.0f,GRAY(120,255));
    disc(8,15,3.5f,GRAY(115,255));
    for (int i=0;i<5;i++){ int x=4+rng_i(&arng,12),y=5+rng_i(&arng,11);
        if((px_get(x,y)>>24)) disc((float)x,(float)y,1.4f,GRAY(20,255)); } // 어두운 구멍
    disc(10,10,2.2f,RGB(63,224,197));    // 청록 코어
    finish_sprite(true);
}
static void gen_boss_rot(void){
    begin_sprite(SPR_BOSS_ROT,48,48);
    // 벌레 군체: 겹친 원 덩어리 + 눈 다수
    for (int i=0;i<10;i++){
        float a=i*0.63f;
        float cx=24+cosf(a)*10.0f, cy=26+sinf(a)*8.0f;
        disc(cx,cy,8.0f+(i%3),GRAY(95+i*9,255));
    }
    disc(24,20,11.0f,GRAY(160,255));
    for (int i=0;i<7;i++){
        int x=10+rng_i(&arng,28), y=12+rng_i(&arng,26);
        disc((float)x,(float)y,1.7f,GRAY(15,255));
        px_set(x,y,RGB(255,61,127));
    }
    // 갉는 입
    for (int x=18;x<31;x++) if (x&1) px_set(x,30,GRAY(20,255));
    finish_sprite(true);
}
static void gen_boss_echo(void){
    begin_sprite(SPR_BOSS_ECHO,48,48);
    // 플레이어 불씨의 큰 잔상 — 속이 빈 윤곽
    for (int y=4;y<46;y++){
        float t=(y-4)/42.0f;
        float w = t<0.3f ? t*3.0f : (t<0.8f ? 0.9f : 0.9f-(t-0.8f)*3.5f);
        w*=20.0f;
        for (int x=0;x<48;x++){
            float dx=fabsf(x+0.5f-24.0f);
            if (dx<w && dx>w-3.5f) px_set(x,y,GRAY(190,255));
            else if (dx<w-3.5f && ((x+y)&3)==0) px_set(x,y,GRAY(70,150));
        }
    }
    disc(19,20,2.0f,GRAY(240,255)); disc(29,20,2.0f,GRAY(240,255));
    finish_sprite(false);
}
static void gen_boss_defrag(void){
    begin_sprite(SPR_BOSS_DEFRAG,48,48);
    fill_rect(6,6,36,36,GRAY(130,255));
    for (int y=0;y<6;y++) for (int x=0;x<6;x++){
        int v=100+(((x*7+y*13)%5)*22);
        fill_rect(7+x*6,7+y*6,5,5,GRAY(v,255));
    }
    fill_rect(12,18,8,6,RGB(63,224,197)); fill_rect(28,18,8,6,RGB(63,224,197)); // eyes
    fill_rect(16,32,16,3,GRAY(30,255));
    finish_sprite(true);
}
static void gen_boss_null(void){
    begin_sprite(SPR_BOSS_NULL,48,48);
    disc(24,24,20.0f,GRAY(18,255));
    for (int j=0;j<48;j++) for (int i=0;i<48;i++){
        float dx=i+0.5f-24,dy=j+0.5f-24; float d=sqrtf(dx*dx+dy*dy);
        if (d>17.0f&&d<20.0f) px_set(i,j,RGB(190,240,255));
        else if (d<17.0f && rng_i(&arng,30)==0) px_set(i,j,GRAY(60,255));
    }
    disc(24,24,4.5f,RGB(255,61,127));
    disc(24,24,2.0f,GRAY(0,255));
    finish_sprite(false);
}
static void gen_weapons(void){
    // 모두 45도 들린 형태, 16x16
    begin_sprite(SPR_SWORD,16,16);
    for (int i=0;i<10;i++){ px_set(4+i,11-i,GRAY(220,255)); px_set(5+i,11-i,GRAY(170,255)); }
    aline(3,10,5,12,GRAY(110,255)); px_set(2,13,GRAY(140,255)); px_set(3,12,GRAY(140,255));
    finish_sprite(true);

    begin_sprite(SPR_CANNON,16,16);
    fill_rect(2,9,5,4,GRAY(120,255));
    aline(6,10,13,3,GRAY(190,255)); aline(7,11,14,4,GRAY(190,255)); aline(6,11,14,3,GRAY(150,255));
    px_set(14,3,RGB(63,224,197)); px_set(13,3,RGB(63,224,197));
    finish_sprite(true);

    begin_sprite(SPR_SPRAY,16,16);
    fill_rect(3,9,4,4,GRAY(130,255));
    aline(7,10,12,5,GRAY(180,255)); aline(7,11,13,8,GRAY(180,255)); aline(7,9,11,3,GRAY(180,255));
    px_set(13,4,RGB(255,122,61)); px_set(14,7,RGB(255,122,61)); px_set(12,2,RGB(255,122,61));
    finish_sprite(true);

    begin_sprite(SPR_GLAIVE,16,16);
    for (int i=0;i<11;i++){
        float a=i/10.0f*3.14159f;
        px_set(8+(int)(cosf(a)*6.0f),8-(int)(sinf(a)*6.0f),GRAY(200,255));
        px_set(8+(int)(cosf(a)*5.0f),8-(int)(sinf(a)*5.0f),GRAY(150,255));
    }
    disc(8,8,1.5f,GRAY(120,255));
    finish_sprite(true);

    begin_sprite(SPR_LANCE,16,16);
    for (int i=0;i<13;i++){ px_set(1+i,14-i,GRAY(200,255)); }
    px_set(14,1,GRAY(255,255)); px_set(13,2,GRAY(255,255));
    fill_rect(1,13,3,3,GRAY(110,255));
    finish_sprite(true);

    begin_sprite(SPR_WAND,16,16);
    for (int i=0;i<9;i++) px_set(4+i,12-i,GRAY(120,255));
    disc(13,3,2.5f,RGB(63,224,197)); disc(13,3,1.0f,GRAY(255,255));
    finish_sprite(true);
}
static void gen_items(void){
    begin_sprite(SPR_RELIC,12,12);
    fill_rect(2,2,8,8,GRAY(140,255));
    fill_rect(4,4,4,4,RGB(63,224,197));
    px_set(1,3,GRAY(170,255)); px_set(1,6,GRAY(170,255)); px_set(10,3,GRAY(170,255)); px_set(10,6,GRAY(170,255));
    finish_sprite(true);

    begin_sprite(SPR_SHARD,10,12);
    for (int y=0;y<12;y++){ int w=(y<6)?y:(11-y); for(int x=5-w;x<=4+w;x++) px_set(x,y,RGB(170,235,255)); }
    for (int y=2;y<10;y++) px_set(4,y,GRAY(255,255));
    finish_sprite(true);

    begin_sprite(SPR_CORE_SHARD,14,16);
    for (int y=0;y<16;y++){ int w=(y<8)?y:(15-y); for(int x=7-w;x<=6+w;x++) px_set(x,y,RGB(124,252,228)); }
    for (int y=4;y<12;y++) px_set(6,y,GRAY(255,255));
    px_set(7,7,GRAY(255,255)); px_set(5,7,GRAY(255,255)); px_set(6,6,GRAY(255,255)); px_set(6,8,GRAY(255,255));
    finish_sprite(true);

    begin_sprite(SPR_HEART,12,11);
    disc(3.5f,3.5f,2.8f,RGB(255,90,140)); disc(8.5f,3.5f,2.8f,RGB(255,90,140));
    for (int y=4;y<11;y++){ int w=10-(y-4)*1.5; for (int x=6-w/2;x<6+w-w/2;x++) px_set(x,y,RGB(255,90,140)); }
    px_set(3,2,RGB(255,200,220));
    finish_sprite(true);

    begin_sprite(SPR_EXIT,20,20);
    // 블랙홀: 사건의 지평선(암흑) + 광자 링(백광) + 강착원반 나선 — 회전/흡입 연출은 draw에서
    for (int y=0;y<20;y++){
        for (int x=0;x<20;x++){
            float dx=x+0.5f-10.0f, dy=y+0.5f-10.0f;
            float r=sqrtf(dx*dx+dy*dy)/9.5f;
            if (r>1.0f) continue;
            float ang=atan2f(dy,dx);
            if (r<0.34f){ px_set(x,y,PACK(4,3,10,255)); continue; }              // 심부 암흑
            if (r<0.47f){ px_set(x,y,GRAY(230+(int)(25.0f*sinf(ang*2.0f)),255)); continue; } // 광자 링
            // 강착원반: 링에 붙을수록 밝고, 나선 팔 + 도플러 비대칭
            float swirl=fmodf(ang*3.0f - r*11.0f + 18.85f, 6.2832f);
            float fall=1.0f-(r-0.47f)/0.53f;
            float dop=1.0f+0.4f*sinf(ang+0.8f);
            int v=(int)((swirl<2.1f? 215.0f:70.0f)*fall*fall*dop);
            int al = r>0.9f? (int)(255.0f*(1.0f-r)*10.0f):255;
            if (v>=22) px_set(x,y,GRAY(v,al));
            else if (((x*5+y*3)&7)==0) px_set(x,y,GRAY(45,150));  // 외곽 성긴 먼지
        }
    }
    finish_sprite(false);

    begin_sprite(SPR_BYTE,8,8);
    disc(4,4,3.2f,RGB(255,210,120));
    px_set(3,3,GRAY(255,255)); px_set(2,4,RGB(200,150,60)); px_set(5,5,RGB(200,150,60));
    finish_sprite(true);
}
static void gen_tiles(void){
    int ids[3]={SPR_TILE_FLOOR_A,SPR_TILE_FLOOR_B,SPR_TILE_FLOOR_C};
    for (int k=0;k<3;k++){
        begin_sprite(ids[k],16,16);
        for (int y=0;y<16;y++) for (int x=0;x<16;x++){
            int v=52+rng_i(&arng,10);
            if (x==0||y==0) v-=10;
            px_set(x,y,GRAY(v,255));
        }
        if (k==1){ for (int i=2;i<14;i++) px_set(i,7,GRAY(70,255)); px_set(7,7,GRAY(90,255)); } // 회로선
        if (k==2){ disc(11,5,1.5f,GRAY(40,255)); px_set(4,11,GRAY(75,255)); }
        finish_sprite(false);
    }
    begin_sprite(SPR_TILE_WALL,16,16);
    for (int y=0;y<16;y++) for (int x=0;x<16;x++){
        int v=120+rng_i(&arng,16);
        int bx=(x+((y/4)&1)*8)%16;
        if (y%4==0) v-=46;
        if (bx==0) v-=40;
        if (y%4==1) v+=18;
        px_set(x,y,GRAY(v,255));
    }
    finish_sprite(false);
    begin_sprite(SPR_TILE_WALL_TOP,16,16);
    for (int y=0;y<16;y++) for (int x=0;x<16;x++){
        int v=34+rng_i(&arng,8);
        px_set(x,y,GRAY(v,255));
    }
    finish_sprite(false);
}

void assets_init(void){
    memset(atlas_px,0,sizeof(atlas_px));
    pen_x=0; pen_y=0; row_h=0;

    begin_sprite(SPR_WHITE,4,4);
    fill_rect(0,0,4,4,GRAY(255,255));
    finish_sprite(false);
    // inset uvs to avoid bleed
    spr_uv[SPR_WHITE][0]+=1.0f/ATLAS; spr_uv[SPR_WHITE][1]+=1.0f/ATLAS;
    spr_uv[SPR_WHITE][2]-=1.0f/ATLAS; spr_uv[SPR_WHITE][3]-=1.0f/ATLAS;

    gen_flame(); gen_slime(); gen_bat(); gen_wraith(); gen_chaser();
    gen_golem(); gen_turret(); gen_sentinel(); gen_drone();
    gen_bomber(); gen_sniper(); gen_shielder(); gen_hive();
    gen_boss_rot(); gen_boss_echo(); gen_boss_defrag(); gen_boss_null();
    // 아틀라스 점검: 신규 스프라이트는 작은 적 줄에 추가되어 보스(48px)·타일 앞에 들어감 — 256x256 여유.
    gen_weapons(); gen_items(); gen_tiles();

    sg_image img = sg_make_image(&(sg_image_desc){
        .width=ATLAS,.height=ATLAS,.pixel_format=SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0]=SG_RANGE(atlas_px),.label="atlas"});
    atlas_view = sg_make_view(&(sg_view_desc){ .texture.image=img });

}

void draw_codex_sprite(int id,float x,float y,float w,float h,col3 tint,float a){
    int sx=spr_rect[id][0], sy=spr_rect[id][1];
    int sw=spr_rect[id][2], sh=spr_rect[id][3];
    float pw=w/sw, ph=h/sh;
    for (int py=0;py<sh;py++) for (int px=0;px<sw;px++){
        uint32_t pixel=atlas_px[(sy+py)*ATLAS+sx+px];
        int pr=pixel&255, pg=(pixel>>8)&255, pb=(pixel>>16)&255;
        float pa=((pixel>>24)&255)/255.0f;
        if (pa<=0 || (pr<=20 && pg<=16 && pb<=28)) continue;
        col3 c={(pr/255.0f)*tint.r,(pg/255.0f)*tint.g,(pb/255.0f)*tint.b};
        draw_quad(x-w*0.5f+px*pw,y-h*0.5f+py*ph,pw,ph,c,a*pa);
    }
}
