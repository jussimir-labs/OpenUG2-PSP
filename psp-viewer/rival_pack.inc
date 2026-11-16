/* SOURCE REVIEW ONLY. N2RS v1: immutable nonCPU stock rivals, two owners.
 * Included after model_strips in d723 main.c. No change to N2PV parsing.
 * Call only on Main, outside an unfinished GU list, with no CPU borrower.
 * Content equivalence belongs to the offline oracle; this reader validates
 * layout/CRC/floats/metadata. CRC is identity/error detection, not security. */
#include <zlib.h>
enum { RS_LIMIT=308592, RS_HEAD=64, RS_META=224, RS_TEX=32, RS_MESH=48 };
enum { RS_TOTAL=2, RS_A=3, RS_B=4, RS_SOURCE_CRC=5, RS_SOURCE_BYTES=6,
       RS_NAME=7, RS_NT=8, RS_NM=9, RS_TRI=10, RS_SKIN=11,
       RS_TEX_AT=12, RS_MESH_AT=13, RS_CRC_A=14, RS_CRC_B=15 };
typedef struct { float lo[9][3],hi[9][3];unsigned seen; } RsBounds;
_Static_assert(sizeof(Vertex)==32,"N2RS Vertex ABI");
_Static_assert(sizeof(float)==4 && sizeof(uint32_t)==4,"N2RS word ABI");

static uint32_t rs_name_key(const char *s) {
    uint32_t h=2166136261u;for(;*s;s++)h=(h^(unsigned char)*s)*16777619u;return h;
}
static unsigned rs_source_size(const char *name) {
    if(!strcmp(name,"car-FOCUS.n2pv"))return 261888;
    if(!strcmp(name,"car-PEUGOT.n2pv"))return 308592;
    if(!strcmp(name,"car-COROLLA.n2pv"))return 299824;
    return 0;
}
static float rs_float(const unsigned char *p) { float f;memcpy(&f,p,4);return f; }
static int rs_read_exact(FILE *f,void *p,size_t n) {
    return fread(p,1,n,f)==n && !ferror(f);
}
static int rs_header(const unsigned char raw[RS_HEAD],uint32_t h[16],long length) {
    if(memcmp(raw,"N2RS",4) || word(raw+4)!=1)return 0;
    for(unsigned k=0;k<16;k++)h[k]=word(raw+4*k);
    if(h[RS_A]<RS_HEAD+RS_META || h[RS_A]>RS_LIMIT || !h[RS_B] || h[RS_B]>RS_LIMIT ||
       (h[RS_A]&15) || (h[RS_B]&15) || h[RS_TOTAL]!=h[RS_A]+h[RS_B] ||
       length<0 || (unsigned long)length!=h[RS_TOTAL] ||
       !h[RS_NM] || h[RS_NM]>MAX_MESH || h[RS_NT]>MAX_TEX || !h[RS_TRI])return 0;
    if(h[RS_TEX_AT]!=RS_HEAD+RS_META || h[RS_MESH_AT]!=h[RS_TEX_AT]+h[RS_NT]*RS_TEX ||
       h[RS_MESH_AT]+h[RS_NM]*RS_MESH>h[RS_A])return 0;
    return 1;
}
/* At most one stdio stream is open at a time; no original pack owner.
 * The old N2PV header and full CRC bind this alternate to installed inputs. */
