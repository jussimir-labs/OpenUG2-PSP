/* The career: race -> reward -> progress -> save.
 *
 * Its races are the retail career's (tools/convert_career.py ->
 * career.n2cr): a row per career race of the original (GLOBALB 0x34A11),
 * with its course, stage, kind (world, sponsor, URL), money, laps, rivals
 * and requirement, in career order. The rules are the original's (the
 * GameCube Speed.elf: PlayerCareerState, EventPile, RuntimeCareerStageInfo,
 * SMSMessageQueue):
 *  - the map: each time the player is back on the street the stage's
 *    races on offer are drawn again (career_free_roam): the ones still on
 *    offer and not won stay, the others drawn at random among the stage's
 *    not won, up to the stage's count per kind (1 of each in stage 1, 2 in
 *    stage 2, 3 in 3, 4 in 4 and 5); a sponsor's races up to those of its
 *    contract still to win; the SUV races with an SUV; the hidden races of
 *    the stage (up to the stage's count), off the map until the player
 *    drives into one (career_engage); the URL tournaments once the stage's
 *    world races won reach theirs; at the end of stage 5, the final race
 *    alone. A race won is gone for good;
 *  - money: a win only, the race's (a sponsor race: the signed sponsor's
 *    purse); 2nd-4th nothing (Career::GetBankWonFromRace);
 *  - reputation: a win only, by its margin over the second (time, or
 *    points in a drift or a tournament): under 1% none, 1% 100, 2% 200, 5%
 *    300 (a drift 10/20/30%; an event of no reward kind none), the game's
 *    (EventInProgressState::CalculateMarginOfVictory and
 *    GetReputationForMarginOfVictory); the sponsors' reputation per race
 *    counts the wins only;
 *  - a stage ends with its final race won and its DVD covers shot (stage 1,
 *    with no final: all its races won; the prologue, stage 0, on entering
 *    CRIB_1 after picking the first car); the final is on offer once its counts
 *    of world, sponsor and URL races won are reached
 *    (RuntimeCareerStageInfo::CheckIfClearedStage, CanDoURL);
 *  - a tournament (URL) is raced from its first course, the others after
 *    it, for points; its prize comes at the end.
 * The car: the career starts in Rachel's 350Z; the car lot exchanges it
 * for a first stock car before CRIB_1 completes the prologue. After all
 * three optional races, "Rachel needs her car back" requires that visit.
 * The lot is a free trade-in of the one
 * garage slot (the other four open with sponsors). Cars open by the retail
 * unlock table (cars.h, tools/convert_cars.py).
 * The state is saved on the memory stick (savedata.c) after every race. */
#ifndef NFSU2_CAREER_H
#define NFSU2_CAREER_H
#include <stdint.h>
#include "garage.h"
#include "perf.h"

enum { CAREER_MAX=256 };   /* rows: an event per stage it is raced in (195 today) */
enum { CAREER_FE_SAYS=0x1c };   /* Rachel's garage-menu lines, one per screen index (CareerSpeech's FESpeechEvent) */
/* flags: CAREER_LATER a tournament's later course (raced after its first,
 * not on the map), CAREER_TRACK on a closed track; group: the tournament
 * (0 none) */
