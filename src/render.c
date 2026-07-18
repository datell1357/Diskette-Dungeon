// render.c — 2D 라이팅 파이프라인
// [1] scene RT(480x270): 알베도(스프라이트/타일)  [2] light RT: 가산 광원(+그림자 팬)
// [3] glow RT(1/4): 블룸 대용 가산 글로우          [4] post: 합성+톤맵+스캔라인+색수차+비네팅
#include "game.h"

// sprite uv table, filled by assets.c
extern float spr_uv[SPR_COUNT][4];
extern sg_view atlas_view;

typedef struct {
    sg_image img_scene, img_light, img_glow;
    sg_view att_scene, att_light, att_glow;       // color attachment views
    sg_view tex_scene, tex_light, tex_glow;       // texture views
    sg_sampler smp_nearest, smp_linear;
    sgl_context ctx_scene, ctx_light, ctx_glow;   // + default ctx for UI
    sgl_pipeline pip_scene, pip_light, pip_glow, pip_ui;
    sg_shader post_shader;
    sg_pipeline post_pip;
    sg_view light_tex_view;                       // radial falloff texture
    sg_sampler light_smp;
} RenderState;
static RenderState R;

// ------------------------------------------------------------ shadow casters
#define MAX_SHADOW_BOXES 128
typedef struct { float x,y,w,h; } SBox;
static SBox sboxes[MAX_SHADOW_BOXES];
static int sbox_count = 0;
void shadow_clear(void){ sbox_count = 0; }
void shadow_add_box(float x, float y, float w, float h){
    if (sbox_count < MAX_SHADOW_BOXES){ sboxes[sbox_count++] = (SBox){x,y,w,h}; }
}

// ------------------------------------------------------------ post shader
static const char* POST_VS_GLSL =
"#version 330\n"
"out vec2 uv;\n"
"void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
" uv=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";

static const char* POST_FS_GLSL =
"#version 330\n"
"uniform sampler2D tex_scene; uniform sampler2D tex_light; uniform sampler2D tex_glow;\n"
"uniform vec4 u_par[3];\n"
"in vec2 uv; out vec4 frag_color;\n"
"void main(){\n"
"  float time=u_par[0].x, flash=u_par[0].y, fade=u_par[0].z, flip=u_par[0].w;\n"
"  float ambient=u_par[1].x, glowstr=u_par[1].y, aberr=u_par[1].z, scan=u_par[1].w;\n"
"  vec3 fadecol=u_par[2].xyz; float shake=u_par[2].w;\n"
"  vec2 suv=vec2(uv.x, mix(uv.y,1.0-uv.y,flip));\n"
"  suv += vec2(shake*0.003*sin(time*91.0), shake*0.003*cos(time*113.0));\n"
"  vec2 d=(suv-0.5)*aberr*0.012;\n"
"  vec3 scene; scene.r=texture(tex_scene,suv+d).r; scene.g=texture(tex_scene,suv).g; scene.b=texture(tex_scene,suv-d).b;\n"
"  vec3 light=texture(tex_light,suv).rgb;\n"
"  vec3 glow=texture(tex_glow,suv).rgb;\n"
"  vec3 col=scene*(vec3(ambient)+light*1.7)+glow*glowstr+vec3(flash);\n"
"  col=col/(col+vec3(0.55))*1.5;\n"
"  col=pow(col,vec3(1.06,1.0,0.93));\n"
"  float sl=1.0-scan*0.5*(0.5+0.5*sin(suv.y*270.0*3.14159));\n"
"  col*=sl;\n"
"  vec2 vd=suv-0.5; col*=1.0-dot(vd,vd)*0.9;\n"
"  col=mix(col,fadecol,fade);\n"
"  frag_color=vec4(col,1.0);\n"
"}\n";

static const char* POST_VS_HLSL =
"struct vs_out { float4 pos: SV_Position; float2 uv: TEXCOORD0; };\n"
"vs_out main(uint vid: SV_VertexID){ vs_out o;"
" float2 p=float2(float((vid<<1)&2),float(vid&2)); o.uv=p;"
" o.pos=float4(p*2.0-1.0,0.0,1.0); return o; }\n";

