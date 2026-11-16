/* Experimental original FuturePoints provider. No COL query/physics/write/I/O.
 * Source: GC FuturePoints FillUpArray/AddInterpolatedPoints/Update/GetClosestPoint,
 * original Xbox4306 metadata (literal header), same forward GetPosition kernel.
 * Runtime track identity is the existing 450-point line_track input. Backward
 * geometry and unresolved transitions are explicitly unsupported, never line0.
 */
#include "race.h"
#if ORIGINAL_PATH_CHOICE
#include "original_drive_target.h"
#include "local-data/parkade4306_futurepoint_authoring.h"
#include <math.h>
#include <string.h>
#include "local-data/parkade4306_line_identity.h"
static const RaceLine *bound_line;
static const LinePoint *bound_points;
int path_driver_original_bind_line(const RaceLine *line){
    bound_line=NULL;bound_points=NULL;
    if(!line || line->id!=4306 || line->n!=450 || !line->p)return 0;
    for(unsigned i=0;i<450;i++){
        uint32_t bits[3];memcpy(bits,&line->p[i].x,sizeof bits);
        if(memcmp(bits,original4306_xyz[i],sizeof bits))return 0;
    }
    bound_line=line;bound_points=line->p;return 1;
}
int path_driver_original_line_matches(const RaceLine *line){
    return line && line==bound_line && line->p==bound_points && line->id==4306 && line->n==450;
}
typedef struct {float x,y,dx,dy,wl,wr,off,height,forward_length;int intersection,traffic;} Raw;
typedef struct {unsigned record,author;float d;} Anchor;
typedef struct {OriginalChoicePoint point;float d;unsigned anchor;} Future;
static Raw raw[FPM4306_RAW_COUNT];
static Anchor anchor[FPM4306_AUTHOR_COUNT];
static unsigned nanchor,prepared;
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int s16(const unsigned char *p){unsigned v=u16(p);return v>=32768?(int)v-65536:(int)v;}
static unsigned u32(const unsigned char *p){return u16(p)|u16(p+2)<<16;}
static float f32(const unsigned char *p){unsigned v=u32(p);float f;memcpy(&f,&v,4);return f;}
static float delta(const Raw *a,const Raw *b){return (b->x-a->x)*a->dx+(b->y-a->y)*a->dy;}
static void base_position(const Raw *p,float *x,float *y){*x=p->x+p->dy*p->off;*y=p->y-p->dx*p->off;}
static int transition(unsigned i){
    if(!i || i>=FPM4306_AUTHOR_COUNT)return 0;
    const Fpm4306Raw *a=&fpm4306_raw[fpm4306_author[i-1].record],*b=&fpm4306_raw[fpm4306_author[i].record];
    if(a->route==b->route && b->point==a->point+1)return 1;
    unsigned found=0;
    for(unsigned k=0;k<FPM4306_SIGN_COUNT;k++){
        const Fpm4306Sign *s=&fpm4306_signs[k];
        if(s->old_route==a->route && s->old_point==a->point && s->new_route==b->route && s->new_point==b->point){
            if(s->flags & ((1u<<18)|(1u<<23)|(1u<<26)))return 0;
            found++;
        }
    }
    return found==1?2:0;
}
static void prepare(void){
    if(prepared)return;
    prepared=1;
    for(unsigned i=0;i<FPM4306_RAW_COUNT;i++){
        const unsigned char *p=fpm4306_raw[i].raw56;Raw *q=&raw[i];
        q->x=f32(p);q->y=f32(p+4);q->dx=s16(p+24)/32767.f;q->dy=s16(p+26)/32767.f;
        q->wl=u16(p+20)/256.f;q->wr=u16(p+22)/256.f;
        int off=p[34]>=128?(int)p[34]-256:p[34];
        q->off=fmaxf(q->wl,q->wr)*(off/128.f);q->height=s16(p+18)/8.f;q->forward_length=u16(p+16)/256.f;
        q->intersection=s16(p+28);q->traffic=(u32(p+12)>>19)&63;
        if(!isfinite(q->x)||!isfinite(q->y)||!isfinite(q->off))return;
        /* Verified Init induction: forward raw ClampFixup(0) must preserve0. */
        if(-q->wl-q->off+1>0 || q->wr-q->off-1<0)return;
    }
    if(!fpm4306_author[0].forward)return;
    anchor[0]=(Anchor){fpm4306_author[0].record,0,0};nanchor=1;
    for(unsigned i=1;i<FPM4306_AUTHOR_COUNT;i++){
        if(!fpm4306_author[i].forward)break; /* preserve backward states in table */
        int sign=transition(i);if(!sign)break;
        const Raw *previous=&raw[anchor[nanchor-1].record];
        unsigned rec=fpm4306_author[i].record;float gap=delta(previous,&raw[rec]);
        if(sign==2){
            /* FillUpArray1180-1253 nonDrag: AdvancePoint BEFORE SetData. */
            while(gap<3.f){
                if(++i>=FPM4306_AUTHOR_COUNT || !fpm4306_author[i].forward || !transition(i))return;
                rec=fpm4306_author[i].record;gap=delta(previous,&raw[rec]);
            }
            const Raw *candidate=&raw[rec];
            const Fpm4306Raw *a=&fpm4306_raw[anchor[nanchor-1].record],*b=&fpm4306_raw[rec];
            if(a->route!=b->route && candidate->intersection==-1 && candidate->traffic==63){
                float ax,ay,bx,by;base_position(previous,&ax,&ay);base_position(candidate,&bx,&by);
                if((bx-ax)*previous->dx+(by-ay)*previous->dy<=5.f)break;
            }
        }
        if(!isfinite(gap)||gap<=0)break;
        anchor[nanchor]=(Anchor){rec,i,anchor[nanchor-1].d+gap};nanchor++;
    }
}
static OriginalChoicePoint point(unsigned k,float local){
    const Raw *p=&raw[anchor[k].record];const Fpm4306Raw *id=&fpm4306_raw[anchor[k].record];
    OriginalChoicePoint q={0};q.x=p->x;q.y=p->y;q.dx=p->dx;q.dy=p->dy;
    q.offset=p->off;q.point_d=local;q.source_route=id->route;q.source_point=id->point;q.resolved=1;
    return q; /* raw anchor fixup0 is derived Init induction, not dynamic default */
}
static int minmax(Future *p,unsigned n){
    float low[3][40],high[3][40];
    for(unsigned branch=0;branch<3;branch++){
        for(unsigned i=0;i<n;i++){
            Raw *r=&raw[anchor[p[i].anchor].record];float center=p[i].point.offset+p[i].point.fixup;
            low[branch][i]=-r->wl-center;high[branch][i]=r->wr-center;
        }
        for(int i=(int)n-2;i>=0;i--){
            float allowance=(p[i+1].d-p[i].d)*99999.f;
            if(!isfinite(allowance)||allowance<=0)return 0;
            float l=low[branch][i],h=high[branch][i],width=h-l;
            if(l>1.f && width>=99999.f)l=0;
            if(h<-1.f && width>=99999.f)h=0;
            float ld=branch==2?allowance*3.f:allowance,hd=branch==0?allowance*3.f:allowance;
            low[branch][i]=fmaxf(l,low[branch][i+1]-ld);
            high[branch][i]=fminf(h,high[branch][i+1]+hd);
        }
    }
    for(unsigned i=0;i<n;i++){
        if(low[0][i]!=low[1][i] || low[1][i]!=low[2][i] || high[0][i]!=high[1][i] || high[1][i]!=high[2][i])return 0;
        p[i].point.min_l=low[0][i];p[i].point.max_l=high[0][i];
    }
    return 1; /* actual curvature unknown, all three original branches equal */
}
static int refuse(OriginalChoiceFrame *out,unsigned reason){if(out)out->refusal=reason;return 0;}
int path_driver_original4306(const Car *c,const RaceEvent *e,int li,float u,float look,OriginalChoiceFrame *out){
    (void)u;
    if(!out||!c||!e||e->id!=4306||e->mode!=RMODE_DRIFT||li<0||li>=FPM4306_MAPPING_COUNT)return refuse(out,ORIGINAL_CHOICE_REFUSED_DATA);
    prepare();
    const Fpm4306Mapping *m=&fpm4306_mapping[li];
    if(!m->forward || (m->next_record!=65535 && !m->next_forward))
        return refuse(out,ORIGINAL_CHOICE_REFUSED_BACKWARD);
    if(nanchor<5)return refuse(out,ORIGINAL_CHOICE_REFUSED_DATA);
    unsigned at=0;
    while(at+1<nanchor && anchor[at+1].author<=m->author_index)at++;
    if(at<2 || at+3>=nanchor)return refuse(out,ORIGINAL_CHOICE_REFUSED_CONTEXT); /* no endpoint/corner/backward invention */
    /* Original FillUpArray: count<40 AND (lastD<=horizon OR count<10).
     * No bit26 corners exist in this bundle. The port supplies its actual
     * look distance (existing controller adaptation), not an invented GC
     * CalculateLookaheadDistance speed/time factor. Global D is rebased here. */
    if(!isfinite(look)||look<0)return refuse(out,ORIGINAL_CHOICE_REFUSED_INPUT);
    float horizon=anchor[at].d+look;
    unsigned begin=at-2;Future fp[40];unsigned n=0;
    for(unsigned k=begin;k+1<nanchor && n<40;k++){
        fp[n++]=(Future){point(k,0),anchor[k].d,k};
        if(n>=10 && anchor[k].d>horizon)break;
        float span=anchor[k+1].d-anchor[k].d;
        int add=(int)(span*.125f)-1; /* original 8m insertion, not walk450 */
        for(int j=0;j<add && n<40;j++){
            float local=(j+1)*8.f;
            fp[n++]=(Future){point(k,local),anchor[k].d+local,k};
        }
    }
    /* Unknown/backward truncation is not an authored endpoint: require the
     * original minimum and horizon coverage unless the 40 maximum was hit. */
    if(n<10 || (n<40 && fp[n-1].d<=horizon))return refuse(out,ORIGINAL_CHOICE_REFUSED_CONTEXT);
    for(unsigned i=0;i<n;i++){
        unsigned k=fp[i].anchor;
        if(fp[i].point.point_d>0){
            int end_present=0;for(unsigned j=i+1;j<n;j++)if(fp[j].anchor==k+1 && fp[j].point.point_d==0)end_present=1;
            if(end_present){
                OdtSpan s={0};unsigned start_index=n,end_index=n,next_index=n;
                for(unsigned j=0;j<n;j++)if(fp[j].point.point_d==0){
                    if(fp[j].anchor==k)start_index=j;
                    if(fp[j].anchor==k+1)end_index=j;
                    if(fp[j].anchor==k+2)next_index=j;
                }
                if(start_index==n || end_index==n)return refuse(out,ORIGINAL_CHOICE_REFUSED_CONTEXT);
                base_position(&raw[anchor[k].record],&s.start[0],&s.start[1]);
                base_position(&raw[anchor[k+1].record],&s.end[0],&s.end[1]);
                if(start_index){
                    /* Original uses immediately previous FP, including its
                     * already-computed intermediate fixup, NOT global anchor. */
                    const OriginalChoicePoint *before=&fp[start_index-1].point;
                    float off=before->offset+before->fixup;
                    s.previous[0]=(before->x+before->dy*off)+before->dx*before->point_d;
                    s.previous[1]=(before->y-before->dx*off)+before->dy*before->point_d;
                }else{
                    /* FuturePoints5220 / TrackRoutePoint1411: backward vector
                     * uses previous raw record in the same route, else current. */
                    unsigned rec=anchor[k].record,prev=rec;
                    const Fpm4306Raw *id=&fpm4306_raw[rec];
                    if(id->point){int found=0;
                        for(unsigned j=0;j<FPM4306_RAW_COUNT;j++)if(fpm4306_raw[j].route==id->route && fpm4306_raw[j].point+1==id->point){prev=j;found=1;break;}
                        if(!found)return refuse(out,ORIGINAL_CHOICE_REFUSED_DATA);
                    }
                    s.tangent_mode|=1u;s.start_tangent[0]=-raw[prev].dx;s.start_tangent[1]=-raw[prev].dy;
                }
                if(next_index<n){
                    const OriginalChoicePoint *after=&fp[next_index].point;
                    float off=after->offset+after->fixup;
                    s.next[0]=(after->x+after->dy*off)+after->dx*after->point_d;
                    s.next[1]=(after->y-after->dx*off)+after->dy*after->point_d;
                }else{
                    /* FuturePoints5300: literal forward vector of end FP. */
                    s.tangent_mode|=2u;s.end_tangent[0]=raw[anchor[k+1].record].dx;s.end_tangent[1]=raw[anchor[k+1].record].dy;
                }
                s.d_start=anchor[k].d;s.d_end=anchor[k+1].d;s.known=s.interior=1;
                OriginalChoicePoint *p=&fp[i].point;
                OdtPoint q={p->x,p->y,p->dx,p->dy,p->offset,0,fp[i].d,p->point_d};float xy[2];
                if(odt_interior_target(&s,&q,0,xy,&p->fixup)!=ODT_OK)return refuse(out,ORIGINAL_CHOICE_REFUSED_SPLINE);
            } /* trailing inserted points without endpoint retain their Init0 */
        }
        fp[i].point.length=i+1<n?fp[i+1].d-fp[i].d:
            raw[anchor[k].record].forward_length-fp[i].point.point_d;
    }
    if(!minmax(fp,n))return refuse(out,ORIGINAL_CHOICE_REFUSED_BOUNDS);
    int closest=-1;float best=0;
    for(unsigned i=0;i<n;){
        OriginalChoicePoint *p=&fp[i].point;float offset=p->offset+p->fixup;
        float x=(p->x+p->dy*offset)+p->dx*p->point_d,y=(p->y-p->dx*offset)+p->dy*p->point_d;
        float sx=c->z-x,sy=c->x-y,distance=sx*sx+sy*sy;
        if(distance<=5e-11f)distance=0;
        else{float inv=1.f/sqrtf(distance);
            inv=-(distance*inv*inv-1.f)*inv*.5f+inv;
            distance=(-(distance*inv*inv-1.f)*inv*.5f+inv)*distance;
        }
        float score=fabsf(raw[anchor[fp[i].anchor].record].height-c->ground)*10.f+distance;
        if(closest<0 || score<best){closest=(int)i;best=score;}
        int skip=(int)(distance/50.f)-2;if(skip<0)skip=0;i+=(unsigned)skip+1;
    }
    if(closest<0 || closest+2>=(int)n)return refuse(out,ORIGINAL_CHOICE_REFUSED_CONTEXT);
    memset(out,0,sizeof(*out));out->pose_reference=fp[closest].point;
    unsigned available=n-(unsigned)closest-1;out->count=available>20?20:available;
    for(unsigned i=0;i<out->count;i++)out->point[i]=fp[closest+1+(int)i].point;
    out->reference_dx=cosf(c->yaw);out->reference_dy=sinf(c->yaw);
    out->lateral_scale=0.1509999930858612f; /* original DrivingChoice.SetChoice normal Init */
    out->known=out->fresh_target=out->line0_forward=out->bbox_axes_equivalent=out->racing_car=1;
    return 1;
}
#endif