enum { CAREER_APPROX=1, CAREER_LATER=2, CAREER_TRACK=4, CAREER_SPONSOR_RACE=8 };
typedef struct { uint16_t id;uint8_t stage,type;int money;uint8_t reward,laps,flags,group; } CareerEvent;   /* int: 32 bits, the file layout */
typedef struct {
    unsigned money,rep,races;          /* bank, reputation, races finished */
    uint8_t best[CAREER_MAX];          /* best place per career event, 0 = never finished */
    uint8_t open[CAREER_MAX];          /* 1 = unlocked */
    int8_t car;                        /* fleet index (cars.h), -1 Rachel's 350Z */
    uint8_t slots_open,shows_lo[2];    /* garage slots open (1; sponsors open more); the photo shoots done (bits 0-15, career_show_done) */
    CarLook look;                      /* the car's paint, vinyl, parts (garage.h) */
    CarPerf perf;                      /* its performance packages and tuning (perf.h) */
    /* version 6: the stages */
    uint8_t stage;                     /* the current stage 0-5; 6 when the career is done */
    int8_t sponsor;                    /* the signed sponsor (index), -1 none yet */
    uint8_t won_url,won_choice,sponsor_done,dvds,mags,outruns;   /* this stage's contract: wins, sponsor races (bits), covers; its outruns won (the unique parts' showcases) */
    uint16_t stage_races,prev_avg_rep; /* races won this stage; the previous stage's reputation per race won */
    uint32_t stage_rep;
    uint8_t sms_sent[32],sms_read[32]; /* per message: in the mailbox, read */
    uint32_t shops_seen;               /* shops found in the city (bits) */
    /* the garage's slots (car, look, performance); the car above is the
     * active slot's, mirrored into it on every save */
    int8_t slot_car[5];uint8_t slot_active,shows_hi[2];   /* slot_car: -2 empty; shows_hi: the photo shoots done, bits 16-31 */
    CarLook slot_look[5];
    CarPerf slot_perf[5];
    /* version 9 */
    uint32_t spent;                    /* money spent on the cars: parts, paint, performance */
    uint8_t cash_taken[8];             /* the city's cash picked up (CareerCash.id bits) */
    /* version 12 */
    uint16_t dvd_shot;   /* the DVD covers' sessions done (bits in the DVD showcases' order) */
    uint8_t started,heard;   /* started: the career's opening has played (PlayerCareerState +0x7e28, saved by the original too); heard: CAREER_HEARD_*; in pad bytes, 0 in older saves */
    /* version 13: a row per career race (before, one per event and stage) */
    uint8_t found[CAREER_MAX/8];       /* hidden races found (bits by row) */
    float roam;                        /* seconds of the career's free roam (the help SMS come by it) */
    /* version 14: Rachel's garage-menu lines by when, as the original's
     * save keeps them (CareerSpeech +0x164 its clock, +0x168 a time per
     * screen): a resumed career does not hear them all again */
    float played;                      /* seconds of the career's play (career_play_tick) */
    float fe_said_at[CAREER_FE_SAYS];  /* played at each screen's last line, 0 never */
    /* version 15: where the career's free roam starts again, as the
     * original's save keeps it (PlayerCareerState +0x7e3c the flag, +0x7e40
     * the place, +0x7e50 the heading; GetRespawnParameters, read by
     * RaceStarter::StartCareerFreeRoam and StartCareerFreeRoamFromInGame):
     * GU x y z and yaw; spawn_shop 1 + the shop the car drove into (its
     * exit scene on a resume, CAnimManager::NotifyShopExitNISNeeded), 0 none.
     * Saves before it have none (spawn_on 0): the port's earlier places. */
    float spawn[4];
    uint8_t spawn_on,spawn_shop,spawn_pad[2];
    /* version 16: the shops entered from the street (bits by shop), the
     * original's state record per shop (ShopDataDesc::HasBeenEntered): the
     * shop's dialog says its welcome until then (beta issue 188) */
    uint32_t shops_entered;
} CareerState;
/* Persistence is separate from the earned reward. NONE is no operation;
 * DIRTY means no write was requested. Only SAVED confirms durable progress.
 * BUSY/UNKNOWN preserve the utility's ownership; no rollback is implied.
 * TEMPORARY acknowledges RAM only. BLOCKED is the refused-load write latch. */
enum { CAREER_SAVE_NONE, CAREER_SAVE_SAVED, CAREER_SAVE_DIRTY, CAREER_SAVE_ERROR,
       CAREER_SAVE_BUSY, CAREER_SAVE_UNKNOWN, CAREER_SAVE_TEMPORARY, CAREER_SAVE_BLOCKED };
typedef struct { int status,error; } CareerSaveResult;
typedef struct { int money,rep;int opened[2],nopened,place;int purse,stage_up,cover,cover_speech,sms;CareerSaveResult save; } CareerReward;   /* purse: sponsor's; cover: 1 magazine, 2 DVD, and its line; a showcase's photo: money its cash */