static const char* POST_FS_HLSL =
"Texture2D tex_scene: register(t0); Texture2D tex_light: register(t1); Texture2D tex_glow: register(t2);\n"
"SamplerState smp: register(s0);\n"
"cbuffer params: register(b0) { float4 u_par[3]; };\n"
"struct vs_out { float4 pos: SV_Position; float2 uv: TEXCOORD0; };\n"
"float4 main(vs_out i): SV_Target0 {\n"
"  float time=u_par[0].x, flash=u_par[0].y, fade=u_par[0].z, flip=u_par[0].w;\n"
"  float ambient=u_par[1].x, glowstr=u_par[1].y, aberr=u_par[1].z, scan=u_par[1].w;\n"
"  float3 fadecol=u_par[2].xyz; float shake=u_par[2].w;\n"
"  float2 suv=float2(i.uv.x, lerp(i.uv.y,1.0-i.uv.y,flip));\n"
"  suv += float2(shake*0.003*sin(time*91.0), shake*0.003*cos(time*113.0));\n"
"  float2 d=(suv-0.5)*aberr*0.012;\n"
"  float3 scene; scene.r=tex_scene.Sample(smp,suv+d).r; scene.g=tex_scene.Sample(smp,suv).g; scene.b=tex_scene.Sample(smp,suv-d).b;\n"
"  float3 light=tex_light.Sample(smp,suv).rgb;\n"
"  float3 glow=tex_glow.Sample(smp,suv).rgb;\n"
"  float3 col=scene*(float3(ambient,ambient,ambient)+light*1.7)+glow*glowstr+float3(flash,flash,flash);\n"
"  col=col/(col+float3(0.55,0.55,0.55))*1.5;\n"
"  col=pow(abs(col),float3(1.06,1.0,0.93));\n"
"  float sl=1.0-scan*0.5*(0.5+0.5*sin(suv.y*270.0*3.14159));\n"
"  col*=sl;\n"
"  float2 vd=suv-0.5; col*=1.0-dot(vd,vd)*0.9;\n"
"  col=lerp(col,fadecol,fade);\n"
"  return float4(col,1.0);\n"
"}\n";

static const char* POST_VS_MSL =
"#include <metal_stdlib>\nusing namespace metal;\n"
"struct vs_out { float4 pos [[position]]; float2 uv; };\n"
"vertex vs_out _main(uint vid [[vertex_id]]){ vs_out o;"
" float2 p=float2(float((vid<<1)&2),float(vid&2)); o.uv=p;"
" o.pos=float4(p*2.0-1.0,0.0,1.0); return o; }\n";

static const char* POST_FS_MSL =
"#include <metal_stdlib>\nusing namespace metal;\n"
"struct vs_out { float4 pos [[position]]; float2 uv; };\n"
"struct params_t { float4 par[3]; };\n"
"fragment float4 _main(vs_out in [[stage_in]],\n"
"  constant params_t& u [[buffer(0)]],\n"
"  texture2d<float> tex_scene [[texture(0)]], texture2d<float> tex_light [[texture(1)]],\n"
"  texture2d<float> tex_glow [[texture(2)]], sampler smp [[sampler(0)]]){\n"
"  float time=u.par[0].x, flash=u.par[0].y, fade=u.par[0].z, flip=u.par[0].w;\n"
"  float ambient=u.par[1].x, glowstr=u.par[1].y, aberr=u.par[1].z, scan=u.par[1].w;\n"
"  float3 fadecol=u.par[2].xyz; float shake=u.par[2].w;\n"
"  float2 suv=float2(in.uv.x, mix(in.uv.y,1.0-in.uv.y,flip));\n"
"  suv += float2(shake*0.003*sin(time*91.0), shake*0.003*cos(time*113.0));\n"
"  float2 d=(suv-0.5)*aberr*0.012;\n"
"  float3 scene; scene.r=tex_scene.sample(smp,suv+d).r; scene.g=tex_scene.sample(smp,suv).g; scene.b=tex_scene.sample(smp,suv-d).b;\n"
"  float3 light=tex_light.sample(smp,suv).rgb;\n"
"  float3 glow=tex_glow.sample(smp,suv).rgb;\n"
"  float3 col=scene*(float3(ambient)+light*1.7)+glow*glowstr+float3(flash);\n"
"  col=col/(col+float3(0.55))*1.5;\n"
"  col=pow(col,float3(1.06,1.0,0.93));\n"
"  float sl=1.0-scan*0.5*(0.5+0.5*sin(suv.y*270.0*3.14159));\n"
"  col*=sl;\n"
"  float2 vd=suv-0.5; col*=1.0-dot(vd,vd)*0.9;\n"
"  col=mix(col,fadecol,fade);\n"
"  return float4(col,1.0);\n"
"}\n";

