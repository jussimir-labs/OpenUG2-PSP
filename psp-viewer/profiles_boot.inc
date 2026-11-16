/* The "profiles" boot flow (beta #136): the profile slots through the
 * menus, from the cold title, a Memory Stick with two careers on it (slot
 * 0, the save every build so far wrote, and slot 1 the newer), profiles.sav
 * on slot 0. The system's lists answered by PPSSPP_DIALOG_AUTO=yes (their
 * cursor: Load on the newest, Delete on the last, Save on the first empty)
 * or, without the dialog, by savedata.c's silent stand-in for them.
 *   Career > Load Game: slot 1 in RAM, the garage (or, a career in its
 *   prologue, the street and back by the pause's Quit to Main Menu);
 *   back, Profile Manager > Delete: slot 1 gone (and, as PPSSPP answers the
 *   list again, slot 0 too), "No Profile" and a career from its start;
 *   Create: a name, "save the profile?", the first empty slot, back to it;
 *   Save: that slot saved again; Load: that one back by the list.
 * profiles-result PASS when each landed where it should. */
static int pf_stage,pf_done,pf_ok=1;
static uint64_t pf_begin,pf_pulse,pf_report_at,pf_at;
static unsigned pf_boot_money,pf_boot_races;static int pf_boot_slot=-1;static char pf_boot_profile[16];
static int pf_zero=-1,pf_made=-1;static char pf_made_profile[16];   /* slot 0 after the delete; the slot Create made, its name */
static void pf_record(const char *event) {
    const CareerState *cs=career_state();
    if(bd_log){fprintf(bd_log,"profiles %s wall=%.3f stage=%d front=%d sel=%d prof=%d slot=%d profile=%s money=%u rep=%u races=%u started=%d\n",
                       event,bd_now(),pf_stage,front,front>0 && front<FRONT_N?front_sel[front]:-1,prof_stage,savedata_slot(),profile_name[0]?profile_name:"-",
                       cs->money,cs->rep,cs->races,career_started());fflush(bd_log);}
}
static void pf_check(int ok,const char *what) {
    if(bd_log){fprintf(bd_log,"profiles check %s %s\n",ok?"ok":"FAILED",what);fflush(bd_log);}
    if(!ok)pf_ok=0;
}
static void pf_end(const char *why) {
    pf_record(why);pf_done=1;bd_flow=-1;
    if(bd_log){fprintf(bd_log,"profiles-result %s\n",pf_ok?"PASS":"FAIL");fclose(bd_log);bd_log=NULL;}
}
static void pf_press(SceCtrlData *pad,unsigned b,uint64_t now) { if(now-pf_pulse<700000)return;pf_pulse=now;pad->Buttons=b; }
static void pf_toward(SceCtrlData *pad,int want,uint64_t now) { pf_press(pad,front_sel[front]==want?PSP_CTRL_CROSS:front_sel[front]<want?PSP_CTRL_RIGHT:PSP_CTRL_LEFT,now); }
/* The open carousel's item want: moved onto, X once (its system list runs
 * within that press); 1 from a second and a half after it. */
