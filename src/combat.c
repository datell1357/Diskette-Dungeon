// combat.c — 플레이어/무기/적 AI/보스/탄/픽업 갱신
#include "game.h"
#include "gameplay.h"

// input (ui_story.c에서 채움)
extern bool key_held[512];
extern bool attack_held;
extern v2 mouse_virt;
extern bool mouse_present;

// 검 휘두름 연출용
static float slash_t = 0; static v2 slash_dir;
float combat_slash_t(void){ return slash_t; }
v2 combat_slash_dir(void){ return slash_dir; }
static float lance_thrust_t = 0; static v2 lance_thrust_dir; static float lance_thrust_reach;
float combat_lance_thrust_t(void){ return lance_thrust_t; }
v2 combat_lance_thrust_dir(void){ return lance_thrust_dir; }
float combat_lance_thrust_reach(void){ return lance_thrust_reach; }

static Rng crng = { 0xFEEDFACE777ull };
static bool phase_stepping;
static bool execution_burst;

// ----------------------------------------------------------- 무게 변화 → 스탯 피드백
// 픽업/드롭으로 무게가 바뀌면 화면 고정 안내로 공격/이속/빛 변화를 띄운다.
typedef struct { float dmg, spd, light; } StatSnap;
static StatSnap stat_capture(void){
    return (StatSnap){ 1.0f+weight_frac()*0.5f, player_speed_mul(), player_light_radius() };
}
static void stat_floats(StatSnap before){
    StatSnap after = stat_capture();
    for (int i=0;i<MAX_FLOATERS;i++)
        if (G.floaters[i].screen_fixed) G.floaters[i].t=0.0f;
    float row = 44.0f;
    struct { const char* lbl; float b, a; } st[3] = {
        { "공격", before.dmg, after.dmg },
        { "이속", before.spd, after.spd },
        { "빛",   before.light, after.light },
    };
    for (int i=0;i<3;i++){
        if (st[i].b<=0.0f) continue;
        int pct = (int)lroundf((st[i].a/st[i].b - 1.0f)*100.0f);
        if (pct==0) continue;
        char buf[24];
        snprintf(buf,sizeof(buf),"%s %+d%%",st[i].lbl,pct);
        add_fixed_floater(VIRT_W*0.5f,row,buf,pct>0?COL(0x9FFFF0):COL(0xFF3D7F));
        row += 12.0f;
    }
}

// ----------------------------------------------------------- spawns
void spawn_enemy(int type, v2 pos){
    for (int i=0;i<MAX_ENTITIES;i++){
        Entity* e=&G.ents[i];
        if (e->active) continue;
        memset(e,0,sizeof(*e));
        e->active=true; e->type=type; e->pos=pos;
        e->spawn_t=0.5f;
        float diff = 1.0f + G.difficulty*0.5f + (G.ngplus?0.5f:0.0f);
        switch (type){
            case E_SLIME:      e->hp=4*diff;  e->radius=7; break;
            case E_MINI_SLIME: e->hp=2*diff;  e->radius=4; break;
            case E_BAT:        e->hp=2*diff;  e->radius=6; e->t1=rng_f(&crng)*6.28f; break;
            case E_WRAITH:     e->hp=5*diff;  e->radius=7; break;
            case E_CHASER:     e->hp=4*diff;  e->radius=6; e->t0=rng_f(&crng); break;
            case E_GOLEM:      e->hp=10*diff; e->radius=10; break;
            case E_TURRET:     e->hp=6*diff;  e->radius=7; e->phase=rng_i(&crng,3); break;
            case E_SENTINEL:   e->hp=12*diff; e->radius=9; break;
            case E_DRONE:      e->hp=4*diff;  e->radius=6; e->t1=rng_f(&crng)*6.28f; e->phase=rng_i(&crng,2); break;
            case E_BOMBER:     e->hp=3*diff;  e->radius=6; e->t1=rng_f(&crng)*6.28f; break;
            case E_SNIPER:     e->hp=5*diff;  e->radius=7; e->t0=rng_range(&crng,0.5f,1.5f); break;
            case E_SHIELDER:   e->hp=9*diff;  e->radius=8; break;
            case E_HIVE:       e->hp=8*diff;  e->radius=9; e->t0=2.0f; break;
            case E_ECHO_GHOST: e->hp=1e9f;    e->radius=7; break;
            default: e->hp=10*diff; e->radius=8; break;
        }
        e->maxhp=e->hp;
        return;
    }
}

void spawn_boss(int biome){
    Entity* e=&G.ents[0];
    memset(G.ents,0,sizeof(G.ents));
    e->active=true;
    e->type = E_BOSS_ROT + biome;
    e->pos = V2(G.room.w*TILE*0.72f, G.room.h*TILE*0.5f);
    float diff = (1.0f + G.difficulty*0.35f + (G.ngplus?0.6f:0.0f))*1.5f;
    switch (e->type){
        case E_BOSS_ROT:    e->hp=80*diff;  e->radius=20; break;
        case E_BOSS_ECHO:   e->hp=115*diff; e->radius=18; break;
        case E_BOSS_DEFRAG: e->hp=130*diff; e->radius=20; e->t3=4.0f; break;
        case E_BOSS_NULL:   e->hp=160*diff; e->radius=19; break;
    }
    e->maxhp=e->hp;
    G.boss_intro=true; G.boss_intro_t=2.2f;
    music_set(5);
    sfx_play(SFX_BOSS_ROAR);
    if (e->type==E_BOSS_NULL) G.light_mul=0.55f;
    if (e->type==E_BOSS_ROT) G.ambient_mul=0.5f;
}

static Bullet* spawn_bullet(bool from_player,int kind,v2 pos,v2 vel,float dmg,float life,float radius,int pierce){
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (b->active) continue;
        memset(b,0,sizeof(*b));
        b->active=true; b->from_player=from_player; b->kind=kind;
        b->pos=pos; b->vel=vel; b->dmg=dmg; b->life=life; b->radius=radius; b->pierce=pierce;
        b->last_hit=-1;
        b->attack_group=from_player?G.pl.attack_group:0;
        return b;
    }
    return NULL;
}

static void begin_glaive_return(Bullet* b, Player* p){
    if (b->returning) return;
    b->returning=true;
    b->last_hit=-1;
    b->rehit_t=0;
    float return_speed=weapon_defs[WPN_GLAIVE].speed*1.3f;
    if (b->from_player && player_has_wrelic(WR_GLAIVE_RETURN)){
        return_speed*=1.3f;
        b->dmg*=1.4f;
    }
    b->vel=v2scale(v2norm(v2sub(p->pos,b->pos)),return_speed);
}

static void spawn_glaive_burn_zone(v2 pos, uint32_t attack_group){
    Bullet* zone=spawn_bullet(true,11,pos,V2(0,0),player_attack_damage()*0.5f,3.0f,18.0f,0);
    if (!zone) return;
    zone->trail_t=1.0f;
    zone->attack_group=attack_group;
}

static int random_unowned_wrelic(int excluded){
    int choices[WR_COUNT], count=0;
    for (int i=0;i<WR_COUNT;i++)
        if (i!=excluded && !player_has_wrelic(i)) choices[count++]=i;
    return count>0? choices[rng_i(&crng,count)] : -1;
}
static int random_unowned_wrelic_for_weapon(int weapon, int excluded){
    int choices[WR_COUNT], count=0;
    for (int i=0;i<WR_COUNT;i++)
        if (weapon_relic_defs[i].weapon==weapon && i!=excluded && !player_has_wrelic(i)) choices[count++]=i;
    return count>0? choices[rng_i(&crng,count)] : -1;
}