// ------------------------------------------------------------ radial light tex
static sg_image make_radial_tex(void){
    enum { N = 64 };
    static uint32_t px[N*N];
    for (int y=0;y<N;y++) for (int x=0;x<N;x++){
        float dx=(x+0.5f)/N-0.5f, dy=(y+0.5f)/N-0.5f;
        float d=sqrtf(dx*dx+dy*dy)*2.0f;          // 0 center .. 1 edge
        float v=clampf(1.0f-d,0.0f,1.0f);
        v=v*v*(3.0f-2.0f*v); v=v*v;               // smooth + sharpen center
        uint8_t b=(uint8_t)(v*255.0f);
        px[y*N+x]=(uint32_t)b<<24 | (uint32_t)b<<16 | (uint32_t)b<<8 | b; // RGBA white*alpha-ish
    }
    return sg_make_image(&(sg_image_desc){
        .width=N,.height=N,.pixel_format=SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0]=SG_RANGE(px), .label="radial"});
}

static sg_image make_rt(int w,int h,const char* label){
    return sg_make_image(&(sg_image_desc){
        .usage.color_attachment=true,.width=w,.height=h,
        .pixel_format=SG_PIXELFORMAT_RGBA8,.sample_count=1,.label=label});
}

void render_init(void){
    sg_setup(&(sg_desc){ .environment=sglue_environment(), .logger.func=slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func=slog_func });
    stm_setup();

    R.img_scene = make_rt(VIRT_W,VIRT_H,"scene");
    R.img_light = make_rt(VIRT_W,VIRT_H,"light");
    R.img_glow  = make_rt(GLOW_W,GLOW_H,"glow");
    R.att_scene = sg_make_view(&(sg_view_desc){ .color_attachment.image=R.img_scene });
    R.att_light = sg_make_view(&(sg_view_desc){ .color_attachment.image=R.img_light });
    R.att_glow  = sg_make_view(&(sg_view_desc){ .color_attachment.image=R.img_glow });
    R.tex_scene = sg_make_view(&(sg_view_desc){ .texture.image=R.img_scene });
    R.tex_light = sg_make_view(&(sg_view_desc){ .texture.image=R.img_light });
    R.tex_glow  = sg_make_view(&(sg_view_desc){ .texture.image=R.img_glow });

    R.smp_nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter=SG_FILTER_NEAREST,.mag_filter=SG_FILTER_NEAREST,
        .wrap_u=SG_WRAP_CLAMP_TO_EDGE,.wrap_v=SG_WRAP_CLAMP_TO_EDGE });
    R.smp_linear = sg_make_sampler(&(sg_sampler_desc){
        .min_filter=SG_FILTER_LINEAR,.mag_filter=SG_FILTER_LINEAR,
        .wrap_u=SG_WRAP_CLAMP_TO_EDGE,.wrap_v=SG_WRAP_CLAMP_TO_EDGE });

    sg_image radial = make_radial_tex();
    R.light_tex_view = sg_make_view(&(sg_view_desc){ .texture.image=radial });
    R.light_smp = R.smp_linear;

    // sgl contexts (offscreen RGBA8, no depth)
    sgl_context_desc_t cdesc = { .max_vertices=65536,.max_commands=16384,
        .color_format=SG_PIXELFORMAT_RGBA8,.depth_format=SG_PIXELFORMAT_NONE,.sample_count=1 };
    R.ctx_scene = sgl_make_context(&cdesc);
    R.ctx_light = sgl_make_context(&cdesc);
    R.ctx_glow  = sgl_make_context(&cdesc);

    sg_pipeline_desc alpha_blend = { .colors[0].blend={
        .enabled=true,
        .src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA,.dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha=SG_BLENDFACTOR_ONE,.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA }};
    sg_pipeline_desc add_blend = { .colors[0].blend={
        .enabled=true,
        .src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA,.dst_factor_rgb=SG_BLENDFACTOR_ONE,
        .src_factor_alpha=SG_BLENDFACTOR_ONE,.dst_factor_alpha=SG_BLENDFACTOR_ONE }};
    R.pip_scene = sgl_context_make_pipeline(R.ctx_scene,&alpha_blend);
    R.pip_light = sgl_context_make_pipeline(R.ctx_light,&add_blend);
    R.pip_glow  = sgl_context_make_pipeline(R.ctx_glow,&add_blend);
    R.pip_ui    = sgl_make_pipeline(&alpha_blend);

    // post shader
    sg_backend bk = sg_query_backend();
    sg_shader_desc sd = {0};
    if (bk==SG_BACKEND_METAL_MACOS){
        sd.vertex_func.source = POST_VS_MSL;   sd.vertex_func.entry = "_main";
        sd.fragment_func.source = POST_FS_MSL; sd.fragment_func.entry = "_main";
    } else if (bk==SG_BACKEND_D3D11){
        sd.vertex_func.source = POST_VS_HLSL;   sd.vertex_func.entry = "main";
        sd.fragment_func.source = POST_FS_HLSL; sd.fragment_func.entry = "main";
    } else {
        sd.vertex_func.source = POST_VS_GLSL;
        sd.fragment_func.source = POST_FS_GLSL;
    }
    sd.uniform_blocks[0] = (sg_shader_uniform_block){
        .stage=SG_SHADERSTAGE_FRAGMENT,.size=48,.msl_buffer_n=0,.hlsl_register_b_n=0,
        .layout=SG_UNIFORMLAYOUT_NATIVE,
        .glsl_uniforms[0]={ .type=SG_UNIFORMTYPE_FLOAT4,.array_count=3,.glsl_name="u_par" }};
    sd.views[0].texture=(sg_shader_texture_view){ .stage=SG_SHADERSTAGE_FRAGMENT,.msl_texture_n=0,.hlsl_register_t_n=0 };
    sd.views[1].texture=(sg_shader_texture_view){ .stage=SG_SHADERSTAGE_FRAGMENT,.msl_texture_n=1,.hlsl_register_t_n=1 };
    sd.views[2].texture=(sg_shader_texture_view){ .stage=SG_SHADERSTAGE_FRAGMENT,.msl_texture_n=2,.hlsl_register_t_n=2 };
    sd.samplers[0]=(sg_shader_sampler){ .stage=SG_SHADERSTAGE_FRAGMENT,.msl_sampler_n=0,.hlsl_register_s_n=0 };
    sd.texture_sampler_pairs[0]=(sg_shader_texture_sampler_pair){ .stage=SG_SHADERSTAGE_FRAGMENT,.view_slot=0,.sampler_slot=0,.glsl_name="tex_scene" };
    sd.texture_sampler_pairs[1]=(sg_shader_texture_sampler_pair){ .stage=SG_SHADERSTAGE_FRAGMENT,.view_slot=1,.sampler_slot=0,.glsl_name="tex_light" };
    sd.texture_sampler_pairs[2]=(sg_shader_texture_sampler_pair){ .stage=SG_SHADERSTAGE_FRAGMENT,.view_slot=2,.sampler_slot=0,.glsl_name="tex_glow" };
    sd.label="post";
    R.post_shader = sg_make_shader(&sd);
    R.post_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader=R.post_shader,.primitive_type=SG_PRIMITIVETYPE_TRIANGLES,
        .depth={ .pixel_format=SG_PIXELFORMAT_DEPTH_STENCIL, .write_enabled=false,
                 .compare=SG_COMPAREFUNC_ALWAYS },
        .label="post-pip" });
}

