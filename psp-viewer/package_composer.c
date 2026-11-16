/* Mathematical rewrite from original Build*Package / UpgradeSpecification.
 * GC function addresses, offsets and operation order documented in README.
 * Float operations are separate binary32, with -ffp-contract=off. */
#include "package_composer.h"
#include "og_profile.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#define MAGIC 0x4f475031u
static const uint32_t counts[10]={1,3,1,1,1,3,1,1,1,2};
static const uint32_t cp_paths[9]={3,7,4,1,5,2,8,6,0};
static uint32_t rd32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float rdf(const unsigned char *p){uint32_t u=rd32(p);float f;memcpy(&f,&u,4);return f;}
static float g(const unsigned char *raw,uint32_t off){return rdf(raw+0x110+off);}
static float add(float a,float b){return a+b;}
static float mul(float a,float b){return a*b;}
static float lerp(float a,float b,float t){float delta=b-a;return add(a,mul(delta,t));}
static int has(const OGPSpec *s,uint32_t i){return (s->installed[i/32]>>(i%32))&1u;}
static void put(OGPSpec *s,uint32_t i){s->installed[i/32]|=1u<<(i%32);}
static void drop(OGPSpec *s,uint32_t i){s->installed[i/32]&=~(1u<<(i%32));}
static int registry_ok(const OGPRegistry *r){return r&&r->magic==MAGIC;}
static int spec_ok(const OGPSpec *s,const OGPRegistry *r){uint32_t i;if(!s||!registry_ok(r)||(s->installed[2]&~15u)||(s->junkman&~1023u))return 0;for(i=0;i<68;i++)if(has(s,i)&&!r->parts[i].present)return 0;return 1;}
int ogp_registry_parse(OGPRegistry *out,const void *bytes,size_t size){
 OGPRegistry t;uint32_t seen[10][3]={{0}},at,path,lv,n,j,k,id;const unsigned char *b=bytes;
 if(!out||!b||size!=OGP_REGISTRY_BYTES)return OGP_INVALID;
 memset(&t,0,sizeof t);
 for(at=0;at<size;at+=0x17c){path=rd32(b+at);lv=rd32(b+at+4);n=rd32(b+at+8);
  if(path>=10||lv<1||lv>3||n>4||seen[path][lv-1])return OGP_INVALID;
  seen[path][lv-1]=1;t.count[path][lv-1]=n;
  for(j=0;j<n;j++){const unsigned char *p=b+at+12+j*0x5c;OGPPart *part;id=rd32(p);
   if(id>=68||t.parts[id].present)return OGP_INVALID;
   t.ids[path][lv-1][j]=id;part=&t.parts[id];part->present=1;part->path=path;part->level=lv;part->slot=j;
   for(k=0;k<4;k++){part->weights[k]=rdf(p+0x30+4*k);if(!isfinite(part->weights[k]))return OGP_INVALID;}
   for(k=0;k<5;k++){part->excludes[k]=(int32_t)rd32(p+0x40+4*k);if(part->excludes[k]<-1||part->excludes[k]>=68)return OGP_INVALID;}
   for(k=0;k<2;k++){part->requires[k]=(int32_t)rd32(p+0x54+4*k);if(part->requires[k]<-1||part->requires[k]>=68)return OGP_INVALID;}
  }
 }
 for(id=0;id<68;id++)if(t.parts[id].present){OGPPart *p=&t.parts[id];for(k=0;k<5;k++)if(p->excludes[k]>=0&&!t.parts[p->excludes[k]].present)return OGP_INVALID;for(k=0;k<2;k++)if(p->requires[k]>=0&&!t.parts[p->requires[k]].present)return OGP_INVALID;}
 t.magic=MAGIC;*out=t;return OGP_OK;
}
static int install(OGPSpec *s,const OGPRegistry *r,uint32_t id,int recursive,OGPSpec *stack){
 const OGPPart *p;uint32_t k;int rc;
 if(id>=68||!r->parts[id].present)return OGP_UNKNOWN_PART;
 if(has(s,id))return OGP_OK;
 if(has(stack,id))return OGP_CYCLE;
 put(stack,id);p=&r->parts[id];
 for(k=0;k<2;k++)if(p->requires[k]>=0&&!has(s,(uint32_t)p->requires[k])){if(!recursive)return OGP_PREREQUISITE;rc=install(s,r,(uint32_t)p->requires[k],1,stack);if(rc)return rc;}
 for(k=0;k<5;k++)if(p->excludes[k]>=0)drop(s,(uint32_t)p->excludes[k]);
 put(s,id);drop(stack,id);return OGP_OK;
}
int ogp_install(OGPSpec *out,const OGPSpec *before,const OGPRegistry *r,uint32_t id,int recursive){OGPSpec t,stack={{0},0};int rc;if(!out||!spec_ok(before,r)||(recursive!=0&&recursive!=1))return OGP_INVALID;t=*before;rc=install(&t,r,id,recursive,&stack);if(!rc)*out=t;return rc;}
int ogp_from_levels(OGPSpec *out,const OGPRegistry *r,const uint8_t levels[10],uint32_t junkman){
 OGPSpec t={{0},0},stack;uint32_t path,lv,j;int rc;
 if(!out||!levels||!registry_ok(r)||(junkman&~1023u))return OGP_INVALID;
 t.junkman=junkman;
 for(path=0;path<10;path++){if(levels[path]>3)return OGP_INVALID;for(lv=0;lv<levels[path];lv++)for(j=0;j<r->count[path][lv];j++){memset(&stack,0,sizeof stack);rc=install(&t,r,r->ids[path][lv][j],0,&stack);if(rc)return rc;}}
 *out=t;return OGP_OK;
}
int ogp_from_carperf(OGPSpec *out,const OGPRegistry *r,const uint8_t levels[9],const uint16_t masks[9],const uint8_t *aero_ids,size_t aero_count,const float dyno[5],uint32_t junkman){
 uint8_t ls[10]={0};uint32_t c,lv,slot,path;OGPSpec t,stack;int rc;
 if(!out||!registry_ok(r)||!levels||!masks||!dyno||(!aero_ids&&aero_count)||aero_count>68)return OGP_INVALID;
 for(c=0;c<5;c++)if(!isfinite(dyno[c])||dyno[c]!=0)return OGP_UNSUPPORTED_DYNO;
 for(c=0;c<9;c++){if(levels[c]>3||masks[c]>4095)return OGP_INVALID;ls[cp_paths[c]]=levels[c];}
 rc=ogp_from_levels(&t,r,ls,junkman);if(rc)return rc;
 for(c=0;c<9;c++){path=cp_paths[c];for(lv=0;lv<3;lv++)for(slot=0;slot<4;slot++)if(lv+1>levels[c]&&(masks[c]&(1u<<(lv*4+slot)))){if(slot>=r->count[path][lv])return OGP_UNKNOWN_PART;memset(&stack,0,sizeof stack);rc=install(&t,r,r->ids[path][lv][slot],1,&stack);if(rc)return rc;}}
 for(c=0;c<aero_count;c++){uint32_t id=aero_ids[c];if(id>=68||!r->parts[id].present||r->parts[id].path!=9)return OGP_UNKNOWN_PART;memset(&stack,0,sizeof stack);rc=install(&t,r,id,1,&stack);if(rc)return rc;}
 *out=t;return OGP_OK;
}
static int levels(OGPAux *a,const OGPSpec *s,const OGPRegistry *r){uint32_t p,lv,j,k;for(p=0;p<10;p++){float sum=0;for(lv=0;lv<3;lv++)for(j=0;j<r->count[p][lv];j++){uint32_t id=r->ids[p][lv][j];if(has(s,id))for(k=0;k<counts[p];k++)if(r->parts[id].weights[k]!=-1)a->components[p][k]=add(a->components[p][k],r->parts[id].weights[k]);}for(k=0;k<counts[p];k++)sum=add(sum,a->components[p][k]);a->average[p]=sum/(float)counts[p];if(a->average[p]<0||a->average[p]>1){sum=0;for(k=0;k<counts[p];k++){float v=a->components[p][k];if(v<0)v=0;if(v>1)v=1;a->components[p][k]=v;sum=add(sum,v);}a->average[p]=sum/(float)counts[p];}if(!isfinite(a->average[p])||a->average[p]<0||a->average[p]>1)return OGP_INVALID;
 a->stored_level[p]=mul(a->average[p],3);a->normalized[p]=a->stored_level[p]/3;a->rounded_level[p]=(uint32_t)add(a->stored_level[p],.5f);}
 return OGP_OK;}
