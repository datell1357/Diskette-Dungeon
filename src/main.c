// 디스켓 던전 — The Last Read (unity build: 게임 코드 전체)
#include "game.h"

#ifdef DD_DEBUG
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
typedef struct {
    int clean_profile;
    uint32_t seed;
    int have_seed;
    int difficulty;
    int have_difficulty;
    int weapon;
    int have_weapon;
    int ngplus;
    int have_ngplus;
    int jump_biome, jump_room;
    int have_jump;
    int action;
    int have_action;
    int force_target[4];
    int have_force_target;
    int force_pending;
    int have_force_pending;
    int force_event[4];
    int have_force_event;
    int death_action;
    int have_death_action;
    char telemetry[1024];
    int have_telemetry;
    int telemetry_interval_ms;
    int have_telemetry_interval;
    int auto_play, god, intro, ending;
    int ending_value;
    char audio_dump[1024];
    int have_audio_dump;
    int duration_ms;
    int have_duration;
    int f10_branch;
    int have_f10_branch;
    char isolated_profile[1024];
    int have_isolated_profile;
    int expect_version;
    int have_expect_version;
    char source_path[1024];
    int have_source_path;
    int retry_key;
    int have_retry_key;
    int showcase_checkpoint;
    int have_showcase_checkpoint;
    int opening_checkpoint;
    int have_opening_checkpoint;
    int ending_checkpoint;
    int have_ending_checkpoint;
    int options_checkpoint;
    int have_options_checkpoint;
    int start_intro_checkpoint;
    int have_start_intro_checkpoint;
    int core_count;
    int have_core_count;
    char checkpoint[64];
    int have_checkpoint;
    int hold_ms;
    int have_hold_ms;
} DebugConfig;

/* Deliberately outside Game: game_init() clears G before this is consumed. */
DebugConfig DBG_CFG;
extern void debug_apply_config_after_game_init(void);
#ifndef DD_DEBUG_SOURCE_PATH
#define DD_DEBUG_SOURCE_PATH ""
#endif
#endif
#ifdef DD_DEBUG
static void debug_error(const char* why);
static void debug_validate_isolated_profile(void);
static FILE* debug_guarded_fopen(const char* path, const char* mode);
#if !defined(_WIN32)
static int debug_guarded_system(const char* command);
#endif
#endif
#include "render.c"
#include "assets.c"
#include "font.c"
#include "audio.c"
#ifdef DD_DEBUG
#define fopen debug_guarded_fopen
#if !defined(_WIN32)
#define system debug_guarded_system
#endif
#endif
#include "game.c"
#ifdef DD_DEBUG
#undef fopen
#undef system
#endif
#include "combat.c"
#include "ui_story.c"

#ifdef DD_DEBUG

