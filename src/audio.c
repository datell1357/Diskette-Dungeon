// audio.c — 절차 신스: sfxr식 SFX + 생성형 칩튠 BGM (오디오 파일 0바이트)
#include "game.h"

#define SR 44100
#define MAX_VOICES 10

typedef struct {
    bool on;
    int wave;          // 0 pulse, 1 tri, 2 noise, 3 sine
    float t;           // phase
    float freq, freq_slide, freq_lim;
    float vol, decay;  // per-sample decay
    float dur;         // seconds left
    float duty, vib_amt, vib_spd, vib_t;
    uint32_t nz;       // noise state
    float nz_val; int nz_cnt; int nz_period;
} Voice;

typedef struct {
    Voice v[MAX_VOICES];
    // music
    int track;            // -1 off
    float step_t;         // seconds into current step
    int step;             // 0..15
    int bar;              // pattern progression
    Rng rng;
    float echo[SR];       // 1s delay line
    int echo_pos;
    float mvol;
    int pending_track;
} AudioState;
static AudioState A;

// sfx trigger queue (game thread -> audio thread)
static volatile int sfx_q[16];
static volatile int sfx_qw=0, sfx_qr=0;

void sfx_play(SfxId id){
    int w=sfx_qw;
    sfx_q[w&15]=(int)id;
    sfx_qw=w+1;
}
void music_set(int track){ A.pending_track = track; }

static float voice_sample(Voice* v){
    if (!v->on) return 0.0f;
    float f=v->freq;
    if (v->vib_amt>0){ v->vib_t+=v->vib_spd/SR; f*=1.0f+sinf(v->vib_t*6.2832f)*v->vib_amt; }
    v->t += f/SR;
    v->freq += v->freq_slide/SR;
    if (v->freq_slide<0 && v->freq<v->freq_lim) v->freq=v->freq_lim;
    if (v->freq_slide>0 && v->freq>v->freq_lim) v->freq=v->freq_lim;
    v->vol *= v->decay;
    v->dur -= 1.0f/SR;
    if (v->dur<=0 || v->vol<0.001f){ v->on=false; return 0.0f; }
    float ph=fractf(v->t);
    float s=0;
    switch (v->wave){
        case 0: s = ph<v->duty? 0.6f:-0.6f; break;
        case 1: s = (ph<0.5f? ph*4.0f-1.0f : 3.0f-ph*4.0f)*0.8f; break;
        case 2: {
            if (--v->nz_cnt<=0){
                v->nz_cnt=v->nz_period;
                v->nz = v->nz*1664525u+1013904223u;
                v->nz_val = ((v->nz>>16&0xFFFF)/32768.0f-1.0f)*0.7f;
            }
            s=v->nz_val;
        } break;
        default: s = sinf(ph*6.2832f)*0.8f; break;
    }
    return s*v->vol;
}

static Voice* alloc_voice(void){
    for (int i=0;i<MAX_VOICES;i++) if(!A.v[i].on) return &A.v[i];
    return &A.v[0];
}

static void start_voice(int wave,float freq,float slide,float lim,float vol,float decay_s,float dur,float duty,float vib_a,float vib_s){
    Voice* v=alloc_voice();
    memset(v,0,sizeof(*v));
    v->on=true; v->wave=wave; v->freq=freq; v->freq_slide=slide; v->freq_lim=lim>0?lim:(slide<0?20.0f:20000.0f);
    v->vol=vol; v->decay = decay_s>0? powf(0.001f,1.0f/(decay_s*SR)) : 1.0f;
    v->dur=dur; v->duty=duty>0?duty:0.5f;
    v->vib_amt=vib_a; v->vib_spd=vib_s;
    v->nz=12345; v->nz_period= freq>0? (int)(SR/(freq*8.0f))+1 : 8;
    if (v->nz_period<1)v->nz_period=1;
}

