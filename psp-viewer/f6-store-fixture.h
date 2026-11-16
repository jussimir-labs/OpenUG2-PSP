/* DIAG_F6_STORE_FIXTURE only. Initial boot marker, NOT a replay.
 * After boot all diagnostic writes are pad/logging; ShopNIS owns its poses. */
static struct { int initialized,csv_opened,finished,save_loaded,pad_error,positive;FILE *log,*effective; } f6_file;
static struct { int attempted,ready,reason,active_car;unsigned money,spent,trades;float ground; } f6_boot;
static CareerState f6_economy_before;
static int f6_economy_unchanged(void) {
 const CareerState *a=&f6_economy_before,*b=career_state();
 return a->money==b->money && a->spent==b->spent && a->rep==b->rep && a->races==b->races && a->car==b->car && a->stage==b->stage && a->slots_open==b->slots_open && a->slot_active==b->slot_active && !memcmp(a->slot_car,b->slot_car,sizeof a->slot_car) && !memcmp(a->slot_look,b->slot_look,sizeof a->slot_look) && !memcmp(a->slot_perf,b->slot_perf,sizeof a->slot_perf) && !memcmp(&a->look,&b->look,sizeof a->look) && !memcmp(&a->perf,&b->perf,sizeof a->perf);
}
static struct { int wanted,committed,shop_preserved,same_exit,back_step,clearance,turn_dialogs;float back_x,back_z,back_yaw,back_distance,back_actual;CareerState after; } f6_positive;
static int f6_state_expected(void) {
    if(!f6_file.positive)return f6_economy_unchanged() && ui_seen.trades==f6_boot.trades;
    const CareerState *b=career_state(),*a=&f6_economy_before;
    if(!f6_positive.committed && b->car==a->car)return f6_economy_unchanged() && ui_seen.trades==f6_boot.trades;
    if(b->car!=f6_positive.wanted || ui_seen.trades!=f6_boot.trades+1 || b->money!=a->money || b->spent!=a->spent || b->rep!=a->rep || b->races!=a->races || b->stage!=a->stage || b->slots_open!=a->slots_open || b->slot_active!=a->slot_active)return 0;
    CarLook stock;CarPerf perf;garage_stock_look(&stock);perf_stock(&perf);
    if(memcmp(&b->look,&stock,sizeof stock) || memcmp(&b->perf,&perf,sizeof perf) || b->slot_car[b->slot_active]!=b->car || memcmp(&b->slot_look[b->slot_active],&stock,sizeof stock) || memcmp(&b->slot_perf[b->slot_active],&perf,sizeof perf))return 0;
    for(int k=0;k<GARAGE_SLOTS;k++)if(k!=a->slot_active && (b->slot_car[k]!=a->slot_car[k] || memcmp(&b->slot_look[k],&a->slot_look[k],sizeof a->slot_look[k]) || memcmp(&b->slot_perf[k],&a->slot_perf[k],sizeof a->slot_perf[k])))return 0;
    if(!f6_positive.committed){f6_positive.after=*b;f6_positive.committed=1;}
    return 1;
}
/* Geometry queries only: actual model body at the GC ShopExit endpoint,
 * and a short rearward corridor before the original street waypoint. */