static void debug_error(const char* why){
    fprintf(stderr, "{\"error\":\"%s\"}\n", why);
    exit(2);
}
static void debug_invalid_checkpoint(void){
    const char* action=DBG_CFG.action==22?"fixture-ddd-opening":
                       DBG_CFG.action==23?"fixture-ddd-ending":"fixture-ddd-start-intro";
    fprintf(stderr,"{\"error\":\"invalid-checkpoint\",\"action\":\"%s\",\"checkpoint\":\"%s\"}\n",
            action,DBG_CFG.checkpoint);
    exit(2);
}
static unsigned long debug_uint(const char* s, unsigned long max){
    char* end = 0;
    unsigned long v;
    if (!s || !*s || *s=='-' || *s=='+' || *s<'0' || *s>'9') debug_error("malformed-number");
    v = strtoul(s, &end, 10);
    if (*end || v > max) debug_error("malformed-number");
    return v;
}
static int debug_is_launchservices_psn(const char* a){
    const char* p;
    if (strncmp(a,"-psn_0_",7)) return 0;
    p=a+7;
    if (!*p) return 0;
    for (;*p;p++) if (*p<'0' || *p>'9') return 0;
    return 1;
}
static void debug_validate_source_path(const char* path){
    if (!path || !*path || path[0]!='/' || strcmp(path,DD_DEBUG_SOURCE_PATH))
        debug_error("invalid-source-path");
}
static void debug_need(int i, int argc){ if (i+1 >= argc) debug_error("missing-operand"); }
#if !defined(_WIN32)
static void debug_check_chain(const char* path, int final_dir, int allow_missing){
    char current[PATH_MAX];
    const char* part;
    if (!path || path[0]!='/' || strlen(path)>=sizeof current)
        debug_error("unsafe-isolated-profile");
    strcpy(current,"/");
    part=path+1;
    while (*part){
        const char* slash=strchr(part,'/');
        size_t len=slash ? (size_t)(slash-part) : strlen(part);
        size_t used;
        struct stat st;
        if (!len || len>=sizeof current) debug_error("unsafe-isolated-profile");
        used=strlen(current);
        if (used>1) current[used++]='/';
        if (used+len>=sizeof current) debug_error("unsafe-isolated-profile");
        memcpy(current+used,part,len);
        current[used+len]=0;
        if (lstat(current,&st)!=0){
            if (allow_missing && errno==ENOENT) return;
            debug_error("unsafe-isolated-profile");
        }
        if (S_ISLNK(st.st_mode)) debug_error("unsafe-isolated-profile");
        if (slash && !S_ISDIR(st.st_mode)) debug_error("unsafe-isolated-profile");
        if (!slash){
            if (final_dir && !S_ISDIR(st.st_mode)) debug_error("unsafe-isolated-profile");
            return;
        }
        part=slash+1;
    }
    debug_error("unsafe-isolated-profile");
}
static void debug_canonical(const char* path, char* out){
    if (!realpath(path,out)) debug_error("unsafe-isolated-profile");
}
static void debug_validate_save_target(const char* path){
    const char* home=getenv("HOME");
    char expected[PATH_MAX], parent[PATH_MAX];
    struct stat st;
    size_t n;
    if (!home || snprintf(expected,sizeof expected,
                          "%s/Library/Application Support/DisketteDungeon/save.bin",
                          home) >= (int)sizeof expected ||
        strcmp(path,expected)!=0)
        debug_error("unsafe-isolated-profile");
    n=strlen(expected);
    if (n<=strlen("/save.bin")) debug_error("unsafe-isolated-profile");
    memcpy(parent,expected,n-strlen("/save.bin"));
    parent[n-strlen("/save.bin")]=0;
    debug_check_chain(parent,1,0);
    if (lstat(expected,&st)==0){
        if (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode))
            debug_error("unsafe-isolated-profile");
    } else if (errno!=ENOENT) {
        debug_error("unsafe-isolated-profile");
    }
}
static void debug_validate_isolated_profile(void){
    static const char marker_text[]="Diskette Dungeon Mac evidence profile\n";
    const char* home=getenv("HOME");
    char cwd[PATH_MAX], canonical_cwd[PATH_MAX];
    char base[PATH_MAX], canonical_base[PATH_MAX];
    char legacy_base[PATH_MAX], canonical_legacy_base[PATH_MAX];
    char canonical_home[PATH_MAX], marker[PATH_MAX], canonical_marker[PATH_MAX];
    const char* case_start;
    const char* home_sep;
    size_t blen, hlen, marker_len;
    struct stat st;
    int fd;
    char got[sizeof marker_text];
    char extra;
    size_t got_len=0;
    if (!home || strcmp(home,DBG_CFG.isolated_profile)!=0 || home[0]!='/')
        debug_error("unsafe-isolated-profile");
    if (strpbrk(home,"\"\\$`\n\r"))
        debug_error("unsafe-isolated-profile");
    if (!getcwd(cwd,sizeof cwd)) debug_error("unsafe-isolated-profile");
    debug_canonical(cwd,canonical_cwd);
    if (snprintf(base,sizeof base,"%s/build/evidence",canonical_cwd) >= (int)sizeof base)
        debug_error("unsafe-isolated-profile");
    debug_canonical(base,canonical_base);
    if (snprintf(legacy_base,sizeof legacy_base,"%s/mac-first/save",canonical_base) >= (int)sizeof legacy_base)
        debug_error("unsafe-isolated-profile");
    debug_canonical(legacy_base,canonical_legacy_base);
    debug_check_chain(home,1,0);
    debug_canonical(home,canonical_home);
    if (strcmp(home,canonical_home)!=0)
        debug_error("unsafe-isolated-profile");
    blen=strlen(canonical_legacy_base); hlen=strlen(canonical_home);
    if (hlen>blen && !strncmp(canonical_home,canonical_legacy_base,blen) &&
        canonical_home[blen]=='/'){
        case_start=canonical_home+blen+1;
        home_sep=strchr(case_start,'/');
        if (!*case_start || !home_sep || strcmp(home_sep,"/home")!=0)
            debug_error("unsafe-isolated-profile");
    } else {
        blen=strlen(canonical_base);
        if (hlen<=blen || strncmp(canonical_home,canonical_base,blen)!=0 ||
            canonical_home[blen]!='/')
            debug_error("unsafe-isolated-profile");
        case_start=canonical_home+blen+1;
        home_sep=strchr(case_start,'/');
        if (!*case_start || case_start[0]=='/' || !home_sep ||
            strchr(case_start,'/')!=home_sep || strcmp(home_sep,"/save/home")!=0)
            debug_error("unsafe-isolated-profile");
    }
    if (lstat(canonical_home,&st)!=0 || !S_ISDIR(st.st_mode) ||
        st.st_uid!=geteuid())
        debug_error("unsafe-isolated-profile");
    marker_len=strlen("/home");
    if (hlen<=marker_len ||
        snprintf(marker,sizeof marker,"%.*s/.dd-agent-owned-profile",
                 (int)(hlen-marker_len),canonical_home) >= (int)sizeof marker)
        debug_error("unsafe-isolated-profile");
    debug_check_chain(marker,0,0);
    debug_canonical(marker,canonical_marker);
    if (strcmp(marker,canonical_marker)!=0 ||
        lstat(canonical_marker,&st)!=0 || !S_ISREG(st.st_mode) ||
        st.st_uid!=geteuid())
        debug_error("unsafe-isolated-profile");
    fd=open(canonical_marker,O_RDONLY|O_NOFOLLOW);
    if (fd<0) debug_error("unsafe-isolated-profile");
    while (got_len<sizeof marker_text-1){
        ssize_t n=read(fd,got+got_len,sizeof marker_text-1-got_len);
        if (n<=0){ close(fd); debug_error("unsafe-isolated-profile"); }
        got_len+=(size_t)n;
    }
    if (read(fd,&extra,1)!=0 || close(fd)!=0 ||
        memcmp(got,marker_text,sizeof marker_text-1)!=0)
        debug_error("unsafe-isolated-profile");
}
#else
static void debug_validate_save_target(const char* path){
    (void)path;
    debug_error("unsafe-isolated-profile");
}
static void debug_validate_isolated_profile(void){
    debug_error("unsafe-isolated-profile");
}
#endif
static FILE* debug_guarded_fopen(const char* path, const char* mode){
    if (DBG_CFG.have_isolated_profile){
        debug_validate_isolated_profile();
        debug_validate_save_target(path);
    }
    return fopen(path,mode);
}
#if !defined(_WIN32)
static int debug_guarded_system(const char* command){
#if defined(_WIN32)
    if (DBG_CFG.have_isolated_profile)
        debug_error("unsafe-isolated-profile");
#else
    if (DBG_CFG.have_isolated_profile){
        const char* home=getenv("HOME");
        char dir[PATH_MAX], expected[PATH_MAX];
        if (!home ||
            snprintf(dir,sizeof dir,"%s/Library/Application Support/DisketteDungeon",
                     home) >= (int)sizeof dir ||
            snprintf(expected,sizeof expected,"mkdir -p \"%s\"",dir) >= (int)sizeof expected ||
            strcmp(command,expected)!=0)
            debug_error("unsafe-isolated-profile");
        debug_validate_isolated_profile();
        debug_check_chain(dir,1,1);
    }
#endif
    return system(command);
}
#endif
static void debug_jump(const char* s){
    char* end = 0; unsigned long b, r;
    if (!s || !*s || *s=='-' || *s=='+' || *s<'0' || *s>'9') debug_error("malformed-jump");
    b = strtoul(s, &end, 10);
    if (end==s || *end!=',' || b>3) debug_error("malformed-jump");
    s=end+1;
    if (!*s || *s<'0' || *s>'9') debug_error("malformed-jump");
    r = strtoul(s, &end, 10);
    if (*end || r > 8) debug_error("malformed-jump");
    DBG_CFG.jump_biome=(int)b; DBG_CFG.jump_room=(int)r; DBG_CFG.have_jump=1;
}
static void debug_tuple(const char* s, int* out, int n, unsigned long* max){
    int i;
    for (i=0;i<n;i++){
        char* end=0; unsigned long v;
        if (!*s || *s==',' || *s=='-' || *s=='+' || *s<'0' || *s>'9') debug_error("malformed-tuple");
        v=strtoul(s,&end,10);
        if (end==s || v>max[i] || (*end && *end!=',')) debug_error("malformed-tuple");
        out[i]=(int)v;
        if (i<n-1){ if (*end!=',') debug_error("malformed-tuple"); s=end+1; }
        else if (*end) debug_error("malformed-tuple");
    }
}
static int debug_action(const char* s){
    static const char* names[] = {
        "snapshot-room","snapshot-reward","fixture-qne","fixture-qae",
        "fixture-qcol","fixture-qcol-input","produce-save-v1","produce-save-v2",
        "fixture-save-roundtrip","fixture-save-reject","fixture-invariant-failure",
        "fixture-modifiers","fixture-haste","fixture-endings",
        "fixture-ddd-legacy-settlement","fixture-ddd-retry-contract",
        "fixture-ddd-forfeit","fixture-ddd-promise-labels",
        "fixture-ddd-shake-menu","fixture-ddd-shake-roundtrip",
        "fixture-ddd-ui-showcase","fixture-ddd-opening","fixture-ddd-ending","fixture-ddd-options",
        "fixture-ddd-start-intro","fixture-story-signals","fixture-ddd-training"
    };
    int i;
    for (i=0;i<(int)(sizeof(names)/sizeof(names[0]));i++)
        if (!strcmp(s,names[i])) return i+1;
    debug_error("unknown-action"); return 0;
}
static void debug_parse_expect(const char* s){
    if (!strcmp(s,"v1")) DBG_CFG.expect_version=1;
    else if (!strcmp(s,"v2")) DBG_CFG.expect_version=2;
    else if (!strcmp(s,"v3")) DBG_CFG.expect_version=3;
    else if (!strcmp(s,"v4")) DBG_CFG.expect_version=4;
    else debug_error("invalid-expect");
}
static void debug_validate_cli(void){
    int a=DBG_CFG.action;
    int finite=DBG_CFG.have_action;
    int room_action=a>=1 && a<=6;
    int save_action=a==9 || a==10 || a==20 || a==24;
    if (DBG_CFG.have_telemetry_interval && !DBG_CFG.have_telemetry)
        debug_error("telemetry-interval-without-telemetry");
    if (finite){
        if (!save_action && !DBG_CFG.clean_profile) debug_error("clean-profile-required");
        if (!save_action && !DBG_CFG.have_seed) debug_error("seed-required");
        if (!save_action &&
            (!DBG_CFG.have_difficulty || !DBG_CFG.have_weapon || !DBG_CFG.have_ngplus))
            debug_error("explicit-config-required");
        if (room_action && !DBG_CFG.have_jump) debug_error("jump-required");
        if (a==13 && !DBG_CFG.have_jump) debug_error("jump-required");
        if (!room_action && a!=13 && DBG_CFG.have_jump)
            debug_error("jump-incompatible");
        if (DBG_CFG.have_telemetry) debug_error("action-telemetry-incompatible");
        if (DBG_CFG.auto_play || DBG_CFG.intro || DBG_CFG.ending>=0 || DBG_CFG.have_audio_dump)
            debug_error("action-option-incompatible");
    } else if (DBG_CFG.have_telemetry){
        if (!DBG_CFG.clean_profile || !DBG_CFG.have_seed || !DBG_CFG.have_difficulty ||
            !DBG_CFG.have_weapon || !DBG_CFG.have_ngplus || !DBG_CFG.have_jump)
            debug_error("telemetry-config-required");
        if (DBG_CFG.intro || DBG_CFG.ending>=0 || DBG_CFG.have_audio_dump)
            debug_error("telemetry-option-incompatible");
    }
    if (DBG_CFG.have_seed && !DBG_CFG.seed && a!=16) debug_error("out-of-range");
    if (DBG_CFG.have_retry_key && a!=16) debug_error("retry-key-incompatible");
    if (a==16 && !DBG_CFG.have_retry_key) debug_error("retry-key-required");
    if (a==20 && (!DBG_CFG.have_expect_version || DBG_CFG.expect_version!=2))
        debug_error("shake-roundtrip-expect-v2");
    if (a==24 && (!DBG_CFG.have_options_checkpoint || !DBG_CFG.have_expect_version ||
                  DBG_CFG.expect_version!=4))
        debug_error("options-fixture-controls-required");
    if (a==21 && (!DBG_CFG.have_showcase_checkpoint || !DBG_CFG.have_hold_ms))
        debug_error("showcase-controls-required");
    if (a==22 && DBG_CFG.have_checkpoint && !DBG_CFG.have_opening_checkpoint)
        debug_invalid_checkpoint();
    if (a==23 && DBG_CFG.have_checkpoint && !DBG_CFG.have_ending_checkpoint)
        debug_invalid_checkpoint();
    if (a==25 && DBG_CFG.have_checkpoint && !DBG_CFG.have_start_intro_checkpoint)
        debug_invalid_checkpoint();
    if (a==22 && (!DBG_CFG.have_opening_checkpoint || !DBG_CFG.have_hold_ms))
        debug_error("opening-controls-required");
    if (a==23 && (!DBG_CFG.have_ending_checkpoint || !DBG_CFG.have_core_count || !DBG_CFG.have_hold_ms))
        debug_error("ending-controls-required");
    if (a==25 && (!DBG_CFG.have_start_intro_checkpoint || !DBG_CFG.have_hold_ms))
        debug_error("start-intro-controls-required");
    if (a!=21 && DBG_CFG.have_showcase_checkpoint)
        debug_error("showcase-controls-incompatible");
    if (a!=22 && DBG_CFG.have_opening_checkpoint)
        debug_error("opening-controls-incompatible");
    if (a!=23 && DBG_CFG.have_ending_checkpoint)
        debug_error("ending-controls-incompatible");
    if (a!=23 && DBG_CFG.have_core_count)
        debug_error("core-count-incompatible");
    if (a!=24 && DBG_CFG.have_options_checkpoint)
        debug_error("options-checkpoint-incompatible");
    if (a!=25 && DBG_CFG.have_start_intro_checkpoint)
        debug_error("start-intro-checkpoint-incompatible");
    if (a!=21 && a!=22 && a!=23 && a!=24 && a!=25 && DBG_CFG.have_checkpoint &&
        !DBG_CFG.have_showcase_checkpoint)
        debug_error("unknown-checkpoint");
    if (a!=21 && a!=22 && a!=23 && a!=24 && a!=25 && DBG_CFG.have_hold_ms)
        debug_error("showcase-controls-incompatible");
    if (a==22 && (DBG_CFG.seed!=1 || DBG_CFG.difficulty!=0 || DBG_CFG.weapon!=0 ||
                  DBG_CFG.ngplus!=1 || DBG_CFG.hold_ms!=10000))
        debug_error("opening-controls-mismatch");
    if (a==23 && (DBG_CFG.seed!=1 || DBG_CFG.difficulty!=0 || DBG_CFG.weapon!=0 ||
                  DBG_CFG.ngplus!=1 || DBG_CFG.hold_ms!=10000))
        debug_error("ending-controls-mismatch");
    if (a==25 && (DBG_CFG.seed!=1 || DBG_CFG.difficulty!=0 || DBG_CFG.weapon!=0 ||
                  DBG_CFG.ngplus!=1 || DBG_CFG.hold_ms!=10000))
        debug_error("start-intro-controls-mismatch");
    if (a==23 && ((DBG_CFG.ending_checkpoint==1 && DBG_CFG.core_count!=0) ||
                  (DBG_CFG.ending_checkpoint==2 && (DBG_CFG.core_count<1 || DBG_CFG.core_count>3)) ||
                  (DBG_CFG.ending_checkpoint==3 && DBG_CFG.core_count!=4)))
        debug_error("invalid-core-count");
    if (DBG_CFG.have_source_path && a!=21)
        debug_error("source-path-incompatible");
    if (DBG_CFG.have_force_target && (a!=3 && a!=4)) debug_error("force-target-incompatible");
    if (DBG_CFG.have_force_pending && (a<5 || a>6) && (a!=3 && a!=4)) debug_error("force-pending-incompatible");
    if (DBG_CFG.have_force_event && a!=5 && a!=6) debug_error("force-event-incompatible");
    if (DBG_CFG.have_death_action && (a!=3 && a!=4)) debug_error("death-action-incompatible");
    if (a==3){
        if (!DBG_CFG.have_force_target || DBG_CFG.force_target[0]!=0 || DBG_CFG.force_target[1]!=0 ||
            DBG_CFG.force_target[2]!=0 || DBG_CFG.force_target[3]!=0 || !DBG_CFG.have_force_pending ||
            DBG_CFG.force_pending!=1 || !DBG_CFG.have_death_action || DBG_CFG.death_action!=4)
            debug_error("fixture-controls-mismatch");
    } else if (a==4){
        if (!DBG_CFG.have_force_target || DBG_CFG.force_target[0]!=0 || DBG_CFG.force_target[1]!=8 ||
            DBG_CFG.force_target[2]!=1 || DBG_CFG.force_target[3]!=1 || !DBG_CFG.have_force_pending ||
            DBG_CFG.force_pending!=2 || !DBG_CFG.have_death_action || DBG_CFG.death_action!=3)
            debug_error("fixture-controls-mismatch");
    } else if (a==5 || a==6){
        if (!DBG_CFG.have_force_pending || DBG_CFG.force_pending!=1 || !DBG_CFG.have_force_event ||
            DBG_CFG.force_event[0]!=2 || DBG_CFG.force_event[1]!=1 || DBG_CFG.force_event[2]!=2 ||
            DBG_CFG.force_event[3]!=1)
            debug_error("fixture-controls-mismatch");
    } else if (DBG_CFG.have_force_target || DBG_CFG.have_force_pending ||
               DBG_CFG.have_force_event || DBG_CFG.have_death_action){
        debug_error("fixture-controls-incompatible");
    }
    if (a==2 && DBG_CFG.have_jump && DBG_CFG.jump_room==8) debug_error("reward-boss-room");
    if (DBG_CFG.have_duration && !DBG_CFG.have_telemetry)
        debug_error("duration-without-telemetry");
    if (DBG_CFG.have_telemetry && !DBG_CFG.have_duration)
        debug_error("duration-required");
    if (DBG_CFG.have_f10_branch){
        if (!DBG_CFG.auto_play || DBG_CFG.have_action || !DBG_CFG.have_telemetry ||
            !DBG_CFG.have_duration || !DBG_CFG.have_jump)
            debug_error("f10-controls-mismatch");
    }
    if (DBG_CFG.have_isolated_profile){
        debug_validate_isolated_profile();
        if (!DBG_CFG.have_action || (a!=9 && a!=10 && a!=20 && a!=24) ||
            !DBG_CFG.have_expect_version || DBG_CFG.clean_profile)
            debug_error("unsafe-isolated-profile");
    }
    if (DBG_CFG.have_expect_version && !DBG_CFG.have_isolated_profile)
        debug_error("expect-without-isolated-profile");
}
static void debug_parse(int argc, char** argv){
    int i; unsigned long lim4[4]={MAX_ENTITIES-1,E_TYPE_COUNT-1,1,2};
    unsigned long event_lim[4]={MAX_ENTITIES-1,E_TYPE_COUNT-1,2,2};
    memset(&DBG_CFG,0,sizeof DBG_CFG);
    DBG_CFG.difficulty=1; DBG_CFG.weapon=0; DBG_CFG.telemetry_interval_ms=1000;
    DBG_CFG.ending=-1;
    for(i=1;i<argc;i++){
        const char* a=argv[i];
        if(debug_is_launchservices_psn(a)){}
        else if(!strcmp(a,"--clean-profile")){ if(DBG_CFG.clean_profile++) debug_error("duplicate-option"); }
        else if(!strcmp(a,"--seed")){ debug_need(i,argc); if(DBG_CFG.have_seed)debug_error("duplicate-option"); DBG_CFG.seed=(uint32_t)debug_uint(argv[++i],4294967295UL); DBG_CFG.have_seed=1; }
        else if(!strcmp(a,"--difficulty")){ debug_need(i,argc); if(DBG_CFG.have_difficulty)debug_error("duplicate-option"); DBG_CFG.difficulty=(int)debug_uint(argv[++i],2); DBG_CFG.have_difficulty=1; }
        else if(!strcmp(a,"--weapon")){ debug_need(i,argc); if(DBG_CFG.have_weapon)debug_error("duplicate-option"); DBG_CFG.weapon=(int)debug_uint(argv[++i],5); DBG_CFG.have_weapon=1; }
        else if(!strcmp(a,"--ngplus")){ debug_need(i,argc); if(DBG_CFG.have_ngplus)debug_error("duplicate-option"); DBG_CFG.ngplus=(int)debug_uint(argv[++i],1); DBG_CFG.have_ngplus=1; }
        else if(!strcmp(a,"--jump")){ debug_need(i,argc); if(DBG_CFG.have_jump)debug_error("duplicate-option"); debug_jump(argv[++i]); }
        else if(!strcmp(a,"--action")){ debug_need(i,argc); if(DBG_CFG.have_action)debug_error("duplicate-option"); DBG_CFG.action=debug_action(argv[++i]); DBG_CFG.have_action=1; }
        else if(!strcmp(a,"--force-target")){ debug_need(i,argc); if(DBG_CFG.have_force_target)debug_error("duplicate-option"); debug_tuple(argv[++i],DBG_CFG.force_target,4,lim4); DBG_CFG.have_force_target=1; }
        else if(!strcmp(a,"--force-pending")){ debug_need(i,argc); if(DBG_CFG.have_force_pending)debug_error("duplicate-option"); DBG_CFG.force_pending=(int)debug_uint(argv[++i],2); if(!DBG_CFG.force_pending)debug_error("out-of-range"); DBG_CFG.have_force_pending=1; }
        else if(!strcmp(a,"--force-event")){ debug_need(i,argc); if(DBG_CFG.have_force_event)debug_error("duplicate-option"); debug_tuple(argv[++i],DBG_CFG.force_event,4,event_lim); if(DBG_CFG.force_event[0]<1||DBG_CFG.force_event[0]>2||DBG_CFG.force_event[3]!=1)debug_error("out-of-range"); DBG_CFG.have_force_event=1; }
        else if(!strcmp(a,"--death-action")){ debug_need(i,argc); if(DBG_CFG.have_death_action)debug_error("duplicate-option"); if(!strcmp(argv[++i],"damage"))DBG_CFG.death_action=1; else if(!strcmp(argv[i],"burn"))DBG_CFG.death_action=2; else if(!strcmp(argv[i],"self-destruct"))DBG_CFG.death_action=3; else if(!strcmp(argv[i],"finalize-twice"))DBG_CFG.death_action=4; else debug_error("unknown-death-action"); DBG_CFG.have_death_action=1; }
        else if(!strcmp(a,"--telemetry")){ debug_need(i,argc); if(DBG_CFG.have_telemetry)debug_error("duplicate-option"); if(!*argv[++i]||strlen(argv[i])>=sizeof DBG_CFG.telemetry)debug_error("invalid-telemetry-path"); strcpy(DBG_CFG.telemetry,argv[i]); DBG_CFG.have_telemetry=1; }
        else if(!strcmp(a,"--telemetry-interval-ms")){ debug_need(i,argc); if(DBG_CFG.have_telemetry_interval)debug_error("duplicate-option"); DBG_CFG.telemetry_interval_ms=(int)debug_uint(argv[++i],5000); if(DBG_CFG.telemetry_interval_ms<100)debug_error("out-of-range"); DBG_CFG.have_telemetry_interval=1; }
        else if(!strcmp(a,"--auto")){ if(DBG_CFG.auto_play)debug_error("duplicate-option"); DBG_CFG.auto_play=1; }
        else if(!strcmp(a,"--god")){ if(DBG_CFG.god)debug_error("duplicate-option"); DBG_CFG.god=1; }
        else if(!strcmp(a,"--intro")){ if(DBG_CFG.intro)debug_error("duplicate-option"); DBG_CFG.intro=1; }
        else if(!strcmp(a,"--ending")){ debug_need(i,argc); if(DBG_CFG.ending>=0)debug_error("duplicate-option"); DBG_CFG.ending=(int)debug_uint(argv[++i],3); }
        else if(!strcmp(a,"--duration-ms")){ debug_need(i,argc); if(DBG_CFG.have_duration)debug_error("duplicate-option"); DBG_CFG.duration_ms=(int)debug_uint(argv[++i],86400000UL); if(DBG_CFG.duration_ms<1)debug_error("out-of-range"); DBG_CFG.have_duration=1; }
        else if(!strcmp(a,"--f10-branch")){ debug_need(i,argc); if(DBG_CFG.have_f10_branch)debug_error("duplicate-option"); if(!strcmp(argv[++i],"keep"))DBG_CFG.f10_branch=1; else if(!strcmp(argv[i],"discard"))DBG_CFG.f10_branch=2; else debug_error("unknown-f10-branch"); DBG_CFG.have_f10_branch=1; }
        else if(!strcmp(a,"--isolated-profile")){ debug_need(i,argc); if(DBG_CFG.have_isolated_profile)debug_error("duplicate-option"); if(!*argv[++i]||strlen(argv[i])>=sizeof DBG_CFG.isolated_profile)debug_error("unsafe-isolated-profile"); strcpy(DBG_CFG.isolated_profile,argv[i]); DBG_CFG.have_isolated_profile=1; }
        else if(!strcmp(a,"--expect")){ debug_need(i,argc); if(DBG_CFG.have_expect_version)debug_error("duplicate-option"); debug_parse_expect(argv[++i]); DBG_CFG.have_expect_version=1; }
        else if(!strcmp(a,"--source-path")){ debug_need(i,argc); if(DBG_CFG.have_source_path)debug_error("duplicate-option"); if(strlen(argv[++i])>=sizeof DBG_CFG.source_path)debug_error("invalid-source-path"); debug_validate_source_path(argv[i]); strcpy(DBG_CFG.source_path,argv[i]); DBG_CFG.have_source_path=1; }
        else if(!strcmp(a,"--retry-key")){ debug_need(i,argc); if(DBG_CFG.have_retry_key)debug_error("duplicate-option"); if(!strcmp(argv[++i],"r"))DBG_CFG.retry_key=SAPP_KEYCODE_R; else if(!strcmp(argv[i],"enter"))DBG_CFG.retry_key=SAPP_KEYCODE_ENTER; else debug_error("unknown-retry-key"); DBG_CFG.have_retry_key=1; }
        else if(!strcmp(a,"--checkpoint")){
            const char* checkpoint;
            debug_need(i,argc);
            if(DBG_CFG.have_checkpoint) debug_error("duplicate-option");
            checkpoint=argv[++i];
            if(strlen(checkpoint)>=sizeof DBG_CFG.checkpoint) debug_error("invalid-checkpoint");
            strcpy(DBG_CFG.checkpoint,checkpoint); DBG_CFG.have_checkpoint=1;
            if(!strcmp(checkpoint,"death")) DBG_CFG.showcase_checkpoint=1;
            else if(!strcmp(checkpoint,"door")) DBG_CFG.showcase_checkpoint=2;
            else if(!strcmp(checkpoint,"pause")) DBG_CFG.showcase_checkpoint=3;
            else if(!strcmp(checkpoint,"hit")) DBG_CFG.showcase_checkpoint=4;
            else if(!strcmp(checkpoint,"boss-reward")) DBG_CFG.showcase_checkpoint=5;
            else if(!strcmp(checkpoint,"relic-swap")) DBG_CFG.showcase_checkpoint=6;
            else if(!strcmp(checkpoint,"weapon-relic-swap")) DBG_CFG.showcase_checkpoint=10;
            else if(!strcmp(checkpoint,"memory-event")) DBG_CFG.showcase_checkpoint=7;
            else if(!strcmp(checkpoint,"boss-intro")) DBG_CFG.showcase_checkpoint=8;
            else if(!strcmp(checkpoint,"core-flashback")) DBG_CFG.showcase_checkpoint=9;
            else if(!strcmp(checkpoint,"fire-trail-start")) DBG_CFG.showcase_checkpoint=11;
            else if(!strcmp(checkpoint,"fire-trail-mid")) DBG_CFG.showcase_checkpoint=12;
            else if(!strcmp(checkpoint,"fire-trail-end")) DBG_CFG.showcase_checkpoint=13;
            else if(!strcmp(checkpoint,"lance-thrust-start")) DBG_CFG.showcase_checkpoint=14;
            else if(!strcmp(checkpoint,"lance-thrust-mid")) DBG_CFG.showcase_checkpoint=15;
            else if(!strcmp(checkpoint,"lance-thrust-end")) DBG_CFG.showcase_checkpoint=16;
            else if(!strcmp(checkpoint,"wand-rain-start")) DBG_CFG.showcase_checkpoint=17;
            else if(!strcmp(checkpoint,"wand-rain-mid")) DBG_CFG.showcase_checkpoint=18;
            else if(!strcmp(checkpoint,"wand-rain-end")) DBG_CFG.showcase_checkpoint=19;
            else if(!strcmp(checkpoint,"wand-charge-base")) DBG_CFG.showcase_checkpoint=20;
            else if(!strcmp(checkpoint,"wand-charge-25")) DBG_CFG.showcase_checkpoint=21;
            else if(!strcmp(checkpoint,"wand-charge-50")) DBG_CFG.showcase_checkpoint=22;
            else if(!strcmp(checkpoint,"wand-charge-75")) DBG_CFG.showcase_checkpoint=23;
            else if(!strcmp(checkpoint,"wand-charge-full")) DBG_CFG.showcase_checkpoint=24;
            else if(!strcmp(checkpoint,"cannon-frag-wall")) DBG_CFG.showcase_checkpoint=25;
            else if(!strcmp(checkpoint,"cannon-rail-start")) DBG_CFG.showcase_checkpoint=26;
            else if(!strcmp(checkpoint,"cannon-rail-mid")) DBG_CFG.showcase_checkpoint=27;
            else if(!strcmp(checkpoint,"cannon-rail-end")) DBG_CFG.showcase_checkpoint=28;
            else if(!strcmp(checkpoint,"cannon-rail-frag")) DBG_CFG.showcase_checkpoint=29;
            else if(!strcmp(checkpoint,"cannon-rail-fuse-start")) DBG_CFG.showcase_checkpoint=30;
            else if(!strcmp(checkpoint,"cannon-rail-fuse-end")) DBG_CFG.showcase_checkpoint=31;
            else if(!strcmp(checkpoint,"cannon-rail-recoil")) DBG_CFG.showcase_checkpoint=32;
            else if(!strcmp(checkpoint,"insert")) DBG_CFG.opening_checkpoint=1;
            else if(!strcmp(checkpoint,"seek")) DBG_CFG.opening_checkpoint=2;
            else if(!strcmp(checkpoint,"retry")) DBG_CFG.opening_checkpoint=3;
            else if(!strcmp(checkpoint,"recover")) DBG_CFG.opening_checkpoint=4;
            else if(!strcmp(checkpoint,"transfer")) DBG_CFG.opening_checkpoint=5;
            else if(!strcmp(checkpoint,"title-handoff")) DBG_CFG.opening_checkpoint=6;
            else if(!strcmp(checkpoint,"skip-key")) DBG_CFG.opening_checkpoint=7;
            else if(!strcmp(checkpoint,"skip-mouse")) DBG_CFG.opening_checkpoint=8;
            else if(!strcmp(checkpoint,"wake")) DBG_CFG.opening_checkpoint=9;
            else if(!strcmp(checkpoint,"scan")) DBG_CFG.opening_checkpoint=10;
            else if(!strcmp(checkpoint,"reveal")) DBG_CFG.opening_checkpoint=11;
            else if(!strcmp(checkpoint,"title-flow")) DBG_CFG.opening_checkpoint=12;
            else if(!strcmp(checkpoint,"recovery-failed")) DBG_CFG.ending_checkpoint=1;
            else if(!strcmp(checkpoint,"partial-recovery")) DBG_CFG.ending_checkpoint=2;
            else if(!strcmp(checkpoint,"complete-recovery")) DBG_CFG.ending_checkpoint=3;
            else if(!strcmp(checkpoint,"options-toggle")) DBG_CFG.options_checkpoint=1;
            else if(!strcmp(checkpoint,"options-invalid-state")) DBG_CFG.options_checkpoint=2;
            else if(!strcmp(checkpoint,"options-invalid-input")) DBG_CFG.options_checkpoint=3;
            else if(!strcmp(checkpoint,"first-run")) DBG_CFG.start_intro_checkpoint=1;
            else if(!strcmp(checkpoint,"placement")) DBG_CFG.start_intro_checkpoint=2;
            else if(!strcmp(checkpoint,"latch")) DBG_CFG.start_intro_checkpoint=3;
            else if(!strcmp(checkpoint,"drive-stop-hold")) DBG_CFG.start_intro_checkpoint=4;
            else if(!strcmp(checkpoint,"track")) DBG_CFG.start_intro_checkpoint=5;
            else if(!strcmp(checkpoint,"fragment")) DBG_CFG.start_intro_checkpoint=6;
            else if(!strcmp(checkpoint,"handoff")) DBG_CFG.start_intro_checkpoint=7;
            else if(!strcmp(checkpoint,"repeat-bypass")) DBG_CFG.start_intro_checkpoint=8;
            else if(!strcmp(checkpoint,"queued-replay")) DBG_CFG.start_intro_checkpoint=9;
            else if(!strcmp(checkpoint,"post-replay-bypass")) DBG_CFG.start_intro_checkpoint=10;
            if(DBG_CFG.showcase_checkpoint) DBG_CFG.have_showcase_checkpoint=1;
            if(DBG_CFG.opening_checkpoint) DBG_CFG.have_opening_checkpoint=1;
            if(DBG_CFG.ending_checkpoint) DBG_CFG.have_ending_checkpoint=1;
            if(DBG_CFG.options_checkpoint) DBG_CFG.have_options_checkpoint=1;
            if(DBG_CFG.start_intro_checkpoint) DBG_CFG.have_start_intro_checkpoint=1;
        }
        else if(!strcmp(a,"--core-count")){ debug_need(i,argc); if(DBG_CFG.have_core_count)debug_error("duplicate-option"); DBG_CFG.core_count=(int)debug_uint(argv[++i],4); DBG_CFG.have_core_count=1; }
        else if(!strcmp(a,"--hold-ms")){ debug_need(i,argc); if(DBG_CFG.have_hold_ms)debug_error("duplicate-option"); DBG_CFG.hold_ms=(int)debug_uint(argv[++i],60000UL); if(DBG_CFG.hold_ms<1000)debug_error("out-of-range"); DBG_CFG.have_hold_ms=1; }
        else if(!strcmp(a,"--audiodump")){ debug_need(i,argc); if(DBG_CFG.have_audio_dump)debug_error("duplicate-option"); if(!*argv[++i]||strlen(argv[i])>=sizeof DBG_CFG.audio_dump)debug_error("invalid-audio-path"); strcpy(DBG_CFG.audio_dump,argv[i]); DBG_CFG.have_audio_dump=1; }
        else debug_error("unknown-option");
    }
    if(DBG_CFG.have_action && !DBG_CFG.clean_profile &&
       DBG_CFG.action!=9 && DBG_CFG.action!=10 && DBG_CFG.action!=20 && DBG_CFG.action!=24) debug_error("clean-profile-required");
    if(DBG_CFG.have_action && !DBG_CFG.have_seed &&
       DBG_CFG.action!=9 && DBG_CFG.action!=10 && DBG_CFG.action!=20 && DBG_CFG.action!=24) debug_error("seed-required");
    debug_validate_cli();
}
#endif

static void init_cb(void){
    sapp_set_window_title("Diskette Dungeon - The Last Read");
    render_init(); assets_init(); font_init(); audio_init(); game_init();
#ifdef DD_DEBUG
    debug_apply_config_after_game_init();
#endif
}
static void frame_cb(void){ game_frame(); }
static void cleanup_cb(void){ audio_shutdown(); render_shutdown(); }
static void event_cb(const sapp_event* e){ game_event(e); }

sapp_desc sokol_main(int argc, char* argv[]){
#ifdef DD_DEBUG
    DBG_CFG.ending=-1;
    debug_parse(argc,argv);
#else
    (void)argc; (void)argv;
#endif
    return (sapp_desc){ .init_cb=init_cb, .frame_cb=frame_cb, .cleanup_cb=cleanup_cb, .event_cb=event_cb,
        .width=960, .height=540, .sample_count=1, .high_dpi=false,
        .window_title="Diskette Dungeon - The Last Read", .icon.sokol_default=true, .logger.func=slog_func };
}
