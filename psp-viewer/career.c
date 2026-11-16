/* The career (see career.h). */
#include "career.h"
#include "cars.h"
#include "savedata.h"
#include <stddef.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CareerEvent events[CAREER_MAX];static int nevents;
static CareerStage stages[STAGE_N];static CareerSponsor sponsors[SPONSOR_MAX];static CareerCover covers[COVER_MAX];
static CareerShow shows[SHOW_MAX];static int nshows;
static CareerRecord records[CAREER_MAX];static CareerRule rules[STAGE_N];static int nrecords,nrules;   /* version 9 */
typedef struct { uint8_t mode[5];int8_t rating[5];uint8_t pad[2]; } RivalPerf;
typedef struct { int16_t races;uint16_t pad;float start,end; } Ramp;
static RivalPerf rival_perf[CAREER_MAX];static Ramp ramps[STAGE_N];static int nrival_perf,nramps;   /* version 10 */
enum { PRESET_MAX=16 };
typedef struct { char pack[24],model[16],neon[16]; } RivalPreset;
typedef struct { uint8_t preset[5],pad[3]; } RivalCar;
static RivalPreset presets[PRESET_MAX];static RivalCar rival_car[CAREER_MAX];static int npresets,nrival_car;   /* version 11 */
typedef struct { float at[3];uint32_t name; } RaceZone;
static RaceZone zones[CAREER_MAX];static int nzones;   /* version 12 */
typedef struct { uint32_t terms,races,icon; } SponsorText;
static SponsorText sponsor_text[SPONSOR_MAX];static int nsponsor_text;   /* version 13 */
enum { ENGAGE_BODY_MAX=256 };
typedef struct { uint32_t subject,type,from;uint16_t first,count; } EngageText;
static uint32_t engage_body[ENGAGE_BODY_MAX];static EngageText engage_text[CAREER_MAX];static int nengage_body,nengage_text;   /* version 14 */
static int engaged=-1;   /* the race the player drove into (career_engage) */
static CareerSms sms[SMS_MAX];static CareerSmsMail sms_mail[SMS_MAX];static int nsms_mail;   /* version 15 */
typedef struct { float star,to; } ShowHeights;
static ShowHeights show_y[SHOW_MAX];static int nshow_y;   /* version 16 */
typedef struct { uint32_t header,welcome; } ShopText;
static ShopText shop_text[SHOP_MAX];static int nshop_text;   /* version 17: the shops' dialog texts (offsets) */
static CareerShop shops[SHOP_MAX];static CareerTip tips[TIP_MAX];static CareerCash cash[CASH_MAX];static int ncash;static char *texts;static uint32_t text_size;
static int nsponsors,ncovers,nsms,nshops,ntips;
static int rating;   /* the player's car, x10 */
static CareerState st;
/* No save-format or earned-state fields: these flags describe unsaved RAM. */
static CareerSaveResult persist_result;
static int persist_dirty,persist_uncertain;
CareerSaveResult career_save_result(void) { return persist_result; }
int career_save_pending(void) { return persist_dirty; }
int career_save_uncertain(void) { return persist_uncertain; }
static void persist_loaded(void) {
    persist_result=(CareerSaveResult){CAREER_SAVE_NONE,0};persist_dirty=persist_uncertain=0;
}
/* Saves in the background on the street (career_save_async): a save asked
 * for while one runs follows it with the newest state; a save leaves the
 * career clean only if nothing changed after its snapshot (change_gen). */
static int async_mode,async_busy,async_again;static unsigned change_gen,async_gen;static const char *async_path;
static int defer_mode,deferred;static const char *deferred_path;static unsigned defer_held,defer_written;   /* career_save_defer: a save held for the street's next stop */
static void persist_changed(void) {
    persist_dirty=1;change_gen++;
    if(persist_result.status==CAREER_SAVE_NONE || persist_result.status==CAREER_SAVE_SAVED)
        persist_result=(CareerSaveResult){CAREER_SAVE_DIRTY,0};
}
static CareerSaveResult persist_progress(const char *path) {
    persist_changed();
    if(path){career_save(path);return persist_result;}
    /* This operation did not write; do not erase an earlier timeout/error. */
    return (CareerSaveResult){CAREER_SAVE_DIRTY,0};
}
const char *career_type_names[6]={"Circuito","Sprint","Street X","URL","Drag","Drift"};

static int skip_table(FILE *f,size_t size) { uint32_t n;return fread(&n,4,1,f)==1 && n<4096 && fseek(f,(long)(n*size),SEEK_CUR)==0; }
static int read_table(FILE *f,void *to,size_t size,int max,int *count) {
    uint32_t n;if(fread(&n,4,1,f)!=1 || n>(uint32_t)max)return 0;
    if(n && fread(to,size,n,f)!=n)return 0;
    *count=(int)n;return 1;
}
int career_load_data(const char *path) {
    nevents=nsponsors=ncovers=nsms=nshops=ntips=ncash=nshows=nrecords=nrules=nrival_perf=nramps=npresets=nrival_car=nzones=nsponsor_text=nengage_body=nengage_text=nsms_mail=nshow_y=nshop_text=0;free(texts);texts=NULL;text_size=0;engaged=-1;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2CA",4) || fread(&version,4,1,f)!=1 || version<1 || version>17 || fread(&n,4,1,f)!=1 || n>CAREER_MAX){fclose(f);return 0;}
    for(uint32_t k=0;k<n;k++)if(fread(&events[k],sizeof(CareerEvent),1,f)!=1){fclose(f);return 0;}
    nevents=(int)n;
    if(version>=2) {   /* the career's world: stages, sponsors, covers, SMS, shops, texts */
        int ns,ok=read_table(f,stages,sizeof *stages,STAGE_N,&ns) && read_table(f,sponsors,sizeof *sponsors,SPONSOR_MAX,&nsponsors) &&
                  read_table(f,covers,sizeof *covers,COVER_MAX,&ncovers) && read_table(f,sms,sizeof *sms,SMS_MAX,&nsms) &&
                  read_table(f,shops,sizeof *shops,SHOP_MAX,&nshops) &&
                  (version<3 || read_table(f,tips,sizeof *tips,TIP_MAX,&ntips)) &&
                  (version<4 || read_table(f,cash,sizeof *cash,CASH_MAX,&ncash)) &&
                  (version<5 || (version<7?skip_table(f,version<6?40:172):read_table(f,shows,sizeof *shows,SHOW_MAX,&nshows))) &&   /* versions 5-6 without the cameras or the countdowns: none */
                  (version<9 || (read_table(f,records,sizeof *records,CAREER_MAX,&nrecords) && read_table(f,rules,sizeof *rules,STAGE_N,&nrules))) &&
                  (version<10 || (read_table(f,rival_perf,sizeof *rival_perf,CAREER_MAX,&nrival_perf) && read_table(f,ramps,sizeof *ramps,STAGE_N,&nramps))) &&
                  (version<11 || (read_table(f,presets,sizeof *presets,PRESET_MAX,&npresets) && read_table(f,rival_car,sizeof *rival_car,CAREER_MAX,&nrival_car))) &&
                  (version<12 || read_table(f,zones,sizeof *zones,CAREER_MAX,&nzones)) &&
                  (version<13 || read_table(f,sponsor_text,sizeof *sponsor_text,SPONSOR_MAX,&nsponsor_text)) &&
                  (version<14 || (read_table(f,engage_body,sizeof *engage_body,ENGAGE_BODY_MAX,&nengage_body) && read_table(f,engage_text,sizeof *engage_text,CAREER_MAX,&nengage_text))) &&
                  (version<15 || read_table(f,sms_mail,sizeof *sms_mail,SMS_MAX,&nsms_mail)) &&
                  (version<16 || read_table(f,show_y,sizeof *show_y,SHOW_MAX,&nshow_y)) &&
                  (version<17 || read_table(f,shop_text,sizeof *shop_text,SHOP_MAX,&nshop_text));
        if(ok && fread(&text_size,4,1,f)==1 && text_size<1u<<20 && (texts=malloc(text_size+1)) && fread(texts,1,text_size,f)==text_size)texts[text_size]=0;
        else{nsponsors=ncovers=nsms=nshops=ntips=ncash=nshows=nrecords=nrules=nrival_perf=nramps=npresets=nrival_car=nzones=nsponsor_text=nengage_body=nengage_text=nsms_mail=nshow_y=nshop_text=0;free(texts);texts=NULL;text_size=0;}
        if(nsms_mail!=nsms)nsms_mail=0;   /* a byte set per SMS, or none */
        for(int k=0;k<nsms_mail;k++)if(sms_mail[k].box<SMS_BOX_INBOX || sms_mail[k].box>SMS_BOX_UNLOCKS)sms_mail[k].box=SMS_BOX_RACHEL;
        if(nrival_perf!=nevents)nrival_perf=0;
        if(nrival_car!=nevents)nrival_car=0;
        if(nzones!=nevents)nzones=0;
        if(nshow_y!=nshows)nshow_y=0;   /* a pair per showcase, or none */
        if(nshop_text!=nshops)nshop_text=0;   /* a pair per shop, or none */
        if(nsponsor_text!=nsponsors)nsponsor_text=0;
        if(nengage_text!=nevents)nengage_text=0;
        for(int k=0;k<nengage_text;k++)if(engage_text[k].first+engage_text[k].count>nengage_body)engage_text[k].count=0;
        for(int k=0;k<npresets;k++){presets[k].pack[23]=presets[k].model[15]=presets[k].neon[15]=0;}
        for(int k=0;k<ns;k++)if(version<8)stages[k].outruns=-1;   /* files before version 8 lack the limit */
        if(nrecords!=nevents)nrecords=0;   /* a row each, or none */
        for(int k=0;k<nrecords;k++){if(records[k].rivals>5)records[k].rivals=5;if(records[k].after!=0xffff && records[k].after>=nevents)records[k].after=0xffff;}
        for(int k=0;k<nrules;k++)if(rules[k].final!=0xffff && rules[k].final>=nevents)rules[k].final=0xffff;
        for(int k=0;k<ncash;k++)if(cash[k].id>=64)cash[k].id=63;
        for(int k=0;k<ntips;k++)if(tips[k].sms>=nsms)tips[k].sms=0;
        for(int k=0;k<nsponsors;k++)sponsors[k].name[15]=0;
        for(int k=0;k<nshops;k++)shops[k].name[23]=0;
        for(int k=0;k<nshows;k++){shows[k].name[23]=shows[k].magazine[7]=0;if(shows[k].cover>=ncovers)shows[k].cover=-1;if(shows[k].says>5)shows[k].says=5;}
    }
    fclose(f);return 1;
}
const CareerStage *career_stage_info(int s) { return s>=0 && s<STAGE_N?&stages[s]:NULL; }
const CareerSponsor *career_sponsor(int k) { return k>=0 && k<nsponsors?&sponsors[k]:NULL; }
static const char *sponsor_text_of(int k,int which) {
    if(k<0 || k>=nsponsor_text)return NULL;
    const char *t=career_text(which==0?sponsor_text[k].terms:which==1?sponsor_text[k].races:sponsor_text[k].icon);
    return *t?t:NULL;
}
const char *career_sponsor_terms(int k) { return sponsor_text_of(k,0); }
const char *career_sponsor_races(int k) { return sponsor_text_of(k,1); }
const char *career_sponsor_icon(int k) { return sponsor_text_of(k,2); }
int career_engage_text(int k,unsigned pick,const char **subject,const char **type,const char **from,const char **body) {
    if(k<0 || k>=nengage_text)return 0;
    const EngageText *e=&engage_text[k];
    *subject=career_text(e->subject);*type=career_text(e->type);*from=career_text(e->from);
    *body=e->count?career_text(engage_body[e->first+pick%e->count]):"";
    return 1;
}
int career_url_purse(int stage) {   /* PlayerCareerState::GetURLPurse: the stage's first URL race's money */
    for(int k=0;k<nevents;k++)if(events[k].stage==stage && events[k].reward==2)return events[k].money;
    return 0;
}
int career_cover_count(void) { return ncovers; }
const CareerCover *career_cover(int i) { return i>=0 && i<ncovers?&covers[i]:NULL; }
int career_sms_count(void) { return nsms; }
const CareerSms *career_sms(int i) { return i>=0 && i<nsms?&sms[i]:NULL; }
const char *career_text(uint32_t off) { return texts && off<text_size?texts+off:""; }
int career_shop_count(void) { return nshops; }
static int stage_check(void);
int career_show_count(void) { return nshows; }
const CareerShow *career_show(int i) { return i>=0 && i<nshows?&shows[i]:NULL; }
int career_show_heights(int i,float *star,float *to) {
    if(i<0 || i>=nshow_y || !isfinite(show_y[i].star) || !isfinite(show_y[i].to))return 0;
    *star=show_y[i].star;*to=show_y[i].to;return 1;
}
static uint32_t shows_done(void) { return st.shows_lo[0]|st.shows_lo[1]<<8|(uint32_t)st.shows_hi[0]<<16|(uint32_t)st.shows_hi[1]<<24; }
static int dvd_order(int i) { int n=0;for(int k=0;k<i;k++)n+=shows[k].kind==SHOW_DVD;return n; }   /* a DVD showcase's bit */
int career_show_done(int i) {
    if(i>=0 && i<nshows && shows[i].kind==SHOW_DVD)return st.dvd_shot>>dvd_order(i)&1;
    return i>=0 && i<32 && (shows_done()>>i&1);
}
static const CareerRecord *rec(int k) { return k>=0 && k<nrecords?&records[k]:NULL; }
const CareerRecord *career_record(int k) { return rec(k); }
const CareerRule *career_rule(int s) { return s>=0 && s<nrules?&rules[s]:NULL; }
static int later(int k) { return events[k].flags&CAREER_LATER; }
static int won(int k) { return st.best[k]==1; }
static int is_hidden(int k) { const CareerRecord *r=rec(k);return r && r->hidden; }
static int is_suv(int k) { const CareerRecord *r=rec(k);return r && r->suv; }
static int found(int k) { return st.found[k>>3]>>(k&7)&1; }
/* A stage's races won of a kind (the record's +0x34, CareerEvent.reward):
 * 3 the world's (not the SUV races), 1 a sponsor's, 2 URL (the game's
 * RuntimeCareerStageInfo::GetNumWorld/Sponsor/URLRacesCompleted: each race
 * once). */
