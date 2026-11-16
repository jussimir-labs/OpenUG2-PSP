/* DIAG_MYCARS_FLOW only: regular front_input buttons; no state setup,
 * unlock override, helper install or automatic approval. */
static struct { int phase,reason,target,car;unsigned stock,trial,confirmed,skin_before,skin_after;int initial_paint,paint_target;CareerState career; } mcflow;
static unsigned mc_geometry(void) {
    unsigned h=2166136261u;
    for(uint32_t i=0;i<model.bytes;i++)h=(h^model.data[i])*16777619u;
    return h;
}
static unsigned mc_skin(void) { unsigned h=2166136261u;for(unsigned i=0;i<sizeof skin16;i++)h=(h^((unsigned char *)skin16)[i])*16777619u;return h; }
static void mc_result(void) {
    FILE *f=fopen("mycars-flow.json","wb");if(!f)return;
    fprintf(f,"{\"skin_before\":%u,\"skin_after\":%u,\"paint\":%d,\"full_look_equal\":%d,\"quick\":%d,\"state\":%d,",mcflow.skin_before,mcflow.skin_after,player_look.paint,mycars_look(0) && !memcmp(mycars_look(0),&player_look,sizeof player_look),race.quick,race.state);
    fprintf(f,"\"phase\":%d,\"reason\":%d,\"target\":%d,\"edited_car\":%d,\"stock_geometry\":%u,\"preview_geometry\":%u,\"confirmed_geometry\":%u,\"count\":%d,\"career_unchanged\":%d,\"car\":%d,\"look_kit\":%d,\"meshes\":%u,\"triangles\":%u}",mcflow.phase,mcflow.reason,mcflow.target,mcflow.car,mcflow.stock,mcflow.trial,mcflow.confirmed,mycars_count(),!memcmp(&mcflow.career,career_state(),sizeof mcflow.career),player_car,player_look.opt[0],(unsigned)model.nm,(unsigned)model.triangles);
    fclose(f);
}
static uint32_t mc_step(unsigned frame,int script) {
    static int captured,on=-1;static unsigned since;
    if(!captured){mcflow.career=*career_state();captured=1;if(script==117){mcflow.phase=30;mcflow.target=mycars_look(0)?mycars_look(0)->opt[0]:0;}else if(script==118)mcflow.phase=40;else if(script==119)mcflow.phase=60;else if(script==120)mcflow.phase=70;}
    if(on!=mcflow.phase){on=mcflow.phase;since=frame;}
    if((mcflow.phase==24 && front==FRONT_MAIN) || mcflow.phase==36 || mcflow.phase==56 || mcflow.phase==68){mc_result();return 0;}
    if(mcflow.reason || frame-since>240){if(!mcflow.reason)mcflow.reason=90;mc_result();return 0;}
    if(frame-since<20 || frame%20)return 0;
    int phase=mcflow.phase;
    if(front==FRONT_TITLE)return PSP_CTRL_START;
    if(phase==0 || phase==30 || phase==40){if(front==FRONT_MAIN){if(front_sel[front]!=2)return nav_press(2);mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==1 || phase==8){if(front==FRONT_MYCARS){mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==2 || phase==9 || phase==18 || phase==33){if(front==FRONT_CUSTOM){if(shop_menu!=SHOPM_CAR){mcflow.reason=1;mc_result();return 0;}mcflow.phase++;return shop_press(ITEM_BODY);}}
    else if(phase==3 || phase==10 || phase==19 || phase==34){if(front==FRONT_CUSTOM && shop_menu==SHOPM_BODY){mcflow.phase++;return shop_press(CUST_KIT);}}
    else if(phase==4 || phase==11 || phase==13){if(front==FRONT_PARTS && cust_cat==CUST_KIT){
        if(!mcflow.stock){capture_frame_as("mycars-stock-parts.ppm");mcflow.stock=mc_geometry();for(int k=1;k<cust_count(CUST_KIT);k++)if(cust_open(CUST_KIT,k)){mcflow.target=k;break;}if(!mcflow.target){mcflow.reason=2;mc_result();return 0;}}
        if(front_sel[front]!=mcflow.target)return nav_press(mcflow.target);
        if(player_look.opt[0]!=mcflow.target || player_custom_car!=player_car || player_custom_lod!=1)return 0;
        mcflow.trial=mc_geometry();if(mcflow.trial==mcflow.stock){mcflow.reason=3;mc_result();return 0;}
        capture_frame_as(phase==4?"mycars-preview.ppm":"mycars-confirm-preview.ppm");mcflow.phase++;
        return phase==4?PSP_CTRL_TRIANGLE:PSP_CTRL_CROSS;
    }}
    else if(phase==5 || phase==15 || phase==21){if(front==FRONT_CUSTOM && shop_menu==SHOPM_BODY){
        if(phase==5 && (mycars_count()!=0 || player_look.opt[0] || mc_geometry()!=mcflow.stock)){mcflow.reason=4;mc_result();return 0;}
        if(phase==5)capture_frame_as("mycars-cancel.ppm");mcflow.phase++;return PSP_CTRL_TRIANGLE;
    }}
    else if(phase==6 || phase==16 || phase==22){if(front==FRONT_CUSTOM && shop_menu==SHOPM_CAR){mcflow.phase++;return PSP_CTRL_TRIANGLE;}}
    else if(phase==7){if(front==FRONT_MYCARS){if(mycars_count()!=0){mcflow.reason=5;mc_result();return 0;}mcflow.phase++;return 0;}}
    else if(phase==12){if(front==FRONT_PARTS && mycars_ask){capture_frame_as("mycars-confirm-no.ppm");mcflow.phase++;return PSP_CTRL_CROSS;}} /* default No */
    else if(phase==14){if(front==FRONT_PARTS){if(mycars_ask){if(!mycars_yes)return PSP_CTRL_LEFT;capture_frame_as("mycars-confirm-yes.ppm");return PSP_CTRL_CROSS;}
        if(mycars_count()!=1 || mycars_edit_slot!=0 || mycars_fitted.opt[0]!=mcflow.target){mcflow.reason=6;mc_result();return 0;}
        mcflow.confirmed=mc_geometry();if(mcflow.confirmed!=mcflow.trial){mcflow.reason=7;mc_result();return 0;}
        capture_frame_as("mycars-installed.ppm");mcflow.phase++;return PSP_CTRL_TRIANGLE;
    }}
    else if(phase==17 || phase==31 || phase==41){if(front==FRONT_MYCARS){if(phase==31 || phase==41){if(mycars_count()!=1){mcflow.reason=11;mc_result();return 0;}mcflow.target=mycars_look(0)->opt[0];}if(!mycars_tab)return PSP_CTRL_RTRIGGER;mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==32){if(front==FRONT_CUSTOM){mcflow.phase++;return 0;}}
    else if(phase==20 || phase==35){if(front==FRONT_PARTS){if(player_look.opt[0]!=mcflow.target || player_custom_car!=player_car || player_custom_lod!=1)return 0;
        if(phase==20 && mc_geometry()!=mcflow.confirmed){mcflow.reason=8;mc_result();return 0;}
        capture_frame_as(phase==20?"mycars-reentry.ppm":"mycars-reopen.ppm");
        if(phase==35){if(!mycars_look(0) || memcmp(&player_look,mycars_look(0),sizeof player_look)){mcflow.reason=12;mc_result();return 0;}mcflow.phase=36;mcflow.confirmed=mc_geometry();mc_result();return 0;}
        mcflow.phase++;return PSP_CTRL_TRIANGLE;
    }}
    else if(phase==23){if(front==FRONT_MYCARS){mcflow.phase++;return PSP_CTRL_TRIANGLE;}}
    else if(phase==24 && front==FRONT_MAIN){mc_result();return 0;}
    else if(phase==36)return 0;
    else if(phase==70){if(front==FRONT_MAIN){if(front_sel[front]!=2)return nav_press(2);mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==71){if(front==FRONT_MYCARS){if(mycars_tab)return PSP_CTRL_RTRIGGER;if(mycars_count()!=0){mcflow.reason=33;mc_result();return 0;}
        int c[64];if(!custom_cars(c)){mcflow.reason=34;mc_result();return 0;}mcflow.car=c[front_sel[front]];mcflow.phase=42;return PSP_CTRL_CROSS;}}
    else if(phase==72){if(front==FRONT_CUSTOM || front==FRONT_MYCARS)return PSP_CTRL_TRIANGLE;if(front==FRONT_MAIN){if(mycars_count()!=1){mcflow.reason=35;mc_result();return 0;}mcflow.phase=60;return 0;}}
    else if(phase==42 || phase==52){if(front==FRONT_CUSTOM && shop_menu==SHOPM_CAR){int want=shop_toward(ITEM_GRAPHICS);if(front_sel[front]!=want)return nav_press(want);mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==43 || phase==53){if(front==FRONT_CUSTOM && shop_menu==SHOPM_GRAPHICS){mcflow.phase++;return shop_press(ITEM_PAINT);}}
    else if(phase==44 || phase==54){if(front==FRONT_CUSTOM && shop_menu==SHOPM_PAINT){mcflow.phase++;return shop_press(CUST_PAINT);}}
    else if(phase==45 || phase==47){if(front==FRONT_PAINT){
        if(phase==45 && !mcflow.skin_before){mcflow.skin_before=mc_skin();mcflow.initial_paint=mycars_fitted.paint;mcflow.paint_target=mcflow.initial_paint+1;}
        if(!mycars_choice_open(VR_PAINT,mcflow.paint_target)){mcflow.reason=20;mc_result();return 0;}
        if(cust_look.paint!=mcflow.paint_target){if(phase==47 && mc_skin()!=mcflow.skin_before){mcflow.reason=21;mc_result();return 0;}return PSP_CTRL_RIGHT;}
        if(player_look.paint!=mcflow.paint_target)return 0;
        mcflow.skin_after=mc_skin();if(mcflow.skin_after==mcflow.skin_before){mcflow.reason=22;mc_result();return 0;}
        capture_frame_as(phase==45?"mycars-paint-preview.ppm":"mycars-paint-confirm.ppm");mcflow.phase++;return PSP_CTRL_CROSS;
    }}
    else if(phase==46){if(front==FRONT_PAINT && paint_ask){mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==48){if(front==FRONT_PAINT && paint_ask){if(!paint_yes)return PSP_CTRL_LEFT;mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==49){if(front==FRONT_PAINT){if(mycars_fitted.paint!=mcflow.paint_target || mycars_look(0)->paint!=mcflow.paint_target || mc_skin()!=mcflow.skin_after){mcflow.reason=23;mc_result();return 0;}capture_frame_as("mycars-paint-installed.ppm");mcflow.phase=script==120?72:50;return PSP_CTRL_TRIANGLE;}}
    else if(phase==50){if(front==FRONT_CUSTOM)return PSP_CTRL_TRIANGLE;else if(front==FRONT_MYCARS){mcflow.phase=51;return 0;}}
    else if(phase==51){if(front==FRONT_MYCARS){if(!mycars_tab)return PSP_CTRL_RTRIGGER;mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==55){if(front==FRONT_PAINT){if(player_look.paint!=mcflow.paint_target || mc_skin()!=mcflow.skin_after || memcmp(mycars_look(0),&player_look,sizeof player_look)){mcflow.reason=24;mc_result();return 0;}capture_frame_as("mycars-paint-reentry.ppm");mcflow.phase++;mc_result();return 0;}}
    else if(phase==60){if(front==FRONT_MAIN){if(front_sel[front]!=1)return nav_press(1);mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==61){if(front==FRONT_QRMODE){if(front_sel[front]!=0)return nav_press(0);mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==62){if(front==FRONT_QRTRACK){mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==63){if(front==FRONT_QROPTS){mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==64 && script==120){if(front==FRONT_QRCAR){
        /* beta issue 61: the edited car as the selector offers it, no L/R:
         * stay where the cursor is when it shows that model, else go to the
         * list's first car of that model */
        int c[64],n=qr_cars(c),at=front_sel[front];
        if(at<0 || at>=n || c[at]!=mcflow.car){int k=0;while(k<n && c[k]!=mcflow.car)k++;if(k==n){mcflow.reason=32;mc_result();return 0;}return nav_press(k);}
        if(player_car!=mcflow.car || !model.nm)return 0;
        capture_frame_as("mycars-qr-selector.ppm");mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==64){if(front==FRONT_QRCAR){if(qr_filter!=QRF_MYCARS)return PSP_CTRL_LTRIGGER;if(!mycars_count() || memcmp(mycars_look(0),&player_look,sizeof player_look))return 0;capture_frame_as("mycars-quickrace-selector.ppm");mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==65){if(front==FRONT_QRCAR && qr_dialog){mcflow.phase++;return PSP_CTRL_CROSS;}}
    else if(phase==66 && script==120){if(mode==MODE_DRIVE && race.quick && race.state!=RACE_OFF){mcflow.phase=67;return 0;}}
    else if(phase==67){if(frame-since<100)return 0;capture_frame_as("mycars-qr-race.ppm");
        if(qr_tuned_slot!=0 || player_car!=mycars_car(0) || memcmp(mycars_look(0),&player_look,sizeof player_look)){mcflow.reason=31;mc_result();return 0;}
        mcflow.phase=68;mc_result();return 0;}
    else if(phase==66){if(mode==MODE_DRIVE && race.quick && race.state!=RACE_OFF){if(qr_tuned_slot!=0 || player_car!=mycars_car(0) || memcmp(mycars_look(0),&player_look,sizeof player_look) || player_custom_lod!=0){mcflow.reason=30;mc_result();return 0;}mcflow.phase=68;mc_result();return 0;}}
    mc_result();return 0;
}

static uint32_t mc_input(unsigned frame,int script) {
    int phase=mcflow.phase,f=front;uint32_t buttons=mc_step(frame,script);
    if(buttons || frame%20==0){FILE *out=fopen("mycars-input.csv","a");if(out){fprintf(out,"%u,%d,%d,%d,%d,%u,%d,%d,%d\n",frame,phase,f,front_sel[f],shop_menu,(unsigned)buttons,mycars_edit,mycars_tab,prof_stage);fclose(out);}}
    return buttons;
}