static int f6_back_clearance(void) {
    const CarPhys *ph=car.phys;
    if(!ph || !isfinite(ph->body[0]) || !isfinite(ph->body[1]) || ph->body[0]<=0 || ph->body[1]<=0)return 0;
    f6_positive.back_x=car.x;f6_positive.back_z=car.z;f6_positive.back_yaw=car.yaw;
    const CarPhys *reference=car_phys("car-PEUGOT");
    f6_positive.back_distance=ph->body[1]*.5f+fmaxf(0,ph->body[0]-reference->body[0])*.5f+.25f;
    /* Model half-width, extra half-length over the proven Peugeot, .25m margin. */
    float fx=sinf(car.yaw),fz=cosf(car.yaw),rx=fz,rz=-fx;
    for(float d=0;d<=f6_positive.back_distance+.5f;d+=.25f) {
        float x=car.x-fx*d,z=car.z-fz*d,gy;
        if(!world_collision_known(x,z) || !world_ground(x,z,car.ground,1.2f,.6f,&gy))return 0;
        for(int i=-2;i<=2;i++)for(int j=-1;j<=1;j++) {
            float px=x+fx*(ph->body[0]*.25f*i)+rx*(ph->body[1]*.5f*j);
            float pz=z+fz*(ph->body[0]*.25f*i)+rz*(ph->body[1]*.5f*j);
            if(!world_collision_known(px,pz))return 0;
            int kind=world_solid_kind(px,pz,gy,1.2f,.6f);
            if(kind==SOLID_WALL){float nx,nz,lo,hi;
                if(world_wall_strip(floorf(px)+.5f,floorf(pz)+.5f,&nx,&nz,&lo,&hi)){
                    float q=nx*px+nz*pz;if(q<lo || q>hi)kind=SOLID_NONE;
                }
            }
            if(kind)return 0; /* same wall-strip geometry as vehicle.c, no bypass */
        }
    }
    return 1;
}
static float f6_street_distance(float x,float y,float z) {
    if(roads_count()<=5704)return 1e30f;
    const RoadNode *a=roads_node(5703),*b=roads_node(5704);
    float dx=b->x-a->x,dz=b->z-a->z,l2=dx*dx+dz*dz;
    if(!isfinite(l2) || l2<1e-6f)return 1e30f;
    float u=fmaxf(0,fminf(1,((x-a->x)*dx+(z-a->z)*dz)/l2));
    if(fabsf(y-a->y-u*(b->y-a->y))>1.f)return 1e30f;
    return hypotf(x-a->x-u*dx,z-a->z-u*dz);
}
static int f6_natural_close(void) {
    int pad_ok=1;
    if(f6_file.effective){pad_ok=fclose(f6_file.effective)==0;f6_file.effective=NULL;}
    if(!f6_file.csv_opened)return pad_ok;
    FILE *f=f6_file.log;f6_file.log=NULL;f6_file.csv_opened=0;
    return fclose(f)==0 && pad_ok;
}
/* Called once after loading_stop and before frames=0/the main loop. */
static int f6_store_boot_prepare(int ordinal) {
    if(f6_boot.attempted)return f6_boot.ready;
    f6_boot.attempted=1;f6_boot.ground=18.093f;
    f6_economy_before=*career_state();
    f6_boot.active_car=career_state()->car;f6_boot.money=career_state()->money;
    f6_boot.spent=career_state()->spent;f6_boot.trades=ui_seen.trades;
    f6_positive.wanted=-1;
    for(int k=0;k<fleet_count();k++)if(!strcmp(fleet_car(k)->model,"FOCUS"))f6_positive.wanted=k;
    const float x=-909.398f,z=-720.737f,heading=1.672302223f;
    float gy=18.093f;char profile[32]={0};fleet_phys(player_car,profile,sizeof profile);
    const CareerShop *shop=career_shop(9);
#define F6_BOOT_REFUSE(n) do{f6_boot.reason=(n);goto report;}while(0)
    if(ordinal!=1 || !f6_file.save_loaded || career_state()->stage!=1 || strcmp(profile,"car-PEUGOT") ||
       mode!=MODE_DRIVE || world_level || !world_ok || admission_failed || world_critical_closed() ||
       race.state!=RACE_OFF || front!=FRONT_OFF || menu!=MENU_OFF || shopnis_active() || shop_city!=-1)
        F6_BOOT_REFUSE(10);
    if(!shop || strcmp(shop->name,"CC_CAR_LOT_1") || !career_shop_visible(9) || !shopzones_has(shop->name) ||
       roads_count()<=5704 || hypotf(x-shop->x,z-shop->z)<12.f)F6_BOOT_REFUSE(11);
    /* Same product overlay and streaming calls, no collision bypass. */
    ge_fence();
    if(!world_overlay_load("bar-free1.n2cy",x,z))F6_BOOT_REFUSE(12);
    snprintf(overlay_name,sizeof overlay_name,"%s","bar-free1.n2cy");
    race_teleport_settle(x,z);   /* only streams; does not write a car pose */
    if(admission_failed || world_critical_closed() || !world_collision_known(x,z) ||
       !world_ground(x,z,18.093f,1.f,1.f,&gy) || !isfinite(gy) || fabsf(gy-18.093f)>1.f)
        F6_BOOT_REFUSE(12);
    for(int i=-1;i<=1;i++)for(int j=-1;j<=1;j++) {
        float px=x+sinf(heading)*1.92f*i+cosf(heading)*.825f*j;
        float pz=z+cosf(heading)*1.92f*i-sinf(heading)*.825f*j;
        if(!world_collision_known(px,pz))F6_BOOT_REFUSE(12);
    }
    if(f6_street_distance(x,gy,z)>2.f || !car_fits(x,gy,z,heading))F6_BOOT_REFUSE(13);
    {Car initial=car;
     car_place(&initial,x,gy,z,heading,model.floor_y,player_top());
     if(car_inside_solid(&initial))F6_BOOT_REFUSE(13);
     car=initial;}   /* the sole diagnostic initial-pose assignment, before simulation */
    gps_stage(1);camera_snap();f6_boot.ground=gy;f6_boot.ready=1;
report:
    {FILE *f=fopen("f6-initial.json","w");
     if(!f){f6_boot.ready=0;f6_boot.reason=17;}
     else {int ok=fprintf(f,"{\"kind\":\"partial-initial-marker-not-replay\",\"measured_steps\":0,\"start_frame\":0,\"save_loaded\":%d,\"stage\":%d,\"profile\":\"%s\",\"ready\":%d,\"reason\":%d,\"requested\":[%.4f,%.4f,%.4f,%.7f],\"actual\":[%.4f,%.4f,%.4f,%.7f],\"velocity\":[%.4f,%.4f,%.4f],\"overlay\":\"%s\",\"fixture_csv_frame\":1211,\"dataset\":\"macro935-six-overrides-external-SHA-check-required\"}\n",
          f6_file.save_loaded,career_state()->stage,profile,f6_boot.ready,f6_boot.reason,x,gy,z,heading,
          car.x,car.ground,car.z,car.yaw,car.vx,car.vy,car.vz,overlay_name)>=0;
          if(fclose(f)!=0)ok=0;if(!ok){f6_boot.ready=0;f6_boot.reason=17;}}
    }
#undef F6_BOOT_REFUSE
    return f6_boot.ready;
}
static void f6_natural_pad(SceCtrlData *pad,unsigned frame,float dt,int ordinal,int *end_frame) {
    static int phase=1,wp,dialog_step,screen_seen,exit_started,reason,street_ready;
    static int cycle=1,confirm_step,confirmation_seen,confirmation_cancelled,rearmed;
    static unsigned first,screen_at;static float seconds,progress_at,px,pz;
    static int progress_phase=-1;
    static const float in[3][2]={{-897,-722},{-883,-722},{-875.5f,-719.5f}};
    static const float out[2][2]={{-897,-722},{-909.5f,-721}};
    float ax=0,az=0,err=0,target=0,stall=0,dstreet=f6_street_distance(car.x,car.ground,car.z);
    pad->Buttons=0;pad->Lx=pad->Ly=128;
    if(f6_file.finished)return;
    if(!f6_file.initialized) {
        f6_file.initialized=1;first=frame;reason=f6_boot.reason;
        f6_file.log=fopen("f6-natural.csv","w");f6_file.csv_opened=f6_file.log!=NULL;
        if(!f6_file.csv_opened || fprintf(f6_file.log,"frame,phase,wp,x,y,z,yaw,speed,buttons,lx,wall,resets,nan,inside,scene,shop_city,mode,front,menu,diag_s,dt,aim_x,aim_z,aim_error,desired_speed,stall_s,street_dist,street_ready,enter_began,enter_ended,exit_began,exit_ended,skipped,doors,doors_open,reason,money,spent,trades\n")<0){phase=9;reason=17;}
        if(!f6_boot.ready || ordinal!=1 || frame!=0){phase=9;if(!reason)reason=10;}
    }
    seconds+=fmaxf(0,dt);
    if(frame-first>=3599 || seconds>120.f){phase=9;reason=4;}   /* bounded: no 300-second airport retry */
    if(f6_file.pad_error){phase=9;reason=17;}
    if(admission_failed || play.resets || play.nan){phase=9;reason=1;}
    if(!f6_state_expected()){phase=9;reason=14;}
    if((phase==1 || phase==5 || phase==6 || phase==7) && (car.wall_hits || car_inside_solid(&car))){phase=9;reason=7;}
    if((phase==1 || phase==6) && menu==MENU_SHOP) {
        if(shop_at!=9){phase=9;reason=15;}
        else {phase=2;dialog_step=0;screen_seen=0;confirm_step=0;capture_frame_as("f6-natural-dialog.ppm");}
    }
    if(phase==2) {
        if(menu!=MENU_SHOP || shop_at!=9){phase=9;reason=15;}
        else if(dialog_step==0)dialog_step=1;
        else if(dialog_step==2)dialog_step=1;
        else if(engage_choice){pad->Buttons=PSP_CTRL_LEFT;dialog_step=2;}
        else {pad->Buttons=PSP_CTRL_CROSS;phase=3;}
    }
    if(phase==1 || phase==5 || phase==6) {
        int inbound=phase!=5;int n=inbound?3:2;const float (*path)[2]=inbound?in:out;
        if(wp<n && hypotf(car.x-path[wp][0],car.z-path[wp][1])<2.5f)wp++;
        if(wp>=n && phase==5) {   /* no optional orbital waypoint */
            pad->Buttons=fabsf(speed_of(&car))>.2f?PSP_CTRL_SQUARE:0;
        } else {
            const CareerShop *shop=career_shop(9);
            ax=wp<n?path[wp][0]:shop->x;az=wp<n?path[wp][1]:shop->z;
            err=wrap_pi(atan2f(ax-car.x,az-car.z)-car.yaw);
            target=fabsf(err)>.5f?2.3f:5.f;
            if(inbound && (shopzones_at_height(shop->name,car.x,car.ground,car.z,shop->y) ||
                            hypotf(car.x-shop->x,car.z-shop->z)<12.f))target=2.3f;
            /* Inbound starts facing its first target. Outbound is the existing
             * historically observed forward/pad manoeuvre, not GPS U-turn recovery. */
            if(phase==6)err=wrap_pi(err+3.14159265f);
            if(phase==1 && fabsf(err)>1.5707963f){phase=9;reason=6;}
            else {pad->Lx=bot_lx(fmaxf(-1,fminf(1,err*(phase==6?-1.7f:1.7f))));
                  float v=fabsf(speed_of(&car));pad->Buttons=phase==6?(speed_of(&car)>.2f || v<target?PSP_CTRL_SQUARE:v>target+.5f?PSP_CTRL_CROSS:0):(v>target+.5f?PSP_CTRL_SQUARE:v<target?PSP_CTRL_CROSS:0);}
        }
    }
    if(phase==3 && mode==MODE_SHOWROOM && front!=FRONT_OFF) {
        if(front!=FRONT_CARLOT || shopsc_stats.began[0]!=(unsigned)cycle || shopsc_stats.ended[0]!=(unsigned)cycle){phase=9;reason=15;}
        else {
            if(!screen_seen){screen_seen=1;screen_at=frame;}
            if(cycle==2){if(frame==screen_at+30)pad->Buttons=PSP_CTRL_TRIANGLE;}
            else if(frame>=screen_at+30 && !(frame%2)) {
                int list[FLEET_MAX],n=lot_cars(list),selected=front_sel[FRONT_CARLOT];
                if(confirm_step==0) {
                    if(selected<0 || selected>=n){phase=9;reason=15;}
                    else if(f6_file.positive?list[selected]!=f6_positive.wanted:career_slot_of(list[selected])>=0)pad->Buttons=PSP_CTRL_RIGHT;
                    else {pad->Buttons=PSP_CTRL_CROSS;confirm_step=1;}
                } else if(confirm_step==1) {
                    if(!lot_confirm){phase=9;reason=18;}
                    else {
                        confirmation_seen=1;
                        if(f6_file.positive){f6_positive.shop_preserved=shop_city==9;pad->Buttons=PSP_CTRL_CROSS;confirm_step=3;}
                        else {pad->Buttons=PSP_CTRL_TRIANGLE;confirm_step=2;}
                    }
                } else if(confirm_step==2) {
                    if(lot_confirm){phase=9;reason=18;}
                    else {confirmation_cancelled=1;confirm_step=3;pad->Buttons=PSP_CTRL_TRIANGLE;}
                }
            }
        }
    }
    if(phase==3 && shopsc_stats.began[1]==(unsigned)cycle){phase=4;exit_started=1;}
    if(phase==4 && f6_file.positive && !f6_positive.same_exit) {
        float marker[3];unsigned angle;
        if(!f6_positive.shop_preserved || !shopnis_marker("CC_CAR_LOT_1",marker,&angle) || hypotf(car.x-marker[1],car.z-marker[0])>12.f || shopnis_which()!=SHOPNIS_EXIT){phase=9;reason=19;}
        else f6_positive.same_exit=1;
    }
    if(phase==4 && shopsc_stats.ended[1]==(unsigned)cycle){
        phase=f6_file.positive?7:5;wp=0;capture_frame_as("f6-natural-exit.ppm");
        if(f6_file.positive){f6_positive.clearance=f6_back_clearance();if(!f6_positive.clearance){phase=9;reason=20;}}
    }
    if(phase==7) {
        const float fx=sinf(f6_positive.back_yaw),fz=cosf(f6_positive.back_yaw);
        f6_positive.back_actual=-(car.x-f6_positive.back_x)*fx-(car.z-f6_positive.back_z)*fz;
        ax=f6_positive.back_x-fx*f6_positive.back_distance;az=f6_positive.back_z-fz*f6_positive.back_distance;
        err=wrap_pi(f6_positive.back_yaw-car.yaw);target=.8f;
        if(menu==MENU_SHOP && shop_at==9){pad->Buttons=PSP_CTRL_CIRCLE;f6_positive.turn_dialogs++;}
        else if(menu!=MENU_OFF || front!=FRONT_OFF || shopnis_active()){phase=9;reason=21;}
        else if(!f6_positive.back_step) {
            pad->Lx=bot_lx(fmaxf(-1,fminf(1,-err*1.7f)));
            if(f6_positive.back_actual>=f6_positive.back_distance){f6_positive.back_step=1;pad->Buttons=PSP_CTRL_CROSS;}
            else {float v=speed_of(&car);pad->Buttons=v>-target?PSP_CTRL_SQUARE:v<-target-.3f?PSP_CTRL_CROSS:0;}
        } else {
            pad->Buttons=speed_of(&car)<-.05f?PSP_CTRL_CROSS:0;
            if(speed_of(&car)>=-.05f){phase=5;wp=0;pad->Buttons=0;pad->Lx=128;}
        }
    }
    if(phase==5) {
        int live=!shopnis_active() && shop_city==-1 && mode==MODE_DRIVE && front==FRONT_OFF && menu==MENU_OFF;
        if(live && dstreet<=2.f && speed_of(&car)>1.f)street_ready++;else street_ready=0;
        if(!f6_file.positive && cycle==1 && live && wp>=2 && fabsf(speed_of(&car))<.2f && shop_declined==-1 && !worldanim_stats()->doors_open) {
            cycle=2;phase=6;wp=0;street_ready=0;rearmed=1;
        }
        if(f6_file.positive && street_ready>=3 && f6_positive.committed && f6_positive.same_exit && confirmation_seen && player_car==f6_positive.wanted && shopsc_stats.began[0]==1 && shopsc_stats.ended[0]==1 && shopsc_stats.began[1]==1 && shopsc_stats.ended[1]==1 && !shopsc_stats.skipped && worldanim_stats()->doors==23 && worldanim_stats()->doors_open==0 && !car.wall_hits && !play.resets && !play.nan && !car_inside_solid(&car)){phase=8;}
        if(!f6_file.positive && cycle==2 && street_ready>=3 && screen_seen && confirmation_seen && confirmation_cancelled && rearmed && exit_started && shopsc_stats.began[0]==2 && shopsc_stats.ended[0]==2 &&
           shopsc_stats.began[1]==2 && shopsc_stats.ended[1]==2 && !shopsc_stats.skipped &&
           worldanim_stats()->doors==23 && worldanim_stats()->doors_open==0 && !car.wall_hits &&
           !play.resets && !play.nan && !car_inside_solid(&car)){phase=8;capture_frame_as("f6-natural-street.ppm");}
    }
    if(phase==1 || phase==5 || phase==6 || phase==7) {
        if(progress_phase!=phase){progress_phase=phase;progress_at=seconds;px=car.x;pz=car.z;}
        if(hypotf(car.x-px,car.z-pz)>=3.f){progress_at=seconds;px=car.x;pz=car.z;}
        stall=seconds-progress_at;
        if(stall>=8.f){phase=9;reason=3;}
    } else progress_phase=-1;
    if(shopsc_stats.skipped){phase=9;reason=16;}
    if(phase>=8){pad->Buttons=0;pad->Lx=pad->Ly=128;}
    if(f6_file.csv_opened && fprintf(f6_file.log,"%u,%d,%d,%.4f,%.4f,%.4f,%.5f,%.4f,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.5f,%.3f,%.4f,%.4f,%d,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u\n",
        frame,phase,wp,car.x,car.ground,car.z,car.yaw,speed_of(&car),(unsigned)pad->Buttons,(unsigned)pad->Lx,
        car.wall_hits,play.resets,play.nan,car_inside_solid(&car),shopnis_which(),shop_city,mode,front,menu,
        seconds,dt,ax,az,err,target,stall,dstreet,street_ready,shopsc_stats.began[0],shopsc_stats.ended[0],
        shopsc_stats.began[1],shopsc_stats.ended[1],shopsc_stats.skipped,worldanim_stats()->doors,
        worldanim_stats()->doors_open,reason,career_state()->money,career_state()->spent,ui_seen.trades)<0){phase=9;reason=17;}
    if(phase>=8) {
        f6_file.finished=1;int closed=f6_natural_close();if(!closed){phase=9;reason=17;}
        FILE *result=fopen("f6-fixture-result.json","w");
        if(!result){phase=9;reason=17;}
        else {int ok=fprintf(result,"{\"phase\":%d,\"reason\":%d,\"frame\":%u,\"csv_close_ok\":%d,\"street_ready\":%d,\"street_dist\":%.4f,\"enter\":[%u,%u],\"exit\":[%u,%u],\"skipped\":%u,\"doors\":[%u,%u],\"confirmation\":[%d,%d],\"rearmed\":%d,\"economy_slots_unchanged\":%d,\"reverse_clearance\":%d,\"reverse_requested\":%.4f,\"reverse_actual\":%.4f,\"turn_dialog_cancels\":%d,\"positive\":%d,\"committed\":%d,\"wanted_car\":%d,\"actual_car\":%d,\"player_car\":%d,\"same_shop_exit\":%d,\"state_expected\":%d,\"no_buy\":%d,\"money\":[%u,%u],\"spent\":[%u,%u],\"trades\":[%u,%u]}\n",
            phase,reason,frame,closed,street_ready,dstreet,shopsc_stats.began[0],shopsc_stats.ended[0],
            shopsc_stats.began[1],shopsc_stats.ended[1],shopsc_stats.skipped,worldanim_stats()->doors,worldanim_stats()->doors_open,
            confirmation_seen,confirmation_cancelled,rearmed,f6_economy_unchanged(),
            f6_positive.clearance,f6_positive.back_distance,f6_positive.back_actual,f6_positive.turn_dialogs,
            f6_file.positive,f6_positive.committed,f6_positive.wanted,career_state()->car,player_car,f6_positive.same_exit,f6_state_expected(),
            ui_seen.trades==f6_boot.trades && career_state()->money==f6_boot.money && career_state()->spent==f6_boot.spent && career_state()->car==f6_boot.active_car,
            f6_boot.money,career_state()->money,f6_boot.spent,career_state()->spent,f6_boot.trades,ui_seen.trades)>=0;
            if(fclose(result)!=0)ok=0;if(!ok){phase=9;reason=17;}}

        capture_frame_as(phase==8?"f6-natural-passed.ppm":"f6-natural-failed.ppm");*end_frame=(int)(frame+1);
    }
}