static int stage_won(int s,int kind) {
    int n=0;for(int k=0;k<nevents;k++)n+=events[k].stage==s && !later(k) && events[k].reward==kind && (kind!=3 || !is_suv(k)) && won(k);
    return n;
}
static int stage_wins(void) { return stage_won(st.stage,3); }
/* The stage's DVD covers open now: those whose stars the car's rating
 * reached when it was last back on the street (career_free_roam, the
 * game's RecalcDVDUnlocks), not yet shot. */
static uint16_t dvd_open;
static int dvd_rating(const CareerShow *v) { return v->cover>=0 && v->cover<ncovers?covers[v->cover].rating:1000; }
static int dvds_shot(int s,int *all) {
    int n=0,m=0;for(int i=0;i<nshows;i++)if(shows[i].kind==SHOW_DVD && shows[i].stage==s){m++;n+=career_show_done(i);}
    if(all)*all=m;
    return n;
}
int career_show_open(int i) {
    const CareerShow *v=career_show(i);
    if(v && v->kind==SHOW_DVD)return v->stage==st.stage && (dvd_open>>dvd_order(i)&1) && !career_show_done(i);   /* a DVD cover's: its stars reached, not yet shot */
    /* a photo shoot by the stage's world races won, a unique part's by its outruns won (default.xbe 0x140630, 0x140580) */
    return v && (v->kind==SHOW_MAGAZINE || (v->kind==SHOW_UNIQUE && (v->opens || v->perf))) && v->stage==st.stage && !career_show_done(i)
           && (v->kind==SHOW_UNIQUE?st.outruns:stage_wins())>=v->wins;
}
int career_unique_open(int kind) {
    int any=0;
    for(int i=0;i<nshows;i++)if(shows[i].kind==SHOW_UNIQUE && shows[i].opens==kind){any=1;if(career_show_done(i))return 1;}
    return !any;
}
int career_perf_unique(void) {
    int m=0;for(int i=0;i<nshows;i++)if(shows[i].kind==SHOW_UNIQUE && career_show_done(i))m|=shows[i].perf;
    return m;
}
/* A cover's photo pays SHOW_CASH (every showcase's +0x26) and the stage's
 * cash per star of the car's rating (PlayerCareerState::
 * NotifyShowcaseCompleted: 500 + stars x 100 from stage 2). */
enum { SHOW_CASH=500 };
CareerReward career_show_take(int i,const char *save_path) {
    CareerReward r={0};
    if(!career_show_open(i))return r;
    if(shows[i].kind==SHOW_DVD) {   /* the DVD's session: its cover */
        st.dvd_shot|=(uint16_t)(1u<<dvd_order(i));r.cover=2;st.dvds++;
    } else {
        uint32_t d=shows_done()|1u<<i;
        st.shows_lo[0]=(uint8_t)d;st.shows_lo[1]=(uint8_t)(d>>8);st.shows_hi[0]=(uint8_t)(d>>16);st.shows_hi[1]=(uint8_t)(d>>24);
        if(shows[i].kind==SHOW_MAGAZINE) {
            st.mags++;r.cover=1;
            const CareerCover *c=career_cover(shows[i].cover);if(c)r.cover_speech=c->speech;
        }
    }
    if(r.cover) {
        const CareerRule *u=career_rule(st.stage);
        r.money=SHOW_CASH+(u?u->star:100)*rating/10;st.money+=(unsigned)r.money;
    }
    r.stage_up=stage_check();   /* (a DVD shot can close the stage) */
    r.save=persist_progress(save_path);
    return r;
}
const CareerShop *career_shop(int i) { return i>=0 && i<nshops?&shops[i]:NULL; }
int career_count(void) { return nevents; }
const CareerEvent *career_event(int k) { return k>=0 && k<nevents?&events[k]:NULL; }
int career_find(int event_id) {
    if(engaged>=0 && engaged<nevents) {   /* the race engaged, or its tournament's course */
        if(events[engaged].id==event_id)return engaged;
        if(events[engaged].group)for(int k=0;k<nevents;k++)if(events[k].group==events[engaged].group && events[k].id==event_id)return k;
    }
    int first=-1,reached=-1,here=-1;
    for(int k=0;k<nevents;k++) {
        if(events[k].id!=event_id)continue;
        if(events[k].stage==st.stage){if(st.open[k])return k;if(here<0)here=k;}
        if(first<0)first=k;
        if(events[k].stage<st.stage)reached=k;   /* rows in stage order: the last is the highest */
    }
    return here>=0?here:reached>=0?reached:first;
}
static int suv_model(int car) {   /* the SUVs (their CarTypeInfo's flag, FECarRecord::IsSUV) */
    const FleetCar *f=fleet_car(car);
    return f && (!strcmp(f->model,"ESCALADE") || !strcmp(f->model,"HUMMER") || !strcmp(f->model,"NAVIGATOR"));
}
int career_owns_suv(void) {   /* PlayerCareerState::DoesPlayerOwnSUV */
    if(suv_model(st.car))return 1;
    for(int k=0;k<GARAGE_SLOTS && k<st.slots_open;k++)if(st.slot_car[k]!=SLOT_EMPTY && suv_model(st.slot_car[k]))return 1;
    return 0;
}
int career_shown(int k) {   /* PlayerCareerState::IsEventVisibleOnRadar */
    if(k<0 || k>=nevents || !st.open[k] || later(k))return 0;
    /* a hidden race (CareerEventData's flag at 0x7f): off the radar and the
     * world map until found, NotifyEventDiscovered putting it on the minimap.
     * The prologue's own too (4011, STAGE_0_CIRCUIT_0, in El Norte, beta
     * issue 111): its stage still ends without it, at the car lot
     * (CareerSpeech::HandleCareerEventEnd: DDAY_EVENT_A's end) */
    if(is_hidden(k) && !found(k))return 0;
    return !is_suv(k) || career_owns_suv();
}
/* IsEventVisibleOnRadar returns 0 for a hidden race before anything else:
 * NotifyEventDiscovered unlocks its zone (the street's) but the minimap
 * and the world map ask this first, so a hidden race is never on them
 * (beta issue 180: the prologue's 4011 came on the minimap once found). */