static int rs_source_matches(const char *name,const uint32_t h[16]) {
    unsigned expected=rs_source_size(name);
    if(!expected || h[RS_SOURCE_BYTES]!=expected || h[RS_NAME]!=rs_name_key(name))return 0;
    FILE *f=fopen(name,"rb");if(!f)return 0;
    unsigned char block[4096];int ok=0;uLong crc=crc32(0L,Z_NULL,0);
    if(fseek(f,0,SEEK_END) || ftell(f)!=(long)expected || fseek(f,0,SEEK_SET) ||
       !rs_read_exact(f,block,32) || memcmp(block,"N2PV",4) || word(block+4)!=3 ||
       word(block+8)!=h[RS_NT] || word(block+12)!=h[RS_NM] ||
       word(block+16)!=h[RS_TRI] || word(block+24)!=h[RS_SKIN] || fseek(f,0,SEEK_SET))goto done;
    for(unsigned at=0;at<expected;) {
        unsigned n=expected-at;if(n>sizeof block)n=sizeof block;
        if(!rs_read_exact(f,block,n))goto done;
        crc=crc32(crc,block,n);at+=n;
    }
    ok=(uint32_t)crc==h[RS_SOURCE_CRC];
done:{int closed=fclose(f);f=NULL;if(closed)ok=0;return ok;}
}
static void rs_bounds_init(RsBounds *b) {
    memset(b,0,sizeof *b);
    for(unsigned k=0;k<9;k++)for(unsigned c=0;c<3;c++){b->lo[k][c]=1e9f;b->hi[k][c]=-1e9f;}
}
static void rs_bounds_add(RsBounds *b,unsigned group,const float pos[3]) {
    b->seen|=1u<<group;
    for(unsigned c=0;c<3;c++){b->lo[group][c]=fminf(b->lo[group][c],pos[c]);b->hi[group][c]=fmaxf(b->hi[group][c],pos[c]);}
}
static int rs_vertices(const unsigned char *p,unsigned n,unsigned flags,RsBounds *b) {
    for(unsigned j=0;j<n;j++,p+=32) {
        float v[8];memcpy(v,p,sizeof v);
        for(unsigned c=0;c<8;c++)if(!isfinite(v[c]))return 0;
        if(flags&F_REFLECT) {
            float norm=sqrtf(v[2]*v[2]+v[3]*v[3]+v[4]*v[4]);
            if(!isfinite(norm) || norm<.1f)return 0;
        }
        rs_bounds_add(b,0,v+5);
        if((flags&F_WHEEL) && !(flags&F_WHEEL_FIXED))rs_bounds_add(b,1+((flags>>8)&3),v+5);
        if(!(flags&(F_WHEEL|F_DECAL)))for(unsigned k=0;k<4;k++)
            if((int)((flags&SLOT_MASK)>>SLOT_SHIFT)==PIECE_SLOT[k])rs_bounds_add(b,5+k,v+5);
    }
    return 1;
}
static int rs_metadata(const unsigned char *a,const RsBounds *b) {
    const unsigned char *p=a+RS_HEAD;unsigned hubs=word(p+216),pieces=word(p+220);
    if(hubs&~15u || pieces&~15u || hubs!=((b->seen>>1)&15) || pieces!=((b->seen>>5)&15))return 0;
    for(unsigned k=0;k<9;k++)for(unsigned c=0;c<3;c++) {
        float lo=rs_float(p+24*k+4*c),hi=rs_float(p+24*k+12+4*c);
        if(!isfinite(lo) || !isfinite(hi))return 0;
        if(b->seen&(1u<<k)) {
            /* Numeric equality accepts either zero sign; publication uses
             * the ORIGINAL stored float bits, not reordered strip extrema. */
            if(lo!=b->lo[k][c] || hi!=b->hi[k][c] || lo>hi)return 0;
        } else if(lo!=0.f || hi!=0.f)return 0;
    }
    return 1;
}
static int rs_validate(const unsigned char *a,const unsigned char *b,const uint32_t h[16]) {
    size_t cursor=h[RS_MESH_AT]+h[RS_NM]*RS_MESH;
    for(unsigned k=0;k<h[RS_NT];k++) {
        const unsigned char *t=a+h[RS_TEX_AT]+k*RS_TEX;
        unsigned w=word(t+4),height=word(t+8),bytes=word(t+16),off=word(t+24);
        float repeat=rs_float(t+20);
        if(word(t+12)!=1 || w<16 || height<8 || w>512 || height>512 ||
           (w&(w-1)) || (height&(height-1)) || bytes!=w*height ||
           !(repeat>0.f && repeat<=16.f) || word(t+28) || off!=cursor ||
           cursor>h[RS_A] || 1024u+bytes>h[RS_A]-cursor)return 0;
        cursor+=align16(1024u+bytes);
    }
    if(cursor!=h[RS_A])return 0;
    RsBounds bounds;rs_bounds_init(&bounds);cursor=0;unsigned tris=0;
    const unsigned allowed=F_ALL|F_WHEEL|F_WHEEL_INDEX|F_WHEEL_FIXED|F_DECAL|F_SPINNER|
        F_INSIDE_HOOD|F_INSIDE_TRUNK|F_AUDIO|F_RIGHT|F_LID|SLOT_MASK|DECAL_SLOT_MASK;
    for(unsigned k=0;k<h[RS_NM];k++) {
        const unsigned char *m=a+h[RS_MESH_AT]+k*RS_MESH;
        unsigned nv=word(m),ni=word(m+4),tex=word(m+8),flags=word(m+16),rep=word(m+24);
        unsigned count=word(m+28),off=word(m+32),bytes=word(m+36),orphans=word(m+40),orph_at=word(m+44);
        float uv=rs_float(m+20);
        if(!nv || nv>65535 || !ni || ni%3 || (flags&~allowed) || !(uv>0.f) || !isfinite(uv) ||
           (tex!=UINT32_MAX && tex>=h[RS_NT]) || off!=cursor || cursor>h[RS_B] ||
           bytes>h[RS_B]-cursor || (bytes&15) || orphans>nv)return 0;
        if(rep==1) {
            if(count<3 || count>65535 || bytes!=align16(count*32u)+align16(orphans*32u) ||
               orph_at!=off+align16(count*32u) || !rs_vertices(b+off,count,flags,&bounds) ||
               !rs_vertices(b+orph_at,orphans,flags,&bounds))return 0;
        } else if(rep==2) {
            /* Source byte/count cap bounds arithmetic before multiplication. */
            if(count!=ni || orphans || orph_at || ni>(h[RS_B]/2u) ||
               bytes!=align16(nv*32u)+align16(ni*2u) || !rs_vertices(b+off,nv,flags,&bounds))return 0;
            const unsigned char *idx=b+off+align16(nv*32u);
            for(unsigned j=0;j<ni;j++){uint16_t v;memcpy(&v,idx+2*j,2);if(v>=nv)return 0;}
        } else return 0;
        if(ni/3>UINT32_MAX-tris)return 0;tris+=ni/3;cursor+=bytes;
    }
    return cursor==h[RS_B] && tris==h[RS_TRI] && rs_metadata(a,&bounds);
}
/* No failure points after validation. All addresses borrow exactly one
 * of a/b, which are independent memalign owners and never aliases. */