// ----------------------------------------------------------- damage
static void enemy_finalize_death(Entity* e, int reason){
    if (!e->active) return;
    bool is_boss=e->type>=E_BOSS_ROT&&e->type<=E_BOSS_NULL;
    int type=e->type;
    v2 pos=e->pos;
    bool bonus=e->event_bonus!=0;
    uint8_t trait=e->event_trait;
    bool elite=e->elite;
    e->active=false;
    e->event_bonus=0;
    e->event_trait=ELITE_NONE;
    if (bonus) G.bytes_run+=2;
    if (!is_boss &&
        (trait==ELITE_VOLATILE || (reason!=DEATH_REASON_BOMBER && trait==ELITE_NONE && elite))){
        for(int k=0;k<6;k++){float a=k*1.0472f;
            spawn_bullet(false,6,pos,V2(cosf(a)*100.0f,sinf(a)*100.0f),1,2.6f,3.5f,0);}
        burst(pos,12,COL(0xFFF0D0),130,0.5f,2.4f,true);
    }
    if (reason==DEATH_REASON_BOMBER){
        for(int k=0;k<8;k++){float a=k*0.7854f;
            spawn_bullet(false,6,pos,V2(cosf(a)*120.0f,sinf(a)*120.0f),1,2.6f,3.5f,0);}
        burst(pos,18,COL(0xFF7A3D),150,0.5f,2.6f,true);
        G.shake=fmaxf(G.shake,4.0f); sfx_play(SFX_SHOOT);
        return;
    }
    G.kills++;
    G.hitstop=fmaxf(G.hitstop,is_boss?0.3f:0.09f);
    G.shake=fmaxf(G.shake,is_boss?6.0f:2.5f);
    burst(pos,is_boss?60:14,COL(0x7CFCE4),is_boss?220:140,0.7f,2.6f,true);
    burst(pos,is_boss?30:8,COL(0xFF3D7F),90,0.5f,2.0f,false);
    sfx_play(is_boss?SFX_BOSS_DIE:SFX_ENEMY_DIE);
    if (type==E_GOLEM){spawn_enemy(E_MINI_SLIME,v2add(pos,V2(-8,0)));spawn_enemy(E_MINI_SLIME,v2add(pos,V2(8,0)));}
    if (type==E_SLIME){spawn_enemy(E_MINI_SLIME,v2add(pos,V2(-7,-3)));spawn_enemy(E_MINI_SLIME,v2add(pos,V2(7,3)));}
    if (!is_boss && rng_i(&crng,3)==0) spawn_pickup(PK_BYTE,pos,(Weapon){0,0},0,0);
    if (is_boss){
        G.timescale=0.25f; G.light_mul=1.0f; G.ambient_mul=1.0f;
        int RW=G.room.w,RH=G.room.h;
        spawn_pickup(PK_CORE,V2(RW*TILE*0.5f,RH*TILE*0.5f),(Weapon){0,0},0,G.room.biome);
        int left_relic=random_unowned_wrelic_for_weapon(G.pl.weapon.type,-1);
        if (left_relic<0) left_relic=random_unowned_wrelic(-1);
        int right_relic=random_unowned_wrelic(left_relic);
        v2 cc=V2(RW*TILE*0.5f,RH*TILE*0.5f);
        if (left_relic>=0) spawn_pickup(PK_WRELIC,v2add(cc,V2(-40,30)),(Weapon){0,0},left_relic,0);
        if (right_relic>=0) spawn_pickup(PK_WRELIC,v2add(cc,V2(40,30)),(Weapon){0,0},right_relic,0);
        int exdir=opposite(G.room.entry_dir),ex0,ey0,ex1,ey1;
        if(exdir==DIR_R){ex0=ex1=RW-1;ey0=RH/2;ey1=RH/2+1;}
        else if(exdir==DIR_L){ex0=ex1=0;ey0=RH/2;ey1=RH/2+1;}
        else if(exdir==DIR_D){ey0=ey1=RH-1;ex0=RW/2;ex1=RW/2+1;}
        else {ey0=ey1=0;ex0=RW/2;ex1=RW/2+1;}
        G.room.tiles[ey0][ex0]=T_EXIT;G.room.tiles[ey1][ex1]=T_EXIT;G.room.cleared=true;
        for(int i=1;i<MAX_ENTITIES;i++)G.ents[i].active=false;
        for(int i=0;i<MAX_BULLETS;i++)if(!G.bullets[i].from_player)G.bullets[i].active=false;
        music_set(G.room.biome<3?G.room.biome+1:6);
    }
}
#ifdef DD_DEBUG
void dd_debug_clear_room_enemies(void){
    for (int pass=0;pass<MAX_ENTITIES;pass++){
        bool any=false;
        for (int i=0;i<MAX_ENTITIES;i++){
            Entity* e=&G.ents[i];
            if (!e->active || e->type==E_ECHO_GHOST) continue;
            e->hp=0;
            enemy_finalize_death(e,DEATH_REASON_DAMAGE);
            any=true;
        }
        if (!any) return;
    }
}
#endif
static void enemy_damage(Entity* e,float dmg,v2 from,float burn,float slow,bool crit,bool from_player,uint32_t attack_group){
    if(e->type==E_ECHO_GHOST)return;
    bool training_dummy=G.training_active && e==&G.ents[0];
    bool is_boss=e->type>=E_BOSS_ROT&&e->type<=E_BOSS_NULL;
    bool execute=from_player && !is_boss && !execution_burst &&
        G.pl.weapon.type==WPN_SWORD && player_has_wrelic(WR_SWORD_EXECUTE) &&
        e->hp<=e->maxhp*0.25f;
    if (execute) dmg=e->hp;
    if (from_player){
        float proximity=1.0f-v2len(v2sub(e->pos,G.pl.pos))/player_light_radius();
        dmg*=1.0f+clampf(proximity,0.0f,1.0f)*0.20f;
    }
    if(e->type==E_SENTINEL){v2 face=v2norm(e->vel),in=v2norm(v2sub(e->pos,from));if(face.x*in.x+face.y*in.y<-0.4f)dmg*=0.35f;}
    if(e->type==E_SHIELDER){v2 face=V2(cosf(e->face),sinf(e->face)),in=v2norm(v2sub(from,e->pos));
        if(face.x*in.x+face.y*in.y>0.64f){dmg*=0.25f;burst(e->pos,4,COL(0x9FFFF0),80,0.3f,1.8f,true);sfx_play(SFX_DENY);}}
    e->hp-=dmg;e->flash=0.12f;
    if (from_player){
        for (int i=0;i<MAX_ENTITIES;i++) if (e==&G.ents[i]){
            EnemyFeedback* feedback=&G.enemy_feedback[i];
            bool same_attack=feedback->t>0 && feedback->attack_group==attack_group;
            feedback->pos=e->pos;
            feedback->t=3.0f;
            feedback->hp=clampf(e->hp/e->maxhp,0.0f,1.0f);
            feedback->damage=same_attack?feedback->damage+dmg:dmg;
            feedback->radius=e->radius;
            feedback->attack_group=attack_group;
            feedback->hits=same_attack?feedback->hits+1:1;
            feedback->crit=same_attack?(feedback->crit||crit):crit;
            feedback->elite=e->elite;
            e->player_damage=feedback->damage;
            e->player_damage_crit=feedback->crit;
            break;
        }
        e->player_damaged=true;
        e->player_damage_t=0.9f;
    }
    if (training_dummy){
        e->hp=e->maxhp;
        for (int i=0;i<MAX_ENTITIES;i++) if (e==&G.ents[i]){
            G.enemy_feedback[i].hp=1.0f;
            break;
        }
        if (!from_player || G.pl.impact_group!=attack_group){
            G.pl.impact_group=attack_group;
            G.hitstop=fmaxf(G.hitstop,crit?0.07f:0.035f);
            G.shake=fmaxf(G.shake,crit?2.5f:1.2f);
            burst(e->pos,crit?10:5,COL(0xFF3D7F),110,0.4f,2.2f,true);
        }
        sfx_play(SFX_HIT);
        return;
    }
    if (burn > e->burn) e->burn = burn;
    if (slow > e->slow) e->slow = slow;
    v2 kb=v2norm(v2sub(e->pos,from));
    if(!is_boss)e->vel=v2add(e->vel,v2scale(kb,90.0f));
    if (!from_player || G.pl.impact_group!=attack_group){
        G.pl.impact_group=attack_group;
        G.hitstop=fmaxf(G.hitstop,crit?0.07f:0.035f);G.shake=fmaxf(G.shake,crit?2.5f:1.2f);
        burst(e->pos,crit?10:5,COL(0xFF3D7F),110,0.4f,2.2f,true);
    }
    sfx_play(SFX_HIT);
    if(e->hp<=0){
        v2 death_pos=e->pos;
        if (execute){
            G.pl.hp=fminf((float)G.pl.maxhp,G.pl.hp+0.25f);
            execution_burst=true;
            for (int i=0;i<MAX_ENTITIES;i++){
                Entity* other=&G.ents[i];
                if (!other->active || other==e || other->type==E_ECHO_GHOST) continue;
                if (v2len(v2sub(other->pos,death_pos))<40.0f)
                    enemy_damage(other,player_attack_damage()*0.5f,death_pos,burn,slow,false,true,attack_group);
            }
            execution_burst=false;
        }
        enemy_finalize_death(e,DEATH_REASON_DAMAGE);
        if (from_player && !phase_stepping && G.pl.weapon.type==WPN_SWORD &&
            player_has_wrelic(WR_SWORD_PHASE)){
            Entity* target=NULL;
            float best=180.0f;
            for (int i=0;i<MAX_ENTITIES;i++){
                Entity* other=&G.ents[i];
                if (!other->active || other->type==E_ECHO_GHOST) continue;
                float dist=v2len(v2sub(other->pos,death_pos));
                if (dist<best){ best=dist; target=other; }
            }
            if (target){
                G.pl.pos=target->pos;
                G.pl.vel=V2(0,0);
                G.pl.iframes=fmaxf(G.pl.iframes,0.25f);
                phase_stepping=true;
                enemy_damage(target,player_attack_damage()*2.0f,G.pl.pos,burn,slow,crit,true,attack_group);
                phase_stepping=false;
            }
        }
    }
}

#ifdef DD_DEBUG
extern int dbg_god;
#endif

static void player_take_damage_typed(v2 from,float damage,int source_type){
    Player* p=&G.pl;
    if (p->iframes>0 || p->dash_t>0) return;
#ifdef DD_DEBUG
    if (dbg_god){ p->iframes=0.3f; return; }
#endif
    player_sync_light_shield();
    float absorbed=fminf(p->shield,damage);
    p->shield-=absorbed;
    p->hp-=damage-absorbed;
    p->iframes = 1.0f;
    v2 kb=v2norm(v2sub(p->pos,from));
    p->vel = v2add(p->vel, v2scale(kb,180.0f));
    G.shake = fmaxf(G.shake,4.0f);
    G.hitstop = fmaxf(G.hitstop,0.08f);
    G.flash_white = 0.25f;
    burst(p->pos,12,COL(0x3FE0C5),130,0.5f,2.2f,true);
    sfx_play(SFX_HURT);
    if (p->hp<=0){
        if (p->relics[RELIC_BACKUP]){
            p->relics[RELIC_BACKUP]=false;
            p->hp = (float)(p->maxhp/2+1);
            set_msg("백업 비트가 소모되어 복원되었다.");
            sfx_play(SFX_HEAL);
            burst(p->pos,30,COL(0x9FFFF0),180,0.8f,3.0f,true);
            p->iframes=2.0f;
            return;
        }
        G.death_source_type=source_type;
        // 데이터 손상 — 사망
        settle_run_once(SETTLE_DEATH);
    }
}

void player_take_damage_amount(v2 from,float damage){
    player_take_damage_typed(from,damage,-1);
}

void player_take_damage(v2 from){
    player_take_damage_amount(from,1.0f);
}

// ----------------------------------------------------------- weapons
static bool path_blocked_by_wall(v2 from, v2 to){
    v2 d=v2sub(to,from);
    int steps=(int)ceilf(fmaxf(fabsf(d.x),fabsf(d.y))/4.0f);
    for (int i=1;i<steps;i++){
        v2 p=v2add(from,v2scale(d,(float)i/(float)steps));
        if (tile_solid((int)(p.x/TILE),(int)(p.y/TILE))) return true;
    }
    return false;
}

static v2 lance_endpoint(v2 from,v2 dir,float reach){
    v2 end=from;
    for (float d=4.0f;d<=reach;d+=4.0f){
        v2 next=v2add(from,v2scale(dir,d));
        if (tile_solid((int)(next.x/TILE),(int)(next.y/TILE))) break;
        end=next;
    }
    return end;
}

static void lance_explode(Bullet* b){
    for (int j=0;j<MAX_ENTITIES;j++){
        Entity* e=&G.ents[j];
        if (!e->active||e->type==E_ECHO_GHOST) continue;
        if (v2len(v2sub(e->pos,b->pos))<32.0f)
            enemy_damage(e,b->dmg*0.4f,b->pos,b->burn,b->slow,false,true,b->attack_group);
    }
    burst(b->pos,16,COL(0xFF7A3D),150,0.5f,2.6f,true);
    G.shake=fmaxf(G.shake,3.0f); sfx_play(SFX_SHOOT);
}

static void spawn_lance_pin(Bullet* source,int target){
    Bullet* pin=spawn_bullet(true,12,source->pos,V2(0,0),source->dmg*0.4f,0.5f,40.0f,0);
    if (!pin) return;
    pin->last_hit=target;
    pin->attack_group=source->attack_group;
    pin->burn=source->burn; pin->slow=source->slow;
}

static void trigger_lance_pin(Bullet* pin){
    for (int j=0;j<MAX_ENTITIES;j++){
        Entity* e=&G.ents[j];
        if (!e->active||e->type==E_ECHO_GHOST||j==pin->last_hit) continue;
        if (v2len(v2sub(e->pos,pin->pos))>=pin->radius) continue;
        e->pos=pin->pos; e->vel=V2(0,0);
        enemy_damage(e,pin->dmg,pin->pos,pin->burn,pin->slow,false,true,pin->attack_group);
    }
    burst(pin->pos,12,COL(0x7CFCE4),110,0.4f,2.2f,true);
}