int career_on_radar(int k) { return career_shown(k) && !is_hidden(k); }
void career_spawn_set(const float pose[4],int shop) {
    if(!pose || !isfinite(pose[0]) || !isfinite(pose[1]) || !isfinite(pose[2]) || !isfinite(pose[3]))return;
    memcpy(st.spawn,pose,sizeof st.spawn);st.spawn_on=1;
    st.spawn_shop=(uint8_t)(shop>=0 && shop<nshops && shop<255?shop+1:0);
}
int career_spawn(float pose[4],int *shop) {
    if(shop)*shop=st.spawn_on && st.spawn_shop && st.spawn_shop-1<nshops?st.spawn_shop-1:-1;
    if(!st.spawn_on)return 0;
    if(pose)memcpy(pose,st.spawn,sizeof st.spawn);
    return 1;
}
/* A row's key in the save: its record and its place among the record's
 * courses (a tournament's later courses share its record), 0xc000 | place
 * << 8 | record; before version 13, the event id | its stage << 13 */
static int row_key(int k) {
    const CareerRecord *r=rec(k);if(!r)return events[k].id|events[k].stage<<13;
    int place=0;for(int j=k-1;j>=0 && later(j+1) && events[j].group==events[k].group;j--)place++;
    return 0xc000|(place&3)<<8|(r->record&255);
}
const CareerState *career_state(void) { return &st; }

/* A deleted message: read and out of the mailbox (sent clear), as the
 * original keeps it in its queue marked deleted and read: never sent again
 * (SMSMessageQueue::AddMessage skips one queued) nor counted. */
static int sms_bit(const uint8_t *bits,int i) { return bits[i>>3]>>(i&7)&1; }
static int sms_deleted(int i) { return !sms_bit(st.sms_sent,i) && sms_bit(st.sms_read,i); }
/* The messages sent since the career was loaded (RAM only): an autopopup's
 * not yet seen (SMSMessageQueue's +5 per queued message, AnyUnseenAutoPopups;
 * a save's older ones count as seen) */
static uint8_t sms_fresh[SMS_MAX/8];
static void sms_send(int i) {
    if(i<0 || i>=nsms || sms_deleted(i))return;
    if(!sms_bit(st.sms_sent,i))sms_fresh[i>>3]|=(uint8_t)(1<<(i&7));
    st.sms_sent[i>>3]|=(uint8_t)(1<<(i&7));
}
static void sms_type(int type) { for(int i=0;i<nsms;i++)if(sms[i].type==type)sms_send(i); }
static void pile_build(void);
static unsigned pile_seed=0x2545f491u;
static int storage_initialized,storage_writable=1;
int career_save_allowed(void) { return storage_writable; }
static void career_new(void) {
    memset(&st,0,sizeof(st));
    st.car=FLEET_RACHEL;st.slots_open=1;garage_stock_look(&st.look);perf_stock(&st.perf);
    st.sponsor=-1;st.heard=CAREER_HEARD_KNOWN;
    for(int k=0;k<GARAGE_SLOTS;k++)st.slot_car[k]=SLOT_EMPTY;
    st.slot_car[0]=FLEET_RACHEL;
    /* Rachel's welcome: the prologue sends SMS_INSTRUCTION (Stage0StateManager),
     * the first of the help messages; the others come with the free roam's
     * seconds (career_roam_tick) */
    int first=-1;for(int i=0;i<nsms;i++)if(sms[i].type==SMS_HELP && (first<0 || sms[i].cond<sms[first].cond))first=i;
    sms_send(first);
    pile_build();
    memset(sms_fresh,0,sizeof sms_fresh);
}
/* Do not discard a refused load or an outstanding savedata request to
 * start over. */
int career_restart_ready(void) {
    return storage_initialized && storage_writable && !persist_uncertain &&
           persist_result.status!=CAREER_SAVE_BUSY && savedata_idle();
}
int career_restart(void) {
    /* Called only after the menu's explicit confirmation. */
    if(!career_restart_ready())return 0;
    engaged=-1;rating=0;dvd_open=0;pile_seed=0x2545f491u;
    career_new();persist_loaded();persist_changed();
    return 1;   /* RAM only; the normal career save boundaries still own I/O. */
}
/* career.sav: 'N2SV', u32 version 16, u32 rows, CareerState, u16 key per
 * row (row_key: its career race's record and course), u32 checksum (state and keys).
 * The keys carry best[] over to a career whose rows changed. Version 13
 * has a row per career race of the original; versions 9-12 had one per
 * event and stage, keyed by the event id | its stage << 13 (their results
 * go to the first race of that stage on that course; the races on offer
 * are drawn again); before version 9, 128 rows, one per event, keyed by
 * the id alone (its first stage's). The closed tracks' came in with
 * version 5. Older saves load the state they had: version 1 (up to
 * open[]) in Rachel's car, version 2 (up to the garage slots) with a stock
 * look, version 3 (up to the look) with stock performance, versions 4-5
 * (up to the performance) at the highest stage they raced in, with no
 * sponsor; versions 1-4 were the city's events alone, in today's order.
 * Versions 1-6 are prefixes of version 6's state (StateV6), whose looks had
 * no decals; version 7's looks had one per zone (its first slot now).
 * Version 10's performance holds the parts fitted one by one (CarPerf:
 * versions 1-9 had its first 16 bytes, CarPerfV9). Version 11's looks end
 * with the vinyl layers (versions 8-10: LookV8, its first 71 bytes).
 * Version 12 adds the DVD covers' sessions (dvd_shot), version 13 the
 * hidden races found and the free roam's clock, version 14 the career's
 * clock of play and Rachel's garage-menu lines by it, version 15 the free
 * roam's place to start again (spawn), version 16 the shops entered: the
 * state of version 11 to 15 is its
 * first bytes (a version 14 save, the 0.2.6 to 0.3.1 betas', has no place:
 * spawn_on 0). */
typedef struct { uint8_t paint,vinyl,vcol[3],opt[GPARTS],neon,tint,version; } LookV6;
typedef struct { LookV6 v6;uint8_t decal[DECAL_ZONES]; } LookV7;
typedef struct { LookV6 v6;uint8_t decal[DECAL_ZONES][DECAL_SLOTS],decal_layout; } LookV8;
enum { CAREER_OLD=128 };   /* rows of versions 1-8 */
/* Versions 1-9 kept the performance in 16 bytes (CarPerfV9: no parts). */
#define OLD_STATE(Look,Perf,ROWS,MORE) struct { \
    unsigned money,rep,races;uint8_t best[ROWS],open[ROWS];int8_t car;uint8_t slots_open,pad[2]; \
    Look look;Perf perf; \
    uint8_t stage;int8_t sponsor;uint8_t won_url,won_choice,sponsor_done,dvds,mags,outruns; \
    uint16_t stage_races,prev_avg_rep;uint32_t stage_rep;uint8_t sms_sent[32],sms_read[32];uint32_t shops_seen; \
    int8_t slot_car[5];uint8_t slot_active,pad6[2];Look slot_look[5];Perf slot_perf[5];MORE }
typedef OLD_STATE(LookV6,CarPerfV9,CAREER_OLD,) StateV6;
typedef OLD_STATE(LookV7,CarPerfV9,CAREER_OLD,) StateV7;
typedef OLD_STATE(LookV8,CarPerfV9,CAREER_OLD,) StateV8;
typedef OLD_STATE(LookV8,CarPerfV9,CAREER_MAX,uint32_t spent;uint8_t cash_taken[8];) StateV9;
typedef OLD_STATE(LookV8,CarPerf,CAREER_MAX,uint32_t spent;uint8_t cash_taken[8];) StateV10;
enum { STATE_V1=3*4+2*CAREER_OLD, STATE_V2=STATE_V1+4, STATE_V3=STATE_V2+sizeof(LookV6), STATE_V5=STATE_V3+sizeof(CarPerfV9) };
static void look_v6(CarLook *l,const LookV6 *o) { memset(l,0,sizeof *l);memcpy(l,o,sizeof *o); }   /* the same first bytes */
static void look_v7(CarLook *l,const LookV7 *o) { look_v6(l,&o->v6);for(int z=0;z<DECAL_ZONES;z++)l->decal[z][0]=o->decal[z]; }
static void look_v8(CarLook *l,const LookV8 *o) { memset(l,0,sizeof *l);memcpy(l,o,sizeof *o); }   /* the same first bytes */
/* The fields of an older state (its first size bytes) over s. */
#define FROM_OLD(name,T,LOOK) static void name(CareerState *s,const T *o,size_t size) { \
    COPY(money);COPY(rep);COPY(races);COPY(best);COPY(open);COPY(car);COPY(slots_open); \
    if(offsetof(T,look)+sizeof o->look<=size){LOOK(&s->look,&o->look);}COPY(perf); \
    COPY(stage);COPY(sponsor);COPY(won_url);COPY(won_choice);COPY(sponsor_done);COPY(dvds);COPY(mags);COPY(outruns); \
    COPY(stage_races);COPY(prev_avg_rep);COPY(stage_rep);COPY(sms_sent);COPY(sms_read);COPY(shops_seen); \
    COPY(slot_car);COPY(slot_active); \
    for(int k=0;k<5;k++)if(offsetof(T,slot_look)+(k+1)*sizeof o->slot_look[0]<=size){LOOK(&s->slot_look[k],&o->slot_look[k]);} \
    for(int k=0;k<5;k++)if(offsetof(T,slot_perf)+(k+1)*sizeof o->slot_perf[0]<=size)memcpy(&s->slot_perf[k],&o->slot_perf[k],sizeof o->slot_perf[0]); }   /* the 16 bytes before the parts */