void render_shutdown(void){ sgl_shutdown(); sg_shutdown(); }

static float r_ambient = 0.085f;
static float r_scanline = 0.35f;
void render_set_ambient(float a){ r_ambient = a; }
void render_set_scanline(float s){ r_scanline = s; }

// ------------------------------------------------------------ frame
void render_begin_frame(void){
    // contexts are recorded during game drawing; nothing to do here
}

static void ortho_world(float cam_x, float cam_y){
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f,(float)VIRT_W,(float)VIRT_H,0.0f,-1.0f,1.0f);
    sgl_matrix_mode_modelview();
    sgl_translate(-cam_x,-cam_y,0.0f);
}

void draw_scene_begin(float cam_x,float cam_y){
    sgl_set_context(R.ctx_scene);
    ortho_world(cam_x,cam_y);
    sgl_load_pipeline(R.pip_scene);
}
void draw_light_begin(float cam_x,float cam_y){
    sgl_set_context(R.ctx_light);
    ortho_world(cam_x,cam_y);
    sgl_load_pipeline(R.pip_light);
}
void draw_glow_begin(float cam_x,float cam_y){
    sgl_set_context(R.ctx_glow);
    ortho_world(cam_x,cam_y);
    sgl_load_pipeline(R.pip_glow);
}
void draw_ui_begin(void){
    sgl_set_context(SGL_DEFAULT_CONTEXT);
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f,(float)VIRT_W,(float)VIRT_H,0.0f,-1.0f,1.0f);
    sgl_load_pipeline(R.pip_ui);
}