static void fire_weapon(float dt){
    Player* p=&G.pl;
    const WeaponDef* wd=&weapon_defs[p->weapon.type];
    float cd_mul = p->relics[RELIC_OVERCLOCK]?0.7f:1.0f;
    if (p->weapon.type==WPN_SWORD && player_has_wrelic(WR_SWORD_WAVE)) cd_mul*=1.3f; // 검기: 공속 -30%
    const float kinship_cd_mul =
        1.0f-0.04f*fminf((float)G.memory.kept[MEM_TAG_KINSHIP],2.0f);
    float dmg = player_attack_damage();
    float burn = p->weapon.prefix==PFX_HOT? 3.0f:0.0f;
    float slow = p->weapon.prefix==PFX_COLD? 2.0f:0.0f;
    bool crit = p->weapon.prefix==PFX_BROKEN && rng_i(&crng,10)<3;
    if (crit) dmg*=2.0f;

    // 비트 캐논: 차징
    if (p->weapon.type==WPN_CANNON){
        if (attack_held){
            p->charging=true;
            p->charge = clampf(p->charge+dt/0.55f,0,1);
            return;
        }
        if (p->charging){
            p->charging=false;
            if (p->attack_cd<=0){
                p->attack_group++;
                float mul = 1.0f+p->charge*2.0f;
                bool rail = player_has_wrelic(WR_CANNON_RAIL) && p->charge>0.85f; // 완충 시 관통 레일
                float bspeed = rail? wd->speed*1.9f : wd->speed;
                float blife  = rail? 1.6f : 1.2f;
                float brad   = rail? 3.5f : 4.0f+p->charge*4.0f;
                int   bpc    = rail? 999 : 3;
                bool recoil = player_has_wrelic(WR_CANNON_RECOIL);
                if (recoil){ mul*=1.5f; brad*=1.4f; }
                Bullet* b=spawn_bullet(true,1,p->pos,v2scale(p->aim,bspeed),dmg*mul,blife,brad,bpc);
                if (b){ b->burn=burn; b->slow=slow; b->crit=crit; b->delayed_fuse=player_has_wrelic(WR_CANNON_FUSE); }
                if (recoil) p->vel=v2add(p->vel,v2scale(p->aim,-130.0f));
                p->attack_cd = wd->cooldown*cd_mul*kinship_cd_mul;
                G.shake=fmaxf(G.shake,1.0f+p->charge*2.0f);
                sfx_play(SFX_SHOOT);
            }
            p->charge=0;
        }
        return;
    }
    if (!attack_held || p->attack_cd>0) return;
    p->attack_cd = wd->cooldown*cd_mul*kinship_cd_mul;
    p->attack_group++;

    switch (p->weapon.type){
    case WPN_SWORD: {
        slash_t = 0.14f; slash_dir = p->aim;
        sfx_play(SFX_SHOOT);
        bool whirl = player_has_wrelic(WR_SWORD_WHIRL); // 전방향 베기
        float reach = whirl? 40.0f : 34.0f;
        for (int i=0;i<MAX_ENTITIES;i++){
            Entity* e=&G.ents[i];
            if (!e->active) continue;
            v2 d=v2sub(e->pos,p->pos);
            float dist=v2len(d);
            if (e->spawn_t>0) e->spawn_t=0; // 맞으면 즉시 등장 완료 취급
            if (dist<reach+e->radius){
                v2 nd=v2norm(d);
                if ((whirl || nd.x*p->aim.x+nd.y*p->aim.y>0.35f) &&
                    !path_blocked_by_wall(p->pos,e->pos))
                    enemy_damage(e,dmg,p->pos,burn,slow,crit,true,p->attack_group);
            }
        }
        if (whirl) for (int i=0;i<MAX_BULLETS;i++){
            Bullet* b=&G.bullets[i];
            if (!b->active||b->from_player) continue;
            if (v2len(v2sub(b->pos,p->pos))<38.0f){
                b->active=false; burst(b->pos,3,COL(0x9FFFF0),60,0.3f,1.5f,true);
            }
        }
        // 검기: 관통하며 사거리 끝까지 날아가는 칼날 (벽에 닿으면 소멸)
        if (player_has_wrelic(WR_SWORD_WAVE)){
            Bullet* b=spawn_bullet(true,8,p->pos,v2scale(p->aim,360.0f),dmg,1.25f,7.0f,999);
            if (b){ b->burn=burn; b->slow=slow; b->crit=crit; }
        }
    } break;
    case WPN_SPRAY: {
        sfx_play(SFX_SHOOT);
        float base=atan2f(p->aim.y,p->aim.x);
        bool wide = player_has_wrelic(WR_SPRAY_WIDE);
        bool rico = player_has_wrelic(WR_SPRAY_RICO);
        bool pierce = player_has_wrelic(WR_SPRAY_PIERCE);
        bool choke = player_has_wrelic(WR_SPRAY_CHOKE);
        int pellets = wide? 8:(pierce?4:5);
        float spread = choke?0.055f:(wide?0.13f:0.16f);
        float plife  = choke?0.36f:(wide?0.20f:0.26f);
        float pellet_dmg=dmg*(choke?1.35f:1.0f);
        for (int i=0;i<pellets;i++){
            float a=base+(i-(pellets-1)*0.5f)*spread+rng_range(&crng,-0.05f,0.05f);
            Bullet* b=spawn_bullet(true,2,p->pos,V2(cosf(a)*wd->speed,sinf(a)*wd->speed),pellet_dmg,plife,3.0f,pierce?1:0);
            if (b){ b->burn=burn;b->slow=slow;b->crit=crit; if(rico){ b->bounces=1; b->life+=0.18f; } }
        }
        p->vel=v2add(p->vel,v2scale(p->aim,-40.0f)); // 반동
    } break;
    case WPN_GLAIVE: {
        if (p->glaive_out){ p->attack_cd=0; break; }
        int per_direction=player_has_wrelic(WR_GLAIVE_ORBIT)?2:1;
        int directions=player_has_wrelic(WR_GLAIVE_TWIN)?2:1;
        bool launched=false;
        for (int direction=0;direction<directions;direction++){
            v2 aim=direction==0?p->aim:v2scale(p->aim,-1.0f);
            for (int n=0;n<per_direction;n++){
                float angle=per_direction==2?(n==0?-0.1745329f:0.1745329f):0.0f;
                float cs=cosf(angle), sn=sinf(angle);
                v2 shot=V2(aim.x*cs-aim.y*sn,aim.x*sn+aim.y*cs);
                Bullet* b=spawn_bullet(true,3,p->pos,v2scale(shot,wd->speed),dmg,3.0f,7.0f,999);
                if (!b) continue;
                b->burn=burn; b->slow=slow; b->crit=crit;
                launched=true;
            }
        }
        if (launched){ p->glaive_out=true; sfx_play(SFX_SHOOT); }
    } break;
    case WPN_LANCE: {
        sfx_play(SFX_SHOOT);
        G.shake=fmaxf(G.shake,1.5f);
        if (player_has_wrelic(WR_LANCE_CHARGE)){
            const float reach=68.0f, half_width=3.0f;
            v2 end=lance_endpoint(p->pos,p->aim,reach);
            float actual_reach=v2len(v2sub(end,p->pos));
            int targets[MAX_ENTITIES], count=0;
            for (int i=0;i<MAX_ENTITIES;i++){
                Entity* e=&G.ents[i];
                if (!e->active||e->type==E_ECHO_GHOST) continue;
                v2 offset=v2sub(e->pos,p->pos);
                float forward=offset.x*p->aim.x+offset.y*p->aim.y;
                float side=fabsf(offset.x*p->aim.y-offset.y*p->aim.x);
                if (forward>=0&&forward<=actual_reach+e->radius&&side<=half_width+e->radius)
                    targets[count++]=i;
            }
            float thrust_dmg=dmg;
            if (player_has_wrelic(WR_LANCE_PIERCE)) thrust_dmg*=1.0f+0.2f*(float)count;
            lance_thrust_t=0.16f; lance_thrust_dir=p->aim; lance_thrust_reach=actual_reach;
            for (int n=0;n<count;n++){
                Entity* e=&G.ents[targets[n]];
                Bullet strike=(Bullet){.active=true,.from_player=true,.kind=4,.pos=e->pos,
                    .dmg=thrust_dmg,.radius=half_width,.burn=burn,.slow=slow,.crit=crit,
                    .attack_group=p->attack_group};
                enemy_damage(e,thrust_dmg,p->pos,burn,slow,crit,true,p->attack_group);
                if (player_has_wrelic(WR_LANCE_PIN)){
                    e->root=fmaxf(e->root,0.5f); e->vel=V2(0,0);
                    spawn_lance_pin(&strike,targets[n]);
                }
            }
            if (player_has_wrelic(WR_LANCE_BLAST)){
                Bullet strike=(Bullet){.active=true,.from_player=true,.kind=4,.pos=end,
                    .dmg=thrust_dmg,.radius=half_width,.burn=burn,.slow=slow,.crit=crit,
                    .attack_group=p->attack_group};
                lance_explode(&strike);
            }
            p->vel=v2add(p->vel,v2scale(p->aim,750.0f));
            p->iframes=fmaxf(p->iframes,0.5f);
            burst(p->pos,6,COL(0x3FE0C5),90,0.3f,2,true);
        } else {
            Bullet* b=spawn_bullet(true,4,p->pos,v2scale(p->aim,wd->speed),dmg,1.1f,5.0f,999);
            if (b){ b->burn=burn;b->slow=slow;b->crit=crit; }
            p->vel=v2add(p->vel,v2scale(p->aim,-60.0f));
        }
    } break;
    case WPN_WAND: {
        sfx_play(SFX_SHOOT);
        int n = player_has_wrelic(WR_WAND_FORK)? 4:2;
        float base=atan2f(p->aim.y,p->aim.x);
        for (int i=0;i<n;i++){
            float a=base+(i-(n-1)*0.5f)*0.25f;
            Bullet* b=spawn_bullet(true,5,p->pos,V2(cosf(a)*wd->speed,sinf(a)*wd->speed),dmg,1.6f,3.5f,0);
            if (b){ b->burn=burn;b->slow=slow;b->crit=crit; }
        }
    } break;
    default: break;
    }
}

// ----------------------------------------------------------- enemy AI
static v2 history_at(float seconds_ago){
    int n=(int)(seconds_ago*60.0f);
    if (n>255) n=255;
    return G.history[(G.hist_head-n)&255];
}