/* The stages, sponsors, covers, SMS and shops (tools/convert_career.py). */
enum { STAGE_N=6, SPONSOR_MAX=24, COVER_MAX=40, SMS_MAX=256, SHOP_MAX=32, TIP_MAX=64, CASH_MAX=64 };
typedef struct { uint8_t stage,nsponsors;uint16_t stakes;uint8_t sponsor[5],url,choice,dvd,mags,finish;int8_t outruns;uint8_t pad; } CareerStage;   /* outruns: the stage's outrun wins at most (-1 no limit); url, choice: the final's URL and world races won */
/* career.n2cr version 9: each row's career race (its record in 0x34A11;
 * hidden until found, an SUV race, its rivals and their names (text
 * offsets); req: 3 on offer once the stage's world races won reach need's
 * high half, 4 the stage's final, need packing the world, sponsor and URL
 * races won it needs in its bytes 1-3; after: the row won first, 0xffff
 * none) and each stage's rules (cap: the races on offer at once by race
 * type, RMODE_ order; suv: the SUV races'; hidden: the hidden ones', -1 no
 * limit; final: its row, 0xffff none; star: a showcase photo's cash per
 * star; sponsor_races: the final's sponsor races won). */
typedef struct { uint16_t record;uint8_t hidden,suv,rivals,req;uint16_t after;uint32_t need,rival[5]; } CareerRecord;
typedef struct { uint8_t cap[6],suv;int8_t hidden;uint16_t final;int16_t star;uint8_t sponsor_races,pad[3]; } CareerRule;
const CareerRecord *career_record(int k);
const CareerRule *career_rule(int stage);
typedef struct { char name[16];uint16_t purse,bonus,rep;uint8_t types[3],pad; } CareerSponsor;
typedef struct { uint8_t stage,dvd,rating,speech; } CareerCover;   /* speech: 1 + the bit of Rachel's line, 0 none */
typedef struct { uint8_t type,stage;uint16_t cond;int32_t money;uint32_t sender,subject,body; } CareerSms;
/* career.n2cr version 15, per SMS (SmsData bytes 3-5): the mailbox it is
 * filed in once read (SMS_BOX_), 1 if it opens by itself in the free roam
 * (SMSMessageQueue::AnyUnseenAutoPopups), its envelope's colour
 * (SMSGetEnvelopeImageHashAndColour's category) */
typedef struct { uint8_t box,popup,envelope,pad; } CareerSmsMail;
enum { SMS_BOX_INBOX=1, SMS_BOX_TIPS, SMS_BOX_EVENTS, SMS_BOX_RACHEL, SMS_BOX_UNLOCKS };   /* MailboxCategories: Inbox, Game Tips, Special Events, Rachel, Unlocks */
typedef struct { char name[24];uint8_t kind,stage,hidden,pad;float x,y,z; } CareerShop;
typedef struct { float x,z;uint16_t sms;uint8_t radius,flags; } CareerTip;   /* an engage tip: its SMS where the car passes; flags 1 (CAREER_TIP_QUEUE): the SMS only reaches the mailbox (0 in older files: it opens itself) */
#define CAREER_TIP_QUEUE 1
typedef struct { float x,z;uint16_t amount;uint8_t stage,id; } CareerCash;   /* cash on the street, there from that stage */
/* SMS types: 1 a race won (cond: its row), 2 a shop entered (cond: its
 * kind), 3 the free roam's seconds (cond), 9/10/12 the stage's sponsor,
 * URL and world races won (cond), 13 a showcase open (cond: it), 14 a DVD
 * cover's stars reached (cond) */
enum { SMS_END=1, SMS_SHOP=2, SMS_HELP=3, SMS_OUTRUN_INTRO=4, SMS_OUTRUN_WON=5, SMS_OUTRUN_LOST=6, SMS_DVD=14 };
enum { SHOP_LOT, SHOP_PERF, SHOP_BODY, SHOP_GRAPHICS, SHOP_SPECIALTY, SHOP_CRIB };
const CareerStage *career_stage_info(int stage);
const CareerSponsor *career_sponsor(int k);
/* The Sponsorship screen's texts (uiSponsorsMain::RefreshHeader): the
 * contract's terms (SPONSOR_DETAILS_<NAME>), the sponsor's races
 * (SPONSOR_INFO_<NAME>), its logo's texture (DECAL_ICON_<NAME>); NULL in a
 * career.n2cr before version 13. */
const char *career_sponsor_terms(int k);
const char *career_sponsor_races(int k);
const char *career_sponsor_icon(int k);
/* The event dialog's texts (RaceEngageDialog): its subject ("Sponsored
 * Sprint"), type line, sender and body, the pick-th of the row's (drawn as
 * the dialog opens); 0: none (a career.n2cr before version 14) */