#define COPY(f) do{if(offsetof(__typeof__(*o),f)+sizeof o->f<=size)memcpy(&s->f,&o->f,sizeof o->f);}while(0)
FROM_OLD(state_from_v6,StateV6,look_v6)
FROM_OLD(state_from_v7,StateV7,look_v7)
FROM_OLD(state_from_v8,StateV8,look_v8)
FROM_OLD(state_from_v9_,StateV9,look_v8)
FROM_OLD(state_from_v10_,StateV10,look_v8)
static void state_from_v9(CareerState *s,const StateV9 *o,size_t size) {
    state_from_v9_(s,o,size);
    s->spent=o->spent;memcpy(s->cash_taken,o->cash_taken,sizeof s->cash_taken);
}
static void state_from_v10(CareerState *s,const StateV10 *o,size_t size) {
    state_from_v10_(s,o,size);
    s->spent=o->spent;memcpy(s->cash_taken,o->cash_taken,sizeof s->cash_taken);
}
#undef COPY
static uint32_t sum(const void *p,size_t n) {
    const uint8_t *b=p;uint32_t h=2166136261u;
    for(size_t i=0;i<n;i++){h^=b[i];h*=16777619u;}
    return h;
}
static uint32_t sum_ids(uint32_t h,const uint16_t *ids,int n) {
    const uint8_t *b=(const uint8_t *)ids;
    for(int i=0;i<2*n;i++){h^=b[i];h*=16777619u;}
    return h;
}
/* The save in memory (savedata.c writes and reads it whole). */
enum { SAVE_MAX=12+sizeof(CareerState)+2*CAREER_MAX+4 };
static uint8_t blob[SAVE_MAX];
typedef struct { const uint8_t *p;int left; } Cursor;
static int get(Cursor *c,void *to,size_t n) {
    if(n>(size_t)c->left)return 0;
    memcpy(to,c->p,n);c->p+=n;c->left-=(int)n;return 1;
}
int career_load(const char *path) {
    int got=savedata_read(path,blob,SAVE_MAX);
    if(got<=0) {
        if(savedata_last_error()!=0) { /* cancellation may be positive */
            if(!storage_initialized){career_new();storage_initialized=1;}
            storage_writable=0;return -1;
        }
        career_new();storage_initialized=storage_writable=1;persist_loaded();return 0; /* absent */
    }
    CareerState before=st;unsigned seed=pile_seed;int had_state=storage_initialized;
    career_new();
    Cursor c={blob,got};
    char magic[4];uint32_t version=0,n=0,check=0;CareerState s=st;uint16_t ids[CAREER_MAX];
    int ok=get(&c,magic,4) && !memcmp(magic,"N2SV",4) && get(&c,&version,4) && version>=1 && version<=16 &&
           get(&c,&n,4) && n<=(version<9?CAREER_OLD:CAREER_MAX);
    if(!ok)goto refused; /* do not inspect an unparsed version/layout */
    size_t size=version==1?STATE_V1:version==2?STATE_V2:version==3?STATE_V3:version<=5?STATE_V5:version==6?sizeof(StateV6):
                version==7?sizeof(StateV7):version==8?sizeof(StateV8):version==9?sizeof(StateV9):version==10?sizeof(StateV10):version==11?offsetof(CareerState,dvd_shot):version==12?offsetof(CareerState,found):version==13?offsetof(CareerState,played):version==14?offsetof(CareerState,spawn):version==15?offsetof(CareerState,shops_entered):sizeof(s);
    static union { StateV6 v6;StateV7 v7;StateV8 v8;StateV9 v9;StateV10 v10; } old;   /* static: the state is large for the stack */
    void *into=version<11?(void *)&old:(void *)&s;
    ok=ok && get(&c,into,size);
    if(ok && version>=5)ok=get(&c,ids,2*n) && get(&c,&check,4) && check==sum_ids(sum(into,size),ids,(int)n);
    else if(ok) {   /* the city's events, in order: today's without the tracks' */
        ok=get(&c,&check,4) && check==sum(into,size);
        int m=0;
        for(int k=0;k<nevents;k++) {   /* each event once (its first row) */
            int again=0;for(int j=0;j<k;j++)again|=events[j].id==events[k].id;
            if(!again && !(events[k].flags&CAREER_TRACK) && m<CAREER_MAX)ids[m++]=events[k].id;
        }
        ok=ok && m==(int)n;
    }
    if(!ok)goto refused;
    if(ok && version<7)state_from_v6(&s,&old.v6,size);
    else if(ok && version==7)state_from_v7(&s,&old.v7,size);
    else if(ok && version==8)state_from_v8(&s,&old.v8,size);
    else if(ok && version==9)state_from_v9(&s,&old.v9,size);
    else if(ok && version==10)state_from_v10(&s,&old.v10,size);
    if(ok && version<9)   /* the id alone: the event's first stage */
        for(uint32_t j=0;j<n;j++)for(int k=0;k<nevents;k++)if(events[k].id==ids[j]){ids[j]=(uint16_t)(events[k].id|events[k].stage<<13);break;}
    if(ok) {
        CareerState t=s;
        memset(t.best,0,sizeof t.best);memset(t.open,0,sizeof t.open);
        if(version<13)memset(t.found,0,sizeof t.found);
        for(uint32_t j=0;j<n;j++) {
            int to=-1;
            if(ids[j]>=0xc000){for(int k=0;k<nevents;k++)if(row_key(k)==ids[j]){to=k;break;}}
            else for(int k=0;k<nevents;k++)   /* (event, stage): its first race there not yet given a result */
                if(events[k].id==(ids[j]&0x1fff) && events[k].stage==ids[j]>>13 && !later(k) && !t.best[k]){to=k;break;}
            if(to>=0)t.best[to]=s.best[j];
        }
        st=t;
        if(version<6) {   /* before the stages: the highest one raced in, a sponsor to pick, the car in slot 0 */
            for(int k=0;k<GARAGE_SLOTS;k++)st.slot_car[k]=SLOT_EMPTY;
            st.slot_active=0;st.slot_car[0]=st.car;
            int top=career_top_stage();st.stage=(uint8_t)(top<0?0:top);st.sponsor=-1;
            memset(st.sms_sent,0,sizeof st.sms_sent);memset(st.sms_read,0,sizeof st.sms_read);
            int first=-1;for(int i=0;i<nsms;i++)if(sms[i].type==SMS_HELP && (first<0 || sms[i].cond<sms[first].cond))first=i;
            sms_send(first);
        }
    }
    if(ok && (st.car<FLEET_RACHEL || st.car>=fleet_count()))st.car=FLEET_RACHEL;
    if(!st.slots_open)st.slots_open=1;
    if(!st.look.version)garage_stock_look(&st.look);
    /* the paints of the first palette, the one vinyl of version 2's looks */
    {const FleetCar *f=fleet_car(st.car);garage_look_upgrade(&st.look,f?f->model:NULL);}
    for(int k=0;k<GARAGE_SLOTS;k++){const FleetCar *f=fleet_car(st.slot_car[k]);garage_look_upgrade(&st.slot_look[k],f?f->model:NULL);}
    if(!st.perf.version)perf_stock(&st.perf);
    st.dvds=(uint8_t)dvds_shot(st.stage,NULL);   /* this stage's DVD covers shot (before version 13: won) */
    pile_build();   /* the races on offer: not saved, drawn again (as the original's pile) */
    storage_initialized=storage_writable=1;persist_loaded();
    memset(sms_fresh,0,sizeof sms_fresh);   /* (a save's messages: seen) */
    if(ok && savedata_from_legacy())career_save(path);   /* career.sav from before the savedata: written back as one */
    return 1;
refused:
    if(had_state){memcpy(&st,&before,sizeof st);pile_seed=seed;}
    storage_initialized=1;storage_writable=0;return -1;
}
/* a car fresh from the lot: the stock look, in its stock paint (the
 * game's: its record's BASE_PAINT, fleet_stock_paint) */
static void stock_look(int car) {
    garage_stock_look(&st.look);
    st.look.paint=(uint8_t)fleet_stock_paint(car);
}
static void slot_sync(void) {   /* the active car into its slot */
    if(st.slot_active>=GARAGE_SLOTS)st.slot_active=0;
    st.slot_car[st.slot_active]=st.car;st.slot_look[st.slot_active]=st.look;st.slot_perf[st.slot_active]=st.perf;
}
int career_slot_of(int car) { slot_sync();for(int k=0;k<GARAGE_SLOTS;k++)if(st.slot_car[k]==car)return k;return -1; }
int career_free_slot(void) { slot_sync();for(int k=0;k<st.slots_open && k<GARAGE_SLOTS;k++)if(st.slot_car[k]==SLOT_EMPTY)return k;return -1; }
int career_add_car(int car,const char *save_path) {
    int k=career_free_slot();if(k<0)return -1;
    st.slot_active=(uint8_t)k;st.car=(int8_t)car;stock_look(car);perf_stock(&st.perf);slot_sync();
    if(save_path)career_save(save_path);
    return k;
}
void career_switch_car(int k,const char *save_path) {
    slot_sync();if(k<0 || k>=GARAGE_SLOTS || st.slot_car[k]==SLOT_EMPTY)return;
    st.slot_active=(uint8_t)k;st.car=st.slot_car[k];st.look=st.slot_look[k];st.perf=st.slot_perf[k];
    if(!st.look.version)garage_stock_look(&st.look);
    if(!st.perf.version)perf_stock(&st.perf);
    if(save_path)career_save(save_path);
}
/* The state as career.sav's bytes into blob, its line for the XMB: the size. */
static int serialize(char *detail,size_t nd) {
    uint16_t ids[CAREER_MAX];for(int k=0;k<nevents;k++)ids[k]=(uint16_t)row_key(k);
    uint32_t version=16,n=(uint32_t)nevents,check=sum_ids(sum(&st,sizeof(st)),ids,nevents);
    uint8_t *o=blob;
    memcpy(o,"N2SV",4);o+=4;memcpy(o,&version,4);o+=4;memcpy(o,&n,4);o+=4;
    memcpy(o,&st,sizeof st);o+=sizeof st;memcpy(o,ids,2*n);o+=2*n;memcpy(o,&check,4);o+=4;
    if(st.stage>=STAGE_N)snprintf(detail,nd,"Career complete   $%u   REP %u",st.money,st.rep);
    else snprintf(detail,nd,"Stage %d of %d   $%u   REP %u   %u races",st.stage+1,STAGE_N,st.money,st.rep,st.races);
    return (int)(o-blob);
}
/* A new profile's career into a slot the player picks on the system's list
 * (beta #136: the original's new profile is a save of its own, the others
 * kept): a fresh career's bytes, the one in RAM untouched; *slot the one
 * written. 1 saved, 0 cancelled, -1 an error. career_restart follows it. */