static void update_enemy(Entity* e,float real_dt,float ai_dt){
    Player* p=&G.pl;
    if (e->spawn_t>0){ e->spawn_t-=real_dt; return; }
    float slow_mul = e->slow>0? 0.5f:1.0f;
    if (e->slow>0) e->slow-=real_dt;
    if (e->burn>0){
        e->burn-=real_dt;
        e->hp -= 0.5f*real_dt*3.0f;
        if (rng_i(&crng,8)==0) spawn_particle(e->pos,V2(rng_range(&crng,-20,20),-40),0.4f,2,COL(0xFF7A3D),true,2,0);
        if (e->hp<=0){ enemy_finalize_death(e,DEATH_REASON_BURN); return; }
    }
    if (e->root>0){
        e->root-=real_dt;
        e->vel=V2(0,0);
        return;
    }
    v2 to_p = v2sub(p->pos,e->pos);
    float dist = v2len(to_p);
    v2 dir = v2norm(to_p);

    switch (e->type){
    case E_SLIME: case E_MINI_SLIME: {
        e->t0-=ai_dt;
        if (e->t0<=0){
            e->phase++;
            // 큰 슬라임은 3번째 도약마다 플레이어 진행방향을 예측한 강한 돌진
            bool leap = e->type==E_SLIME && (e->phase%3==0) && dist<170.0f;
            float pow = e->type==E_SLIME? (leap?280.0f:150.0f) : 200.0f;
            v2 aimd = leap? v2norm(v2sub(v2add(p->pos,v2scale(p->vel,0.3f)),e->pos)) : dir;
            e->vel = v2scale(aimd,pow*slow_mul);
            if (leap) burst(e->pos,5,COL(0xFF3D7F),70,0.3f,1.8f,true);
            e->t0 = e->type==E_SLIME? (leap?1.1f:rng_range(&crng,0.7f,1.1f)) : rng_range(&crng,0.45f,0.8f);
        }
        e->vel = v2scale(e->vel, 1.0f-4.0f*ai_dt);
        e->pos = resolve_collision(e->pos,e->vel,e->radius,ai_dt);
    } break;
    case E_BAT: {
        // 주기적 급강하: 3초마다 0.5초간 플레이어를 향해 3배속 직진
        float bat_dt = e->state==0 ? ai_dt : real_dt;
        e->t0 -= bat_dt;
        if (e->state==0){
            e->t1 += ai_dt*4.0f;
            v2 v = v2add(v2scale(dir,55.0f), V2(-dir.y,dir.x));
            v = v2add(v, v2scale(V2(-dir.y,dir.x), sinf(e->t1)*45.0f));
            e->vel = v2scale(v,slow_mul);
            if (e->t0<=0){ e->state=1; e->t0=0.4375f;
                           v2 lead=v2norm(v2sub(v2add(p->pos,v2scale(p->vel,0.25f)),e->pos));
                           e->vel=v2scale(lead,164.0f*slow_mul);
                           burst(e->pos,4,COL(0xFF3D7F),60,0.3f,1.5f,true); }
        } else {
            if (e->t0<=0){ e->state=0; e->t0=rng_range(&crng,2.3f,3.1f); }
        }
        e->pos = resolve_collision(e->pos,e->vel,e->radius,bat_dt);
    } break;
    case E_BOMBER: {
        // 배드비트: 추격(살짝 사인 흔들림) → 28px 내 도화선 → 폭발
        if (e->state==0){
            e->t1 += ai_dt*5.0f;
            v2 perp=V2(-dir.y,dir.x);
            e->vel = v2add(v2scale(dir,70.0f), v2scale(perp,sinf(e->t1)*22.0f));
            e->vel = v2scale(e->vel,slow_mul);
            e->pos = resolve_collision(e->pos,e->vel,e->radius,ai_dt);
            if (dist<28.0f){ e->state=1; e->t0=0.7f; e->vel=V2(0,0); }
        } else {
            e->t0 -= real_dt; // 도화선 (정지·점멸)
            if (rng_i(&crng,3)==0) spawn_particle(e->pos,V2(rng_range(&crng,-30,30),-40),0.3f,2,COL(0xFF7A3D),true,2,0);
            if (e->t0<=0){
                enemy_finalize_death(e,DEATH_REASON_BOMBER); return;
            }
        }
    } break;
    case E_SNIPER: {
        // 포인터: 정지 저격. 대기 → 조준(텔레그래프) → 1발
        e->t0 -= (e->state==0 ? ai_dt : real_dt);
        if (e->state==0){
            if (e->t0<=0 && dist<340.0f){
                // 대략적 시야: 선 위 10점 샘플
                bool los=true;
                for (int k=1;k<10;k++){ float f=k/10.0f;
                    v2 sp=v2add(e->pos,v2scale(to_p,f));
                    if (tile_solid((int)(sp.x/TILE),(int)(sp.y/TILE))){ los=false; break; } }
                if (los){ e->state=1; e->t0=0.9f; e->target=p->pos; }
                else e->t0=0.4f;
            }
        } else {
            if (e->t0>0.25f) e->target=p->pos; // 발사 0.25초 전 위치 고정
            if (e->t0<=0){
                v2 d=v2norm(v2sub(e->target,e->pos));
                spawn_bullet(false,1,e->pos,v2scale(d,562.5f),2.0f,2.0f,3.0f,0);
                sfx_play(SFX_SHOOT);
                e->state=0; e->t0=rng_range(&crng,1.8f,2.4f);
            }
        }
    } break;
    case E_SHIELDER: {
        // 패리티: 느린 추격 + 항상 플레이어를 바라봄
        e->face = atan2f(dir.y,dir.x);
        e->vel = v2scale(dir,38.0f*slow_mul);
        e->pos = resolve_collision(e->pos,e->vel,e->radius,ai_dt);
    } break;
    case E_HIVE: {
        // 둥지: 공격 안 함. ~3.2초마다 미니슬라임 1마리 (활성 4 미만)
        e->t0 -= real_dt;
        if (e->t0<=0){
            int minis=0;
            for (int i=0;i<MAX_ENTITIES;i++) if (G.ents[i].active&&G.ents[i].type==E_MINI_SLIME) minis++;
            if (minis<4){
                spawn_enemy(E_MINI_SLIME, v2add(e->pos,V2(rng_range(&crng,-12,12),rng_range(&crng,-12,12))));
                burst(e->pos,6,COL(0x3FE0C5),90,0.4f,2.0f,true);
            }
            e->t0=3.2f;
        }
    } break;
    case E_WRAITH: {
        v2 tgt = history_at(1.5f);
        v2 d = v2norm(v2sub(tgt,e->pos));
        e->vel = v2scale(d,62.0f*slow_mul);
        e->pos = v2add(e->pos,v2scale(e->vel,ai_dt)); // 벽 통과
        e->pos.x=clampf(e->pos.x,TILE,G.room.w*TILE-TILE);
        e->pos.y=clampf(e->pos.y,TILE,G.room.h*TILE-TILE);
    } break;
    case E_CHASER: {
        float phase_dt = e->state==0 ? ai_dt : real_dt;
        e->t0 -= (e->state==0 ? ai_dt : real_dt);
        if (e->state==0){
            e->vel=v2scale(e->vel,1.0f-6.0f*ai_dt);
            if (e->t0<=0){
                e->state=1; e->t0=0.39375f;
                v2 tgt=history_at(1.0f);
                e->vel=v2scale(v2norm(v2sub(tgt,e->pos)),264.0f*slow_mul);
                burst(e->pos,4,COL(0xFF3D7F),60,0.3f,1.5f,true);
            }
        } else {
            if (rng_i(&crng,3)==0) spawn_particle(e->pos,V2(0,0),0.3f,2.5f,COL(0xFF3D7F),true,1,0);
            if (e->t0<=0){ e->state=0; e->t0=rng_range(&crng,1.3f,2.0f); }
        }
        e->pos = resolve_collision(e->pos,e->vel,e->radius,phase_dt);
    } break;
    case E_GOLEM: {
        // 느린 추격 + 60px 내 텔레그래프(0.5s) → 슬램 + 자기 경직(0.8s)
        if (e->state==0){
            e->vel = v2scale(dir,32.0f*slow_mul);
            e->pos = resolve_collision(e->pos,e->vel,e->radius,ai_dt);
            if (dist<60.0f){ e->state=1; e->t0=0.5f; }
        } else if (e->state==1){
            e->t0-=real_dt;
            if (rng_i(&crng,2)==0) spawn_particle(e->pos,V2(rng_range(&crng,-20,20),-30),0.3f,2.5f,COL(0xFF3D7F),true,1,0);
            if (e->t0<=0){
                for (int k=0;k<6;k++){ float a=k*1.0472f;
                    spawn_bullet(false,6,e->pos,V2(cosf(a)*90.0f,sinf(a)*90.0f),1,2.8f,3.5f,0); }
                burst(e->pos,10,COL(0x9FFFF0),120,0.5f,2.4f,true);
                G.shake=fmaxf(G.shake,3.0f); sfx_play(SFX_SHOOT);
                e->state=2; e->t0=0.8f;
            }
        } else { // 경직
            e->t0-=real_dt;
            if (e->t0<=0) e->state=0;
        }
    } break;
    case E_TURRET: {
        // 고정 포탑: phase로 3가지 발사 모드
        e->t0 -= ai_dt;
        if (e->t0<=0 && dist<300.0f){
            if (e->phase==1){ // 회전 나선: 0.22초마다 단발, 각도 누적
                e->t2 += 0.5f;
                spawn_bullet(false,6,e->pos,V2(cosf(e->t2)*150.0f,sinf(e->t2)*150.0f),1,2.8f,3.5f,0);
                e->t0=0.22f;
            } else if (e->phase==2){ // 8방향 링, 2.6초마다
                for (int k=0;k<8;k++){ float a=k*0.7854f;
                    spawn_bullet(false,6,e->pos,V2(cosf(a)*150.0f,sinf(a)*150.0f),1,2.8f,3.5f,0); }
                e->t0=2.6f;
            } else { // 기존 4연사
                e->state++;
                spawn_bullet(false,6,e->pos,v2scale(dir,160.0f),1,2.8f,3.5f,0);
                e->t0 = (e->state%4==0)? 1.6f:0.18f;
            }
            sfx_play(SFX_SHOOT);
        }
    } break;
    case E_SENTINEL: {
        float phase_dt = e->state==0 ? ai_dt : real_dt;
        e->t0 -= (e->state==0 ? ai_dt : real_dt);
        if (e->state==0){
            e->vel = v2scale(dir,45.0f*slow_mul);
            if (e->t0<=0 && dist<120.0f){ e->state=1; e->t0=0.525f; e->target=p->pos; }
        } else {
            v2 d=v2norm(v2sub(e->target,e->pos));
            e->vel = v2scale(d,192.0f*slow_mul);
            if (e->t0<=0){ e->state=0; e->t0=3.0f; }
        }
        e->pos = resolve_collision(e->pos,e->vel,e->radius,phase_dt);
    } break;
    case E_DRONE: {
        // 고정 원거리: 제자리 부유 + 부채꼴 사격 (phase로 3갈래/5갈래)
        e->t1 += real_dt*2.0f;
        e->t0 -= ai_dt;
        if (e->t0<=0 && dist<320.0f){
            float base=atan2f(dir.y,dir.x);
            if (e->phase==1){ // 5갈래 넓은 부채꼴, 느린 탄, 긴 쿨다운
                for (int k=-2;k<=2;k++){ float a2=base+k*0.28f;
                    spawn_bullet(false,6,e->pos,V2(cosf(a2)*100.0f,sinf(a2)*100.0f),1,3.2f,3.5f,0); }
                e->t0 = rng_range(&crng,2.0f,2.6f);
            } else { // 기존 3갈래
                for (int k=-1;k<=1;k++){ float a2=base+k*0.22f;
                    spawn_bullet(false,6,e->pos,V2(cosf(a2)*140.0f,sinf(a2)*140.0f),1,3.0f,3.5f,0); }
                e->t0 = rng_range(&crng,1.4f,2.0f);
            }
            sfx_play(SFX_SHOOT);
        }
    } break;
    case E_ECHO_GHOST: {
        e->pos = history_at(e->t2); // t2 = 지연 시간
    } break;
    }

    // 접촉 피해 (배드비트는 도화선 중 폭발이 위협이므로 접촉 끔)
    bool fuse = (e->type==E_BOMBER && e->state==1);
    if (!fuse && dist < e->radius+5.0f) player_take_damage_typed(e->pos,0.5f,e->type);
}

// ----------------------------------------------------------- bosses
static float boss_damage(float base){
    return base+(G.difficulty==2?0.5f:0.0f);
}

static float boss_barrage_cooldown(float hard_interval){
    return hard_interval+(2-G.difficulty)*0.625f;
}

static void boss_radial(Entity* e,int n,float speed,float offset){
    for (int i=0;i<n;i++){
        float a = offset + i*6.2832f/n;
        spawn_bullet(false,7,e->pos,V2(cosf(a)*speed,sinf(a)*speed),1,3.5f,4.0f,0);
    }
    sfx_play(SFX_SHOOT);
}
// 조준 산탄: dir 방향 중심으로 n발 부채꼴
static void boss_spread(Entity* e, v2 d, int n, float arc, float speed){
    float base=atan2f(d.y,d.x);
    for (int i=0;i<n;i++){
        float a=base+(i-(n-1)*0.5f)*arc;
        spawn_bullet(false,7,e->pos,V2(cosf(a)*speed,sinf(a)*speed),1,3.5f,4.0f,0);
    }
    sfx_play(SFX_SHOOT);
}
// 틈 있는 링: gap_ang 방향에 안전 지대를 비운 원형 탄막
static void boss_ring_gap(Entity* e, int n, float speed, float gap_ang){
    for (int i=0;i<n;i++){
        float a=i*6.2832f/n;
        float dd=fabsf(fmodf(a-gap_ang+9.4248f,6.2832f)-3.1416f);
        if (3.1416f-dd<0.9f) continue; // 플레이어 쪽 틈
        spawn_bullet(false,7,e->pos,V2(cosf(a)*speed,sinf(a)*speed),1,3.5f,4.0f,0);
    }
    sfx_play(SFX_SHOOT);
}

// 범위지정 위험구역 추가/갱신 (kind: 0 원형 1 가로줄 2 세로줄)
static void add_zone(v2 pos, float r, float warn, int kind){
    for (int i=0;i<MAX_ZONES;i++){
        if (!G.zones[i].active){
            G.zones[i]=(AoeZone){true,kind,pos,r,warn,warn};
            return;
        }
    }
}
static void update_zones(float dt){
    Player* p=&G.pl;
    for (int i=0;i<MAX_ZONES;i++){
        AoeZone* z=&G.zones[i];
        if (!z->active) continue;
        z->t-=dt;
        if (z->t<=0){ // 폭발
            bool hit;
            if (z->kind==1) hit = fabsf(p->pos.y-z->pos.y)<z->r;       // 가로줄
            else if (z->kind==2) hit = fabsf(p->pos.x-z->pos.x)<z->r;  // 세로줄
            else hit = v2len(v2sub(p->pos,z->pos))<z->r;               // 원형
            if (hit) player_take_damage_amount(z->pos,boss_damage(1.0f));
            if (z->kind==1) for (int k=0;k<14;k++) burst(V2(rng_range(&crng,0,G.room.w*TILE),z->pos.y),2,COL(0xFF3D7F),110,0.45f,2.2f,true);
            else if (z->kind==2) for (int k=0;k<14;k++) burst(V2(z->pos.x,rng_range(&crng,0,G.room.h*TILE)),2,COL(0xFF3D7F),110,0.45f,2.2f,true);
            else burst(z->pos,14,COL(0xFF3D7F),130,0.5f,2.5f,true);
            G.shake=fmaxf(G.shake,2.5f);
            sfx_play(SFX_HIT);
            z->active=false;
        }
    }
}

// 짧은 벽 토막 배치 (플레이어/보스 근처는 비움)
static void place_wall_seg(float cx, float cy, bool horizontal, int half, Entity* e, Player* p){
    int tcx=(int)(cx/TILE), tcy=(int)(cy/TILE);
    for (int k=-half;k<=half;k++){
        int x = horizontal? tcx+k : tcx;
        int y = horizontal? tcy : tcy+k;
        if (x<1||y<1||x>=G.room.w-1||y>=G.room.h-1) continue;
        v2 tp=V2(x*TILE+8.0f,y*TILE+8.0f);
        if (v2len(v2sub(tp,p->pos))<TILE*1.6f) continue;
        if (v2len(v2sub(tp,e->pos))<TILE*1.6f) continue;
        G.room.tiles[y][x]=T_WALL;
        burst(tp,5,COL(0x6FBFB0),70,0.4f,2,false);
    }
}
// DEFRAG 벽 배리어: 기존 내부 벽 지우고 플레이어/보스 앞을 가로막는 짧은 벽 생성
static void defrag_barrier(Entity* e, Player* p){
    for (int y=2;y<G.room.h-2;y++) for (int x=2;x<G.room.w-2;x++) // 보스방은 본래 개방형 → 안전
        if (G.room.tiles[y][x]==T_WALL) G.room.tiles[y][x]=T_FLOOR;
    v2 bp_dir = v2norm(v2sub(p->pos,e->pos));          // 보스→플레이어
    v2 mid = v2add(e->pos, v2scale(bp_dir, TILE*3.0f)); // 보스 앞
    bool h1 = fabsf(bp_dir.x) < fabsf(bp_dir.y);        // 진행축에 수직으로 깔기
    place_wall_seg(mid.x, mid.y, h1, 2, e, p);
    v2 fwd = (p->aim.x==0&&p->aim.y==0)? bp_dir : p->aim; // 플레이어 진행/조준 앞
    v2 fp = v2add(p->pos, v2scale(fwd, TILE*2.5f));
    bool h2 = fabsf(fwd.x) < fabsf(fwd.y);
    place_wall_seg(fp.x, fp.y, h2, 1, e, p);
    G.shake=fmaxf(G.shake,3.0f);
    sfx_play(SFX_DOOR);
}

