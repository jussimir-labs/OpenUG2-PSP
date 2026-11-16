/* Rewritten original tire/clutch force stages. Immutable numeric tables only.
 * The local packed scratch interface is internal; public API is typed. */
static float limit(float x,float lo,float hi){return fminf(hi,fmaxf(lo,x));}
static unsigned contact_angle(float forward,float lateral){
    int quadrant=0;
    if(forward<0){forward=-forward;quadrant=1;}
    if(lateral<0){lateral=-lateral;quadrant^=3;}
    unsigned a;
    if(forward>lateral)a=(unsigned)atan_lut[(int)(lateral/forward*256.f)]*32u;
    else if(lateral>forward)a=16384u-(unsigned)atan_lut[(int)(forward/lateral*256.f)]*32u;
    else a=forward==0?0u:8192u;
    if(quadrant==3)a=0u-a;
    else if(quadrant==1)a=32768u-a;
    else if(quadrant==2)a=a-32768u;
    return a&65535u;
}
static float contact_slip_angle(float forward,float lateral){
    float a=(float)contact_angle(forward,lateral)*sine_coeff[0];
    if(a>sine_coeff[4])a-=sine_coeff[2];
    if(forward<0){a+=sine_coeff[4];if(a>sine_coeff[4])a-=sine_coeff[2];}
    return a;
}
/* PAL's sine is a polynomial on the quantized binary angle. Its coefficients
 * are immutable original data, independent of the native platform's libm approximation. */