/* Captured after script fallback/rewind and immediately before input dispatch. */
static void f6_effective_pad(const SceCtrlData *p,uint32_t pressed,unsigned frame) {
    if(f6_file.finished)return;
    if(!f6_file.effective){
        f6_file.effective=fopen("f6-effective-pad.csv","w");
        if(!f6_file.effective || fprintf(f6_file.effective,"frame,buttons,lx,ly,pressed,menu,front,scene,shop_city\n")<0){f6_file.pad_error=1;return;}
    }
    if(fprintf(f6_file.effective,"%u,%u,%u,%u,%u,%d,%d,%d,%d\n",frame,(unsigned)p->Buttons,(unsigned)p->Lx,(unsigned)p->Ly,(unsigned)pressed,menu,front,shopnis_which(),shop_city)<0)f6_file.pad_error=1;
}

/* Separate private restart: the normal main boot calls career_load and loads its car. */
static void f6_reload_report(void) {
    const CareerState *c=career_state();char profile[32];fleet_phys(player_car,profile,sizeof profile);
    FILE *f=fopen("f6-reload.json","w");if(!f)return;
    fprintf(f,"{\"normal_boot_save_loaded\":%d,\"car\":%d,\"player_car\":%d,\"profile\":\"%s\",\"money\":%u,\"spent\":%u,\"rep\":%u,\"races\":%u,\"stage\":%u,\"slots_open\":%u,\"slot_active\":%u,\"garage\":[%d,%d,%d,%d,%d]}\n",f6_file.save_loaded,c->car,player_car,profile,c->money,c->spent,c->rep,c->races,c->stage,c->slots_open,c->slot_active,c->slot_car[0],c->slot_car[1],c->slot_car[2],c->slot_car[3],c->slot_car[4]);fclose(f);
}