static void update_boss(Entity* e,float dt){
    Player* p=&G.pl;
    if (G.boss_intro) return;
    v2 dir = v2norm(v2sub(p->pos,e->pos));
    float hp_frac = e->hp/e->maxhp;
    e->t0 -= dt*1.25f;

    switch (e->type){
    case E_BOSS_ROT: {
        float spd = hp_frac<0.33f? 1.35f:1.0f;
        switch (e->state){
        case 0: // 추격 허브 — 돌진/원거리/소환을 섞어 고른다
            e->vel = v2scale(dir,46.0f*spd);
            e->pos = resolve_collision(e->pos,e->vel,e->radius,dt);
            if (e->t0<=0){
                int pick;
                // 직전이 돌진(phase==1)이었으면 반드시 원거리/소환으로 — 돌진 연타 방지
                if (e->phase==1) pick = rng_i(&crng,3)<2?2:3;
                else { int r=rng_i(&crng,12); pick = r<4?1:(r<7?2:(r<9?3:4)); }
                e->state=pick; e->phase=pick; e->t1=0;
                e->t0 = pick==1? 0.45f:0.0f; // 돌진은 텔레그래프 후 발사
            }
            break;
        case 1: // 돌진: 텔레그래프 → 단발 돌진 → 끝에 포자 → 쿨 1초
            if (e->t1==0){
                e->vel = v2scale(e->vel,1.0f-6.0f*dt);
                e->pos = resolve_collision(e->pos,e->vel,e->radius,dt);
                if (rng_i(&crng,2)==0) spawn_particle(e->pos,V2(rng_range(&crng,-20,20),-30),0.3f,2.5f,COL(0xFF3D7F),true,1,0);
                if (e->t0<=0){
                    v2 d=v2norm(v2sub(p->pos,e->pos));
                    e->vel=v2scale(d,300.0f*spd);
                    sfx_play(SFX_DASH);
                    e->t1=1; e->t0=0.55f;
                }
            } else {
                e->pos = resolve_collision(e->pos,e->vel,e->radius,dt);
                e->vel = v2scale(e->vel,1.0f-2.0f*dt);
                if (e->t0<=0){
                    boss_radial(e,12,110.0f,rng_f(&crng)*6.28f);
                    e->state=0; e->t0=boss_barrage_cooldown(0.75f); e->t1=0;
                }
            }
            break;
        case 2: // 포자 방사 (원거리)
            if (e->t0<=0){
                boss_radial(e,20,120.0f,rng_f(&crng)*6.28f);
                e->t1++;
                e->t0=boss_barrage_cooldown(0.52f/spd);
                if (e->t1>=4){ e->state=0; e->t0=boss_barrage_cooldown(1.0f); e->t1=0; }
            }
            break;
        case 3: { // 슬라임 소환 + 견제탄
            int slimes=0;
            for (int i=1;i<MAX_ENTITIES;i++) if (G.ents[i].active&&(G.ents[i].type==E_SLIME||G.ents[i].type==E_MINI_SLIME)) slimes++;
            if (slimes<5){
                spawn_enemy(E_SLIME,v2add(e->pos,V2(rng_range(&crng,-30,30),rng_range(&crng,-30,30))));
                burst(e->pos,8,COL(0xFF3D7F),100,0.5f,2,true);
            }
            boss_radial(e,14,105.0f,rng_f(&crng)*6.28f);
            e->state=0; e->t0=boss_barrage_cooldown(1.15f);
        } break;
        case 4:
            if (e->t0<=0){
                boss_ring_gap(e,22,125.0f,atan2f(dir.y,dir.x));
                e->t1++;
                e->t0=boss_barrage_cooldown(0.46f/spd);
                if (e->t1>=3){ e->state=0; e->t0=boss_barrage_cooldown(0.9f); e->t1=0; }
            }
            break;
        }
    } break;
    case E_BOSS_ECHO: {
        // 메아리 유령 유지 (1.5초 지연 잔상)
        int ghosts=0;
        for (int i=1;i<MAX_ENTITIES;i++) if (G.ents[i].active&&G.ents[i].type==E_ECHO_GHOST) ghosts++;
        int want = hp_frac<0.35f? 3 : (hp_frac<0.7f? 2:1);
        if (ghosts<want){
            spawn_enemy(E_ECHO_GHOST,history_at(1.5f));
            for (int i=1;i<MAX_ENTITIES;i++)
                if (G.ents[i].active&&G.ents[i].type==E_ECHO_GHOST&&G.ents[i].t2==0)
                    G.ents[i].t2 = ghosts==0?1.5f:3.0f;
        }
        switch (e->state){
        case 0: // 떠다니며 대기
            e->t1 += dt;
            e->vel = V2(cosf(e->t1*0.8f)*30.0f, sinf(e->t1*1.1f)*24.0f);
            e->pos = v2add(e->pos,v2scale(e->vel,dt));
            if (e->t0<=0){ e->state=1; e->t0=0; e->t2=0; }
            break;
        case 1:
            if (e->t0<=0){
                // 5연사: 갈수록 벌어지는 3갈래 부채꼴
                float base=atan2f(dir.y,dir.x);
                for (int k=-2;k<=2;k++){
                    float a2=base+k*(0.08f+0.04f*e->t2);
                    spawn_bullet(false,7,e->pos,V2(cosf(a2)*175.0f,sinf(a2)*175.0f),1,3,4,0);
                }
                sfx_play(SFX_SHOOT);
                e->t2++;
                e->t0=boss_barrage_cooldown(0.11f);
                if (e->t2>=6){ e->state=2; e->t0=boss_barrage_cooldown(0.55f); e->t2=0; }
            }
            break;
        case 2: // 텔레포트 + 메아리 사격: 플레이어의 '과거 자취'를 쫓는 탄 (ECHO 고유)
            if (e->t0<=0){
                burst(e->pos,16,COL(0x7CFCE4),140,0.5f,2.5f,true);
                e->pos = V2(rng_range(&crng,TILE*4,(G.room.w-4)*TILE),rng_range(&crng,TILE*3,(G.room.h-3)*TILE));
                burst(e->pos,16,COL(0x7CFCE4),140,0.5f,2.5f,true);
                sfx_play(SFX_DASH);
                int shots = hp_frac<0.5f? 4:3;
                for (int k=0;k<shots;k++){
                    v2 past=history_at(0.5f+k*0.5f);
                    v2 d=v2norm(v2sub(past,e->pos));
                    spawn_bullet(false,7,e->pos,v2scale(d,180.0f),1,3.2f,4,0);
                }
                sfx_play(SFX_SHOOT);
                e->state=3; e->t0=boss_barrage_cooldown(0.32f);
            }
            break;
        case 3:
            if (e->t0<=0){
                boss_ring_gap(e,22,115.0f,atan2f(dir.y,dir.x));
                boss_spread(e,dir,5,0.14f,185.0f);
                e->state=0; e->t0=boss_barrage_cooldown(rng_range(&crng,0.85f,1.4f));
            }
            break;
        }
    } break;
    case E_BOSS_DEFRAG: {
        // 조각모음: 범위지정(AoE) 위주 + 5초마다 벽 배리어로 진입 차단
        e->t3 -= dt*1.25f;
        if (e->t3<=0){ e->t3=5.0f; defrag_barrier(e,p); }
        switch (e->state){
        case 0: // 짧은 부유 후 다음 공격 (거의 멈춰서 위치 선점)
            e->vel = v2scale(dir,18.0f);
            e->pos = resolve_collision(e->pos,e->vel,e->radius,dt);
            if (e->t0<=0){
                int r=rng_i(&crng,12);
                // 0-2 추적타격, 3-4 격자휩쓸기, 5-6 산개폭격, 7 라인융단, 8 조준연사, 9 단발빔
                e->state = r<3?4:(r<5?5:(r<7?6:(r<8?2:(r<9?3:(r<10?1:7)))));
                e->t1=0; e->t2=0;
                if (e->state==1){ // 단발 행/열 빔 텔레그래프
                    e->t1 = (float)rng_i(&crng,2);
                    e->t2 = e->t1==0? rng_range(&crng,TILE*2,(G.room.h-2)*TILE) : rng_range(&crng,TILE*3,(G.room.w-2)*TILE);
                    e->t0 = 0.65f;
                } else if (e->state==3){ e->t0=0.2f; e->target=p->pos; }
                else e->t0=0.0f; // 2/4/5/6 즉시 시작
            }
            break;
        case 1: // 단발 빔 (행/열 밴드, 즉발)
            if (e->t0<=0){
                G.shake=fmaxf(G.shake,5.0f);
                sfx_play(SFX_BOSS_ROAR);
                bool row = e->t1==0;
                float c = e->t2;
                float pp = row? p->pos.y : p->pos.x;
                if (fabsf(pp-c)<TILE*1.2f) player_take_damage_typed(V2(row?p->pos.x-10:c, row?c:p->pos.y-10),boss_damage(1.0f),e->type);
                for (int i=0;i<40;i++){
                    v2 bp = row? V2(rng_range(&crng,0,G.room.w*TILE),c) : V2(c,rng_range(&crng,0,G.room.h*TILE));
                    spawn_particle(bp,V2(rng_range(&crng,-30,30),rng_range(&crng,-30,30)),0.4f,2.5f,COL(0x3FE0C5),true,3,0);
                }
                e->state=0; e->t0=0.9f;
            }
            break;
        case 2: { // 라인 융단: 가로 또는 세로 여러 줄 동시 예고 후 폭발 (안전 틈 1줄)
            bool row=rng_i(&crng,2)==0;
            int lines=3+rng_i(&crng,3);
            for (int k=0;k<lines;k++){
                if (row){ float cy=rng_range(&crng,TILE*2,(G.room.h-2)*TILE); add_zone(V2(0,cy),TILE*1.1f,0.85f,1); }
                else    { float cx=rng_range(&crng,TILE*2,(G.room.w-2)*TILE); add_zone(V2(cx,0),TILE*1.1f,0.85f,2); }
            }
            sfx_play(SFX_BOSS_ROAR);
            e->state=0; e->t0=1.0f;
        } break;
        case 3: // 조준 연사
            if (e->t0<=0){
                v2 d=v2norm(v2sub(p->pos,e->pos));
                spawn_bullet(false,7,e->pos,v2scale(d,230.0f),1,2.5f,4,0);
                sfx_play(SFX_SHOOT);
                e->t1++;
                e->t0=boss_barrage_cooldown(0.10f);
                if (e->t1>=16){ e->state=0; e->t0=boss_barrage_cooldown(0.9f); e->t1=0; }
            }
            break;
        case 4: // 추적 타격: 플레이어와 예측 위치에 위험구역 (3파)
            if (e->t0<=0){
                add_zone(p->pos,30.0f,0.85f,0);
                add_zone(v2add(p->pos,v2scale(p->vel,0.45f)),28.0f,0.85f,0);   // 진행방향 예측
                add_zone(v2add(p->pos,V2(rng_range(&crng,-55,55),rng_range(&crng,-55,55))),26.0f,0.85f,0);
                sfx_play(SFX_BOSS_ROAR);
                e->t1++;
                e->t0=0.78f;
                if (e->t1>=4){ e->state=0; e->t0=0.85f; e->t1=0; }
            }
            break;
        case 5: { // 격자 휩쓸기: 한 줄(열)이 시차로 전진하며 폭발하는 벽
            if (e->t0<=0){
                int col=(int)e->t1;
                float cx=(col*2+2)*TILE;
                if (cx > (G.room.w-2)*TILE){ e->state=0; e->t0=1.3f; e->t1=0; break; }
                for (int yy=1;yy<G.room.h-1;yy+=3)
                    add_zone(V2(cx,yy*TILE+8.0f),18.0f,0.5f,0);
                e->t1+=1;
                e->t0=0.16f;
                if (((int)e->t1)%3==0) sfx_play(SFX_HIT);
            }
        } break;
        case 6: // 산개 폭격: 무작위 위험구역 다수 (2파)
            if (e->t0<=0){
                int n=8+rng_i(&crng,5);
                for (int k=0;k<n;k++)
                    add_zone(V2(rng_range(&crng,TILE*2,(G.room.w-2)*TILE),rng_range(&crng,TILE*2,(G.room.h-2)*TILE)),
                             30.0f,0.9f+rng_f(&crng)*0.3f,0);
                add_zone(p->pos,30.0f,0.9f,0);
                sfx_play(SFX_BOSS_ROAR);
                e->t1++;
                e->t0=1.0f;
                if (e->t1>=3){ e->state=0; e->t0=0.85f; e->t1=0; }
            }
            break;
        case 7: {
            for (int k=0;k<3;k++){
                float cy=rng_range(&crng,TILE*2,(G.room.h-2)*TILE);
                float cx=rng_range(&crng,TILE*2,(G.room.w-2)*TILE);
                add_zone(V2(0,cy),TILE,0.72f,1);
                add_zone(V2(cx,0),TILE,0.72f,2);
            }
            sfx_play(SFX_BOSS_ROAR);
            e->state=0; e->t0=0.9f;
        } break;
        }
    } break;
    case E_BOSS_NULL: {
        int phase = hp_frac>0.6f?0:(hp_frac>0.25f?1:2);
        if (phase!=e->phase){
            e->phase=phase;
            e->state=0; e->t0=0; e->t2=0; // 페이즈 진입 시 패턴 상태 초기화
            sfx_play(SFX_BOSS_ROAR);
            G.shake=fmaxf(G.shake,6.0f);
            if (phase==1){ G.ambient_mul=0.0f; set_msg("어둠이 빛을 삼킨다."); }
            if (phase==2){ G.ambient_mul=1.6f; G.light_mul=1.0f; set_msg("정화가 시작된다."); }
        }
        e->t1 += dt;
        if (phase==0){
            // 정상 가동: 부유하며 3가지 화려한 탄막을 순환 (state=패턴, t2=연사 카운트)
            e->vel=V2(cosf(e->t1*0.5f)*26.0f,sinf(e->t1*0.7f)*20.0f);
            e->pos=v2add(e->pos,v2scale(e->vel,dt));
            if (e->t0<=0){
                switch (e->state){
                case 0: // 역방향 이중 나선
                    boss_radial(e,6,105.0f, e->t1*2.0f);
                    boss_radial(e,6,105.0f,-e->t1*2.0f);
                    e->t2++; e->t0=boss_barrage_cooldown(0.12f);
                    if (e->t2>=12){ e->state=1; e->t2=0; e->t0=boss_barrage_cooldown(0.4f); }
                    break;
                case 1: // 꽃: 속도 다른 이중 링
                    boss_radial(e,18,80.0f,rng_f(&crng)*6.28f);
                    boss_radial(e,18,118.0f,0.224f);
                    e->t2++; e->t0=boss_barrage_cooldown(0.7f);
                    if (e->t2>=3){ e->state=2; e->t2=0; e->t0=boss_barrage_cooldown(0.4f); }
                    break;
                default: // 조준 산탄 연사
                    boss_spread(e,dir,7,0.16f,175.0f);
                    e->t2++; e->t0=boss_barrage_cooldown(0.5f);
                    if (e->t2>=3){ e->state=0; e->t2=0; e->t0=boss_barrage_cooldown(0.8f); }
                    break;
                }
            }
        } else if (phase==1){
            // 암전: 순간이동이 핵심. 깜빡 → 새 위치 등장 → 텔레포트마다 다른 탄막
            switch (e->state){
            case 0: // 텔레그래프 (수렴 입자)
                if (e->t0<=0){ e->state=1; e->t0=0.4f; burst(e->pos,12,COL(0x7CFCE4),120,0.5f,2,true); }
                break;
            case 1: // 사라짐 → 무작위 위치 재등장
                if (e->t0<=0){
                    burst(e->pos,16,COL(0x7CFCE4),140,0.5f,2.5f,true);
                    e->pos=V2(rng_range(&crng,TILE*4,(G.room.w-4)*TILE),rng_range(&crng,TILE*3,(G.room.h-3)*TILE));
                    burst(e->pos,16,COL(0xFF3D7F),140,0.5f,2.5f,true);
                    sfx_play(SFX_DASH);
                    e->state=2; e->t0=0.25f;
                }
                break;
            default: // 등장 탄막 — 텔레포트마다 교체
                if (e->t0<=0){
                    int pat=((int)e->t2)%3;
                    if (pat==0) boss_ring_gap(e,24,120.0f,atan2f(dir.y,dir.x)); // 플레이어 쪽 틈
                    else if (pat==1){ boss_radial(e,14,100.0f,0.0f); boss_radial(e,14,140.0f,0.314f); } // 이중 링
                    else boss_spread(e,dir,9,0.14f,185.0f); // 광각 산탄
                    e->t2++;
                    e->state=0; e->t0=boss_barrage_cooldown(rng_range(&crng,0.7f,1.0f));
                }
                break;
            }
        } else {
            // 정화: 발악 — 빠른 회전 나선 + 가끔 틈 있는 대형 링
            e->vel=V2(cosf(e->t1*0.9f)*40.0f,sinf(e->t1*1.2f)*30.0f);
            e->pos=v2add(e->pos,v2scale(e->vel,dt));
            if (e->t0<=0){
                boss_radial(e,5,150.0f,e->t1*3.0f);
                boss_radial(e,5,150.0f,e->t1*3.0f+3.1416f);
                e->t2++;
                if (((int)e->t2)%6==0) boss_ring_gap(e,28,100.0f,rng_f(&crng)*6.28f);
                e->t0=boss_barrage_cooldown(0.08f);
            }
        }
    } break;
    }
    // 접촉 피해
    if (v2len(v2sub(p->pos,e->pos)) < e->radius+5.0f) player_take_damage_typed(e->pos,boss_damage(1.0f),e->type);
}