static float pal_sin(float angle){
    unsigned binary=(unsigned)(int)(angle*sine_coeff[9])&65535u;
    float a=(float)binary*sine_coeff[0],sign=1.f;
    if(a>=sine_coeff[1])a-=sine_coeff[2];
    else if(a>=sine_coeff[3]){sign=-1.f;a-=sine_coeff[4];}
    float a2=a*a,a3=a*a2,a5=a3*a2,a7=a5*a2,a9=a7*a2;
    return (((a-a3*sine_coeff[5])+a5*sine_coeff[6])-a7*sine_coeff[7]+a9*sine_coeff[8])*sign;
}
static float load_curve(int angle_row,float load){
    float at=limit(load*.5f,0.f,5.f);int i=(int)at;
    if(i>=5)return curves[angle_row*6+5];
    float t=at-i;return (1-t)*curves[angle_row*6+i]+t*curves[angle_row*6+i+1];
}
static float lateral_request(const float *r,float slip_angle){
    float angle_index=fabsf(slip_angle)*57.2957763671875f*(12.f/r[8])*.5f;
    int a=(int)angle_index;float load=r[0]*r[27]*r[26]/r[9];
    float force;
    if(a>=6)force=load_curve(6,load);
    else {float lo=load_curve(a,load),hi=load_curve(a+1,load);force=lo+(angle_index-a)*(hi-lo);}
    return force*r[10]*r[25];
}
static void finalize_request(const float *r,float *o,float angle,float fx,float fy){
    float pilot=1.f;
    if((int)r[21]==1 && r[19]==0 && r[37]>=0 && r[23]>1)
        pilot=r[36]+(1-r[36])*limit((r[22]-13.410818f)/8.940545f,0,1);
    o[0]=angle;o[1]=fx;o[2]=fy;
    int ellipse=r[4]>0 && r[19]==0;
    if(ellipse)fx*=r[28];
    float requested=hypotf(fx,fy);
    float cap=r[0]*r[27]*r[6]*r[11]*r[13]*r[14]*r[15]*pilot;
    float usage=cap<=.001f?1.f:fminf(1.f,requested/cap);
    int capped=requested>cap && requested>.001f;
    if(capped){float fraction=cap/requested;fx*=fraction;fy*=fraction;}
    else if(ellipse)fx/=r[28];
    if(r[18]!=0 || r[19]!=0)usage=1;
    if(r[20]==0)usage=0;
    if(fabsf(r[1])>1)fx-=pal_sin(angle)*fy*.15f;
    o[3]=fx;o[4]=fy;o[5]=usage;o[6]=cap;o[7]=requested;o[8]=(float)capped;
    for(int i=0;i<3;i++)o[9+i]=r[38+i]*fx+r[41+i]*fy+r[44+i]*r[0];
    o[12]=r[18]!=0?1.f:0.f;
}
static float linear_samples(const float *v,int n,float offset,float inv_step,float x){
    float p=limit((x-offset)*inv_step,0,(float)(n-1));int i=(int)p;
    return i>=n-1?v[n-1]:(1-(p-i))*v[i]+(p-i)*v[i+1];
}
static float axle_fraction(const float *d,const float *r0,const float *r1,int rear){
    float bias=(r0[20]!=0 && r1[20]!=0)?d[19]:.5f;
    if(d[21]==1)return 1-bias;
    if(d[21]==2)return bias;
    float total=r0[37]+r1[37];if(fabsf(total)<=1e-5f)return .5f;
    float fraction=(rear?r0[37]:r1[37])/total;
    if(fraction<bias)return 1-d[19]*.5f;
    if(fraction>1-bias)return d[19]*.5f;
    return fraction;
}
static void apply_clutch(float delta,const float *d,const float *records,float *torque,float *engine_clutch,float *total){
    if(d[1]==1)return;
    *total+=delta;*engine_clutch-=delta*fmaxf(.25f,d[20]*.25f);
    float front=delta*d[18]*(1-d[16])*d[14],rear=delta*d[18]*d[16]*d[15];
    float fr=axle_fraction(d,records,records+52,0),rr=axle_fraction(d,records+104,records+156,1);
    const float amount[4]={front*(1-fr),front*fr,rear*rr,rear*(1-rr)};
    for(int k=0;k<4;k++)if(records[52*k+19]==0 && ((k<2&&d[55]<=.999f)||(k>=2&&d[55]>=.001f)))torque[k]+=amount[k];
}
static float differential_alpha(const float *d,const float *records,const float *body,const float *fx,const float *torque){
    float a[4];for(int k=0;k<4;k++)a[k]=(torque[k]-fx[k]*records[52*k+5])/body[4+7*k+2];
    float sum=0;
    if(d[55]>=.001f)sum=(a[2]+a[3])*.5f*d[15];
    if(d[55]<=.999f){sum+=(a[0]+a[1])*.5f*d[14];if(d[55]>=.001f)sum*=.5f;}
    return sum*d[18];
}
static int core_force_closure(float *in,float *out,float eng,float previous_error,OGState *state){
        const float *body=in+208,*d=in+240;
        float fx[4],fy[4],angle[4],torque[4]={0},engine_clutch=0,total=0;
        float guess=0;float error_rpm=(d[3]-d[4])*9.549304f;
        if(d[0]==3)guess=eng;
        else if(d[0]==0 || d[0]==2){float spring=d[17];if(previous_error!=0 && previous_error*error_rpm<0 && fabsf(previous_error)>2000 && fabsf(error_rpm)>2000)spring*=.5f;guess=limit(d[4]-d[3],-d[22],d[22])*spring*d[2];}
        apply_clutch(guess,d,in,torque,&engine_clutch,&total);
        for(int k=0;k<4;k++){
            float *r=in+52*k;const float *br=d+39+4*k;
            float old_omega=r[37];uint32_t old_lock=(uint32_t)r[19];
            if(!old_lock){float braking=4.f*br[0]*br[2]+10.f*br[1]*br[3];torque[k]+=old_omega>0?-braking:braking;}
            if(r[3]<-.5f && torque[k]<0 && br[1]*br[3]==0)torque[k]*=.5f/-r[3];
            angle[k]=contact_slip_angle(r[1],r[2]);
            float pilot=1;
            if((int)r[21]==1 && !old_lock && old_omega>=0 && r[23]>1)
                pilot=r[36]+(1-r[36])*limit((r[22]-13.410818f)/8.940545f,0,1);
            float sliding=r[7]*r[12]*r[13]*r[14]*pilot;
            float length2=r[3]*r[3]+r[2]*r[2],speed2=r[1]*r[1]+r[2]*r[2];
            int called=length2>5e-11f && speed2!=0;
            float hold=called?fabsf(r[1])*(r[0]*r[27]*sliding/sqrtf(speed2)):0;
            OGBrakeLockResult lock;
            int rc=og_branch_brake_lock(&lock,old_omega,old_lock,hold,r[5],br[0],br[1],br[2],br[3],called);
            if(rc)return rc;
            state->wheel[k].omega=lock.omega;state->wheel[k].brake_lock=lock.locked;
            r[37]=lock.omega;r[19]=(float)lock.locked;
            r[18]=(float)(called && (fabsf(r[3])>.5f || fabsf(angle[k])>r[8] || lock.locked));
            if(r[18]!=0){
                float rate=r[0]*r[27]*sliding/sqrtf(length2);
                fx[k]=rate*r[3]*r[16];fy[k]=-rate*r[2]*r[17];
                if(r[22]*2.237f<1&&sliding>.1f){fx[k]/=sliding;fy[k]/=sliding;}
            }else{
                fx[k]=r[16]*torque[k]/r[5];fy[k]=lateral_request(r,angle[k])*r[17];
                if(r[1]<=0)fy[k]=-fy[k];if(angle[k]>0)fy[k]=-fy[k];
            }
        }
        for(int pass=0;pass<5;pass++){
            float sumx=body[2],sumy=body[3],moment=0;
            for(int k=0;k<4;k++){
                const float *p=body+4+7*k;float x=p[3]*fx[k]+p[5]*fy[k],y=p[4]*fx[k]+p[6]*fy[k];sumx+=x;sumy+=y;
                /* PAL318478..31848c: product, add to prior moment, subtract
                 * second product. Keep that source order near regime gates. */
                moment=(moment+p[0]*y)-p[1]*x;
            }
            /* PAL3184d0/3184d4 uses two divisions, not a multiplication. */
            float ax=sumx/body[0],ay=sumy/body[0],alpha=moment/(1.f/body[1]);
            for(int k=0;k<4;k++){
                const float *r=in+52*k,*p=body+4+7*k;if(r[18]!=0 || r[19]!=0)continue;
                float along=p[3]*(ax-alpha*p[1])+p[5]*(ay+alpha*p[0]);
                fx[k]+=(((torque[k]-fx[k]*r[5])/p[2])*r[5]-along)*p[2]/r[5];
            }
            if(d[0]==3){
                float inv=1/d[5];
                if(d[55]<=.999f)inv+=(1-d[16])/(body[6])*d[14]*d[18]*d[14]*d[18]*.5f;
                if(d[55]>=.001f)inv+=d[16]/(body[20])*d[15]*d[18]*d[15]*d[18]*.5f;
                float delta=((eng+engine_clutch)/d[5]-differential_alpha(d,in,body,fx,torque))/inv;
                apply_clutch(delta,d,in,torque,&engine_clutch,&total);
            }
        }
        for(int k=0;k<4;k++){float local[52];for(int n=0;n<52;n++)local[n]=in[52*k+n];local[4]=torque[k];finalize_request(local,out+13*k,angle[k],fx[k],fy[k]);out[52+k]=torque[k];}
        out[56]=total;out[57]=engine_clutch;out[58]=(d[0]==0||d[0]==2)?error_rpm:previous_error;
    return 0;
}