int career_engage_text(int k,unsigned pick,const char **subject,const char **type,const char **from,const char **body);
int career_url_purse(int stage);          /* a URL race's money in that stage (Each URL Victory) */
int career_cover_count(void);
const CareerCover *career_cover(int i);   /* the covers in table order (per stage: magazines, DVDs) */
/* The sponsors a stage offers: those whose average reputation per race the
 * previous stage reached (at least the least demanding one); returns how
 * many, indices to out. */
int career_sponsors_offered(int *out);
/* Sign a sponsor of the current stage: its signing bonus, a garage slot. */
void career_sign_sponsor(int k,const char *save_path);
int career_needs_sponsor(void);            /* a contract stage without one */
/* The contract's progress: done/needed per term (URL wins, sponsor races,
 * wins of choice, DVD covers); stages 0-1: events finished / to finish. */
typedef struct { int url,url_need,sponsor,sponsor_need,choice,choice_need,dvd,dvd_need,finish,finish_need; } CareerContract;
void career_contract(CareerContract *c);
int career_done(void);                     /* every stage complete */
/* Visual rating of the player's car (x10, 0-100), for the covers. */
void career_set_rating(int rating10);
int career_available(int k);              /* an event of a stage reached */
/* SMS */
int career_sms_count(void);
const CareerSms *career_sms(int i);
const char *career_text(uint32_t off);
int career_sms_new(void);                  /* unread in the mailbox */
void career_sms_read(int i);
int career_sms_sent(int i);               /* come (in the mailbox, or deleted from it) */
int career_sms_listed(int i);             /* in the mailbox */
int career_sms_unread(int i);             /* in the mailbox, not read */
void career_sms_delete(int i);            /* out of the mailbox, for good (read, sent clear) */
const CareerSmsMail *career_sms_mail(int i);
void career_sms_send(int i,const char *save_path);   /* one message into the mailbox */
int career_tip_send(int i,const char *save_path);   /* an engage tip's SMS, the first time: its money banked (0 if sent before) */
int career_tip_count(void);
const CareerTip *career_tip(int i);
int career_cash_count(void);
const CareerCash *career_cash(int i);
int career_cash_here(int i);              /* there now: its stage reached, not yet picked up */
int career_take_cash(int i,const char *save_path);   /* its amount banked; 0 if not there */
int career_sms_had(int type);           /* one of this type in the mailbox */
/* The free roam's autopopup (SMSMessageQueue::Tick): a message come since
 * the load that opens itself (CareerSmsMail.popup), unread; -1 none; then
 * every such one marked seen (MarkAllAutoPopupsSeen). */
int career_sms_autopopup(void);
void career_sms_autopopups_seen(void);
void career_sms_event(int type,const char *save_path);   /* an outrun's intro, win or loss */
/* Shops in the city */
/* The career's showcases (career.n2cr version 5, the game's 0x34A1B): the
 * magazines' photo shoots (kind 0) and the unique parts' (kind 2) at their
 * places in the city, each opening at its stage once the stage's wins reach
 * its count. Driving into an open photo shoot puts the car on its
 * magazine's cover (its cover: the covers' table); into a unique part's,
 * opens that kind of unique part in the shops (opens: tools/convert_rating.py's
 * kinds, 11 hoods, 12 spoilers, 13 rims, 22 wide body kits, 62 vinyls); a
 * performance one's, perf: the performance shop's categories whose unique
 * part it opens (bit n: GLOBALB 0x34A15's category n, tools/convert_perf.py;
 * what that part does, the data read does not say: the port counts them). A photo shoot's magazine (its
 * masthead's prefix: BB, SCNORWA...) and the game's 3 cameras for it
 * (MAGAZINES_SHOWCASE.BIN, tools/convert_career.py show_cameras: degrees
 * around the car from its front, degrees up, metres away, the point looked
 * at ahead, to the left and up of the car, the lens in degrees; a distance
 * of 0, none); a studio cover's pose (MAGAZINES_FRONTEND.BIN, studio_covers:
 * the car's turn on the turntable and its front wheels' steering in
 * degrees, the lens, the eye and the point looked at in the studio's metres
 * (x y level, z up, the car at 0), the camera's roll; a lens of 0, none). */
/* A showcase (career.n2cr version 7): its start, where its countdown
 * leads (to_x, to_z: the photographer's place or the unique part's shop)
 * and in how many seconds, Rachel's texts for it (says of say[], a %s for
 * the time) and the dialog's subject. */
