/* Host-only Outrun fixture on the city graph. The player uses the normal
 * Rachel preset, inputs, physics and challenge flow. Seed only the approach
 * to the junction so the test can repeat the overtake in a short run. */
static int outrun_turn_setup(PathDriver *driver) {
    static const int road[]={12183,3623,3624,12189,3710,3711,11942,3724,3725,11940,3626,11941};
    enum { N=sizeof road/sizeof *road, SAMPLES=128, JUNCTION=6, STRAIGHT=3712 };
    static LinePoint point[SAMPLES];
    static float distance[SAMPLES];
    static RaceLine line;
    float s[N]={0};
    if(roads_count()<=12189 || player_car!=FLEET_RACHEL || !outrun_allowed())return 0;
    for(int i=1;i<N;i++) {
        const RoadNode *a=roads_node(road[i-1]),*b=roads_node(road[i]);
        const uint16_t *links=roads_links(road[i-1]);int linked=0;
        for(int j=0;j<a->count;j++)if(links[j]==road[i])linked=1;
        if(!linked)return 0;
        s[i]=s[i-1]+hypotf(b->x-a->x,b->z-a->z);
    }
    int n=0;
    for(float d=0;d<=s[N-1] && n<SAMPLES;d+=2.f,n++) {
        LinePoint *out=&point[n];
        for(int j=-2;j<=2;j++) {
            float u=fmaxf(0,fminf(s[N-1],d+j*4.f));
            int k=1;while(k<N-1 && s[k]<u)k++;
            const RoadNode *a=roads_node(road[k-1]),*b=roads_node(road[k]);
            float t=(u-s[k-1])/(s[k]-s[k-1]);
            out->x+=(a->x+(b->x-a->x)*t)/5;
            out->y+=(a->y+(b->y-a->y)*t)/5;
            out->z+=(a->z+(b->z-a->z)*t)/5;
        }
        out->room_l=out->room_r=65;
        if(n)distance[n]=distance[n-1]+hypotf(out->x-point[n-1].x,out->z-point[n-1].z);
    }
    for(int i=1;i+1<n;i++) {
        float a=atan2f(point[i].x-point[i-1].x,point[i].z-point[i-1].z);
        float b=atan2f(point[i+1].x-point[i].x,point[i+1].z-point[i].z);
        point[i].k=-2.f*remainderf(b-a,6.2831853f)/(distance[i+1]-distance[i-1]);
    }
    line=(RaceLine){.id=-1,.n=n,.p=point,.s=distance,.length=distance[n-1]};
    *driver=(PathDriver){.cruise=31.f,.line=&line,.main=&line,.li=-1,.fork=-1,.keep_right=0};
    const RoadNode *a=roads_node(road[0]),*b=roads_node(road[1]);
    float yaw=atan2f(b->x-a->x,b->z-a->z),hx=sinf(yaw),hz=cosf(yaw);
    race_teleport_settle(a->x,a->z);
    car_place(&car,a->x-2.5f*hz,a->y,a->z+2.5f*hx,yaw,model.floor_y,car.top_speed);
    car.vx=18.3f*hx;car.vz=18.3f*hz;

    rivals_load();
    OutrunRival *r=&outrun.r[0];
    if(!r->car)return 0;
    outrun.state=OUTRUN_ROAM;outrun.engageable=0;outrun.last_placed=outrun.clock;
    r->on=1;r->n=JUNCTION+2;r->progress=5.f;r->v=14.1f;r->side=2;r->window=1;
    r->lat=3.f;r->lat_goal=3.f;r->cur=STRAIGHT;r->prev=road[JUNCTION];r->dx=hx;r->dz=hz;
    for(int i=0;i<r->n;i++) {
        int node=i<=JUNCTION?road[i]:STRAIGHT;
        const RoadNode *p=roads_node(node);
        r->p[3*i]=p->x;r->p[3*i+1]=p->y;r->p[3*i+2]=p->z;r->node[i]=node;
        r->s[i]=i?r->s[i-1]+hypotf(p->x-r->p[3*(i-1)],p->z-r->p[3*(i-1)+2]):0.f;
    }
    Car *o=r->car;
    car_place(o,a->x+r->progress*hx+3.f*hz,a->y,a->z+r->progress*hz-3.f*hx,yaw,o->floor_y,o->top_speed);
    o->racer=1;o->vx=r->v*hx;o->vz=r->v*hz;r->lx=o->x;r->lz=o->z;
    return 1;
}