static void trigger_sfx(SfxId id){
    switch (id){
    case SFX_HIT:       start_voice(2,900,-4000,100,0.50f,0.10f,0.12f,0,0,0); break;
    case SFX_HURT:      start_voice(0,320,-900,80,0.55f,0.22f,0.25f,0.3f,0,0);
                        start_voice(2,500,-2000,60,0.35f,0.18f,0.2f,0,0,0); break;
    case SFX_DASH:      start_voice(2,2200,-9000,300,0.25f,0.09f,0.1f,0,0,0); break;
    case SFX_PICKUP:    start_voice(0,660,2400,1320,0.30f,0.12f,0.13f,0.5f,0,0); break;
    case SFX_SHARD:     start_voice(3,880,0,0,0.28f,0.30f,0.35f,0,0.01f,5);
                        start_voice(3,1320,0,0,0.18f,0.40f,0.45f,0,0.01f,6); break;
    case SFX_CORE_SHARD:start_voice(3,523,0,0,0.30f,0.8f,0.9f,0,0.008f,4);
                        start_voice(3,784,0,0,0.25f,0.9f,1.0f,0,0.008f,5);
                        start_voice(3,1046,0,0,0.22f,1.1f,1.2f,0,0.008f,6); break;
    case SFX_SHOOT:     start_voice(0,1100,-5500,200,0.22f,0.08f,0.09f,0.25f,0,0); break;
    case SFX_ENEMY_DIE: start_voice(2,700,-2500,80,0.40f,0.25f,0.28f,0,0,0);
                        start_voice(0,440,-1500,60,0.25f,0.2f,0.22f,0.4f,0,0); break;
    case SFX_DOOR:      start_voice(1,196,400,392,0.30f,0.35f,0.4f,0,0,0); break;
    case SFX_BOSS_DIE:  start_voice(2,400,-600,40,0.55f,1.0f,1.1f,0,0,0);
                        start_voice(3,220,-150,55,0.4f,1.2f,1.3f,0,0.02f,3); break;
    case SFX_DENY:      start_voice(0,180,-300,90,0.30f,0.15f,0.16f,0.5f,0,0); break;
    case SFX_UI:        start_voice(0,880,0,0,0.15f,0.05f,0.06f,0.5f,0,0); break;
    case SFX_DROP:      start_voice(1,500,-1200,150,0.28f,0.18f,0.2f,0,0,0); break;
    case SFX_HEAL:      start_voice(3,660,500,990,0.25f,0.3f,0.35f,0,0.01f,5); break;
    case SFX_BOSS_ROAR: start_voice(2,250,-300,60,0.5f,0.8f,0.9f,0,0,0);
                        start_voice(0,110,-60,55,0.45f,0.9f,1.0f,0.3f,0.03f,4); break;
    case SFX_ENDING:    start_voice(3,523,0,0,0.25f,2.0f,2.2f,0,0.005f,3);
                        start_voice(3,659,0,0,0.22f,2.2f,2.4f,0,0.005f,4);
                        start_voice(3,784,0,0,0.20f,2.5f,2.7f,0,0.005f,5); break;
    default: break;
    }
}

// ----------------------------------------------------------------- music
// 생성형: 바이옴별 코드 진행 + 베이스/아르페지오/리드. 미니멀하고 서정적인 칩튠.
static const float NOTE_HZ_BASE = 32.7032f; // C1
static float note_hz(int n){ return NOTE_HZ_BASE*powf(2.0f,n/12.0f); } // n: semitones above C1

// 코드 진행 (A minor 계열, 루트는 C1 기준 semitone)
typedef struct { int root[4]; int minor[4]; float bpm; int density; } TrackDef;
static const TrackDef tracks[7] = {
    {{9,5,7,4},  {1,0,1,0}, 70, 2},   // 0 title — Am F G Em
    {{9,9,5,7},  {1,1,0,0}, 84, 2},   // 1 배드 섹터
    {{2,7,9,4},  {1,0,1,1}, 88, 3},   // 2 잃어버린 트랙 — Dm G Am Em
    {{9,11,0,7}, {1,1,1,0}, 96, 3},   // 3 단편화 지대
    {{4,5,9,7},  {1,0,1,0}, 92, 3},   // 4 부트 레코드
    {{9,8,9,11}, {1,1,1,1}, 120,4},   // 5 보스
    {{0,7,9,5},  {0,0,1,0}, 60, 1},   // 6 엔딩
};