typedef struct { uint8_t stage,kind,wins,opens;int16_t cover;uint16_t perf;float x,z;char name[24];char magazine[8];float cam[3][7],studio[10];
                 float to_x,to_z;uint16_t time;uint8_t says,pad;uint32_t say[5],subject; } CareerShow;
enum { SHOW_MAX=40, SHOW_MAGAZINE=0, SHOW_UNIQUE=2, SHOW_DVD=3 };   /* (a DVD cover's: its session at the star, after the cover is won) */
int career_show_count(void);
const CareerShow *career_show(int i);
/* The heights (GU y) of its star and of its countdown's goal, the trigger
 * zones' (career.n2cr version 16); 0 when the file has none. */
int career_show_heights(int i,float *star,float *to);
int career_show_open(int i);   /* open and not done yet */
int career_show_done(int i);
/* The showcase taken: a photo shoot's cover (CareerReward's cover and its line), saved. */
CareerReward career_show_take(int i,const char *save_path);
/* That kind of unique part open in the shops (its showcase taken; a kind no
 * showcase opens: open). */
int career_unique_open(int kind);
int career_perf_unique(void);   /* the performance categories opened by the showcases taken (bit n: 0x34A15's category n) */
int career_shop_count(void);
const CareerShop *career_shop(int i);
int career_shop_visible(int i);           /* its stage reached and (when hidden) found */
/* The street's shop dialog (EngageShopDialog::Setup, career.n2cr version
 * 17): its header ("Body Shop", "Megalow Parts"), the shop's own welcome
 * before its first visit ("Welcome to the Bayview Body Shop, Beacon Hill
 * East store..."); NULL none (the car lots and the garage, an older file). */
const char *career_shop_header(int i);
const char *career_shop_welcome(int i);
int career_shop_entered(int i);          /* entered from the street (career_visit_shop) */
/* Returns this operation's CAREER_SAVE_* status; no-op returns NONE and
 * leaves any prior pending result intact. Capture error via the getter only
 * for an operation that ran, before another career save/load. */
int career_shop_found(int i,const char *save_path);
void career_test_win_stage(const char *save_path);   /* smoke tests: the current stage's contract won */
void career_test_pick_car(const char *save_path);    /* diagnostic shortcut: first car plus CRIB_1 visit, not a normal-play proof */
/* The garage: GARAGE_SLOTS slots, the first open from the start, one more
 * per sponsor signed. */
enum { GARAGE_SLOTS=5, SLOT_EMPTY=-2 };
int career_slot_of(int fleet_index);      /* the slot holding that car, -1 none */
int career_free_slot(void);               /* an open, empty slot, -1 none */
int career_add_car(int fleet_index,const char *save_path);   /* into a free slot, made the active car; -1 none free */
void career_switch_car(int slot,const char *save_path);
/* Car lot confirmation: commit the car and save together; >=0 active slot,
 * -1 unavailable/invalid selection, -2 save failed (career unchanged). */
enum { CAREER_LOT_TRADE, CAREER_LOT_ADD, CAREER_LOT_USE };
int career_lot_car(int fleet_index,int action,const char *save_path);

int career_load_data(const char *path);    /* career.n2cr; 0 if missing */
int career_count(void);
const CareerEvent *career_event(int k);    /* k: career order */
/* The career row of a course (an event id), -1 if none: the race engaged
 * (career_engage) when it is on that course, else one on offer, else the
 * current stage's first, else the highest stage's reached, else its first. */
int career_find(int event_id);
int career_shown(int k);                   /* on the street: on offer (open[k]), found (a hidden race), with an SUV (an SUV race) */
/* On the minimap, the world map and the GPS's places (PlayerCareerState::
 * IsEventVisibleOnRadar, read by Minimap::AddUnlockedTriggerZone and
 * UnlockedTriggerZone::IsVisibleOnRadar): career_shown, never a hidden
 * race, found or not (CareerEventData +0x7f; beta issue 180). */
int career_on_radar(int k);
/* The free roam's place to start again (CareerState.spawn, version 15):
 * set as the original's NotifyEnterEvent (the street's place as a race is
 * engaged), NotifyEnterShopFromFreeRoam (shop: the shop driven into, its
 * exit scene on a resume), NotifyEnterCribFromPauseMenu and
 * NotifyExitToMainMenuFromFreeRoamOrRace (from the free roam only); in RAM,
 * written by the next save. career_spawn: 1 and the place when there is
 * one, *shop the shop (-1 none). */
