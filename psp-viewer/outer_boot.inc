/* Explicit boot-drive.txt "outer hold": continuous normal-pad smoke.
 * No race/car/progress writes, resets, teleports, accelerated time or exits.
 * Read-only line follower; PSPLink runner owns termination even on failure.
 * "outer pause": the same cold boot, then by the pad 20 s into the race the
 * pause's Restart (OK), 15 s into the restarted race the pause's Quit to
 * Main Menu (OK), and 5 s on the main menu. */
extern unsigned fe_load_failures;
static int bo_stage,bo_cursor=-1,bo_lap=-2,bo_done;
static uint64_t bo_begin,bo_pulse,bo_log_at,bo_menu_at;
static float bo_x,bo_z,bo_distance,bo_progress,bo_progress_at;
static unsigned bo_nan;
static int bo_ps;   /* "outer pause": its step */
static void bo_record(const char *event) {
    const WorldStats *w=world_stats();
    if(bd_log){fprintf(bd_log,"outer %s wall=%.3f stage=%d front=%d screen=%d mode=%d race=%d event=%d laps=%d lap=%d finished=%d dnf=%d clock=%.3f distance=%.3f loads=%u read_fail=%u reset=%u fe_fail=%u intro=%.3f\n",event,bd_now(),bo_stage,front,front>0 && front<FRONT_N && front_s[front]!=NULL,mode,race.state,race_event(race.event)?race_event(race.event)->id:-1,race.laps,race.r[0].lap,race.r[0].finished,race.r[0].dnf,race.clock,bo_distance,w->loads,w->failed_why[1],car_nan_resets,fe_load_failures,race_intro);fflush(bd_log);}
}
static void bo_end(int passed,const char *why) {
    bd_tground_summary();bo_record(why);bo_done=1;bd_flow=-1;
    if(bd_log){fprintf(bd_log,"outer-result %s\n",passed?"PASS":"FAIL");fclose(bd_log);bd_log=NULL;}
    /* Remain alive, pad released; no process exit and no PSPLink reset here. */
}
static void bo_action(SceCtrlData *pad,unsigned action,uint64_t now) {
    if(now-bo_pulse<500000)return;
    bo_pulse=now;pad->Buttons=action;
}
/* The pause's item with this action: Left/Right to it, X; then OK (Right) and X. 1 once asked and answered. */
static int bo_pause_pick(SceCtrlData *pad,uint64_t now,int action) {
    if(menu!=MENU_MAIN)return 0;
    if(pause_ask){bo_action(pad,pause_ok?PSP_CTRL_CROSS:PSP_CTRL_RIGHT,now);return pause_ok && pad->Buttons==PSP_CTRL_CROSS;}
    int n,want=-1;const PauseItem *it=pause_items(&n);for(int k=0;k<n;k++)if(it[k].action==action)want=k;
    if(want<0)return 0;
    bo_action(pad,menu_sel<want?PSP_CTRL_RIGHT:menu_sel>want?PSP_CTRL_LEFT:PSP_CTRL_CROSS,now);return 0;
}
/* "outer pause" in the race: 1 while it has the pad (the pause, the restart's countdown, the quit) */
static int bo_pause_step(SceCtrlData *pad,uint64_t now) {
    switch(bo_ps) {
    case 0:case 4:
        if(race.state!=RACE_ON || race.clock<(bo_ps?15.f:20.f) || menu!=MENU_OFF)return 0;
        bo_action(pad,PSP_CTRL_START,now);if(pad->Buttons){bo_ps++;bo_record(bo_ps==1?"pause-restart":"pause-quit");}return 1;
    case 1:case 5:
        if(menu==MENU_MAIN && pause_ask && pause_s){static int shot[2];if(!shot[bo_ps>1]++)capture_frame_as(bo_ps==1?"outer-04b-pause.ppm":"outer-05b-pause-quit.ppm");}   /* the carousel then its question drawn by now */
        if(menu==MENU_OFF && now-bo_pulse>3000000){bo_end(0,"pause-not-open");return 1;}
        if(bo_pause_pick(pad,now,bo_ps==1?P_RESTART:P_MAIN)){bo_ps++;bo_record(bo_ps==2?"restart-ok":"quit-ok");}
        return 1;
    case 2:   /* the restart: its countdown, then the race on from the start */
        if(race.state==RACE_ON && race.clock<3.f && menu==MENU_OFF && !loading_on){bo_ps=4;bo_x=car.x;bo_z=car.z;bo_cursor=-1;bo_lap=-2;bo_progress=bo_distance=0;bo_progress_at=race.clock;bo_record("restarted");capture_frame_as("outer-04c-restarted.ppm");return 0;}
        return 1;
    case 6:   /* the quit: the main menu */
        if(front==FRONT_MAIN && front_s[FRONT_MAIN] && mode==MODE_SHOWROOM && race.state==RACE_OFF && !loading_on){bo_ps=7;bo_stage=6;bo_menu_at=now;bo_record("return-main");capture_frame_as("outer-06-main.ppm");}
        return 1;
    }
    return 1;
}
static void bo_pad(SceCtrlData *pad,uint64_t now) {
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(!bo_begin){bo_begin=now;bo_nan=car_nan_resets;bo_record("cold-title");capture_frame_as("outer-01-title.ppm");}
    if(bo_done)return;
    if(now-bo_begin>1200000000ULL){bo_end(0,"deadline");return;}
    if(admission_failed || fe_load_failures || world_stats()->failed_why[1] || car_nan_resets!=bo_nan){bo_end(0,"load-or-reset-failure");return;}
    if(now-bo_log_at>5000000){bo_log_at=now;bo_record("progress");}
    if(bo_stage==0 && front==FRONT_TITLE){if(now-bo_begin>100000000)bo_action(pad,PSP_CTRL_START,now);return;}
    if(bo_stage==0 && front==FRONT_MAIN){bo_record("main-visible");capture_frame_as("outer-02-main.ppm");bo_stage=1;}
    if(bo_stage<2) {
        if(loading_on || switch_phase)return;
        if(prof_stage!=PROF_OFF){bo_end(0,"unexpected-profile-dialog");return;}
        unsigned key=PSP_CTRL_CROSS;int want=-1;
        if(front==FRONT_MAIN)want=1;
        else if(front==FRONT_QRMODE)want=0;
        else if(front==FRONT_QRTRACK){int list[QR_MAX],n=qr_tracks(list);for(int k=0;k<n;k++)if(race_event(list[k])->id==4001)want=k;if(want<0){bo_end(0,"missing-outer-ring");return;}}
        else if(front==FRONT_QROPTS){if(qr_opt[QO_LAPS]!=2)key=front_sel[front]<2?PSP_CTRL_DOWN:front_sel[front]>2?PSP_CTRL_UP:qr_opt[QO_LAPS]>2?PSP_CTRL_LEFT:PSP_CTRL_RIGHT;}
        else if(front==FRONT_QRCAR){if(qr_dialog)key=qr_manual?PSP_CTRL_LEFT:PSP_CTRL_CROSS;else want=0;}
        if(want>=0 && front_sel[front]!=want)key=front_sel[front]<want?PSP_CTRL_RIGHT:PSP_CTRL_LEFT;
        if(front!=FRONT_OFF){bo_action(pad,key,now);return;}
        if(mode!=MODE_DRIVE || race.state!=RACE_COUNTDOWN || !race_event(race.event) || race_event(race.event)->id!=4001 || race.laps!=2){bo_end(0,"wrong-race-entry");return;}
        bo_stage=2;bo_record("intro-start");capture_frame_as("outer-03-intro.ppm");
    }
    if(bo_stage==2) {
        /* Watch the entire scene. No CROSS shortcut during countdown. */
        if(race.state!=RACE_ON)return;
        bo_stage=3;bo_x=car.x;bo_z=car.z;bo_progress_at=race.clock;bo_record("race-start");capture_frame_as("outer-04-start.ppm");
    }
    if(bo_stage==3) {
        if(bo_pause && bo_pause_step(pad,now))return;
        if(race.state==RACE_DONE) {
            if(!race.r[0].finished || race.r[0].dnf || race.r[0].lap!=2 || bo_distance<12000.f){bo_end(0,"incomplete-race");return;}
            bo_stage=4;bo_record("natural-finish");
        } else {
            if(race.state!=RACE_ON || car.fell){bo_end(0,"race-or-ground-lost");return;}
            float dx=car.x-bo_x,dz=car.z-bo_z;bo_distance+=sqrtf(dx*dx+dz*dz);bo_x=car.x;bo_z=car.z;
            if(bo_lap!=race.r[0].lap){bo_lap=race.r[0].lap;bo_record("lap");}
            if(bo_distance>bo_progress+5.f){bo_progress=bo_distance;bo_progress_at=race.clock;}
            if(race.clock-bo_progress_at>20.f){bo_end(0,"pilot-stuck-no-reset");return;}
            const RaceLine *L=race_line(4001);if(!L || L->n<100){bo_end(0,"missing-line");return;}
            float best=1e30f;int at=0,lo=bo_cursor<0?0:bo_cursor-5,hi=bo_cursor<0?L->n:bo_cursor+22;
            for(int k=lo;k<hi;k++){int j=(k%L->n+L->n)%L->n;const LinePoint *p=&L->p[j];float x=car.x-p->x,z=car.z-p->z,d=sqrtf(x*x+z*z)+fabsf(car.ground-p->y)*.5f;if(d<best){best=d;at=k;}}
            if(best>18.f){bo_end(0,"pilot-left-corridor");return;}
            if(bo_cursor<at || bo_cursor<0)bo_cursor=at;
            float speed=sqrtf(car.vx*car.vx+car.vz*car.vz),distance=0,bend=0;int target=bo_cursor;
            while(distance<7.f+speed*.55f && target-bo_cursor<50){const LinePoint *a=&L->p[target%L->n],*b=&L->p[(target+1)%L->n];float x=a->x-b->x,z=a->z-b->z;distance+=sqrtf(x*x+z*z);target++;}
            for(int k=bo_cursor;k<target+6;k++)bend=fmaxf(bend,fabsf(L->p[k%L->n].k));
            const LinePoint *p=&L->p[target%L->n];float err=atan2f(p->x-car.x,p->z-car.z)-car.yaw;while(err>GU_PI)err-=2*GU_PI;while(err<-GU_PI)err+=2*GU_PI;
            float wanted=fminf(25.f,fminf(sqrtf(3.f/fmaxf(bend,.0001f)),fmaxf(5.f,25.f/(1+2*fabsf(err)))));
            unsigned actions=(speed<wanted-.5f?PSP_CTRL_CROSS:0)|(car.vx*sinf(car.yaw)+car.vz*cosf(car.yaw)>fmaxf(wanted+1.f,4.5f)?PSP_CTRL_SQUARE:0);
            for(unsigned bit=1;bit<0x100000;bit<<=1)if(ctl_map(bit,opt_val[OP_CONTROL],CTL_RACE)&actions)pad->Buttons|=bit;
            pad->Lx=(unsigned char)bot_lx(err*1.5f);return;
        }
    }
    if(bo_stage==4) {
        if(fin.phase<2 || !results_s || !postrace_s || loading_on)return;
        bo_stage=5;bo_record("results-visible");capture_frame_as("outer-05-results.ppm");bo_pulse=now;return;
    }
    if(bo_stage==5) {
        if(front==FRONT_MAIN && front_s[FRONT_MAIN] && mode==MODE_SHOWROOM && race.state==RACE_OFF && !loading_on){bo_stage=6;bo_menu_at=now;bo_record("return-main");capture_frame_as("outer-06-main.ppm");return;}
        unsigned key=PSP_CTRL_CROSS;
        if(post_stage==3){if(postrace_ask)key=postrace_ok?PSP_CTRL_CROSS:PSP_CTRL_LEFT;else{int n;const PostItem *items=postrace_items(&n);if(postrace_sel<0 || postrace_sel>=n){bo_end(0,"postrace-selection");return;}if(items[postrace_sel].action!=PR_QUIT)key=PSP_CTRL_RIGHT;}}
        bo_action(pad,key,now);return;
    }
    if(bo_stage==6 && now-bo_menu_at>5000000){if(bo_pause)capture_frame_as("outer-07-main-5s.ppm");bo_end(front==FRONT_MAIN && front_s[FRONT_MAIN] && !loading_on,"complete");}
}