// 플레이어 탄이 소멸할 때의 무기 유물 효과 (파편 분열 / 충격 폭발)
static void bullet_death_fx(Bullet* b){
    if (!b->from_player) return;
    if (b->kind==1 && b->fuse_armed){
        float explosion_radius=44.0f*(player_has_wrelic(WR_CANNON_RECOIL)?1.4f:1.0f);
        for (int j=0;j<MAX_ENTITIES;j++){
            Entity* e=&G.ents[j];
            if (!e->active||e->type==E_ECHO_GHOST) continue;
            if (v2len(v2sub(e->pos,b->pos))<explosion_radius)
                enemy_damage(e,b->dmg*0.85f,b->pos,b->burn,b->slow,b->crit,true,b->attack_group);
        }
        burst(b->pos,22,COL(0xFF7A3D),180,0.55f,2.8f,true);
        G.shake=fmaxf(G.shake,3.5f);
    }
    if (b->kind==1 && player_has_wrelic(WR_CANNON_FRAG)){ // 포탄 → 6갈래 파편
        for (int k=0;k<6;k++){ float a=k*1.0472f+rng_f(&crng);
            Bullet* frag=spawn_bullet(true,0,b->pos,V2(cosf(a)*210.0f,sinf(a)*210.0f),b->dmg*0.5f,0.5f,3.0f,0);
            if (frag) frag->attack_group=b->attack_group;
        }
        burst(b->pos,10,COL(0x7CFCE4),130,0.4f,2.2f,true);
    }
    if (b->kind==4 && player_has_wrelic(WR_LANCE_BLAST)) lance_explode(b);
}

static bool arm_delayed_fuse(Bullet* b){
    if (b->kind!=1 || !b->delayed_fuse) return false;
    b->delayed_fuse=false;
    b->fuse_armed=true;
    b->vel=V2(0,0);
    b->life=0.35f;
    b->radius*=1.4f;
    return true;
}

// ----------------------------------------------------------- bullets
static void update_bullets(float dt){
    Player* p=&G.pl;
    for (int i=0;i<MAX_BULLETS;i++){
        Bullet* b=&G.bullets[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life<=0){
            if (b->kind==3){ begin_glaive_return(b,p); b->life=3.0f; }
            else if (b->kind==12){ trigger_lance_pin(b); b->active=false; continue; }
            else if (b->kind==10){
                if (b->last_hit>=0 && b->last_hit<MAX_ENTITIES){
                    Entity* target=&G.ents[b->last_hit];
                    if (target->active && target->type!=E_ECHO_GHOST)
                        enemy_damage(target,b->dmg,b->pos,b->burn,b->slow,b->crit,true,b->attack_group);
                }
                b->active=false;
                continue;
            }
            else if (arm_delayed_fuse(b)) continue;
            else { bullet_death_fx(b); b->active=false; continue; }
        }
        if (b->kind==10||b->kind==12) continue;
        if (b->kind==11){
            b->trail_t-=dt;
            if (b->trail_t<=0){
                for (int j=0;j<MAX_ENTITIES;j++){
                    Entity* e=&G.ents[j];
                    if (!e->active||e->type==E_ECHO_GHOST) continue;
                    if (v2len(v2sub(e->pos,b->pos))<e->radius+b->radius)
                        enemy_damage(e,b->dmg,b->pos,0,0,false,true,b->attack_group);
                }
                b->trail_t+=1.0f;
            }
            continue;
        }
        // 유도
        if (b->kind==5 && b->from_player){
            float best=1e9f; Entity* tgt=NULL;
            for (int j=0;j<MAX_ENTITIES;j++){
                Entity* e=&G.ents[j];
                if (!e->active||e->type==E_ECHO_GHOST) continue;
                float d=v2len(v2sub(e->pos,b->pos));
                if (d<best){ best=d; tgt=e; }
            }
            if (tgt){
                v2 want=v2scale(v2norm(v2sub(tgt->pos,b->pos)),weapon_defs[WPN_WAND].speed);
                b->vel=v2add(b->vel,v2scale(v2sub(want,b->vel),6.0f*dt));
            }
        }
        if (b->kind==5 && !b->from_player){
            // 적 유도탄 (NULL)
            v2 want=v2scale(v2norm(v2sub(p->pos,b->pos)),110.0f);
            b->vel=v2add(b->vel,v2scale(v2sub(want,b->vel),2.5f*dt));
        }
        // 글레이브 귀환
        if (b->kind==3){
            float d=v2len(v2sub(b->pos,p->pos));
            if (!b->returning && d>130.0f) begin_glaive_return(b,p);
            if (b->returning){
                float return_speed=weapon_defs[WPN_GLAIVE].speed*1.3f;
                if (b->from_player && player_has_wrelic(WR_GLAIVE_RETURN)) return_speed*=1.3f;
                v2 want=v2scale(v2norm(v2sub(p->pos,b->pos)),return_speed);
                b->vel=v2add(b->vel,v2scale(v2sub(want,b->vel),8.0f*dt));
                // glaive_out 해제는 update_play 페일세이프가 담당 (쌍날: 둘 다 복귀해야 재발사)
                if (d<12.0f){ b->active=false; continue; }
            }
        }
        v2 previous_pos=b->pos;
        b->pos = v2add(b->pos,v2scale(b->vel,dt));
        // 벽
        bool wave_hit_wall=b->kind==8 && path_blocked_by_wall(previous_pos,b->pos);
        bool glaive_hit_wall=b->kind==3 &&
            (path_blocked_by_wall(previous_pos,b->pos) || tile_solid((int)(b->pos.x/TILE),(int)(b->pos.y/TILE)));
        if (glaive_hit_wall){
            b->pos=previous_pos;
            begin_glaive_return(b,p);
            continue;
        }
        if (b->kind!=3 && (wave_hit_wall || tile_solid((int)(b->pos.x/TILE),(int)(b->pos.y/TILE)))){
            if (b->kind==2 && b->from_player && b->bounces>0){ // 도탄: 막힌 축만 반사
                b->pos = v2sub(b->pos,v2scale(b->vel,dt)); // 충돌 직전으로 복귀
                bool solx=tile_solid((int)((b->pos.x+b->vel.x*dt)/TILE),(int)(b->pos.y/TILE));
                bool soly=tile_solid((int)(b->pos.x/TILE),(int)((b->pos.y+b->vel.y*dt)/TILE));
                if (solx) b->vel.x=-b->vel.x;
                if (soly) b->vel.y=-b->vel.y;
                if (!solx&&!soly){ b->vel.x=-b->vel.x; b->vel.y=-b->vel.y; }
                b->bounces--;
                burst(b->pos,2,COL(0x7CFCE4),50,0.2f,1.2f,true);
                continue;
            }
            if (arm_delayed_fuse(b)) continue;
            burst(b->pos,4,b->from_player?COL(0x7CFCE4):COL(0xFF3D7F),60,0.25f,1.5f,true);
            bullet_death_fx(b); b->active=false; continue;
        }
        if (b->pos.x<0||b->pos.y<0||b->pos.x>G.room.w*TILE||b->pos.y>G.room.h*TILE){
            if (b->kind==3){
                b->pos=previous_pos;
                begin_glaive_return(b,p);
                continue;
            }
            if (arm_delayed_fuse(b)) continue;
            bullet_death_fx(b); b->active=false; continue;
        }
        if (b->kind==3 && b->from_player && player_has_wrelic(WR_GLAIVE_TRAIL)){
            b->trail_t-=dt;
            if (b->trail_t<=0){
                spawn_glaive_burn_zone(b->pos,b->attack_group);
                b->trail_t+=0.18f;
            }
        }
        // 명중
        if (b->from_player){
            if (b->rehit_t>0) b->rehit_t-=dt;
            for (int j=0;j<MAX_ENTITIES;j++){
                Entity* e=&G.ents[j];
                if (!e->active||e->type==E_ECHO_GHOST) continue;
                if (j==b->last_hit && b->rehit_t>0) continue;
                float rr=e->radius+b->radius;
                if (v2len(v2sub(e->pos,b->pos))<rr){
                    enemy_damage(e,b->dmg,b->pos,b->burn,b->slow,b->crit,b->from_player,b->attack_group);
                    bool lance_stops=false;
                    if (b->kind==4 && player_has_wrelic(WR_LANCE_PIN)){
                        e->root=fmaxf(e->root,0.5f); e->vel=V2(0,0);
                        spawn_lance_pin(b,j); lance_stops=true;
                    }
                    if (b->kind==4 && player_has_wrelic(WR_LANCE_BLAST)) lance_stops=true;
                    if (b->kind==4 && player_has_wrelic(WR_LANCE_PIERCE) && !lance_stops) b->dmg*=1.2f;
                    if (b->kind==5 && player_has_wrelic(WR_WAND_RING)){
                        for (int k=0;k<MAX_ENTITIES;k++){
                            Entity* other=&G.ents[k];
                            if (!other->active||other==e||other->type==E_ECHO_GHOST) continue;
                            if (v2len(v2sub(other->pos,b->pos))<30.0f)
                                enemy_damage(other,b->dmg*0.4f,b->pos,b->burn,b->slow,false,true,b->attack_group);
                        }
                    }
                    if (b->kind==5 && player_has_wrelic(WR_WAND_DELAY)){
                        Bullet* echo=spawn_bullet(true,10,e->pos,V2(0,0),b->dmg*0.6f,0.35f,0.0f,0);
                        if (echo){ echo->last_hit=j; echo->attack_group=b->attack_group; echo->burn=b->burn; echo->slow=b->slow; echo->crit=b->crit; }
                    }
                    // 연쇄 메아리: 유도탄 명중 시 가까운 다른 적에게 작은 연쇄탄
                    if (b->kind==5 && player_has_wrelic(WR_WAND_CHAIN)){
                        float best=130.0f*130.0f; int t=-1;
                        for (int k=0;k<MAX_ENTITIES;k++){
                            Entity* e2=&G.ents[k];
                            if (!e2->active||k==j||e2->type==E_ECHO_GHOST) continue;
                            v2 dd=v2sub(e2->pos,b->pos); float d2=dd.x*dd.x+dd.y*dd.y;
                            if (d2<best){ best=d2; t=k; }
                        }
                        if (t>=0){
                            v2 cd=v2norm(v2sub(G.ents[t].pos,b->pos));
                            Bullet* chain=spawn_bullet(true,0,b->pos,v2scale(cd,260.0f),b->dmg*0.6f,0.6f,3.0f,0);
                            if (chain) chain->attack_group=b->attack_group;
                        }
                    }
                    b->last_hit=j; b->rehit_t=0.5f;
                    if (lance_stops){ bullet_death_fx(b); b->active=false; break; }
                    if (arm_delayed_fuse(b)) break;
                    if (b->pierce>0){ b->pierce--; }
                    else { bullet_death_fx(b); b->active=false; }
                    break;
                }
            }
        } else {
            if (v2len(v2sub(p->pos,b->pos))<b->radius+5.0f){
                player_take_damage_amount(b->pos,b->kind==7?boss_damage(b->dmg):b->dmg*0.5f);
                b->active=false;
            }
        }
    }
}

