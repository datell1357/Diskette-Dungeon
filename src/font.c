// font.c — Galmuri9 비트맵 폰트 (SIL OFL 1.1, (c) Lee Minseo)
// 빌드 타임에 게임이 쓰는 글리프만 추출(tools/gen_font.py → font_data.h, ~6KB).
// 런타임에 아틀라스 텍스처 1장 생성, 텍스트는 텍스처드 쿼드로 그린다.
#include "game.h"
#include "font_data.h"

#define FCELL_W 16
#define FCELL_H 18
#define FCOLS 32
#define LINE_H 14.0f

static sg_view font_view;
static sg_sampler font_smp;
static float gl_uv[FONT_NGLYPHS][4];

void font_init(void){
    int rows = (FONT_NGLYPHS + FCOLS - 1) / FCOLS;
    int aw = FCOLS * FCELL_W;
    int ah = rows * FCELL_H;
    uint32_t* px = (uint32_t*)calloc((size_t)aw*(size_t)ah, sizeof(uint32_t));
    if (!px) return;
    for (int g=0; g<FONT_NGLYPHS; g++){
        const FontGlyph* fg = &font_glyphs[g];
        int cx = (g % FCOLS) * FCELL_W;
        int cy = (g / FCOLS) * FCELL_H;
        int rb = (fg->bw + 7) / 8;
        for (int y=0; y<fg->bh; y++){
            for (int x=0; x<fg->bw; x++){
                uint8_t byte = font_bits[fg->off + y*rb + x/8];
                if (byte & (0x80 >> (x&7)))
                    px[(cy+y)*aw + cx+x] = 0xFFFFFFFFu;
            }
        }
        gl_uv[g][0] = cx/(float)aw;
        gl_uv[g][1] = cy/(float)ah;
        gl_uv[g][2] = (cx+fg->bw)/(float)aw;
        gl_uv[g][3] = (cy+fg->bh)/(float)ah;
    }
    sg_image img = sg_make_image(&(sg_image_desc){
        .width=aw,.height=ah,.pixel_format=SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0]={ .ptr=px, .size=(size_t)aw*(size_t)ah*4 },
        .label="font-atlas"});
    font_view = sg_make_view(&(sg_view_desc){ .texture.image=img });
    font_smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter=SG_FILTER_NEAREST,.mag_filter=SG_FILTER_NEAREST,
        .wrap_u=SG_WRAP_CLAMP_TO_EDGE,.wrap_v=SG_WRAP_CLAMP_TO_EDGE });
    free(px);
}

// 스마트 문장부호 → 수록 글리프로 폴백
static uint32_t punct_map(uint32_t c){
    switch (c){
        case 0x2018: case 0x2019: return '\'';
        case 0x201C: case 0x201D: return '"';
        case 0x2013: return 0x2014;
        default: return c;
    }
}

static int glyph_find(uint32_t cp){
    int lo=0, hi=FONT_NGLYPHS-1;
    while (lo<=hi){
        int mid=(lo+hi)/2;
        if (font_glyphs[mid].cp==cp) return mid;
        if (font_glyphs[mid].cp<cp) lo=mid+1; else hi=mid-1;
    }
    return -1;
}

// 픽셀 폰트는 정수 배율만 (또렷함 유지)
static float iscale(float s){ return s<1.5f ? 1.0f : (s<2.5f ? 2.0f : 3.0f); }

static uint32_t utf8_next(const char** p){
    const uint8_t* s=(const uint8_t*)*p;
    uint32_t c=*s;
    if (!c) return 0;
    int n=1;
    if (c<0x80) n=1;
    else if ((c>>5)==6){ c&=0x1F; n=2; }
    else if ((c>>4)==14){ c&=0x0F; n=3; }
    else if ((c>>3)==30){ c&=0x07; n=4; }
    else { *p+=1; return '?'; }
    for (int i=1;i<n;i++){ if((s[i]&0xC0)!=0x80){ *p+=1; return '?'; } c=(c<<6)|(s[i]&0x3F); }
    *p+=n;
    return c;
}

float text_width(const char* utf8,float s){
    float si=iscale(s);
    float w=0; const char* p=utf8;
    uint32_t c;
    while ((c=utf8_next(&p))){
        if (c=='\n') break;
        c=punct_map(c);
        int g=glyph_find(c);
        w += (g>=0 ? font_glyphs[g].dw : 8) * si;
    }
    return w;
}

void draw_text(const char* utf8,float x,float y,float s,col3 c,float a){
    float si=iscale(s);
    float cx=floorf(x), cy=floorf(y);
    const char* p=utf8;
    uint32_t ch;
    sgl_enable_texture();
    sgl_texture(font_view,font_smp);
    sgl_begin_quads();
    sgl_c4f(c.r,c.g,c.b,a);
    while ((ch=utf8_next(&p))){
        if (ch=='\n'){ cx=floorf(x); cy+=LINE_H*si; continue; }
        ch=punct_map(ch);
        int g=glyph_find(ch);
        if (g<0){
            cx+=8*si;
            continue;
        }
        const FontGlyph* fg=&font_glyphs[g];
        if (fg->bw>0 && fg->bh>0){
            float gx = cx + fg->bx*si;
            float gy = cy + (FONT_ASCENT - fg->bh - fg->by)*si;
            float gw = fg->bw*si, gh = fg->bh*si;
            sgl_v2f_t2f(gx,gy,       gl_uv[g][0],gl_uv[g][1]);
            sgl_v2f_t2f(gx+gw,gy,    gl_uv[g][2],gl_uv[g][1]);
            sgl_v2f_t2f(gx+gw,gy+gh, gl_uv[g][2],gl_uv[g][3]);
            sgl_v2f_t2f(gx,gy+gh,    gl_uv[g][0],gl_uv[g][3]);
        }
        cx += fg->dw*si;
    }
    sgl_end();
}

void draw_text_center(const char* utf8,float cx,float y,float s,col3 c,float a){
    float si=iscale(s);
    const char* line=utf8;
    float cy=y;
    while (*line){
        const char* nl=strchr(line,'\n');
        char buf[256];
        size_t len = nl? (size_t)(nl-line):strlen(line);
        if (len>255) len=255;
        memcpy(buf,line,len); buf[len]=0;
        draw_text(buf,cx-text_width(buf,s)*0.5f,cy,s,c,a);
        cy+=LINE_H*si;
        if (!nl) break;
        line=nl+1;
    }
}