int career_save_fresh_list(const char *path,int *slot) {
    if(!career_restart_ready())return -1;
    static CareerState keep;keep=st;unsigned seed=pile_seed;   /* static: the state is large for the stack */
    career_new();
    char detail[96];int size=serialize(detail,sizeof detail);
    st=keep;pile_seed=seed;
    return savedata_list_save(path,blob,size,detail,slot);
}
static int career_save_mode(const char *path,int manual) {
    persist_changed();
    if(!storage_writable) {   /* No backend request after a refused load. Error may be zero for a bad codec. */
        persist_result=(CareerSaveResult){CAREER_SAVE_BLOCKED,savedata_last_error()};return 0;
    }
    if(!manual && async_mode && defer_mode){deferred=1;deferred_path=path;defer_held++;return 1;}   /* written at the street's next stop (career_save_service) */
    deferred=0;   /* (this write carries the held state too) */
    slot_sync();
    char detail[96];uint8_t *o=blob+serialize(detail,sizeof detail);
    if(!manual && async_mode) {   /* on the street: the utility left running, the game goes on (career_save_service) */
        async_path=path;
        if(async_busy){async_again=1;return 1;}   /* one runs: this state follows it */
        if(savedata_write_async(path,blob,(int)(o-blob),detail)){async_busy=1;async_gen=change_gen;return 1;}
    }
    int ok=manual?savedata_write_manual(path,blob,(int)(o-blob),detail):savedata_write(path,blob,(int)(o-blob),detail);
    int error=savedata_last_error();
    if(ok==1 && !error) {
        if(savedata_career_is_temporary())persist_result=(CareerSaveResult){CAREER_SAVE_TEMPORARY,0};
        else{persist_result=(CareerSaveResult){CAREER_SAVE_SAVED,0};persist_dirty=persist_uncertain=0;}
    } else {
        int status=error==-EBUSY?CAREER_SAVE_BUSY:error==-ETIMEDOUT?CAREER_SAVE_UNKNOWN:CAREER_SAVE_ERROR;
        persist_result=(CareerSaveResult){status,error};
        if(status==CAREER_SAVE_UNKNOWN)persist_uncertain=1;
    }
    return ok==1 && !error;   /* TEMPORARY is an acknowledgment, never a durable-save claim. */
}
int career_save(const char *path) { return career_save_mode(path,0); }
void career_save_async(int on) { async_mode=on; }
void career_save_defer(int on) { defer_mode=on; }
void career_save_defer_stats(unsigned *held,unsigned *written) { *held=defer_held;*written=defer_written; }
int career_save_busy(void) { return async_busy || async_again || deferred; }
void career_save_service(void) {
    if(deferred && !(defer_mode && async_mode) && !async_busy && !async_again){defer_written++;career_save_mode(deferred_path,0);}   /* the held one, the newest state */
    if(!async_busy)return;
    if(savedata_async_poll()!=1)return;
    async_busy=0;int error=savedata_last_error();
    if(!error) {
        if(savedata_career_is_temporary())persist_result=(CareerSaveResult){CAREER_SAVE_TEMPORARY,0};
        else if(async_gen==change_gen){persist_result=(CareerSaveResult){CAREER_SAVE_SAVED,0};persist_dirty=persist_uncertain=0;}
    } else {
        int status=error==-EBUSY?CAREER_SAVE_BUSY:error==-ETIMEDOUT?CAREER_SAVE_UNKNOWN:CAREER_SAVE_ERROR;
        persist_result=(CareerSaveResult){status,error};
        if(status==CAREER_SAVE_UNKNOWN)persist_uncertain=1;
    }
    if(async_again && async_path){async_again=0;career_save_mode(async_path,0);}   /* the newest state, now */
}
int career_save_manual(const char *path) { return career_save_mode(path,1); }
int career_finished_in(int stage) { int n=0;for(int k=0;k<nevents;k++)n+=events[k].stage==stage && st.best[k];return n; }
int career_top_stage(void) { int s=-1;for(int k=0;k<nevents;k++)if(st.best[k] && events[k].stage>s)s=events[k].stage;return s; }
int career_car_open(int i) {
    const FleetCar *c=fleet_car(i);if(!c)return 0;
    if((c->flags&FLEET_OPEN) || career_top_stage()>c->stage)return 1;
    if(c->flags&FLEET_BY_RACE) {   /* the game's rule: its unlocking race won */
        for(int k=0,n=0;k<nevents;k++)if(events[k].stage==c->stage && ++n==c->need)return st.best[k]==1;
        return 0;
    }
    return career_finished_in(c->stage)>=c->need;
}
int career_started(void) {
    return st.started || st.stage>0 || career_finished_in(0) || st.car!=FLEET_RACHEL || st.roam>0;
}
void career_set_started(const char *save_path) {
    if(st.started)return;
    st.started=1;
    if(save_path)career_save(save_path);
}
int career_heard(int what) {
    if(st.heard&what)return 1;
    return !(st.heard&CAREER_HEARD_KNOWN) && (st.car!=FLEET_RACHEL || st.stage>0);
}
void career_set_heard(int what) { st.heard|=(uint8_t)what; }
enum { FE_AGAIN=3600 };   /* s of play between a screen's lines (CareerSpeech::HandleFESpeechEvent) */
void career_play_tick(float dt) { if(dt>0)st.played+=dt; }
int career_fe_said_lately(int i) {
    if(i<0 || i>=CAREER_FE_SAYS || st.fe_said_at[i]<=0)return 0;
    return st.played-st.fe_said_at[i]<=FE_AGAIN;
}
void career_fe_said(int i) { if(i>=0 && i<CAREER_FE_SAYS)st.fe_said_at[i]=st.played>0?st.played:1e-3f; }
int career_events_seen(void) {
    if(st.stage>0 || (st.heard&CAREER_HEARD_EVENTS))return 1;
    for(int k=0;k<nevents;k++)if(events[k].stage==0 && st.best[k])return 1;
    return 0;
}
void career_see_events(void) { if(!(st.heard&CAREER_HEARD_EVENTS)){st.heard|=CAREER_HEARD_EVENTS;pile_build();} }
/* The prologue's races all run (won or not): the original's stage 0
 * timeout, a second after the last one ends (CareerSpeech's Time 8,
 * NotifyCSStage0Timeout: PlayerCareerState +0x7e30). From it the prologue
 * offers no race (BuildListOfEvents) and wants a car from the lot; the
 * save's places carry it, as the original's flag. */
static int prologue_over(void) {
    int prologue=0;for(int k=0;k<nevents;k++)if(events[k].stage==0){prologue++;if(!st.best[k])return 0;}
    return prologue>0;
}
int career_must_pick_car(void) { return st.car==FLEET_RACHEL && prologue_over(); }
static int stage_check(void);
void career_set_car(int i,const char *save_path) { st.car=(int8_t)i;stock_look(i);perf_stock(&st.perf);stage_check();if(save_path)career_save(save_path); }
int career_buy_perf(const CarPerf *perf,int cost,const char *save_path) {
    if(cost>0 && st.money<(unsigned)cost)return -1;
    st.money-=(unsigned)(cost>0?cost:0);st.spent+=(unsigned)(cost>0?cost:0);st.perf=*perf;
    if(save_path)career_save(save_path);
    return 0;
}
void career_test_perf(const CarPerf *perf) { st.perf=*perf; }
void career_mark(int event_id,int place,const char *save_path) {
    int k=career_find(event_id);if(k<0 || place<1)return;
    if(!st.best[k] || place<st.best[k])st.best[k]=(uint8_t)place;
    if(save_path)career_save(save_path);
}
void career_bonus(int money,int rep,const char *save_path) {
    if(money<0)st.money-=(unsigned)-money>st.money?st.money:(unsigned)-money;else st.money+=(unsigned)money;
    st.rep+=(unsigned)(rep>0?rep:0);
    if(save_path)career_save(save_path);
}
void career_test_setup(int car,unsigned money) { st.car=(int8_t)car;stock_look(car);perf_stock(&st.perf);st.money=money; }
int career_buy_look(const CarLook *look,int cost,const char *save_path) {
    if(cost>0 && st.money<(unsigned)cost)return -1;
    st.money-=(unsigned)(cost>0?cost:0);st.spent+=(unsigned)(cost>0?cost:0);st.look=*look;
    if(save_path)career_save(save_path);
    return 0;
}
/* ---- the races on offer (the GameCube's EventPile, PlayerCareerState::
 * BuildListOfEvents): open[] ---- */
/* RuntimeCareerStageInfo::CanDoURL: a URL tournament once the stage's world
 * races won reach its count; the stage's final once its counts of world,
 * sponsor and URL races won are reached and the stage's DVD covers shot */