static void rs_publish(Model *mm,unsigned char *a,unsigned char *b,const uint32_t h[16]) {
    mm->data=a;mm->bytes=h[RS_A];mm->strip_data=b;mm->strip_owned_bytes=h[RS_B];
    mm->nt=h[RS_NT];mm->nm=h[RS_NM];mm->triangles=h[RS_TRI];mm->skin_info=h[RS_SKIN];
    for(unsigned k=0;k<mm->nt;k++) {
        const unsigned char *p=a+h[RS_TEX_AT]+k*RS_TEX;Texture *t=&mm->tex[k];
        t->key=word(p);t->w=word(p+4);t->h=word(p+8);t->vrepeat=rs_float(p+20);
        t->clut=a+word(p+24);t->pixels=(const unsigned char *)t->clut+1024;
        mm->texture_bytes+=1024+t->w*t->h;
    }
    for(unsigned k=0;k<mm->nm;k++) {
        const unsigned char *p=a+h[RS_MESH_AT]+k*RS_MESH;Mesh *m=&mm->mesh[k];
        m->nv=word(p);m->ni=word(p+4);m->texture=word(p+8);m->color=word(p+12);m->flags=word(p+16);m->uvscale=rs_float(p+20);
        if(word(p+24)==1){m->sv=(const Vertex *)(b+word(p+32));m->ns=word(p+28);}
        else{m->v=(const Vertex *)(b+word(p+32));m->i=(const uint16_t *)(b+word(p+32)+align16(m->nv*32u));}
    }
    const unsigned char *meta=a+RS_HEAD;float lo[3],hi[3];memcpy(lo,meta,12);memcpy(hi,meta+12,12);
    for(unsigned c=0;c<3;c++)mm->center[c]=(lo[c]+hi[c])*.5f;
    mm->floor_y=lo[1];mm->half[0]=(hi[0]-lo[0])*.5f;mm->half[1]=(hi[2]-lo[2])*.5f;
    mm->radius=sqrtf((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[2]-lo[2])*(hi[2]-lo[2]))*.5f;
    mm->hubs=(int)word(meta+216);mm->pieces=(int)word(meta+220);
    for(unsigned k=0;k<4;k++) {
        if(mm->hubs&(1<<k))for(unsigned c=0;c<3;c++)
            mm->hub[k][c]=(rs_float(meta+24*(1+k)+4*c)+rs_float(meta+24*(1+k)+12+4*c))*.5f;
        if(mm->pieces&(1<<k)){memcpy(mm->piece_lo[k],meta+24*(5+k),12);memcpy(mm->piece_hi[k],meta+24*(5+k)+12,12);}
    }
}
/* Empty slot only. Pending a/b remain local until FULL validation succeeds.
 * On every error both are freed and mm is untouched (still empty). */