// ----------------------------------------------------------- pickups
static int player_relic_count(void){
    int count=0;
    for (int i=0;i<RELIC_COUNT;i++) if (G.pl.relics[i]) count++;
    return count;
}

static void set_regular_relic(int relic,bool held){
    Player* p=&G.pl;
    if (p->relics[relic]==held) return;
    p->relics[relic]=held;
    if (relic==RELIC_OVERCLOCK){
        p->maxhp+=held?-1:1;
        if (p->hp>p->maxhp) p->hp=(float)p->maxhp;
    }
}

static void begin_relic_swap(Pickup* pk){
    int count=0;
    G.relic_swap_type=pk->type;
    G.relic_swap_id=pk->relic;
    G.relic_swap_pickup=(int)(pk-G.pickups);
    G.relic_swap_sel=0;
    if (pk->type==PK_WRELIC){
        for (int i=0;i<2;i++) G.relic_swap_slots[count++]=i;
    } else {
        for (int i=0;i<RELIC_COUNT;i++)
            if (G.pl.relics[i]) G.relic_swap_slots[count++]=i;
    }
    G.state=ST_RELIC_SWAP;
    G.state_t=0;
    sfx_play(SFX_UI);
}

void player_confirm_relic_swap(int slot){
    int return_state=G.training_active?ST_TRAINING:ST_PLAY;
    if (G.relic_swap_pickup<0||G.relic_swap_pickup>=MAX_PICKUPS){ G.state=return_state; return; }
    Pickup* pk=&G.pickups[G.relic_swap_pickup];
    if (!pk->active||pk->type!=G.relic_swap_type||pk->relic!=G.relic_swap_id){ G.state=return_state; return; }
    if (slot<0){
        if (!G.training_active) pk->active=false;
        set_msg("현재 유물을 유지했다");
        G.state=return_state;
        sfx_play(SFX_UI);
        return;
    }
    int count=G.relic_swap_type==PK_WRELIC?2:4;
    if (slot>=count){ G.state=return_state; return; }
    if (G.relic_swap_type==PK_WRELIC){
        int old_slot=G.relic_swap_slots[slot];
        G.pl.wrelics[old_slot]=pk->relic;
        if (!G.training_active)
            for (int i=0;i<MAX_PICKUPS;i++)
                if (G.pickups[i].active&&G.pickups[i].type==PK_WRELIC) G.pickups[i].active=false;
    } else {
        set_regular_relic(G.relic_swap_slots[slot],false);
        set_regular_relic(pk->relic,true);
        pk->active=false;
    }
    char buf[96];
    const char* name=G.relic_swap_type==PK_WRELIC?weapon_relic_defs[pk->relic].name:relic_defs[pk->relic].name;
    snprintf(buf,sizeof(buf),"%s 교체 완료",name);
    set_msg(buf);
    G.state=return_state;
    sfx_play(SFX_PICKUP);
}

bool player_try_pickup(Pickup* pk){
    Player* p=&G.pl;
    switch (pk->type){
    case PK_BYTE:
        G.bytes_run++;
        if (G.bytes_run==1) set_msg("바이트 수집 — 타이틀에서 무기를 해금할 수 있다");
        sfx_play(SFX_PICKUP);
        pk->active=false;
        return true;
    case PK_HEART:
        if (p->hp>=p->maxhp){ return false; }
        p->hp=fminf((float)p->maxhp,p->hp+2.0f);
        sfx_play(SFX_HEAL);
        burst(pk->pos,10,COL(0xFF5A8C),100,0.5f,2,true);
        pk->active=false;
        return true;
    case PK_SHARD: {
        int kb=player_used_kb()+ (G.pl.relics[RELIC_COMPRESS]?51:64);
        if (kb>player_capacity_kb()){ set_msg("용량 초과 — 무언가 버려야 한다 (Q)"); sfx_play(SFX_DENY); return false; }
        StatSnap before=stat_capture();
        p->shards++;
        if (p->shards==1)
            set_msg("추억 조각: 무게를 차지하지만 빛이 밝아진다 · Q로 버리기");
        sfx_play(SFX_SHARD);
        burst(pk->pos,12,COL(0x9FFFF0),90,0.6f,2,true);
        stat_floats(before);
        add_fixed_floater(VIRT_W*0.5f,32.0f,"추억 조각 +64KB",COL(0x9FFFF0));
        pk->active=false;
        return true;
    }
    case PK_CORE: {
        int kb=player_used_kb()+ (G.pl.relics[RELIC_COMPRESS]?102:128);
        if (kb>player_capacity_kb()){ set_msg("용량 초과 — 핵심 조각을 들 수 없다!"); sfx_play(SFX_DENY); return false; }
        StatSnap before=stat_capture();
        p->cores |= (uint8_t)(1<<pk->core_id);
        sfx_play(SFX_CORE_SHARD);
        burst(pk->pos,30,COL(0xFFFFFF),140,1.0f,3,true);
        stat_floats(before);
        pk->active=false;
        // 회상 컷
        G.fb_core = pk->core_id;
        G.fb_t = 0;
        G.state = ST_FLASHBACK;
        return true;
    }
    case PK_WEAPON: {
        Weapon old=p->weapon;
        StatSnap before=stat_capture();
        p->weapon=pk->weapon;
        if (player_used_kb()>player_capacity_kb()){
            p->weapon=old;
            set_msg("용량 초과 — 이 무기는 너무 무겁다");
            sfx_play(SFX_DENY);
            return false;
        }
        sfx_play(SFX_PICKUP);
        char buf[96];
        snprintf(buf,sizeof(buf),"%s%s 장착",prefix_names[p->weapon.prefix],weapon_defs[p->weapon.type].name);
        set_msg(buf);
        stat_floats(before);
        if (!G.training_active) pk->weapon=old; // 들고 있던 무기를 내려놓음
        return true;
    }
    case PK_RELIC: {
        if (p->relics[pk->relic]) { return false; }
        if (player_relic_count()>=4){ begin_relic_swap(pk); return true; }
        StatSnap before=stat_capture();
        set_regular_relic(pk->relic,true);
        sfx_play(SFX_PICKUP);
        char buf[96];
        snprintf(buf,sizeof(buf),"%s — %s",relic_defs[pk->relic].name,relic_defs[pk->relic].desc);
        set_msg(buf);
        stat_floats(before);
        pk->active=false;
        return true;
    }
    case PK_WRELIC: {
        int wr=pk->relic;
        if (player_has_wrelic(wr)){ set_msg("이미 보유한 무기 유물"); sfx_play(SFX_DENY); return false; }
        int slot = p->wrelics[0]<0?0:(p->wrelics[1]<0?1:-1);
        if (slot<0){ begin_relic_swap(pk); return true; }
        StatSnap before=stat_capture();
        p->wrelics[slot]=wr;
        sfx_play(SFX_PICKUP);
        char buf[96];
        if (G.training_active) snprintf(buf,sizeof(buf),"%s 장착",weapon_relic_defs[wr].name);
        else snprintf(buf,sizeof(buf),"%s — %s",weapon_relic_defs[wr].name,weapon_relic_defs[wr].desc);
        set_msg(buf);
        stat_floats(before);
        if (!G.training_active) pk->active=false;
        // 형제 드롭 제거 — 보스가 떨군 둘 중 하나만 획득
        if (!G.training_active)
            for (int i=0;i<MAX_PICKUPS;i++)
                if (G.pickups[i].active && G.pickups[i].type==PK_WRELIC) G.pickups[i].active=false;
        return true;
    }
    }
    return false;
}