static int can_do(int k) {
    const CareerRecord *r=rec(k);if(!r)return 1;
    int s=events[k].stage,all,shot=dvds_shot(s,&all);
    if(r->req==3)return stage_won(s,3)>=(int)(r->need>>16);
    if(r->req==4)return shot>=all && stage_won(s,3)>=(int)(r->need>>8&255) && stage_won(s,1)>=(int)(r->need>>16&255) &&
                        stage_won(s,2)>=(int)(r->need>>24);
    return 0;
}
/* A kind of the pile (EventPile::MatchesType): 0-5 the world's races by
 * race type (RMODE_ order), 6 the SUV races, 7-12 a sponsor's by race type. */
static int matches(int k,int what) {
    if(what==6)return is_suv(k);
    if(what>6)return events[k].reward==1 && events[k].type==what-7;
    return events[k].reward==3 && !is_suv(k) && events[k].type==what;
}
static int on_course(int id) { for(int k=0;k<nevents;k++)if(st.open[k] && events[k].id==id)return 1;return 0; }
/* A course once on the map: only for a race without its own trigger zone
 * (a career.n2cr before version 12, whose markers were the courses'
 * starts). The original's pile has no such rule (EventPile::GetRandomEvent,
 * BuildListOfEvents' hidden and URL loops): with it, a hidden race on offer
 * kept the world race of its course (stage 5's S5_H_CIRCUIT_35 and
 * S5_CIRCUIT_30, both on 4086) off the map until found, and the 35 world
 * wins of stage 5's final could not be reached (beta issue 204). */
static int course_taken(int k) { return !career_zone(k) && on_course(events[k].id); }
static int pile_count(int what) { int n=0;for(int k=0;k<nevents;k++)n+=st.open[k] && matches(k,what);return n; }
/* how many of a kind at most (EventPile::GetMaxNumberOfEventsByType): the
 * stage's count; the SUV races' with an SUV; a sponsor's, its contract's
 * races of that type not yet won (GetMaxSponsoredEventTypes) */
static int pile_max(int what,int suv) {
    const CareerRule *u=career_rule(st.stage);if(!u)return 0;
    if(what<6)return u->cap[what];
    if(what==6)return suv?u->suv:0;
    const CareerSponsor *sp=career_sponsor(st.sponsor);if(!sp)return 0;
    int t=what-7,n=0;for(int i=0;i<3;i++)n+=sp->types[i]==t;
    if(n)for(int k=0;k<nevents;k++)n-=events[k].stage==st.stage && !later(k) && events[k].reward==1 && events[k].type==t && won(k);
    return n;
}

/* The prologue (stage 0) draws none before its first call (+0x7e34), after
 * its timeout (+0x7e30), nor while its shop event is DDAY_EVENT_B (+0x7e24,
 * 0xdd60e403): the garage's visit the car lot sets when the first car is
 * taken (NotifyEnterShopFromFreeRoam). The port's stage 0 in a car of its
 * own is that state: Rachel's 350Z went back at the lot (beta issue 180). */
static void pile_build(void) {
    if(!nevents)return;
    static uint8_t was[CAREER_MAX];memcpy(was,st.open,sizeof was);memset(st.open,0,sizeof st.open);
    if(st.stage>=STAGE_N)return;   /* the career done: none */
    if(!career_events_seen() || (st.stage==0 && (prologue_over() || st.car!=FLEET_RACHEL)))return;   /* the prologue before its first call, after its races, or its car lot visited: none */
    const CareerRule *u=career_rule(st.stage);
    if(!nrecords || !u) {   /* a career.n2cr before version 9: the stages reached */
        for(int k=0;k<nevents;k++)st.open[k]=events[k].stage<=st.stage && !later(k);
        return;
    }
    int suv=suv_model(st.car),f=u->final;
    /* the end of stage 5: the race the final follows won, the final alone */
    if(f!=0xffff && rec(f)->after!=0xffff){if(won(rec(f)->after) && !won(f)){st.open[f]=1;return;}}
    /* the ones still on offer stay */
    for(int k=0;k<nevents;k++)if(was[k] && events[k].stage==st.stage && !later(k) && !won(k) && (!is_suv(k) || suv))st.open[k]=1;
    /* each kind up to its count, drawn among the stage's not won (EventPile::
     * GetRandomEvent: not the URL tournaments, the hidden races nor the
     * final); a course once only without the races' own zones
     * (course_taken). The sponsor's kinds first:
     * its contract's races share courses with the world's (stage 2's
     * EUROLITE drifts 4311-4313, the world's 4312-4314), and drawn after
     * them, a pile drawn afresh (a load: open[] is not kept) left the
     * contract's races off the map (beta issue 16) */
    for(int at=0;at<13;at++) {
        int what=at<6?at+7:at-6;   /* 7-12, then 0-6 */
        if(what==3 || what==10)continue;   /* (no world or sponsor URL type) */
        for(int max=pile_max(what,suv);pile_count(what)<max;) {
            int cand[CAREER_MAX],n=0;
            for(int k=0;k<nevents;k++)
                if(events[k].stage==st.stage && !later(k) && !st.open[k] && events[k].reward!=2 && !is_hidden(k) && !won(k) && k!=f &&
                   matches(k,what) && (!is_suv(k) || suv) && !course_taken(k))cand[n++]=k;
            if(!n)break;
            pile_seed=pile_seed*1664525u+1013904223u;st.open[cand[(pile_seed>>8)%(unsigned)n]]=1;
        }
    }
    /* the hidden races, up to the stage's count (-1 all), off the map until found */
    int hidden=0;for(int k=0;k<nevents;k++)hidden+=st.open[k] && is_hidden(k);
    for(int k=0;k<nevents && (u->hidden<0 || hidden<u->hidden);k++)
        if(events[k].stage==st.stage && !later(k) && is_hidden(k) && !won(k) && !st.open[k] && !course_taken(k)){st.open[k]=1;hidden++;}
    /* the URL tournaments (and the stage's final) their counts reached */
    for(int k=0;k<nevents;k++)
        if(events[k].stage==st.stage && !later(k) && events[k].reward==2 && !won(k) && !st.open[k] && can_do(k) && !course_taken(k))st.open[k]=1;
}
void career_engage(int k,const char *save_path) {
    if(k<0 || k>=nevents)return;
    engaged=k;
    if(is_hidden(k) && !found(k)){st.found[k>>3]|=(uint8_t)(1<<(k&7));if(save_path)career_save(save_path);}   /* NotifyEventDiscovered */
}
int career_rivals(int k) { const CareerRecord *r=rec(k);return r?r->rivals:0; }
const char *career_rival(int k,int i) { const CareerRecord *r=rec(k);return r && i>=0 && i<r->rivals && r->rival[i]?career_text(r->rival[i]):NULL; }
/* RuntimeCareerStageInfo::GetAIPerformanceMatchingParameter: the stage's
 * rating, from its start to its end as the stage's world, sponsor and URL
 * races are won (the prologue's and stage 1's: the races finished) */
static float ramp_rating(void) {
    if(!nramps)return 0;
    if(st.stage>=nramps)return ramps[nramps-1].end;   /* the career done: the last stage's end */
    const Ramp *p=&ramps[st.stage];CareerContract c;career_contract(&c);
    int done=st.stage<2?c.finish:c.url+c.choice+c.sponsor;
    if(p->races>0 && done<=p->races)return p->start+(p->end-p->start)*(float)done/(float)p->races;
    return p->end;
}
int career_rival_perf(int k,int i,float *rating) {
    if(k<0 || k>=nrival_perf || i<0 || i>=5)return -1;
    int m=rival_perf[k].mode[i];
    if(rating)*rating=m==3?ramp_rating():m==0?(float)rival_perf[k].rating[i]:0;
    return m;
}
const char *career_rival_preset(int k,int i,const char **model,const char **neon) {
    if(k<0 || k>=nrival_car || i<0 || i>=5 || rival_car[k].preset[i]>=npresets)return NULL;
    const RivalPreset *p=&presets[rival_car[k].preset[i]];
    if(model)*model=p->model;
    if(neon)*neon=p->neon;
    return p->pack;
}
const float *career_zone(int k) { return k>=0 && k<nzones && zones[k].name?zones[k].at:NULL; }
/* ---- the stages: sponsors, covers ---- */
static void unlock_sms(void) {   /* the shops', the areas' and the cars' news: once the stage's count reaches the message's
                                     (default.xbe 0x112010: type 12 its world races won, 10 its URL races, 9 its sponsor's races) */
    for(int i=0;i<nsms;i++) {
        int t=sms[i].type;if(t!=9 && t!=10 && t!=12)continue;
        if(career_part_open((t==12?2:t==10?3:4)<<12 | (sms[i].stage&15)<<8 | (sms[i].cond>255?255:sms[i].cond)))sms_send(i);
    }
    for(int i=0;i<nsms;i++)   /* a showcase's news as it opens (type 13: cond its showcase, the game's 0x140390) */
        if(sms[i].type==13 && sms[i].cond<nshows && (career_show_open(sms[i].cond) || career_show_done(sms[i].cond)))sms_send(i);
}
void career_contract(CareerContract *c) {
    memset(c,0,sizeof *c);
    const CareerStage *s=career_stage_info(st.stage);if(!s)return;
    if(st.stage<2) {   /* the prologue's races finished; stage 1's won */
        c->finish_need=s->finish;
        for(int k=0;k<nevents;k++)c->finish+=events[k].stage==st.stage && !later(k) && (st.stage?won(k):st.best[k]!=0);
        return;
    }
    const CareerRule *u=career_rule(st.stage);
    c->url=stage_won(st.stage,2);c->url_need=s->url;c->choice=stage_won(st.stage,3);c->choice_need=s->choice;
    c->dvd=dvds_shot(st.stage,&c->dvd_need);
    c->sponsor=stage_won(st.stage,1);c->sponsor_need=u?u->sponsor_races:3;
}
static int stage_advance(void) {
    st.prev_avg_rep=(uint16_t)(st.stage_races?st.stage_rep/st.stage_races:0);
    st.stage++;st.sponsor=-1;st.won_url=st.won_choice=st.sponsor_done=st.dvds=st.mags=st.outruns=0;st.stage_races=0;st.stage_rep=0;
    pile_build();unlock_sms();
    return st.stage>=STAGE_N?2:1;   /* 2: the career is done */
}
/* RuntimeCareerStageInfo::CheckIfClearedStage: the stage's final won and
 * its DVD covers shot; stage 1 (no final) every race of it won. Stage 0's
 * final is DDAY_EVENT_B, the CRIB_1 visit, not its three optional races
 * (both supported discs' 0x34A18 +0x28). Only the street shop entry below
 * can complete it; buying a car or finishing a race cannot. */