void render_end_frame(float time,float flash,float fade,col3 fade_col,float shake_x,float shake_y){
    float shake = sqrtf(shake_x*shake_x+shake_y*shake_y);
    // scene pass
    sg_begin_pass(&(sg_pass){ .action={ .colors[0]={ .load_action=SG_LOADACTION_CLEAR,
        .clear_value={0.043f,0.027f,0.063f,1.0f} } }, .attachments.colors[0]=R.att_scene });
    sgl_context_draw(R.ctx_scene);
    sg_end_pass();
    // light pass
    sg_begin_pass(&(sg_pass){ .action={ .colors[0]={ .load_action=SG_LOADACTION_CLEAR,
        .clear_value={0,0,0,1} } }, .attachments.colors[0]=R.att_light });
    sgl_context_draw(R.ctx_light);
    sg_end_pass();
    // glow pass
    sg_begin_pass(&(sg_pass){ .action={ .colors[0]={ .load_action=SG_LOADACTION_CLEAR,
        .clear_value={0,0,0,1} } }, .attachments.colors[0]=R.att_glow });
    sgl_context_draw(R.ctx_glow);
    sg_end_pass();
    // post + UI to swapchain
    sg_begin_pass(&(sg_pass){ .action={ .colors[0]={ .load_action=SG_LOADACTION_CLEAR,
        .clear_value={0,0,0,1} } }, .swapchain=sglue_swapchain() });
    sg_apply_pipeline(R.post_pip);
    sg_apply_bindings(&(sg_bindings){
        .views[0]=R.tex_scene,.views[1]=R.tex_light,.views[2]=R.tex_glow,
        .samplers[0]=R.smp_nearest });
    float flip = sg_query_features().origin_top_left ? 1.0f : 0.0f;
    float par[12] = {
        time, flash, fade, flip,
        r_ambient, 1.15f, 0.6f+shake*0.15f, r_scanline,
        fade_col.r, fade_col.g, fade_col.b, shake };
    sg_apply_uniforms(0,&SG_RANGE(par));
    sg_draw(0,3,1);
    sgl_draw(); // UI (default context)
    sg_end_pass();
    sg_commit();
}

// ------------------------------------------------------------ primitives
void draw_quad(float x,float y,float w,float h,col3 c,float a){
    sgl_disable_texture();
    sgl_begin_quads();
    sgl_c4f(c.r,c.g,c.b,a);
    sgl_v2f(x,y); sgl_v2f(x+w,y); sgl_v2f(x+w,y+h); sgl_v2f(x,y+h);
    sgl_end();
}

void draw_line(float x0,float y0,float x1,float y1,float thick,col3 c,float a){
    float dx=x1-x0, dy=y1-y0; float l=sqrtf(dx*dx+dy*dy); if(l<0.0001f) return;
    float nx=-dy/l*thick*0.5f, ny=dx/l*thick*0.5f;
    sgl_disable_texture();
    sgl_begin_quads();
    sgl_c4f(c.r,c.g,c.b,a);
    sgl_v2f(x0+nx,y0+ny); sgl_v2f(x1+nx,y1+ny); sgl_v2f(x1-nx,y1-ny); sgl_v2f(x0-nx,y0-ny);
    sgl_end();
}

void draw_sprite(int id,float x,float y,float w,float h,col3 tint,float a,bool flip_x,float rot){
    float u0=spr_uv[id][0],v0=spr_uv[id][1],u1=spr_uv[id][2],v1=spr_uv[id][3];
    if (flip_x){ float t=u0;u0=u1;u1=t; }
    sgl_enable_texture();
    sgl_texture(atlas_view,R.smp_nearest);
    sgl_push_matrix();
    sgl_translate(x,y,0.0f);
    if (rot!=0.0f) sgl_rotate(rot,0.0f,0.0f,1.0f);
    float hw=w*0.5f,hh=h*0.5f;
    sgl_begin_quads();
    sgl_c4f(tint.r,tint.g,tint.b,a);
    sgl_v2f_t2f(-hw,-hh,u0,v0); sgl_v2f_t2f(hw,-hh,u1,v0);
    sgl_v2f_t2f(hw,hh,u1,v1);   sgl_v2f_t2f(-hw,hh,u0,v1);
    sgl_end();
    sgl_pop_matrix();
}