void player_drop_shard(void){
    Player* p=&G.pl;
    if (p->shards>0){
        StatSnap before=stat_capture();
        p->shards--;
        for (int i=0;i<MAX_PICKUPS;i++) if (!G.pickups[i].active){
            spawn_pickup(PK_SHARD,v2add(p->pos,V2(0,14)),(Weapon){0,0},0,0);
            G.pickups[i].manual_only=true;
            break;
        }
        sfx_play(SFX_DROP);
        set_msg("추억 조각을 내려놓았다... 가벼워졌다.");
        stat_floats(before);
        return;
    }
    // 핵심 조각은 최후의 수단
    for (int i=3;i>=0;i--){
        if (p->cores & (1<<i)){
            StatSnap before=stat_capture();
            p->cores &= (uint8_t)~(1<<i);
            spawn_pickup(PK_CORE,v2add(p->pos,V2(0,14)),(Weapon){0,0},0,i);
            sfx_play(SFX_DROP);
            set_msg("핵심 조각을 내려놓았다 — 엔딩에 영향이 있다!");
            stat_floats(before);
            return;
        }
    }
    set_msg("버릴 추억이 없다.");
    sfx_play(SFX_DENY);
}

// ----------------------------------------------------------- main update
static void update_enemy_feedback(float dt){
    for (int i=0;i<MAX_ENTITIES;i++){
        EnemyFeedback* feedback=&G.enemy_feedback[i];
        if (feedback->t<=0) continue;
        feedback->t-=dt;
        Entity* e=&G.ents[i];
        if (e->active && e->player_damaged){
            feedback->pos=e->pos;
            feedback->hp=clampf(e->hp/e->maxhp,0.0f,1.0f);
            feedback->radius=e->radius;
            feedback->elite=e->elite;
        }
    }
}

void update_play(float dt){
    Player* p=&G.pl;
    update_enemy_feedback(dt);
    if (!G.training_active) clear_reward_label_obstacles();
    player_sync_light_shield();
    G.run_time += dt;
    G.room_t += dt;

    // --- 이동
    v2 mv=V2(0,0);
    if (key_held[SAPP_KEYCODE_W]||key_held[SAPP_KEYCODE_UP]) mv.y-=1;
    if (key_held[SAPP_KEYCODE_S]||key_held[SAPP_KEYCODE_DOWN]) mv.y+=1;
    if (key_held[SAPP_KEYCODE_A]||key_held[SAPP_KEYCODE_LEFT]) mv.x-=1;
    if (key_held[SAPP_KEYCODE_D]||key_held[SAPP_KEYCODE_RIGHT]) mv.x+=1;
    mv=v2norm(mv);
    p->moving = (mv.x!=0||mv.y!=0);
    float speed = 95.0f*player_speed_mul();

    if (p->dash_t>0){
        p->dash_t-=dt;
        p->vel = v2scale(p->dash_dir,330.0f);
        if (rng_i(&crng,2)==0) spawn_particle(p->pos,V2(0,0),0.3f,3,COL(0x3FE0C5),true,1,0);
    } else {
        v2 want=v2scale(mv,speed);
        p->vel=v2add(p->vel,v2scale(v2sub(want,p->vel),14.0f*dt));
    }
    p->dash_cd-=dt;
    if (p->iframes>0) p->iframes-=dt;
    p->pos = resolve_collision(p->pos,p->vel,5.0f,dt);

    // --- 조준
    bool keyboard_attack=key_held[SAPP_KEYCODE_SPACE];
    if (mouse_present && !keyboard_attack){
        v2 world_mouse = v2add(mouse_virt, G.cam);
        p->aim=v2norm(v2sub(world_mouse,p->pos));
        if (p->aim.x==0&&p->aim.y==0) p->aim=V2(1,0);
    } else {
        Entity* target=NULL;
        float nearest=1e9f;
        for (int i=0;i<MAX_ENTITIES;i++){
            Entity* e=&G.ents[i];
            if (!e->active || e->type==E_ECHO_GHOST) continue;
            float d=v2len(v2sub(e->pos,p->pos));
            if (d<nearest){ nearest=d; target=e; }
        }
        if (target) p->aim=v2norm(v2sub(target->pos,p->pos));
        else if (p->moving) p->aim=mv;
    }

    // --- 공격
    p->attack_cd-=dt;
    if (slash_t>0) slash_t-=dt;
    if (lance_thrust_t>0) lance_thrust_t-=dt;
    fire_weapon(dt);

    // --- 글레이브 고착 페일세이프: 탄이 사라졌는데 깃발만 남으면 해제
    if (p->glaive_out){
        bool found=false;
        for (int i=0;i<MAX_BULLETS;i++)
            if (G.bullets[i].active && G.bullets[i].from_player && G.bullets[i].kind==3){ found=true; break; }
        if (!found) p->glaive_out=false;
    }

    // --- 체크섬 회복
    if (p->relics[RELIC_CHECKSUM]){
        p->heal_timer+=dt;
        if (p->heal_timer>45.0f && p->hp<p->maxhp){
            p->heal_timer=0;
            p->hp=fminf((float)p->maxhp,p->hp+0.5f);
            sfx_play(SFX_HEAL);
            burst(p->pos,8,COL(0x9FFFF0),70,0.5f,2,true);
        }
    }

    // --- 무게 구간 변화 안내
    {
        float wf=weight_frac();
        int tier = wf<0.4f?1:(wf<0.8f?2:3);
        if (p->last_weight_tier && tier!=p->last_weight_tier){
            if (tier==1) set_msg("가벼움 — 빠르지만 빛과 공격력이 약하다");
            else if (tier==2) set_msg("적정 무게 — 속도·빛·공격력의 균형");
            else set_msg("무거움 — 느리지만 빛이 밝고 공격이 강하다");
        }
        p->last_weight_tier=tier;
    }

    // --- 히스토리 기록
    G.hist_head=(G.hist_head+1)&255;
    G.history[G.hist_head]=p->pos;

    event_assign_pending_trait();
    // --- 적
    bool any_enemy=false;
    for (int i=0;i<MAX_ENTITIES;i++){
        Entity* e=&G.ents[i];
        if (!e->active) continue;
        if (G.training_active && i==0){
            if (e->flash>0) e->flash-=dt;
            e->vel=V2(0,0);
            continue;
        }
        // 벽 속에 갇힌 적은 가까운 바닥으로 구출 (방 클리어 불가 방지)
        if (!(e->type>=E_BOSS_ROT&&e->type<=E_BOSS_NULL) &&
            tile_solid((int)(e->pos.x/TILE),(int)(e->pos.y/TILE))){
            int etx=(int)(e->pos.x/TILE), ety=(int)(e->pos.y/TILE);
            bool done=false;
            for (int rr=1;rr<7&&!done;rr++)
                for (int dy=-rr;dy<=rr&&!done;dy++) for (int dx=-rr;dx<=rr&&!done;dx++)
                    if (!tile_solid(etx+dx,ety+dy)){
                        e->pos=V2((etx+dx)*TILE+8.0f,(ety+dy)*TILE+8.0f);
                        done=true;
                    }
        }
        if (e->flash>0) e->flash-=dt;
        if (e->player_damage_t>0) e->player_damage_t-=dt;
        if (e->type>=E_BOSS_ROT && e->type<=E_BOSS_NULL){ any_enemy=true; update_boss(e,dt); }
        else {
            if (e->type!=E_ECHO_GHOST) any_enemy=true;
            update_enemy(e,dt, e->event_trait==ELITE_HASTE ? dt*1.15f : dt);
            // 벽 통과형(망령/메아리)을 제외하고 벽 겹침을 매 프레임 밀어내 끼임 방지
            if (e->active && e->type!=E_WRAITH && e->type!=E_ECHO_GHOST)
                e->pos = push_out_of_walls(e->pos, e->radius);
        }
    }
    if (!any_enemy && !G.room.cleared) on_room_cleared();

    update_zones(dt);
    update_bullets(dt);

    // --- 파티클
    for (int i=0;i<MAX_PARTICLES;i++){
        Particle* pa=&G.parts[i];
        if (!pa->active) continue;
        pa->life-=dt;
        if (pa->life<=0){ pa->active=false; continue; }
        pa->vel=v2scale(pa->vel,1.0f-pa->drag*dt);
        pa->vel.y+=pa->grav*dt;
        pa->pos=v2add(pa->pos,v2scale(pa->vel,dt));
    }
    // 플레이어 불씨 잔불
    if (rng_i(&crng,3)==0)
        spawn_particle(v2add(p->pos,V2(rng_range(&crng,-3,3),rng_range(&crng,-2,2))),
                       V2(rng_range(&crng,-8,8),-22),0.45f,2.0f,COL(0x3FE0C5),true,1.5f,0);

    // --- 픽업
    for (int i=0;i<MAX_PICKUPS;i++){
        Pickup* pk=&G.pickups[i];
        if (!pk->active) continue;
        pk->bob+=dt*3.0f;
        float d=v2len(v2sub(pk->pos,p->pos));
        bool auto_collect=pk->type==PK_BYTE || pk->type==PK_HEART ||
                          (pk->type==PK_SHARD && !pk->manual_only);
        if (auto_collect){
            if (d<46.0f) pk->pos=v2add(pk->pos,v2scale(v2norm(v2sub(p->pos,pk->pos)),160.0f*dt));
            if (d<10.0f) player_try_pickup(pk);
        }
    }

    // --- 포탈 흡입 연출: 주변 입자가 나선을 그리며 빨려 들어간다
    {
        v2 pc[4]; int np=portal_list(pc,NULL,4);
        for (int i=0;i<np;i++){
            if (rng_i(&crng,3)) continue;
            float a=rng_f(&crng)*6.2832f;
            float rr=16.0f+rng_f(&crng)*12.0f;
            v2 sp=V2(pc[i].x+cosf(a)*rr, pc[i].y+sinf(a)*rr);
            v2 inw=v2norm(v2sub(pc[i],sp));
            spawn_particle(sp, v2add(v2scale(inw,60.0f),v2scale(V2(-inw.y,inw.x),45.0f)),
                           0.38f, 1.6f, COL(0x7CFCE4), true, 0.0f, 0.0f);
        }
    }

    // --- 문/출구 (모든 벽 일반화)
    if (G.fade_dir==0){
        int RW=G.room.w, RH=G.room.h;
        int tx=(int)(p->pos.x/TILE), ty=(int)(p->pos.y/TILE);
        // 각 벽: 플레이어가 그 벽에 ~7px 이내로 붙고, 해당 행/열 벽 타일이 열린 문/출구인지
        struct { bool adj; int wx, wy; } walls[4];
        walls[DIR_R].adj = p->pos.x > (RW-1)*TILE-7.0f;
        walls[DIR_R].wx=RW-1; walls[DIR_R].wy=ty;
        walls[DIR_L].adj = p->pos.x < TILE+7.0f;
        walls[DIR_L].wx=0;    walls[DIR_L].wy=ty;
        walls[DIR_D].adj = p->pos.y > (RH-1)*TILE-7.0f;
        walls[DIR_D].wx=tx;   walls[DIR_D].wy=RH-1;
        walls[DIR_U].adj = p->pos.y < TILE+7.0f;
        walls[DIR_U].wx=tx;   walls[DIR_U].wy=0;
        for (int d=0; d<4 && G.fade_dir==0; d++){
            if (!walls[d].adj) continue;
            int wx=walls[d].wx, wy=walls[d].wy;
            if (wx<0||wy<0||wx>=RW||wy>=RH) continue;
            uint8_t t = G.room.tiles[wy][wx];
            if (t==T_DOOR_OPEN){
                // 어느 문인지: door_dir/x/y로 매칭
                int which=0;
                for (int dn=0; dn<G.room.door_count; dn++){
                    if (G.room.door_dir[dn]!=d) continue;
                    // 같은 벽 위, 문 2칸 범위 안인지
                    if (d==DIR_R||d==DIR_L){
                        if (wy==G.room.door_y[dn] || wy==G.room.door_y[dn]+1){ which=dn; break; }
                    } else {
                        if (wx==G.room.door_x[dn] || wx==G.room.door_x[dn]+1){ which=dn; break; }
                    }
                }
                G.pending_door = G.room.door_promise[which];
                G.pending_entry_dir = opposite(d);
                fade_to(-2,COL(0x0B0710));
                sfx_play(SFX_DOOR);
            } else if (t==T_EXIT){
                fade_to(-3,COL(0x9FFFF0));
                sfx_play(SFX_ENDING);
            }
        }
    }

    // --- 타이머/연출
    if (G.msg_t>0) G.msg_t-=dt;
    for (int i=0;i<MAX_FLOATERS;i++) if (G.floaters[i].t>0) G.floaters[i].t-=dt;
    if (G.boss_intro){
        G.boss_intro_t-=dt;
        if (G.boss_intro_t<=0) G.boss_intro=false;
    }
    p->anim_t += dt*(p->moving?10.0f:4.0f);
}