static void music_step(void){
    const TrackDef* td=&tracks[A.track];
    int chord = (A.bar>>1)&3;
    int root = td->root[chord];
    int third = root + (td->minor[chord]?3:4);
    int fifth = root + 7;
    int st = A.step;
    // bass: 8분 루트 (옥타브2)
    if ((st&1)==0){
        start_voice(1,note_hz(root+12),0,0,0.20f,0.16f,0.18f,0,0,0);
    }
    // arp: 16분 분산화음 (옥타브 4)
    if (td->density>=2){
        int pick = st%3==0?root: (st%3==1?third:fifth);
        if ((st+A.bar)%4!=3)
            start_voice(0,note_hz(pick+36),0,0,0.065f,0.12f,0.13f,0.3f,0,0);
    }
    // lead: 드문 멜로디 (옥타브 5, 펜타토닉 랜덤워크)
    if (td->density>=2 && (st==0||st==6||st==10||(td->density>=4&&st==12))){
        static int lead_deg=0;
        int scale[5]={0,3,5,7,10};
        lead_deg += rng_i(&A.rng,3)-1;
        if (lead_deg<0)lead_deg=0;
        if (lead_deg>4)lead_deg=4;
        if (rng_i(&A.rng,3)!=0)
            start_voice(0,note_hz(root+48+scale[lead_deg]),0,0,0.085f,0.5f,0.55f,0.5f,0.006f,5);
    }
    // perc: 약한 노이즈 hat
    if (td->density>=3 && (st&3)==2)
        start_voice(2,8000,-4000,2000,0.05f,0.04f,0.05f,0,0,0);
    if (td->density>=4 && (st&7)==4)
        start_voice(2,300,-600,80,0.18f,0.12f,0.13f,0,0,0); // kick-ish
}

// ----------------------------------------------------------------- stream
static void stream_cb(float* buffer,int num_frames,int num_channels){
    // sfx queue
    while (sfx_qr!=sfx_qw){ trigger_sfx((SfxId)sfx_q[sfx_qr&15]); sfx_qr++; }
    if (A.pending_track!=A.track){
        A.track=A.pending_track; A.step=0; A.bar=0; A.step_t=0;
    }
    float step_len = A.track>=0? 60.0f/(tracks[A.track].bpm*4.0f) : 1e9f;
    for (int i=0;i<num_frames;i++){
        if (A.track>=0){
            A.step_t += 1.0f/SR;
            if (A.step_t>=step_len){
                A.step_t-=step_len;
                A.step=(A.step+1)&15;
                if (A.step==0) A.bar++;
                music_step();
            }
        }
        float s=0;
        for (int vi=0;vi<MAX_VOICES;vi++) s+=voice_sample(&A.v[vi]);
        // soft clip + echo
        float e = A.echo[A.echo_pos];
        float out = tanhf(s + e*0.35f);
        A.echo[A.echo_pos] = out;
        A.echo_pos = (A.echo_pos+1)%(SR/3); // ~0.33s delay
        out *= A.mvol;
        for (int c=0;c<num_channels;c++) buffer[i*num_channels+c]=out;
    }
}

#ifdef DD_DEBUG
// 오프라인 신스 검증: 10초 렌더해 WAV로 저장 (귀 대신 파형 검사)
void audio_debug_dump(const char* path){
    memset(&A,0,sizeof(A));
    A.track=-1; A.pending_track=1; A.mvol=0.85f;
    A.rng.s=0x9E3779B97F4A7C15ull;
    enum { SECS=10 };
    static float buf[SR*SECS*2];
    for (int s=0;s<SECS;s++){
        if (s==4) sfx_play(SFX_HIT);
        if (s==5) sfx_play(SFX_CORE_SHARD);
        if (s==6) sfx_play(SFX_SHOOT);
        if (s==7) sfx_play(SFX_HURT);
        stream_cb(buf+s*SR*2, SR, 2);
    }
    FILE* f=fopen(path,"wb");
    if (!f) return;
    int16_t* pcm=(int16_t*)malloc(SR*SECS*2*sizeof(int16_t));
    for (int i=0;i<SR*SECS*2;i++){
        float v=buf[i]; if(v>1)v=1; if(v<-1)v=-1;
        pcm[i]=(int16_t)(v*32767.0f);
    }
    uint32_t data_sz=SR*SECS*2*2, sr=SR, byte_rate=SR*4;
    uint16_t ch=2, bits=16, block=4, fmt=1;
    uint32_t riff_sz=36+data_sz;
    fwrite("RIFF",1,4,f); fwrite(&riff_sz,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); uint32_t fsz=16; fwrite(&fsz,4,1,f);
    fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&sr,4,1,f);
    fwrite(&byte_rate,4,1,f); fwrite(&block,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&data_sz,4,1,f);
    fwrite(pcm,2,SR*SECS*2,f);
    fclose(f); free(pcm);
}
#endif

void audio_init(void){
    memset(&A,0,sizeof(A));
    A.track=-1; A.pending_track=-1; A.mvol=0.85f;
    A.rng.s=0x9E3779B97F4A7C15ull;
    saudio_setup(&(saudio_desc){
        .sample_rate=SR,.num_channels=2,.stream_cb=stream_cb,.logger.func=slog_func });
}
void audio_shutdown(void){ saudio_shutdown(); }