void draw_light_blob(float x,float y,float radius,col3 c,float intensity){
    sgl_enable_texture();
    sgl_texture(R.light_tex_view,R.light_smp);
    sgl_begin_quads();
    sgl_c4f(c.r*intensity,c.g*intensity,c.b*intensity,1.0f);
    sgl_v2f_t2f(x-radius,y-radius,0,0); sgl_v2f_t2f(x+radius,y-radius,1,0);
    sgl_v2f_t2f(x+radius,y+radius,1,1); sgl_v2f_t2f(x-radius,y+radius,0,1);
    sgl_end();
}

void draw_glow_blob(float x,float y,float radius,col3 c,float intensity){
    draw_light_blob(x,y,radius,c,intensity);
}

// ---------------------------------------------------- shadowed key light
// 가시성 다각형: 광원에서 박스 모서리로 레이캐스트 → 부채꼴로 라디얼 텍스처를 그림.
typedef struct { float x0,y0,x1,y1; } Seg;
static Seg segs[MAX_SHADOW_BOXES*4];
static int seg_count;
typedef struct { float ang, x, y; } HitPt;
static HitPt hits[MAX_SHADOW_BOXES*12+8];
static int hit_count;

static bool ray_seg(float ox,float oy,float dx,float dy,Seg s,float* t_out){
    float ex=s.x1-s.x0, ey=s.y1-s.y0;
    float den=dx*ey-dy*ex;
    if (fabsf(den)<1e-9f) return false;
    float t=((s.x0-ox)*ey-(s.y0-oy)*ex)/den;
    float u=((s.x0-ox)*dy-(s.y0-oy)*dx)/den;
    if (t>0.0f && u>=0.0f && u<=1.0f){ *t_out=t; return true; }
    return false;
}

static void cast_ray(float ox,float oy,float ang,float radius){
    float dx=cosf(ang), dy=sinf(ang);
    float best=radius;
    for (int i=0;i<seg_count;i++){
        float t;
        if (ray_seg(ox,oy,dx,dy,segs[i],&t) && t<best) best=t;
    }
    if (hit_count < (int)(sizeof(hits)/sizeof(hits[0]))){
        hits[hit_count++] = (HitPt){ ang, ox+dx*best, oy+dy*best };
    }
}

static int hit_cmp(const void* a,const void* b){
    float d=((const HitPt*)a)->ang-((const HitPt*)b)->ang;
    return d<0?-1:(d>0?1:0);
}

void draw_shadowed_light(float x,float y,float radius,col3 c,float intensity){
    // gather segments near light
    seg_count=0;
    for (int i=0;i<sbox_count && seg_count<=MAX_SHADOW_BOXES*4-4;i++){
        SBox b=sboxes[i];
        if (b.x>x+radius||b.x+b.w<x-radius||b.y>y+radius||b.y+b.h<y-radius) continue;
        segs[seg_count++]=(Seg){b.x,b.y,b.x+b.w,b.y};
        segs[seg_count++]=(Seg){b.x+b.w,b.y,b.x+b.w,b.y+b.h};
        segs[seg_count++]=(Seg){b.x+b.w,b.y+b.h,b.x,b.y+b.h};
        segs[seg_count++]=(Seg){b.x,b.y+b.h,b.x,b.y};
    }
    hit_count=0;
    if (seg_count==0){
        draw_light_blob(x,y,radius,c,intensity);
        return;
    }
    // rays to corners (±epsilon) + base sweep for round edge
    for (int i=0;i<seg_count;i++){
        float cx=segs[i].x0, cy=segs[i].y0;
        float a=atan2f(cy-y,cx-x);
        cast_ray(x,y,a-0.0008f,radius);
        cast_ray(x,y,a+0.0008f,radius);
    }
    for (int i=0;i<24;i++){
        cast_ray(x,y,(float)i*(6.2831853f/24.0f),radius);
    }
    qsort(hits,(size_t)hit_count,sizeof(HitPt),hit_cmp);
    // triangle fan with radial texture coords
    sgl_enable_texture();
    sgl_texture(R.light_tex_view,R.light_smp);
    sgl_begin_triangles();
    sgl_c4f(c.r*intensity,c.g*intensity,c.b*intensity,1.0f);
    float inv=0.5f/radius;
    for (int i=0;i<hit_count;i++){
        HitPt A=hits[i], B=hits[(i+1)%hit_count];
        sgl_v2f_t2f(x,y,0.5f,0.5f);
        sgl_v2f_t2f(A.x,A.y,0.5f+(A.x-x)*inv,0.5f+(A.y-y)*inv);
        sgl_v2f_t2f(B.x,B.y,0.5f+(B.x-x)*inv,0.5f+(B.y-y)*inv);
    }
    sgl_end();
}