static int finite_array(const float *p,uint32_t n){uint32_t i;for(i=0;i<n;i++)if(!isfinite(p[i]))return 0;return 1;}
static int profile_ok(const OGProfile *p){return p->version==OG_PROFILE_VERSION&&(p->qualified_fields&OG_HAS_BASE)&&og_profile_valid(p);}
/* Original GetNextAvailablePart score counts all FOUR non-sentinel slots,
 * then sums the FIRST count entries. Do not replace it with a filtered mean. */
static float part_score(const OGPPart *p){uint32_t n=0,k;float sum=0;for(k=0;k<4;k++)if(p->weights[k]!=-1)n++;if(!n)return NAN;for(k=0;k<n;k++)sum=add(sum,p->weights[k]);return sum/(float)n;}
static int next_aero(const OGPSpec *s,const OGPRegistry *r,uint32_t lv,int lowest){int best=-1;float score=0;uint32_t j;for(j=0;j<r->count[9][lv-1];j++){uint32_t id=r->ids[9][lv-1][j];float v;if(has(s,id))continue;v=part_score(&r->parts[id]);if(!isfinite(v))return -2;if(best<0||(lowest?v<score:v>score)){best=(int)id;score=v;}}return best;}
static void remove_aero(OGPSpec *s,const OGPRegistry *r){uint32_t i;for(i=0;i<68;i++)if(r->parts[i].present&&r->parts[i].path==9)drop(s,i);}
static int average_aero(float *out,const OGPSpec *s,const OGPRegistry *r){OGPAux a;memset(&a,0,sizeof a);if(levels(&a,s,r))return OGP_INVALID;*out=a.average[9];return OGP_OK;}
static int full_aero(OGPSpec *s,const OGPRegistry *r,uint32_t level){uint32_t l,j;int rc;OGPSpec stack;remove_aero(s,r);for(l=0;l<level;l++)for(j=0;j<r->count[9][l];j++){memset(&stack,0,sizeof stack);rc=install(s,r,r->ids[9][l][j],0,&stack);if(rc)return rc;}return OGP_OK;}
int ogp_match_aero(OGPSpec *out,const OGPSpec *before,const OGPRegistry *r,float target){
 OGPSpec base,candidate,best,stack;float current,error;uint32_t lv,limit,order;int reached,rc;
 if(!out||!spec_ok(before,r)||!isfinite(target)||target<0||target>1)return OGP_INVALID;
 base=*before;remove_aero(&base,r);candidate=best=base;
 rc=average_aero(&current,&base,r);if(rc)return rc;
 error=fabsf(current-target);reached=error<0.05;
 limit=(uint32_t)add(mul(target,3),.5f)+1;if(limit>3)limit=3;
 for(lv=1;lv<=limit&&!reached;lv++){
  for(order=0;order<3&&!reached;order++){
   int low=order!=0;uint32_t iterations=0;
   while(!reached){int id=next_aero(&candidate,r,lv,low);float value,diff;
    if(id==-1)break;if(id<0||++iterations>68)return OGP_INVALID;
    memset(&stack,0,sizeof stack);rc=install(&candidate,r,(uint32_t)id,0,&stack);if(rc)return rc;
    rc=average_aero(&value,&candidate,r);if(rc)return rc;diff=fabsf(value-target);
    if(diff<error){best=candidate;error=diff;reached=error<0.05;}
    if(order==2)low=!low;
   }
   candidate=base;
  }
  if(!reached&&lv<limit){rc=full_aero(&base,r,lv);if(rc)return rc;candidate=base;}
 }
 *out=best;return OGP_OK;
}
int ogp_compose(OGProfile *out,OGPAux *aux,const OGProfile *stock,const void *bytes,size_t raw_size,const OGPRegistry *r,const OGPSpec *input_spec,const OGPPolicy *policy){
 OGPSpec work;const OGPSpec *s=input_spec;OGProfile t;OGPAux a;const unsigned char *raw=bytes;uint32_t k,ax,idx,off;float u,main,roll,steer;
 if(!out||!aux||!stock||!raw||raw_size!=OGP_RAW_SIZE||!spec_ok(s,r)||!policy||policy->visual_aero_hook>1)return OGP_INVALID;
 if(policy->opponent)return OGP_UNSUPPORTED_OPPONENT;
 for(k=0;k<5;k++)if(!isfinite(policy->dyno[k])||policy->dyno[k]!=0)return OGP_UNSUPPORTED_DYNO;
 if(!profile_ok(stock)||(stock->qualified_fields&OG_HAS_COMPOSED))return OGP_INVALID_PROFILE;
 /* Every package endpoint read below must be finite before any result commit. */
 for(off=0;off<=0x608;off+=4){/* CPI stock includes one integer drive-count. */if(off==0x1c8)continue;if(!isfinite(g(raw,off)))return OGP_INVALID_PROFILE;}
 t=*stock;memset(&a,0,sizeof a);if(levels(&a,s,r))return OGP_INVALID;
 if(!policy->visual_aero_hook){
  float target=add(add(a.normalized[8],a.normalized[0]),a.normalized[5])/3;
  int rc;if(target<0)target=0;if(target>1)target=1;
  rc=ogp_match_aero(&work,input_spec,r,target);if(rc)return rc;
  s=&work;memset(&a,0,sizeof a);if(levels(&a,s,r))return OGP_INVALID;
 }
 a.composed_spec=*s;
 for(k=0;k<4;k++)a.nitrous_info[k]=g(raw,0x2a0+4*k);
 u=a.average[0];if(u){t.com.x=add(t.com.x,mul(g(raw,0x344),u));t.additional_yaw_inertia=lerp(t.additional_yaw_inertia,g(raw,0x348),u);t.mass=mul(t.mass,lerp(1,g(raw,0x340),u));}
 u=a.average[1];if(u){float box_level=add(mul(a.components[1][2],3),.5f);if(box_level>=1&&box_level<4){idx=(uint32_t)box_level;off=0x310+idx*0x40;t.drive.info_slot_00=g(raw,off);t.drive.info_slot_04=g(raw,off+4);t.drive.final_front=g(raw,off+8);t.drive.final_rear=g(raw,off+12);t.drive.rear_share=g(raw,off+16);for(k=0;k<8;k++)t.drive.ratios[k]=g(raw,off+0x20+4*k);t.drive.forward_count=6;for(k=2;k<8;k++)if(t.drive.ratios[k]==0){t.drive.forward_count=k-2;break;}}t.drive.clutch_rate=lerp(t.drive.clutch_rate,g(raw,0x3ec),a.components[1][0]);t.drive.clutch_spring=lerp(t.drive.clutch_spring,g(raw,0x3e4),a.components[1][0]);t.engine.inertia=add(t.engine.inertia,mul(g(raw,0x41c),a.components[1][1]));}
 u=a.average[2];if(u){a.nitrous_info[0]=lerp(a.nitrous_info[0],g(raw,0x580),a.components[2][0]);a.nitrous_info[3]=g(raw,0x58c);}
 u=a.average[3];if(u){for(k=0;k<9;k++)t.engine.torque[k]=add(t.engine.torque[k],mul(g(raw,0x420+4*k),u));for(k=0;k<3;k++)t.engine.brake_torque[k]=add(t.engine.brake_torque[k],mul(g(raw,0x444+4*k),u));}
 u=a.average[4];for(k=0;k<9;k++){t.engine.turbo_torque[k]=lerp(t.engine.turbo_torque[k],g(raw,0x510+4*k),u);t.engine.torque[k]=add(t.engine.torque[k],t.engine.turbo_torque[k]);}
 u=a.average[5];main=a.components[5][0];roll=a.components[5][1];steer=a.components[5][2];if(u){for(ax=0;ax<2;ax++)for(k=0;k<8;k++)t.suspension[ax][k]=lerp(t.suspension[ax][k],g(raw,0x5c0+ax*0x20+4*k),k==4?roll:main);for(k=0;k<4;k++)t.wheels[k].mount_geometry.z=add(t.wheels[k].mount_geometry.z,mul(g(raw,k<2?0x604:0x608),main));t.com.z=t.com.z-mul(g(raw,0x600),main);t.steering_ratio=lerp(t.steering_ratio,g(raw,0x5bc),steer);}
 u=a.average[6];if(u){t.brake=lerp(t.brake,g(raw,0x5b0),u);t.handbrake=lerp(t.handbrake,g(raw,0x5b4),u);t.rear_brake_share=lerp(t.rear_brake_share,g(raw,0x5b8),u);}
 u=a.average[7];if(has(s,45)||has(s,46)||has(s,51))t.governor_mph=0;if(u){idx=a.rounded_level[7];if(idx>=1&&idx<=3){off=0x410+idx*0x40;t.engine.idle_rpm=add(t.engine.idle_rpm,g(raw,off));t.engine.red_rpm=add(t.engine.red_rpm,g(raw,off+4));t.engine.max_rpm=add(t.engine.max_rpm,g(raw,off+8));}for(k=0;k<9;k++)t.engine.torque[k]=add(t.engine.torque[k],mul(g(raw,0x4e0+4*k),u));for(k=0;k<3;k++)t.engine.brake_torque[k]=add(t.engine.brake_torque[k],mul(g(raw,0x504+4*k),u));}
 u=a.average[8];if(u)for(ax=0;ax<2;ax++)for(k=0;k<5;k++)t.tire[ax][k]=lerp(t.tire[ax][k],g(raw,0x540+ax*0x20+4*k),u);
 u=a.average[9];if(u){t.aero[1]=add(t.aero[1],mul(add(g(raw,0x5a0),g(raw,0x590)),a.components[9][0]));t.aero[2]=add(t.aero[2],mul(add(g(raw,0x5a4),g(raw,0x594)),a.components[9][1]));t.suspension_downforce_share=add(g(raw,0x26c),mul(add(g(raw,0x5a8),g(raw,0x598)),u));}else t.suspension_downforce_share=g(raw,0x26c);
 /* Original tensor rebuild precedes Junkman. Return it separately: OGProfile
  * keeps mass/body only; consumer must not silently rebuild in a wrong phase. */
 for(k=0;k<3;k++){uint32_t x=(k+1)%3,y=(k+2)%3;float sx=mul(t.body[x],t.body[x]),sy=mul(t.body[y],t.body[y]);a.inertia_before_junkman[k]=mul(add(sx,sy),t.mass)/12;}
 if(s->junkman&1u)t.mass=mul(t.mass,1-g(raw,0x2b0));
 if(s->junkman&(1u<<1))t.drive.rear_share=g(raw,0x2b4);
 if(s->junkman&(1u<<2))a.nitrous_info[3]=add(a.nitrous_info[3],g(raw,0x2b8));
 if(s->junkman&(1u<<6))t.brake=add(t.brake,g(raw,0x2bc));
 if(s->junkman&(1u<<8))for(ax=0;ax<2;ax++){t.tire[ax][3]=add(t.tire[ax][3],g(raw,0x2c0+ax*4));t.tire[ax][4]=add(t.tire[ax][4],g(raw,0x2c8+ax*4));}
 for(k=0;k<9;k++){if(s->junkman&(1u<<3))t.engine.torque[k]=add(t.engine.torque[k],g(raw,0x2d0+k*4));if(s->junkman&(1u<<7))t.engine.torque[k]=add(t.engine.torque[k],g(raw,0x2f4+k*4));if(s->junkman&(1u<<4)){float delta=g(raw,0x318+k*4);t.engine.torque[k]=add(t.engine.torque[k],delta);t.engine.turbo_torque[k]=add(t.engine.turbo_torque[k],delta);}}
 /* t=*stock preserves CPI+280/+284. Mode provenance belongs to the pipeline,
  * not a stock/composed input; never carry an earlier mode tag through here. */
 t.qualified_fields=(t.qualified_fields|OG_HAS_COMPOSED)&~(OG_HAS_CAR_CLASS|OG_HAS_NITRO|OG_HAS_NORMAL_TUNING);t.car_class=UINT32_MAX;a.classifier_required_after_composition=1;
 if(!profile_ok(&t)||!finite_array(a.nitrous_info,4)||!finite_array(a.inertia_before_junkman,3))return OGP_INVALID_PROFILE;
 /* No aliasing of aux/out is permitted; ordinary in-place profile compose is. */
 *out=t;*aux=a;return OGP_OK;
}
size_t ogp_profile_size(void){return sizeof(OGProfile);}size_t ogp_registry_size(void){return sizeof(OGPRegistry);}size_t ogp_aux_size(void){return sizeof(OGPAux);}