static int stage_check(void) {
    if(st.stage==0 || st.stage>=STAGE_N)return 0;
    const CareerRule *u=career_rule(st.stage);int done;
    if(u && u->final!=0xffff){int all,shot=dvds_shot(st.stage,&all);done=won(u->final) && shot>=all;}
    else{done=1;for(int k=0;k<nevents;k++)if(events[k].stage==st.stage && !later(k) && !won(k))done=0;}
    return done?stage_advance():0;
}
/* The lot leaves for free roam only after its acquisition is saved. The
 * existing car/slot operations also touch stage news and the event pile;
 * retain those changes only on success. No new heap or persistent buffer. */
int career_lot_car(int car,int action,const char *save_path) {
    if(!save_path || !fleet_car(car) || car<0 || car>127 ||
       action<CAREER_LOT_TRADE || action>CAREER_LOT_USE ||
       st.slots_open<1 || st.slots_open>GARAGE_SLOTS || st.slot_active>=st.slots_open)return -1;
    int owned=-1;
    for(int k=0;k<st.slots_open;k++)if(st.slot_car[k]==car || (k==st.slot_active && st.car==car)){owned=k;break;}
    if(action==CAREER_LOT_USE) { if(owned<0)return -1; }
    else if(owned>=0 || !career_car_open(car))return -1;
    CareerState before;memcpy(&before,&st,sizeof before);
    unsigned seed=pile_seed;
    int slot;
    if(action==CAREER_LOT_ADD)slot=career_add_car(car,NULL);
    else if(action==CAREER_LOT_USE){career_switch_car(owned,NULL);slot=owned;}
    else{career_set_car(car,NULL);slot=st.slot_active;}
    if(slot<0 || !career_save(save_path)) {
        memcpy(&st,&before,sizeof st);pile_seed=seed;
        return slot<0?-1:-2;
    }
    return slot;
}
/* RuntimeCareerStageInfo::NeedToAskForSponsor: none signed this stage and
 * a sponsor's world races won reached (the stage's +0x1c, a short per
 * sponsor: 1 for every sponsor of stages 2-5 in the retail data). The
 * sponsor comes with the stage's first world race won, not as the stage
 * opens (beta issue 202). */
enum { SPONSOR_ASK_WINS=1 };
static int sponsor_due(void) { return st.stage>=2 && st.stage<STAGE_N && st.sponsor<0 && stages[st.stage].nsponsors>0; }   /* a contract stage with none signed (RuntimeCareerStageInfo +0xe8) */
int career_needs_sponsor(void) { return sponsor_due() && stage_wins()>=SPONSOR_ASK_WINS; }
int career_done(void) { return st.stage>=STAGE_N; }
int career_available(int k) { return k>=0 && k<nevents && events[k].stage<=st.stage; }
void career_set_rating(int r10) { rating=r10; }
int career_free_roam(int r10,const char *save_path) {
    rating=r10;
    /* the stage's DVD covers by the car's stars now (RecalcDVDUnlocks): one
     * reached opens, with its SMS (SMSMessageQueue::NotifyDVDCoverElegible,
     * by the stars) and Rachel's line; one no longer reached closes */
    int said=0,sent=0;dvd_open=0;
    for(int i=0;i<nshows;i++) {
        const CareerShow *v=&shows[i];if(v->kind!=SHOW_DVD || v->stage!=st.stage || rating<dvd_rating(v))continue;
        dvd_open|=(uint16_t)(1u<<dvd_order(i));
        for(int m=0;m<nsms;m++)if(sms[m].type==SMS_DVD && sms[m].cond*10==dvd_rating(v) && !career_sms_sent(m)) {
            sms_send(m);sent=1;
            if(!said && !career_show_done(i)){const CareerCover *c=career_cover(v->cover);said=c?c->speech:0;}
        }
    }
    pile_build();unlock_sms();
    if(sent && save_path)career_save(save_path);
    return said;
}
void career_roam_tick(float dt,const char *save_path) {   /* SMSMessageQueue::Tick: the help messages at their seconds */
    if(st.stage>=STAGE_N || dt<=0)return;
    st.roam+=dt;
    int sent=0;
    for(int i=0;i<nsms;i++)if(sms[i].type==SMS_HELP && st.roam>sms[i].cond && !career_sms_sent(i)){sms_send(i);sent=1;}
    if(sent && save_path)career_save(save_path);
}
static int shop_sms(int kind) {   /* SMSMessageQueue::NotifyEnterShop */
    int sent=0;
    for(int i=0;i<nsms;i++)if(sms[i].type==SMS_SHOP && sms[i].cond==kind && !career_sms_sent(i)){sms_send(i);sent=1;}
    return sent;
}
void career_enter_shop(int kind,const char *save_path) {
    if(shop_sms(kind) && save_path)career_save(save_path);
}
int career_visit_shop(int index,const char *save_path) {
    const CareerShop *s=career_shop(index);if(!s)return 0;
    if(index<32)st.shops_entered|=1u<<index;   /* ShopDataDesc::HasBeenEntered (its state record, NotifyEnterShopFromFreeRoam): the welcome said */
    int sent=shop_sms(s->kind),up=0;
    /* PlayerCareerState::NotifyEnterShopFromFreeRoam commits the required
     * shop event. DDAY_A targets CC_CAR_LOT_1, B targets CRIB_1 on Xbox
     * and PS2. An owned first car records the lot's acquisition already;
     * no new save fields, speech flags or fabricated race results. */
    if(st.stage==0 && st.car!=FLEET_RACHEL && s->kind==SHOP_CRIB && !strcmp(s->name,"CRIB_1"))up=stage_advance();
    if((sent || up) && save_path)career_save(save_path);
    return up;
}
int career_sponsors_offered(int *out) {
    const CareerStage *s=career_stage_info(st.stage);if(!s)return 0;
    int n=0,easiest=-1;
    for(int i=0;i<s->nsponsors && i<5;i++) {
        const CareerSponsor *sp=career_sponsor(s->sponsor[i]);if(!sp)continue;
        if(easiest<0 || sp->rep<sponsors[easiest].rep)easiest=s->sponsor[i];
        if(sp->rep<=st.prev_avg_rep)out[n++]=s->sponsor[i];
    }
    if(!n && easiest>=0)out[n++]=easiest;
    return n;
}
void career_sign_sponsor(int k,const char *save_path) {
    const CareerSponsor *sp=career_sponsor(k);if(!sp || !sponsor_due())return;   /* (the screen opens when it is asked; a test's signing need not wait for the win) */
    st.sponsor=(int8_t)k;st.money+=sp->bonus;st.sponsor_done=0;
    if(st.slots_open<5)st.slots_open++;   /* a sponsor opens a garage slot */
    /* its races on the map: the pile drawn afresh, as a load draws it; the
     * world's races kept would hold the contract's courses (NOS and GREDDY
     * signed with none of theirs shown, beta issue 16) */
    memset(st.open,0,sizeof st.open);pile_build();
    if(save_path)career_save(save_path);
}
int career_sms_new(void) { int n=0;for(int i=0;i<nsms;i++)n+=(st.sms_sent[i>>3]>>(i&7)&1) && !(st.sms_read[i>>3]>>(i&7)&1);return n; }
void career_sms_read(int i) { if(i>=0 && i<nsms)st.sms_read[i>>3]|=(uint8_t)(1<<(i&7)); }
int career_sms_had(int type) { for(int i=0;i<nsms;i++)if(sms[i].type==type && career_sms_sent(i))return 1;return 0; }
int career_sms_sent(int i) { return i>=0 && i<nsms && (sms_bit(st.sms_sent,i) || sms_deleted(i)); }
int career_sms_listed(int i) { return i>=0 && i<nsms && sms_bit(st.sms_sent,i); }
int career_sms_unread(int i) { return career_sms_listed(i) && !sms_bit(st.sms_read,i); }
void career_sms_delete(int i) {
    if(!career_sms_listed(i))return;
    st.sms_sent[i>>3]&=(uint8_t)~(1<<(i&7));st.sms_read[i>>3]|=(uint8_t)(1<<(i&7));
}
/* without career.n2cr version 15: filed with Rachel's, the plain envelope, none opening by itself */
const CareerSmsMail *career_sms_mail(int i) {
    static const CareerSmsMail none={SMS_BOX_RACHEL,0,0,0};
    return i>=0 && i<nsms_mail?&sms_mail[i]:&none;
}
void career_sms_send(int i,const char *save_path) { sms_send(i);if(save_path)career_save(save_path); }
/* SMSMessageQueue::AnyUnseenAutoPopups: a message come since the load, that
 * opens itself (its data's +4), still unread; -1 none. The newest first. */
int career_sms_autopopup(void) {
    for(int i=nsms-1;i>=0;i--)
        if((sms_fresh[i>>3]>>(i&7)&1) && i<nsms_mail && sms_mail[i].popup && career_sms_unread(i))return i;
    return -1;
}
void career_sms_autopopups_seen(void) {   /* MarkAllAutoPopupsSeen */
    for(int i=0;i<nsms && i<nsms_mail;i++)if(sms_mail[i].popup)sms_fresh[i>>3]&=(uint8_t)~(1<<(i&7));
}
/* An engage tip's SMS comes the first time the car passes its marker and
 * banks the message's money with it ($25, four of them $50): the original
 * deposits it as it queues the SMS (PlayerCareerState::
 * NotifyInSMSAutoTriggerZone), never again once it is in the mailbox. */