static uint64_t pf_fired;
static int pf_take(SceCtrlData *pad,int want,uint64_t now) {
    if(!pf_fired) {
        if(front_sel[front]!=want){pf_toward(pad,want,now);return 0;}
        if(now-pf_pulse<700000)return 0;
        pf_pulse=now;pad->Buttons=PSP_CTRL_CROSS;pf_fired=now;return 0;
    }
    return now-pf_fired>1500000;
}
static void pf_next(int stage,uint64_t now) { pf_stage=stage;pf_fired=0;pf_at=now; }
static void pf_pad(SceCtrlData *pad,uint64_t now) {
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(pf_done)return;
    if(!pf_begin) {
        pf_begin=now;const CareerState *cs=career_state();
        pf_boot_slot=savedata_slot();pf_boot_money=cs->money;pf_boot_races=cs->races;memcpy(pf_boot_profile,profile_name,16);
        pf_record("cold-title");
    }
    if(now-pf_begin>420000000ULL){pf_check(0,"deadline");pf_end("deadline");return;}
    if(fe_load_failures || admission_failed){pf_check(0,"load-failure");pf_end("load-failure");return;}
    if(now-pf_report_at>5000000){pf_report_at=now;pf_record("progress");}
    if(loading_on || switch_phase || help_on)return;
    switch(pf_stage) {
    case 0:   /* the title, the main menu's Career */
        if(front==FRONT_TITLE){if(title_allow)pf_press(pad,PSP_CTRL_START,now);return;}   /* (the prompt up: START) */
        if(front==FRONT_MAIN){pf_toward(pad,MM_CAREER,now);return;}
        if(front!=FRONT_STARTCAREER){pf_at=now;return;}
        if(now-pf_at<2000000)return;   /* its way in over: the frame as the player sees it */
        {int it[3],n=sc_items(it),load=-1;for(int k=0;k<n;k++)if(it[k]==SC_LOAD)load=k;
         if(bd_log){fprintf(bd_log,"profiles career-menu items=%d first=%d last=%d\n",n,n?it[0]:-1,n?it[n-1]:-1);fflush(bd_log);}
         pf_check(pf_boot_slot==0,"booted on slot 0 (profiles.sav)");
         pf_check(load==n-1,"Load Game last in Career's menu");}
        capture_frame_as("profiles-01-career-menu.ppm");pf_record("career-menu");pf_next(1,now);return;
    case 1: {   /* Load Game: the newest, slot 1 */
        if(front==FRONT_STARTCAREER){int it[3],n=sc_items(it);pf_take(pad,n-1,now);return;}
        if(front!=FRONT_CRIB && !(front==FRONT_OFF && mode==MODE_DRIVE))return;
        if(now-pf_fired<4000000)return;   /* (the garage's scene in) */
        pf_record("loaded");pf_check(savedata_slot()==1,"Load Game took slot 1");
        capture_frame_as("profiles-02-loaded.ppm");pf_next(2,now);return;
    }
    case 2:   /* back to the main menu, the Profile Manager */
        if(now-pf_at<1500000)return;
        if(front==FRONT_CRIB){pf_toward(pad,CRIB_MENU,now);return;}
        if(front==FRONT_OFF && mode==MODE_DRIVE) {
            /* StartCareerHelper: a career past its prologue to the garage; one
             * in it (the newcareer flow's) to the street (beta #184), back
             * here by the pause's Quit to Main Menu */
            if(career_state()->stage>=1){pf_check(0,"Load Game went to the street (a career past its prologue wanted the garage)");pf_end("street");return;}
            static int said;if(!said){said=1;pf_check(1,"Load Game of a career in its prologue to the street");}
            if(menu==MENU_OFF){pf_press(pad,PSP_CTRL_START,now);return;}
            if(menu==MENU_MAIN && pause_ask){pf_press(pad,pause_ok?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
            if(menu==MENU_MAIN){int n;const PauseItem *it=pause_items(&n);for(int q=0;q<n;q++)if(it[q].action==P_MAIN)menu_sel=q;pf_press(pad,PSP_CTRL_CROSS,now);}
            return;
        }
        if(front==FRONT_MAIN){pf_toward(pad,MM_PROFILES,now);return;}
        if(front!=FRONT_PROFILES){pf_fired=now;return;}
        if(now-pf_fired<2000000)return;
        capture_frame_as("profiles-03-manager.ppm");pf_record("manager");pf_next(3,now);return;
    case 3:   /* Delete: the list's last, slot 1, the one in use. PPSSPP_DIALOG_AUTO
               * answers the list again after each delete, so every one listed
               * goes (a player backs out after one): slot 0 too */
        if(!pf_take(pad,PM_DELETE,now))return;
        pf_record("deleted");
        pf_check(!profile_name[0] && !career_started(),"the profile in use deleted: no profile, a career from its start");
        pf_check(savedata_slot_present("career.sav",1)==0,"slot 1 gone from the Memory Stick");
        pf_zero=savedata_slot_present("career.sav",0);
        if(bd_log){fprintf(bd_log,"profiles slot0-after-delete=%d\n",pf_zero);fflush(bd_log);}
        capture_frame_as("profiles-04-deleted.ppm");pf_next(4,now);return;
    case 4:   /* Create: "create a new profile?", a name, its save on the first empty slot */
        if(prof_stage==PROF_NEW_CAREER || prof_stage==PROF_ASK || prof_stage==PROF_SAVE){pf_press(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
        if(front==FRONT_KEYBOARD){pf_press(pad,strlen(kb_name)<4?PSP_CTRL_CROSS:PSP_CTRL_START,now);return;}
        if(front!=FRONT_PROFILES)return;
        if(!profile_name[0]) {   /* not made yet */
            pf_take(pad,PM_CREATE,now);
            if(now-pf_at>90000000){pf_check(0,"Create never saved");pf_end("create-stuck");}
            return;
        }
        pf_record("created");
        pf_made=savedata_slot();
        pf_check(pf_made==(pf_zero==0?0:1) && savedata_slot_present("career.sav",pf_made)==1,"Create saved on the first empty slot");
        pf_check(!career_started(),"Create's career from its start");
        memcpy(pf_made_profile,profile_name,16);
        capture_frame_as("profiles-05-created.ppm");pf_next(6,now);return;
    case 6:   /* Save (PMSave): the career's save on its slot, the name beside it */
        if(!pf_take(pad,PM_SAVE,now))return;
        pf_record("saved");
        pf_check(career_save_result().status==CAREER_SAVE_SAVED && savedata_slot()==pf_made && savedata_slot_present("career.sav",pf_made)==1,"Save wrote the profile's own slot");
        pf_next(5,now);return;
    case 5:   /* Load: the newest, the one just made, by the list */
        if(!pf_take(pad,PM_LOAD,now))return;
        pf_record("reloaded");
        pf_check(savedata_slot()==pf_made && !strcmp(profile_name,pf_made_profile),"Load read the newest profile back");
        capture_frame_as("profiles-06-reloaded.ppm");pf_end("complete");return;
    }
}
/* The "loadgame" boot flow (beta #136): a career saved without a profile
 * (builds before it let one go on unnamed; the original's never are)
 * reached as the player would: Career > Load Game, the list's choice (its
 * slot), "create a new profile?" Yes, a name, its save beside the career,
 * then the garage with the career as it was. loadgame-result PASS when the
 * career and its progress are the booted ones and the profile is named. */
static int lg_stage,lg_done,lg_ok=1;
static uint64_t lg_begin,lg_at;static unsigned lg_money,lg_races;static int lg_slot;
static void lg_end(const char *why) {
    pf_record(why);lg_done=1;bd_flow=-1;
    if(bd_log){fprintf(bd_log,"loadgame-result %s\n",lg_ok?"PASS":"FAIL");fclose(bd_log);bd_log=NULL;}
}
static void lg_check(int ok,const char *what) { if(bd_log){fprintf(bd_log,"loadgame check %s %s\n",ok?"ok":"FAILED",what);fflush(bd_log);}if(!ok)lg_ok=0; }
static void lg_pad(SceCtrlData *pad,uint64_t now) {
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(lg_done)return;
    if(!lg_begin){lg_begin=now;lg_slot=savedata_slot();lg_money=career_state()->money;lg_races=career_state()->races;pf_record("cold-title");}
    if(now-lg_begin>420000000ULL){lg_check(0,"deadline");lg_end("deadline");return;}
    if(fe_load_failures || admission_failed){lg_check(0,"load-failure");lg_end("load-failure");return;}
    if(loading_on || switch_phase || help_on)return;
    if(prof_stage!=PROF_OFF){pf_press(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
    if(front==FRONT_KEYBOARD){pf_press(pad,strlen(kb_name)<4?PSP_CTRL_CROSS:PSP_CTRL_START,now);return;}
    switch(lg_stage) {
    case 0:
        if(front==FRONT_TITLE){if(title_allow)pf_press(pad,PSP_CTRL_START,now);return;}
        if(front==FRONT_MAIN){pf_toward(pad,MM_CAREER,now);return;}
        if(front!=FRONT_STARTCAREER)return;
        {int it[3],n=sc_items(it);
         lg_check(!profile_name[0] && n==2 && it[0]==SC_NEW && it[1]==SC_LOAD,"no profile: Career's menu is Start New Career, Load Game (the original's)");
         lg_stage=1;pf_fired=0;}
        return;
    case 1: {
        if(front==FRONT_STARTCAREER){int it[3],n=sc_items(it);pf_take(pad,n-1,now);lg_at=now;return;}
        if(front!=FRONT_CRIB && !(front==FRONT_OFF && mode==MODE_DRIVE))return;
        if(now-lg_at<3000000)return;
        const CareerState *cs=career_state();
        pf_record("named");
        lg_check(profile_name[0]!=0,"the career has its profile now");
        lg_check(savedata_slot()==lg_slot && cs->money==lg_money && cs->races==lg_races && career_started(),"the same career, its progress kept");
        lg_end("complete");return;
    }
    }
}
/* The "loadresume" boot flow (beta #136, the real PSP's hang of 05/11):
 * Career > Load Game (the list's newest), the garage, Resume Career, and
 * the street played 15 s: its world loaded around the car (no street hold
 * left running), as the original's StartCareerHelper goes on. With the
 * system's list kept (savedata-dialog.on) the list's own dialog runs
 * before it. loadresume-result PASS once the street ran. */
static int lr_stage,lr_done,lr_ok=1;static uint64_t lr_begin,lr_at;static unsigned lr_holds_ms;
static void lr_end(const char *why) {
    pf_record(why);lr_done=1;bd_flow=-1;
    if(bd_log){fprintf(bd_log,"loadresume street holds=%u ms=%u max_ms=%u critical_closed=%d\n",street_holds.n,street_holds.ms,street_holds.max_ms,world_critical_closed());
               fprintf(bd_log,"loadresume-result %s\n",lr_ok?"PASS":"FAIL");fclose(bd_log);bd_log=NULL;}
}
static void lr_check(int ok,const char *what) { if(bd_log){fprintf(bd_log,"loadresume check %s %s\n",ok?"ok":"FAILED",what);fflush(bd_log);}if(!ok)lr_ok=0; }
static void lr_pad(SceCtrlData *pad,uint64_t now) {
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(lr_done)return;
    if(!lr_begin){lr_begin=now;pf_record("cold-title");}
    if(now-lr_begin>420000000ULL){lr_check(0,"deadline (the street never ran)");lr_end("deadline");return;}
    if(fe_load_failures || admission_failed){lr_check(0,"load-failure");lr_end("load-failure");return;}
    if(now-pf_report_at>5000000){pf_report_at=now;pf_record("progress");if(bd_log){fprintf(bd_log,"loadresume progress holds=%u ms=%u critical_closed=%d mode=%d\n",street_holds.n,street_holds.ms,world_critical_closed(),mode);fflush(bd_log);}}
    if(loading_on || switch_phase || help_on)return;
    if(prof_stage!=PROF_OFF){pf_press(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
    if(front==FRONT_KEYBOARD){pf_press(pad,strlen(kb_name)<4?PSP_CTRL_CROSS:PSP_CTRL_START,now);return;}
    switch(lr_stage) {
    case 0:
        if(front==FRONT_TITLE){if(title_allow)pf_press(pad,PSP_CTRL_START,now);return;}
        if(front==FRONT_MAIN){pf_toward(pad,MM_CAREER,now);return;}
        if(front==FRONT_STARTCAREER){int it[3],n=sc_items(it);pf_take(pad,n-1,now);return;}
        if(front!=FRONT_CRIB && !(front==FRONT_OFF && mode==MODE_DRIVE))return;
        lr_stage=1;   /* (the list runs within the press: the garage before pf_take's second and a half) */
        /* fall through */
    case 1:   /* the list's career: the garage (or, its start not done, the street) */
        if(front==FRONT_CRIB){pf_record("loaded");lr_check(savedata_slot()>0,"Load Game took a slot of its own");lr_stage=2;pf_fired=0;}
        else if(front==FRONT_OFF && mode==MODE_DRIVE){pf_record("loaded-street");lr_stage=3;lr_at=now;lr_holds_ms=street_holds.ms;}
        return;
    case 2:   /* Resume Career */
        if(front==FRONT_CRIB){if(pf_take(pad,CRIB_RESUME,now)){}return;}
        if(front==FRONT_OFF && mode==MODE_DRIVE){pf_record("street");lr_stage=3;lr_at=now;lr_holds_ms=street_holds.ms;}
        return;
    case 3:   /* the street played: 15 s of it, the world there */
        if(!(front==FRONT_OFF && mode==MODE_DRIVE))return;
        if(now-lr_at<15000000)return;
        lr_check(!world_critical_closed(),"the world's loader open on the street");
        lr_check(street_holds.ms-lr_holds_ms<8000,"the street not held for its world (15 s played)");
        lr_end("street-ran");return;
    }
}