static int rs_try_empty(Model *mm,const char *legacy) {
    if(mm==&model || mm->data || mm->strip_data || mm->coat_data || !rs_source_size(legacy))return 0;
    char alt[48];int len=(int)strlen(legacy);if(len<5)return 0;
    int n=snprintf(alt,sizeof alt,"%.*s.n2rs",len-5,legacy);if(n<0 || n>=(int)sizeof alt)return 0;
    unsigned char raw[RS_HEAD],again[RS_HEAD],*a=NULL,*b=NULL;uint32_t h[16];
    FILE *f=fopen(alt,"rb");if(!f)return 0;int ok=0;
    if(fseek(f,0,SEEK_END))goto done;long length=ftell(f);
    if(fseek(f,0,SEEK_SET) || !rs_read_exact(f,raw,sizeof raw) || !rs_header(raw,h,length))goto done;
    int closed=fclose(f);f=NULL;
    if(closed || !rs_source_matches(legacy,h))goto done;
    f=fopen(alt,"rb");if(!f || !rs_read_exact(f,again,sizeof again) || memcmp(raw,again,sizeof raw) || fseek(f,0,SEEK_SET))goto done;
    model_load_stage="rival-rs-meta-alloc";model_requested=h[RS_A];a=memalign(16,h[RS_A]);if(!a)goto done;
    model_load_stage="rival-rs-meta-read";if(!rs_read_exact(f,a,h[RS_A]) || memcmp(a,raw,RS_HEAD) ||
       (uint32_t)crc32(crc32(0L,Z_NULL,0),a+RS_HEAD,h[RS_A]-RS_HEAD)!=h[RS_CRC_A])goto done;
    model_load_stage="rival-rs-draw-alloc";model_requested=h[RS_B];b=memalign(16,h[RS_B]);if(!b)goto done;
    model_load_stage="rival-rs-draw-read";if(!rs_read_exact(f,b,h[RS_B]) ||
       (uint32_t)crc32(crc32(0L,Z_NULL,0),b,h[RS_B])!=h[RS_CRC_B] || fgetc(f)!=EOF || ferror(f))goto done;
    model_load_stage="rival-rs-validate";if(!rs_validate(a,b,h))goto done;
    closed=fclose(f);f=NULL;if(closed)goto done;
    sceKernelDcacheWritebackRange(a,h[RS_A]);sceKernelDcacheWritebackRange(b,h[RS_B]);
    rs_publish(mm,a,b,h);a=b=NULL;texture_flush_pending=1;model_load_stage="model-ready";ok=1;
done:if(f){closed=fclose(f);f=NULL;if(closed)ok=0;}free(a);free(b);return ok;
}
/* Same destructive replacement boundary as legacy load_model_into: previous
 * slot is fenced/freed first. Rollback means no partial NEW slot, not retaining
 * the old slot concurrently; doing so would double residency. */
static int load_rival_model(Model *mm,const char *legacy) {
    if(mm==&model)return load_model_into(mm,legacy);
    if(rs_source_size(legacy)) {
        fail_model(mm);
        if(rs_try_empty(mm,legacy)){model_lamps(mm,legacy);return 1;}
    }
    int ok=load_model_into(mm,legacy);if(ok)model_strips(mm);return ok;
}