int career_tip_send(int i,const char *save_path) {
    if(i<0 || i>=nsms || career_sms_sent(i))return 0;
    int money=sms[i].money>0?sms[i].money:0;
    sms_send(i);st.money+=(unsigned)money;
    if(save_path)career_save(save_path);
    return money;
}
int career_tip_count(void) { return ntips; }
int career_cash_count(void) { return ncash; }
const CareerCash *career_cash(int i) { return i>=0 && i<ncash?&cash[i]:NULL; }
int career_cash_here(int i) {
    const CareerCash *c=career_cash(i);if(!c || c->stage>st.stage)return 0;
    return !(st.cash_taken[c->id>>3]>>(c->id&7)&1);
}
int career_take_cash(int i,const char *save_path) {
    if(!career_cash_here(i))return 0;
    const CareerCash *c=&cash[i];st.cash_taken[c->id>>3]|=(uint8_t)(1<<(c->id&7));st.money+=c->amount;
    if(save_path)career_save(save_path);
    return c->amount;
}
const CareerTip *career_tip(int i) { return i>=0 && i<ntips?&tips[i]:NULL; }
void career_sms_event(int type,const char *save_path) {
    if(type==SMS_OUTRUN_INTRO){int had=0;for(int i=0;i<nsms;i++)if(sms[i].type==type)had|=career_sms_sent(i);if(had)return;}
    if(type==SMS_OUTRUN_WON && st.outruns<255)st.outruns++;   /* the stage's outruns won (the game's stage record +0xdc, 0x14d677) */
    sms_type(type);
    if(save_path)career_save(save_path);
}
static const char *shop_text_at(uint32_t off) { const char *t=career_text(off);return *t?t:NULL; }   /* an empty string: none */
const char *career_shop_header(int i) { return i>=0 && i<nshop_text?shop_text_at(shop_text[i].header):NULL; }
const char *career_shop_welcome(int i) { return i>=0 && i<nshop_text?shop_text_at(shop_text[i].welcome):NULL; }
int career_shop_entered(int i) { return i>=0 && i<32 && (st.shops_entered>>i&1); }
int career_shop_visible(int i) {
    const CareerShop *s=career_shop(i);if(!s || s->stage>st.stage)return 0;
    return !s->hidden || (st.shops_seen>>i&1);
}
int career_shop_found(int i,const char *save_path) {
    if(i<0 || i>=nshops || (st.shops_seen>>i&1))return CAREER_SAVE_NONE;
    st.shops_seen|=1u<<i;
    return persist_progress(save_path).status;
}
void career_test_stage(int stage,int wins) {   /* its first world races won */
    st.stage=(uint8_t)stage;
    for(int k=0;k<nevents;k++)if(events[k].stage==stage && !later(k) && events[k].reward==3 && !is_suv(k) && wins>0){st.best[k]=1;wins--;}
    pile_build();
}
void career_test_outruns(int n) { st.outruns=(uint8_t)n; }
void career_test_slots(int n) { st.slots_open=(uint8_t)(n<1?1:n>GARAGE_SLOTS?GARAGE_SLOTS:n); }   /* the garage's open slots (sponsors open them) */
void career_test_rep(int rep) { st.prev_avg_rep=(uint16_t)rep; }
void career_test_prologue_done(void) { for(int k=0;k<nevents;k++)if(events[k].stage==0 && !st.best[k])st.best[k]=2;pile_build(); }   /* the prologue's races all run, none won, still in Rachel's car (the stage 0 timeout: career_must_pick_car, no race on offer) */
void career_test_pick_car(const char *save_path) {
    /* Existing fast-career fixtures skip driving to the lot and crib.
     * Explicitly exercise both operations, never make set_car imply a
     * visit for a player's acquisition or a native load. */
    career_set_car(0,save_path);
    for(int i=0;i<nshops;i++)if(shops[i].kind==SHOP_CRIB && !strcmp(shops[i].name,"CRIB_1")){career_visit_shop(i,save_path);break;}
}
void career_test_dvds(int n) {   /* the stage's first n DVD covers open (the car's stars) */
    for(int i=0;i<nshows;i++)if(shows[i].kind==SHOW_DVD && shows[i].stage==st.stage && n-->0)dvd_open|=(uint16_t)(1u<<dvd_order(i));
}
void career_test_uniques(void) {
    uint32_t d=shows_done();for(int i=0;i<nshows && i<32;i++)if(shows[i].kind==SHOW_UNIQUE)d|=1u<<i;
    st.shows_lo[0]=(uint8_t)d;st.shows_lo[1]=(uint8_t)(d>>8);st.shows_hi[0]=(uint8_t)(d>>16);st.shows_hi[1]=(uint8_t)(d>>24);
}
void career_test_win_stage(const char *save_path) {
    /* the smoke's fast career: the races on offer won as they come (the map
     * drawn again after each, as back on the street), the DVD covers shot at
     * the car's best stars. A contract stage's sponsor comes with its first
     * world race won (NeedToAskForSponsor, beta issue 202): the run stops
     * there, for the street's sponsorship screen; a sponsor already asked
     * (a caller without that screen) is signed here */
    if(career_needs_sponsor()){int o[5];if(career_sponsors_offered(o))career_sign_sponsor(o[0],NULL);}
    int stage0=st.stage,saved=rating;
    for(int guard=0;st.stage==stage0 && guard<CAREER_MAX;guard++) {
        career_free_roam(100,NULL);
        for(int i=0;i<nshows;i++)if(career_show_open(i) && shows[i].kind==SHOW_DVD)career_show_take(i,NULL);
        if(st.stage!=stage0)break;
        int k=0;while(k<nevents && !(st.open[k] && !won(k)))k++;
        if(k==nevents)break;
        engaged=k;career_finish(events[k].id,1,100.f,NULL);
        if(career_needs_sponsor())break;   /* the sponsor asked: its screen next */
    }
    engaged=-1;rating=saved;
    if(save_path)career_save(save_path);
}
/* The reputation of a win, as the game's: the margin (%) over the second
 * in three steps, 1, 2 and 5% for 100, 200 and 300, a drift's (its points)
 * 10, 20 and 30% (the GameCube Speed.elf's GetReputationForMarginOfVictory
 * and its ReputationDataTable, the same as the Xbox's 0x11f2c0 and
 * 0x3a10f0). The table has a row per kind of race (GetReputationCategory-
 * ForEvent: the URL tournaments one, the others by their race type), all
 * alike but the drift's; an event of no reward kind (the prologue's, and
 * this port's events in no career race) has none. */
static int rep_of_win(int k,float margin) {
    if(events[k].reward==0)return 0;
    float s=events[k].type==5?10.f:1.f;   /* 5: drift */
    static const float step[3]={1,2,5};static const int rep[3]={100,200,300};
    int r=0;for(int i=0;i<3;i++)if(margin>=step[i]*s)r=rep[i];
    return r;
}
int career_wins(void) { int n=0;for(int k=0;k<nevents;k++)n+=st.best[k]==1;return n; }
CareerReward career_finish_result(int event_id,int place,float margin,int completed,const char *save_path) {
    CareerReward r={0};r.place=place;
    int k=career_find(event_id);if(k<0 || place<1)return r;
    /* the game pays a win only (0x14d6c0; a lost race, 0x135610, is marked
     * and no more), the race's bank, or for a sponsor race (reward kind 1)
     * with a sponsor signed its purse instead (Career::GetBankWonFromRace,
     * 0x13f990); a race won before, never again (EventInProgressState::
     * IsPossibleToWinReward: it is off the map, but a tournament's course
     * or the race list can reach it) */
    /* An unsuccessful terminal result is an attempt, never a finish/win.
     * Keep an earlier best place; zero still means never finished. */
    int again=won(k);
    if(completed && place==1 && !again) {
        const CareerSponsor *sp=career_sponsor(st.sponsor);
        r.money=events[k].reward==1 && sp?sp->purse:events[k].money>0?events[k].money:0;
        if(events[k].reward==1 && sp)r.purse=sp->purse;
        r.rep=rep_of_win(k,margin);
    }
    st.money+=(unsigned)r.money;st.rep+=(unsigned)r.rep;st.races++;
    if(completed && (!st.best[k] || place<st.best[k]))st.best[k]=(uint8_t)place;
    /* the reputation per race the sponsors ask for: the wins' only (the
     * game adds a win's to its stage's sum and count, RuntimeCareerStage-
     * Info::NotifyCommitSponsorPoints from PlayerCareerState::CommitWinEvent;
     * a lost race adds nothing) */
    if(completed && place==1 && !again && events[k].reward!=0 && events[k].stage==st.stage){st.stage_races++;st.stage_rep+=(unsigned)r.rep;}
    if(completed && place==1 && !again)   /* the races whose win sends an SMS (SMSMessageQueue::NotifyCompleteEvent: SMS_END_OF_GAME_MESSAGE) */
        for(int i=0;i<nsms;i++)if(sms[i].type==SMS_END && sms[i].cond==k){sms_send(i);r.sms=1;}
    if(completed) {
        unlock_sms();
        r.stage_up=stage_check();   /* only an earned result can advance the career */
    }
    r.save=persist_progress(save_path);
    return r;
}
CareerReward career_finish(int event_id,int place,float margin,const char *save_path) {
    return career_finish_result(event_id,place,margin,1,save_path);
}

int career_part_open(int opens) {
    int type=opens>>12,stage=opens>>8&15,n=opens&255;
    if(!nevents || opens<=0)return 1;
    if(type==1)return career_shop_visible(n);
    if(type<2 || type>4 || stage<st.stage)return 1;   /* a stage passed: its counts were reached on the way */
    if(stage>st.stage)return 0;
    return stage_won(st.stage,type==2?3:type==3?2:1)>=n;   /* the stage's world, URL or sponsor races won */
}
_Static_assert(sizeof(CareerShow)==208,"career.n2cr version 7: 208-byte showcases");
