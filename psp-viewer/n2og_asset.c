#include "n2og_asset.h"
#include "og_profile.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define TOTAL 75776u
#define RECORDS 64u
#define REGISTRY 64328u
#define NOS 75728u
#define STRIDE (24u+0x890u)
struct N2OGData {OGPRegistry registry;float nos[9],levels[3];uint8_t bytes[];};
static const char *const models[29]={"PEUGOT","FOCUS","COROLLA","240SX","MIATA","CIVIC","HUMMER","NAVIGATOR","ESCALADE","TIBURON","SENTRA","CELICA","IS300","SUPRA","GOLF","A3","RSX","ECLIPSE","TT","RX8","350Z","G35","3000GT","GTO","MUSTANGGT","SKYLINE","LANCEREVO8","RX7","IMPREZAWRX"};
static uint32_t u32(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static float rf(const uint8_t *p){uint32_t u=u32(p);float f;memcpy(&f,&u,4);return f;}
static int validate(N2OGData *d){
 static const uint32_t header[15]={1,0x01020304,64,29,24,0x890,30,0x17c,9,3,RECORDS,REGISTRY,NOS,TOTAL,0};
 static const float curve[9]={.3f,.4f,.45f,.5f,.55f,.6f,.7f,1.f,.7f},levels[3]={1,1.4f,1.7f};uint32_t last=0;
 if(memcmp(d->bytes,"N2OG",4))return N2OG_INVALID;
 for(unsigned k=0;k<15;k++)if(u32(d->bytes+4+4*k)!=header[k])return N2OG_INVALID;
 for(unsigned k=0;k<29;k++){
  const uint8_t *name=d->bytes+RECORDS+STRIDE*k,*raw=name+24;size_t len=strlen(models[k]);OGProfile stock;uint32_t id=u32(raw+0x840);
  if(memcmp(name,models[k],len)||name[len]||memcmp(name,raw,len)||raw[len]||id>30||(k&&id<=last))return N2OG_INVALID;
  for(size_t z=len+1;z<24;z++)if(name[z])return N2OG_INVALID;
  if(og_profile_from_record(&stock,raw,0x890))return N2OG_INVALID;
  last=id;
 }
 if(ogp_registry_parse(&d->registry,d->bytes+REGISTRY,OGP_REGISTRY_BYTES))return N2OG_INVALID;
 for(unsigned k=0;k<9;k++){d->nos[k]=rf(d->bytes+NOS+4*k);if(memcmp(d->nos+k,curve+k,4))return N2OG_INVALID;}
 for(unsigned k=0;k<3;k++){d->levels[k]=rf(d->bytes+NOS+36+4*k);if(memcmp(d->levels+k,levels+k,4))return N2OG_INVALID;}
 return N2OG_OK;
}
void n2og_free(N2OGData *d){free(d);}
int n2og_load_bytes(const void *b,size_t size,N2OGData **out){
 if(!b||!out||size!=TOTAL)return N2OG_INVALID;N2OGData *d=malloc(sizeof *d+TOTAL);if(!d)return N2OG_NOMEM;memcpy(d->bytes,b,TOTAL);int rc=validate(d);if(rc){free(d);return rc;}*out=d;return N2OG_OK;
}
int n2og_load(const char *path,N2OGData **out){
 if(!path||!out)return N2OG_INVALID;FILE *f=fopen(path,"rb");if(!f)return N2OG_IO;
 if(fseek(f,0,SEEK_END)||ftell(f)!=TOTAL||fseek(f,0,SEEK_SET)){fclose(f);return N2OG_INVALID;}
 N2OGData *d=malloc(sizeof *d+TOTAL);if(!d){fclose(f);return N2OG_NOMEM;}
 size_t n=fread(d->bytes,1,TOTAL,f);fclose(f);if(n!=TOTAL){free(d);return N2OG_IO;}
 int rc=validate(d);if(rc){free(d);return rc;}*out=d;return N2OG_OK;
}
const uint8_t *n2og_find_model(void *context,const char *model,size_t *raw_bytes){
 const N2OGData *d=context;if(raw_bytes)*raw_bytes=0;if(!d||!model)return NULL;
 for(unsigned k=0;k<29;k++){const uint8_t *name=d->bytes+RECORDS+STRIDE*k;if(!strcmp((const char *)name,model)){if(raw_bytes)*raw_bytes=0x890;return name+24;}}return NULL;
}
const OGPRegistry *n2og_registry(const N2OGData *d){return d?&d->registry:NULL;}
const float *n2og_nos_curve(const N2OGData *d){return d?d->nos:NULL;}
const float *n2og_nos_levels(const N2OGData *d){return d?d->levels:NULL;}
size_t n2og_count(const N2OGData *d){return d?29:0;}
const char *n2og_model_name(const N2OGData *d,size_t index){return d&&index<29?(const char *)(d->bytes+RECORDS+STRIDE*index):NULL;}
