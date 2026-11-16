/* Explicit newcareer hold: normal pad from cold title to New Career Yes,
 * full opening film and arrival. With a profile on the stick, as the
 * original: a new name (four letters), "save the profile?" Yes and the
 * system's SAVE (PPSSPP_DIALOG_AUTO answers it headless), which writes the
 * profile and the new career. Without one: No to the profile, no save. */
static int bc_stage,bc_done;
static uint64_t bc_begin,bc_report_at,bc_pulse,bc_city;
static void bc_record(const char *event) {
    char music[512];int blocks,drops,starved;
    audio_music_diag(music,sizeof music);audio_music_health(&blocks,&drops,&starved);
    if(bd_log){fprintf(bd_log,"career %s wall=%.3f stage=%d front=%d screen=%d mode=%d loading=%d prof=%d yes=%d shown=%u fe_fail=%u blocks=%d drops=%d starved=%d music=%s\n",event,bd_now(),bc_stage,front,front>0 && front<FRONT_N && front_s[front]!=NULL,mode,loading_on,prof_stage,prof_yes,shown,fe_load_failures,blocks,drops,starved,music);fflush(bd_log);}
}
static void bc_end(int pass,const char *why) {
    bd_tground_summary();bc_record(why);bc_done=1;bd_flow=-1;
    if(bd_log){fprintf(bd_log,"career-result %s\n",pass?"PASS":"FAIL");fclose(bd_log);bd_log=NULL;}
}
static void bc_action(SceCtrlData *pad,unsigned action,uint64_t now) {
    if(now-bc_pulse<700000)return;
    bc_pulse=now;pad->Buttons=action;
}
static void bc_pad(SceCtrlData *pad,uint64_t now) {
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(!bc_begin){bc_begin=now;bc_record("cold-title");}
    if(now-bc_begin>480000000ULL){bc_end(0,"deadline");return;}
    if(fe_load_failures || admission_failed){bc_end(0,"load-failure");return;}
    if(now-bc_report_at>5000000){bc_report_at=now;bc_record("progress");}
    if(loading_on || switch_phase)return;
    if(bc_stage==0){
        if(front==FRONT_TITLE){if(now-bc_begin>100000000)bc_action(pad,PSP_CTRL_START,now);return;}
        if(front!=FRONT_MAIN)return;
        bc_stage=1;bc_record("main-visible");capture_frame_as("career-01-main.ppm");
    }
    if(bc_stage==1){
        if(front==FRONT_MAIN){bc_action(pad,front_sel[front]==0?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
        if(front!=FRONT_STARTCAREER)return;
        bc_stage=2;bc_record("career-menu");capture_frame_as("career-02-menu.ppm");
    }
    if(bc_stage==2){   /* Career's menu (beta #136): its Start New Career */
        if(prof_stage==PROF_NEW_CAREER || prof_stage==PROF_ASK || front==FRONT_KEYBOARD){bc_stage=3;bc_record("new-career-confirm");capture_frame_as("career-03-confirm.ppm");return;}
        if(front!=FRONT_STARTCAREER){bc_end(0,"unexpected-career-screen");return;}
        int it[3],n=sc_items(it),want=-1;for(int k=0;k<n;k++)if(it[k]==SC_NEW)want=k;
        if(want<0){bc_end(0,"no-start-new-career");return;}
        bc_action(pad,front_sel[front]==want?PSP_CTRL_CROSS:PSP_CTRL_RIGHT,now);return;
    }
    if(bc_stage==3){
        if(prof_stage==PROF_NEW_CAREER){bc_action(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
        if(prof_stage==PROF_ASK){bc_action(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}   /* Yes: a career needs a profile */
        if(prof_stage==PROF_SAVE){bc_action(pad,prof_yes?PSP_CTRL_CROSS:PSP_CTRL_LEFT,now);return;}
        if(front==FRONT_KEYBOARD){bc_action(pad,strlen(kb_name)<4?PSP_CTRL_CROSS:PSP_CTRL_START,now);return;}
        if(front!=FRONT_OFF || mode!=MODE_DRIVE)return;
        bc_stage=4;bc_city=now;bc_record("city-entered");capture_frame_as("career-04-city.ppm");
    }
    if(bc_stage==4 && now-bc_city>12000000){bc_end(front==FRONT_OFF && mode==MODE_DRIVE && !loading_on,"city-running");}
}