void career_spawn_set(const float pose[4],int shop);
int career_spawn(float pose[4],int *shop);
/* Back on the street (the original's NotifyFreeRoamBegun): the stage's DVD
 * covers open by the car's visual rating now (x10, 0-100; their SMS as
 * they open), the races on offer drawn again (open[]). */
int career_free_roam(int rating10,const char *save_path);   /* returns Rachel's line for a DVD cover now open (1 + its bit), 0 none */
/* The race the player drove into (its dialog): career_find gives it for
 * its course from now; a hidden race is found. */
void career_engage(int k,const char *save_path);
/* The free roam's clock, past the prologue: the help SMS at their seconds. */
void career_roam_tick(float dt,const char *save_path);
void career_enter_shop(int kind,const char *save_path);   /* a shop entered (SHOP_ kind): its SMS */
int career_visit_shop(int index,const char *save_path);   /* accepted street entry: SMS and the prologue's CRIB_1 final; returns stage advance */
int career_rivals(int k);                  /* the race's rivals (the record's; 0 none given) */
const char *career_rival(int k,int i);     /* their names */
/* How the original's race starter sets rival i's performance (career.n2cr
 * version 10): 0 a fixed rating, 1 matched to the player's car, 2 the
 * preset's own, 3 the stage's ramp; the rating (0..10) of 0 and 3 into
 * *rating; -1 without the data. */
int career_rival_perf(int k,int i,float *rating);
/* Rival i's car when it is one of the original's presets (career.n2cr
 * version 11: Caleb and his crew in the last stage's finals,
 * RideInfo::FillWithPreset): its pack (opp-CALEB_GTO), its model and its
 * neon by the garage's name ("" none); NULL a random car. */
const char *career_rival_preset(int k,int i,const char **model,const char **neon);
/* Where the street offers race k (career.n2cr version 12): its record's
 * trigger zone, GU x y z, as the original's (CareerEventData +0x28, a
 * TrackPathZone: the marker, the engage, the GPS and the maps), often far
 * from the course's start; NULL without the data. */
const float *career_zone(int k);
int career_owns_suv(void);                 /* an SUV in the garage */
const CareerState *career_state(void);
/* Load the save (or start a new career); 1 when a save was read. */
/* Load: 1 valid, 0 genuinely absent, -1 I/O/cancel/timeout or rejected codec.
 * Failed loads disable CAREER writes until a later valid/absent load. */
int career_load(const char *path);
int career_save_allowed(void);
/* Explicitly confirmed New Career: reset RAM, never profile/My Cars or a
 * file here. Refuse unknown/busy persistence and refused loads. Normal later
 * saves replace the current career, as disclosed by the menu confirmation. */
int career_restart_ready(void);   /* career_restart would start over now */
int career_restart(void);
int career_save_fresh_list(const char *path,int *slot);   /* a fresh career on the system's list of slots (savedata_list_save): 1 saved, 0 cancelled, -1 error */
/* Retry only this serializer, never finish/bonus/discovery again. The int
 * keeps the legacy acknowledgment (also true for TEMPORARY); callers claiming
 * persistence must require career_save_result().status == CAREER_SAVE_SAVED. */
int career_save(const char *path);
int career_save_manual(const char *path);   /* explicit Save Game: native SAVE UI */
/* On the street the automatic saves go in the background (1) and the game
 * does not wait for the card: career_save_service once a frame finishes
 * them; career_save_busy until the last state is written. */
void career_save_async(int on);
/* The PSP-1000's street (1, main.c): its automatic saves kept in RAM and
 * written at the street's next stop (a pause, the map, a race, a shop):
 * the save utility takes the volatile block back, and the city's geometry
 * in it with it, the street reloading round the car at every cash and tip
 * (beta issue 166). The original writes none there: ClaimBankReward and
 * NotifyInSMSAutoTriggerZone bank the money alone. A held save counts in
 * career_save_busy and career_save_pending. */
void career_save_defer(int on);
void career_save_defer_stats(unsigned *held,unsigned *written);   /* the saves held, the held ones written */
void career_save_service(void);
int career_save_busy(void);
CareerSaveResult career_save_result(void);   /* passive, latched per write */
int career_save_pending(void);              /* runtime only, outside N2SV */
int career_save_uncertain(void);            /* timeout seen, until confirmed save or load */
/* A finished race: earned reward and its separate persistence result. */
/* margin: how far the winner beat the second, % (main.c race_margin; 100
 * with nobody to beat) */
CareerReward career_finish(int event_id,int place,float margin,const char *save_path);
/* Explicit outcome for a DNF/KO: record the attempt/save, never award a win
 * or replace the previous best. completed=1 preserves career_finish. */
CareerReward career_finish_result(int event_id,int place,float margin,int completed,const char *save_path);
int career_wins(void);   /* events won (their best place 1) */
extern const char *career_type_names[6];
/* Events finished in a career stage, and the highest stage with one. */
int career_finished_in(int stage);
int career_top_stage(void);
/* a part's opening in the shops met (garage_choice_opens' code): its shop open, or the stage's races,
 * URL races or sponsor races won (a stage passed: met; a stage ahead: not); always without a career */
int career_part_open(int opens);
/* A car the lot offers: open from the start, or its unlock reached. */
int career_car_open(int fleet_index);
/* Rachel's car must go back: the prologue (every stage 0 event) is done. */
int career_must_pick_car(void);
/* The career's opening (the airport's presentation and Rachel's prologue
 * with the controls held) has played: the original's flag that turns its
 * Start Career into Resume Career and skips the opening on a resume
 * (StartCareerHelper, RaceStarter::StartCareerFreeRoam). Saves from before
 * it count a career under way as started. */
int career_started(void);
void career_set_started(const char *save_path);
/* Rachel's garage-menu lines said once a career: the garage's own line
 * and the car lot's first (CareerSpeech +0x14 and +0x10), the performance
 * menu's first (its time slot set), all in the original's save. Saves from
 * before the flags (no CAREER_HEARD_KNOWN) count a career past Rachel's
 * car as having heard them. The flag goes to disk with the next save.
 * CAREER_HEARD_EVENTS: the prologue's first call in, its races on offer
 * (career_see_events). */
enum { CAREER_HEARD_LOT=1,CAREER_HEARD_CRIB=2,CAREER_HEARD_PERF=4,CAREER_HEARD_EVENTS=8,CAREER_HEARD_KNOWN=0x80 };
int career_heard(int what);
void career_set_heard(int what);
/* The career's clock of play and each garage screen's line by it: a line
 * comes again only an hour of play after the last (HandleFESpeechEvent's
 * 3600 s). In the save from version 14; older saves have heard none. */
void career_play_tick(float dt);
int career_fe_said_lately(int i);
void career_fe_said(int i);
/* The prologue's races stay off the map and the street until its first
 * call, 15 s into its free roam (PlayerCareerState +0x7e34, in the save:
 * NotifyCSCanSeeEvents, BuildListOfEvents draws no race in stage 0 before
 * it). A save from before the flag counts a prologue race run. */
int career_events_seen(void);
void career_see_events(void);   /* the flag set, the races on offer drawn */
void career_set_car(int fleet_index,const char *save_path);   /* a stock car: its look resets */
/* Buy and fit a look: 0 and the cash taken, or -1 without the money. */
int career_buy_look(const CarLook *look,int cost,const char *save_path);
void career_test_setup(int car,unsigned money);
void career_test_stage(int stage,int wins);void career_test_outruns(int n);void career_test_slots(int n);void career_test_dvds(int n);   /* smoke tests: the career at a stage, its first world races won; the stage's first n DVD covers open */
void career_test_rep(int rep);   /* smoke tests: the previous stage's reputation per race (the sponsors on offer) */
void career_test_prologue_done(void);   /* smoke tests: the prologue's races all run, still in Rachel's car */
void career_test_uniques(void);   /* smoke tests: every unique part's showcase taken */   /* smoke tests: a car and cash to spend */
/* Buy a performance package or save a tuning: as career_buy_look. */
int career_buy_perf(const CarPerf *perf,int cost,const char *save_path);
void career_test_perf(const CarPerf *perf);
/* Money and reputation won outside an event (an outrun), saved. */
void career_bonus(int money,int rep,const char *save_path);
/* A tournament's other courses: finished with the place, no money. */
void career_mark(int event_id,int place,const char *save_path);

#endif
