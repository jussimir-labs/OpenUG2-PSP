/* The career's save (see savedata.h). */
#include "savedata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

static int last_error;
int savedata_last_error(void) { return last_error; }
int (*savedata_dialog_hook)(int begin);
int (*savedata_interactive_hook)(int begin);
void (*savedata_frame_hook)(int before_update);
void (*savedata_trace_hook)(int store,int mode,int init,int result);

/* Only these stores may reach the utility. Payload formats belong to their
 * serializers; this layer neither adds a header nor changes existing bytes. */
enum Store { STORE_CAREER, STORE_MYCARS, STORE_PROFILE, STORE_COUNT };
static const struct { const char *name,*title; int limit; } stores[] = {
    {"CAREER","Career",SAVEDATA_CAREER_MAX_BYTES},
    {"MYCARS","My Cars",SAVEDATA_MYCARS_MAX_BYTES},
    {"PROFILE","Profile",SAVEDATA_PROFILE_BYTES}
};
/* Profile slots (savedata.h): CAREER and PROFILE carry the slot's number
 * from slot 1 on; slot 0 keeps the names every build so far wrote. */
static int slot,legacy_off;static char owner[16];
static int manual_silent;   /* savedata_manual_silent */
static __attribute__((unused)) int manual_silent_on(void) { return manual_silent; }
void savedata_set_slot(int k) { if(k>=0 && k<SAVEDATA_SLOTS)slot=k; }
int savedata_slot(void) { return slot; }
void savedata_set_owner(const char *name) { snprintf(owner,sizeof owner,"%s",name?name:""); }
void savedata_legacy_off(void) { legacy_off=1; }
static int slotted(enum Store store) { (void)store;return 1; }   /* every store is the profile's, My Cars too (the original's UserProfile: FEPlayerCarDB and the career) */
static __attribute__((unused)) void store_name(enum Store store,int k,char out[20]) {
    if(slotted(store) && k>0)snprintf(out,20,"%s%d",stores[store].name,k);else snprintf(out,20,"%s",stores[store].name);
}
static __attribute__((unused)) int slot_of_name(enum Store store,const char *name) {   /* -1: not one of the store's */
    size_t n=strlen(stores[store].name);
    if(strncmp(name,stores[store].name,n))return -1;
    if(!name[n])return 0;
    return name[n]>='1' && name[n]<'0'+SAVEDATA_SLOTS && !name[n+1]?name[n]-'0':-1;
}
static int valid_request(enum Store store,const void *data,int size) {
    if((unsigned)store>=STORE_COUNT || !data || size<=0) {
        last_error=-EINVAL;return 0;
    }
    if(size>stores[store].limit){last_error=-EFBIG;return 0;}
    return 1;
}

/* 0 means absence only. Stage reads so errors cannot partially change a
 * caller's state. The serializer still checks version, length and checksum. */
static int file_read(const char *path,void *data,int max) {
    if(!path || !*path){last_error=-EINVAL;return -1;}
    FILE *f=fopen(path,"rb");
    if(!f){last_error=errno==ENOENT?0:-errno;return last_error?-1:0;}
    void *buf=malloc((size_t)max);
    if(!buf){fclose(f);last_error=-ENOMEM;return -1;}
    int n=(int)fread(buf,1,(size_t)max,f),tail=fgetc(f);
    int error=ferror(f)?-EIO:tail!=EOF?-EFBIG:n==0?-EIO:0;
    if(fclose(f)!=0 && !error)error=-EIO;
    if(!error)memcpy(data,buf,(size_t)n);
    free(buf);last_error=error;return error?-1:n;
}

#ifdef HOST_SIM
#include <unistd.h>
#include <sys/stat.h>
/* The host's files: the store's path, slot k's with ".k" after it. */
static const char *host_path(enum Store store,const char *legacy,int k,char *buf,size_t n) {
    if(!legacy || !slotted(store) || k<=0)return legacy;
    snprintf(buf,n,"%s.%d",legacy,k);return buf;
}
static int backend_write(enum Store store,const char *legacy,const void *data,int size,const char *detail,int interactive) {
    (void)detail;(void)interactive;
    last_error=0;
    if(!valid_request(store,data,size))return 0;
    char slot_path[1024];legacy=host_path(store,legacy,slot,slot_path,sizeof slot_path);
    if(!legacy || !*legacy){last_error=-EINVAL;return 0;}
    char temp[1024];
    if(snprintf(temp,sizeof temp,"%s.tmp-XXXXXX",legacy)>=(int)sizeof temp){last_error=-ENAMETOOLONG;return 0;}
    int fd=mkstemp(temp);if(fd<0){last_error=-errno;return 0;}
    FILE *f=fdopen(fd,"wb");
    if(!f){last_error=-errno;close(fd);remove(temp);return 0;}
    int ok=fwrite(data,1,(size_t)size,f)==(size_t)size && fflush(f)==0 && fsync(fd)==0;
    if(fclose(f)!=0)ok=0;
    if(ok && rename(temp,legacy)==0)return 1;
    last_error=errno?-errno:-EIO;remove(temp);return 0;
}
static int backend_read(enum Store store,const char *legacy,void *data,int max,int interactive) {
    (void)interactive;
    last_error=0;
    char slot_path[1024];legacy=host_path(store,legacy,slot,slot_path,sizeof slot_path);
    return valid_request(store,data,max)?file_read(legacy,data,max):-1;
}
/* The lists on the host, as PPSSPP_DIALOG_AUTO=yes answers them: Load the
 * latest career, Save the first empty slot, Delete the last one listed. */
static int host_exists(const char *legacy,int k,time_t *when) {
    char b[1024];struct stat at;
    if(stat(host_path(STORE_CAREER,legacy,k,b,sizeof b),&at))return 0;
    if(when)*when=at.st_mtime;return 1;
}
static int backend_slot_present(const char *legacy,int k) { last_error=0;return host_exists(legacy,k,NULL); }
static int backend_list(int mode,const char *legacy,const void *data,int size,const char *detail,int *chosen) {
    last_error=0;int pick=-1;time_t best=0,t;
    for(int k=0;k<SAVEDATA_SLOTS;k++) {
        int here=host_exists(legacy,k,&t);
        if(mode==0 && here && (pick<0 || t>=best)){pick=k;best=t;}
        if(mode==1 && !here && pick<0)pick=k;
        if(mode==2 && here)pick=k;
    }
    if(pick<0)return 0;
    if(chosen)*chosen=pick;
    if(mode==1){int was=slot;slot=pick;int ok=backend_write(STORE_CAREER,legacy,data,size,detail,1);slot=was;return ok?1:-1;}
    if(mode==2){char b[1024];if(remove(host_path(STORE_CAREER,legacy,pick,b,sizeof b))){last_error=-errno;return -1;}}
    return 1;
}
static int backend_store_delete(enum Store store,const char *legacy) {
    char b[1024];last_error=0;
    if(remove(host_path(store,legacy,slot,b,sizeof b)) && errno!=ENOENT){last_error=-errno;return 0;}
    return 1;
}
static int backend_from_legacy(void) { return 0; }
int savedata_idle(void) { return 1; }
void savedata_block_stats(unsigned *n,unsigned *ms,unsigned *max) { *n=*ms=*max=0; }
static int host_async_done;
static int backend_write_async(enum Store store,const char *legacy,const void *data,int size,const char *detail) {
    int ok=backend_write(store,legacy,data,size,detail,0);host_async_done=1;return ok || 1;   /* (the file at once: done on the next poll) */
}
static int backend_async_poll(void) { if(!host_async_done)return -1;host_async_done=0;return 1; }
void savedata_async_stats(unsigned *n,unsigned *max_ms) { *n=*max_ms=0; }
#else
#include <pspdisplay.h>
#include <psputility.h>
#include <pspkernel.h>

/* 9 characters, as a disc's product code. A test build may take another
 * (make SAVE_GAME_NAME=NFSU2TEST): its saves beside the player's, never
 * over them. */
#ifndef NFSU2_SAVE_GAME_NAME
#define NFSU2_SAVE_GAME_NAME "NFSU2VIEW"
#endif
#define GAME_NAME NFSU2_SAVE_GAME_NAME
/* sceUtility LOAD_NO_DATA; local PPSSPP Core/HLE/ErrorCodes.h corroborates
 * this value. Missing DATA.BIN inside an existing save is NOT a new store. */
#define LOAD_NO_DATA 0x80110307u
#ifndef NFSU2_SAVEDATA_WAIT_FRAMES
#define NFSU2_SAVEDATA_WAIT_FRAMES (60*20)
#endif
/* The utility may still own the buffer after timeout. Keep both the parameter
 * block and its private bytes alive; never reinitialize an active request. */
static SceUtilitySavedataParam p;
static void *utility_data;
static int utility_active,shutdown_started;

void kernel_free_watch(void);   /* (main.c) the dialog's own memory too */
static int poll_done(void) {
    kernel_free_watch();
    int status=sceUtilitySavedataGetStatus();
    if(status==PSP_UTILITY_DIALOG_VISIBLE)sceUtilitySavedataUpdate(1);
    else if(status==PSP_UTILITY_DIALOG_QUIT && !shutdown_started) {
        if(sceUtilitySavedataShutdownStart()>=0)shutdown_started=1;
    }
    return status==PSP_UTILITY_DIALOG_FINISHED || (status==PSP_UTILITY_DIALOG_NONE && shutdown_started);
}

static void release_request(void) {
    free(utility_data);utility_data=NULL;utility_active=0;shutdown_started=0;
}
/* No request left and the utility back to none (asking moves it there:
 * only then has it given the volatile block back). */
int savedata_idle(void) {
    /* Never opened this session the PSP answers an error, not none (the
     * PSP-3000, 21/10: the volatile block was never taken back after the
     * power switch's give-back); PPSSPP answers none. Either is idle. */
    int status=sceUtilitySavedataGetStatus();
    return !utility_active && (status==PSP_UTILITY_DIALOG_NONE || status<0);
}

/* ICON0.PNG of the save: the EBOOT's (144 x 80, tools/convert_hud.py), when present */
static void *icon;static int icon_size=-1;
/* Read with the boot's load of the career (its first request), while the
 * heap has room: on the PSP-1000's the first save came after a race, the
 * city's cells in every block, and the 26 KB found none (the save went
 * without its icon from then on). A block that failed is tried again. */
static void load_icon(void) {
    if(icon_size>=0)return;
    FILE *f=fopen("icon0.png","rb");if(!f){icon_size=0;return;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    if(n<=0 || n>=64*1024)icon_size=0;
    else if((icon=malloc((size_t)n))!=NULL && fread(icon,1,(size_t)n,f)==(size_t)n)icon_size=(int)n;
    else if(icon){free(icon);icon=NULL;icon_size=0;}   /* (unreadable: no icon) */
    fclose(f);
}

/* Run the utility in mode until it is done; its result (0 success) and
 * the bytes in the data file (*got). */
/* Diagnostic only: real native reads remain unchanged. Refuse every
 * SAVE/AUTOSAVE before validation, allocation, callbacks or utility init.
 * A refused write is never acknowledged as success. */
#if defined(NFSU2_DIAG_NATIVE_READONLY) && NFSU2_DIAG_NATIVE_READONLY
const unsigned savedata_native_readonly=1;
unsigned savedata_native_write_refusals[STORE_COUNT];
#endif
static int init_result;   /* this request's sceUtilitySavedataInitStart, 1 before it */
static int list_chosen;   /* a list's chosen slot, -1 none */
static int run_request(enum Store store,int mode,void *data,int size,const char *detail,int *got);
static unsigned block_n,block_ms,block_max;   /* the game held by an automatic save (savedata_block_stats) */
static int async_start,async_active,async_store;static uint64_t async_t0;   /* the background save (backend_write_async) */
static unsigned async_n,async_max_ms;
static void async_end(int result) {   /* its end: the utility's result, its time, the volatile block back */
    unsigned ms=(unsigned)((sceKernelGetSystemTimeWide()-async_t0)/1000);async_n++;if(ms>async_max_ms)async_max_ms=ms;
    last_error=result;release_request();async_active=0;
    if(savedata_dialog_hook)savedata_dialog_hook(0);
    if(savedata_trace_hook)savedata_trace_hook(async_store,PSP_UTILITY_SAVEDATA_AUTOSAVE,0,result);
}
static void async_finish(unsigned frames) {   /* a request that cannot wait: the background save to its end first */
    for(unsigned k=0;async_active && k<frames;k++){if(poll_done()){async_end(p.base.result);return;}sceDisplayWaitVblankStart();}
}
void savedata_async_stats(unsigned *n,unsigned *max_ms) { *n=async_n;*max_ms=async_max_ms; }
void savedata_block_stats(unsigned *n,unsigned *ms,unsigned *max) { *n=block_n;*ms=block_ms;*max=block_max; }
static int run(enum Store store,int mode,void *data,int size,const char *detail,int *got) {
    init_result=1;
    uint64_t t0=sceKernelGetSystemTimeWide();
    int result=run_request(store,mode,data,size,detail,got);
    if(mode==PSP_UTILITY_SAVEDATA_AUTOSAVE){unsigned ms=(unsigned)((sceKernelGetSystemTimeWide()-t0)/1000);block_n++;block_ms+=ms;if(ms>block_max)block_max=ms;}
    if(savedata_trace_hook)savedata_trace_hook(store,mode,init_result,result);
    return result;
}
static int run_request(enum Store store,int mode,void *data,int size,const char *detail,int *got) {
    int listing=mode==PSP_UTILITY_SAVEDATA_LISTLOAD || mode==PSP_UTILITY_SAVEDATA_LISTSAVE || mode==PSP_UTILITY_SAVEDATA_LISTDELETE;
    int interactive=mode==PSP_UTILITY_SAVEDATA_SAVE || mode==PSP_UTILITY_SAVEDATA_LOAD || listing;
    int saving=mode==PSP_UTILITY_SAVEDATA_AUTOSAVE || mode==PSP_UTILITY_SAVEDATA_SAVE || mode==PSP_UTILITY_SAVEDATA_LISTSAVE;
    int deleting=mode==PSP_UTILITY_SAVEDATA_LISTDELETE || mode==SCE_UTILITY_SAVEDATA_AUTODELETE;
    if(got)*got=0;
    list_chosen=-1;
#if defined(NFSU2_DIAG_NATIVE_READONLY) && NFSU2_DIAG_NATIVE_READONLY
    if(saving) {
        if((unsigned)store<STORE_COUNT)savedata_native_write_refusals[store]++;
        return -EROFS;
    }
#endif
    if(!valid_request(store,data,size))return last_error;
    if(async_active)async_finish(NFSU2_SAVEDATA_WAIT_FRAMES);   /* a background save first, to its end */
    if(async_active)return -EBUSY;
    if(utility_active) {
        /* A later call may reclaim a timed-out request once shutdown completes. */
        if(!poll_done())return -EBUSY;
        release_request();
    }
    if(interactive && (!savedata_interactive_hook || !savedata_frame_hook))return -EBUSY;
    memset(&p,0,sizeof p);
    p.base.result=-EIO;   /* zero is success only after observed completion */
    p.base.size=sizeof p;
    /* the system's language and confirm button (the utility's own text) */
    int language=PSP_SYSTEMPARAM_LANGUAGE_ENGLISH,swap=PSP_UTILITY_ACCEPT_CROSS;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,&language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP,&swap);
    p.base.language=language;p.base.buttonSwap=swap;
    p.base.graphicsThread=0x11;p.base.accessThread=0x13;p.base.fontThread=0x12;p.base.soundThread=0x10;
    p.mode=(PspUtilitySavedataMode)mode;p.overwrite=1;
    load_icon();
    /* Plain data, no key: version 1. The firmware refuses a save with version
     * 0 and no key (SAVE_PARAM 0x80110388, pspautotests utility/savedata/
     * secureversion on 6.60; seen on the PSP-3000 6.61 on 13/10); PPSSPP
     * accepted it. Saves made so far (PPSSPP only) were plain too. */
    p.secureVersion=1;
    strcpy(p.gameName,GAME_NAME);store_name(store,slot,p.saveName);strcpy(p.fileName,"DATA.BIN");
    /* The lists: every slot's name, the system showing the ones there are
     * (and, to save, "New Data" for the others); the cursor on the newest
     * to load, the first empty to save, the last to delete. */
    static char names[SAVEDATA_SLOTS+1][20];
    if(listing) {
        for(int k=0;k<SAVEDATA_SLOTS;k++)store_name(store,k,names[k]);
        names[SAVEDATA_SLOTS][0]=0;p.saveNameList=names;
        p.focus=mode==PSP_UTILITY_SAVEDATA_LISTLOAD?PSP_UTILITY_SAVEDATA_FOCUS_LATEST:
                mode==PSP_UTILITY_SAVEDATA_LISTSAVE?PSP_UTILITY_SAVEDATA_FOCUS_FIRSTEMPTY:PSP_UTILITY_SAVEDATA_FOCUS_LASTLIST;
    }
    utility_data=malloc((size_t)size);if(!utility_data)return -ENOMEM;
    if(saving)memcpy(utility_data,data,(size_t)size);
    p.dataBuf=utility_data;p.dataBufSize=(SceSize)size;p.dataSize=(SceSize)size;
    if(saving) {
        snprintf(p.sfoParam.title,sizeof p.sfoParam.title,"NFSU2 PSP");
        if(slotted(store) && owner[0])snprintf(p.sfoParam.savedataTitle,sizeof p.sfoParam.savedataTitle,"%s: %s",stores[store].title,owner);   /* the profile's name in the lists */
        else snprintf(p.sfoParam.savedataTitle,sizeof p.sfoParam.savedataTitle,"%s",stores[store].title);
        snprintf(p.sfoParam.detail,sizeof p.sfoParam.detail,"%s",detail?detail:"");
        p.sfoParam.parentalLevel=1;
        if(icon_size>0){p.icon0FileData.buf=icon;p.icon0FileData.bufSize=(SceSize)icon_size;p.icon0FileData.size=(SceSize)icon_size;}
    }
    if(interactive && !savedata_interactive_hook(1)){release_request();return -EBUSY;}
    if(savedata_dialog_hook && !savedata_dialog_hook(1)) {
        release_request();if(interactive)savedata_interactive_hook(0);return -EBUSY;
    }
    int init=sceUtilitySavedataInitStart(&p);init_result=init;
    if(init>=0 && savedata_trace_hook)savedata_trace_hook(store,mode,init,1);   /* open: the request's result to come */
    if(init<0){release_request();if(savedata_dialog_hook)savedata_dialog_hook(0);if(interactive)savedata_interactive_hook(0);return init;}
    utility_active=1;
    if(async_start){async_active=1;return 0;}   /* (savedata_write_async: the caller polls, the game goes on) */
    for(unsigned frames=0;interactive || frames<NFSU2_SAVEDATA_WAIT_FRAMES;frames++) {
        if(interactive)savedata_frame_hook(1);
        if(poll_done()) {
            int result=p.base.result;
            if(result==0 && listing)list_chosen=slot_of_name(store,p.saveName);   /* (the system writes the chosen save's name back) */
            if(result==0 && !saving && !deleting) {
                if(p.dataSize==0 || p.dataSize>(SceSize)size)result=-EIO;
                else {memcpy(data,utility_data,p.dataSize);if(got)*got=(int)p.dataSize;}
            }
            release_request();if(savedata_dialog_hook)savedata_dialog_hook(0);
            if(interactive)savedata_interactive_hook(0);
            return result;
        }
        if(interactive)savedata_frame_hook(0);
        else sceDisplayWaitVblankStart();
    }
    if(savedata_dialog_hook)savedata_dialog_hook(0);   /* (the block comes back once the utility lets it go) */
    return -ETIMEDOUT;
}

static int backend_write(enum Store store,const char *legacy,const void *data,int size,const char *detail,int interactive) {
    (void)legacy;
    last_error=run(store,interactive?PSP_UTILITY_SAVEDATA_SAVE:PSP_UTILITY_SAVEDATA_AUTOSAVE,(void *)data,size,detail,NULL);
    return last_error==0;
}
/* An automatic save in the background (savedata_write_async): the utility
 * started and left to run, its status read once a frame by the caller
 * (savedata_async_poll), the game going on meanwhile. The data are the
 * utility's own copy, taken at the start. */
static int backend_write_async(enum Store store,const char *legacy,const void *data,int size,const char *detail) {
    (void)legacy;
    if(async_active || utility_active){last_error=-EBUSY;return 0;}
    async_start=1;async_t0=sceKernelGetSystemTimeWide();
    last_error=run_request(store,PSP_UTILITY_SAVEDATA_AUTOSAVE,(void *)data,size,detail,NULL);
    async_start=0;
    if(!async_active)return 0;   /* refused at its start: last_error says why */
    async_store=store;return 1;
}
static int backend_async_poll(void) {
    if(!async_active)return -1;
    if(!poll_done())return 0;
    async_end(p.base.result);return 1;
}
static int legacy_read;
static int backend_from_legacy(void) { return legacy_read; }
static int backend_read(enum Store store,const char *legacy,void *data,int max,int interactive) {
    int got=0;legacy_read=0;
    last_error=run(store,interactive?PSP_UTILITY_SAVEDATA_LOAD:PSP_UTILITY_SAVEDATA_AUTOLOAD,data,max,NULL,&got);
    if(last_error==0 && got>0)return got;
    /* Never import old bytes over a damaged/cancelled/busy native request. */
    if(utility_active || interactive || (uint32_t)last_error!=LOAD_NO_DATA)return -1;
    last_error=0;
    if(store!=STORE_CAREER && store!=STORE_PROFILE)return 0;
    if(slot>0 || legacy_off)return 0;   /* the old files are slot 0's, until it is deleted */
    got=file_read(legacy,data,max);legacy_read=got>0;
    return got;
}
/* What a list answered: 0 a choice made (or a save there), the cancel (1)
 * and "no data" as none, anything else an error. */
#define DELETE_NO_DATA 0x80110347u
static int list_outcome(int result) {
    if(result==0)return 1;
    if(result==1 || (uint32_t)result==LOAD_NO_DATA || (uint32_t)result==DELETE_NO_DATA)return 0;
    return -1;
}
/* Slot k's career there, silently (AUTOLOAD into a scratch block). */
static int backend_slot_present(const char *legacy,int k) {
    (void)legacy;
    void *scratch=malloc(SAVEDATA_CAREER_MAX_BYTES);if(!scratch){last_error=-ENOMEM;return -1;}
    int was=slot,got=0;slot=k;
    last_error=run(STORE_CAREER,PSP_UTILITY_SAVEDATA_AUTOLOAD,scratch,SAVEDATA_CAREER_MAX_BYTES,NULL,&got);
    slot=was;free(scratch);
    if(last_error==0)return 1;
    if((uint32_t)last_error==LOAD_NO_DATA){last_error=0;return 0;}
    return -1;
}
static int backend_list(int mode,const char *legacy,const void *data,int size,const char *detail,int *chosen) {
    (void)legacy;
    if(manual_silent_on()) {   /* no one to move the list's cursor: what PPSSPP_DIALOG_AUTO=yes picks, silently */
        int pick=-1;
        for(int k=0;k<SAVEDATA_SLOTS;k++){int here=backend_slot_present(NULL,k);if(here<0)return -1;if(mode==1?!here && pick<0:here)pick=k;}
        if(pick<0)return 0;
        if(chosen)*chosen=pick;
        int was=slot;slot=pick;
        if(mode==1)last_error=run(STORE_CAREER,PSP_UTILITY_SAVEDATA_AUTOSAVE,(void *)data,size,detail,NULL);
        else if(mode==2)last_error=run(STORE_CAREER,SCE_UTILITY_SAVEDATA_AUTODELETE,&pick,1,NULL,NULL);   /* (Load: its highest, the newest ones' place) */
        else last_error=0;
        slot=was;return last_error==0?1:-1;
    }
    int got=0,result;
    if(mode==0) {
        void *scratch=malloc(SAVEDATA_CAREER_MAX_BYTES);if(!scratch){last_error=-ENOMEM;return -1;}
        result=run(STORE_CAREER,PSP_UTILITY_SAVEDATA_LISTLOAD,scratch,SAVEDATA_CAREER_MAX_BYTES,NULL,&got);free(scratch);
    } else if(mode==1)result=run(STORE_CAREER,PSP_UTILITY_SAVEDATA_LISTSAVE,(void *)data,size,detail,NULL);
    else{unsigned char none=0;result=run(STORE_CAREER,PSP_UTILITY_SAVEDATA_LISTDELETE,&none,1,NULL,NULL);}
    last_error=result;
    int out=list_outcome(result);
    if(out==1 && mode!=2) {   /* the slot it chose */
        if(list_chosen<0){last_error=-EIO;return -1;}
        if(chosen)*chosen=list_chosen;
    }
    if(out==0)last_error=0;
    return out;
}
static int backend_store_delete(enum Store store,const char *legacy) {
    (void)legacy;unsigned char none=0;
    last_error=run(store,SCE_UTILITY_SAVEDATA_AUTODELETE,&none,1,NULL,NULL);
    if((uint32_t)last_error==DELETE_NO_DATA || (uint32_t)last_error==LOAD_NO_DATA)last_error=0;
    return last_error==0;
}
#endif

/* One-way boot switch for tests: CAREER and MYCARS stay in RAM (writes
 * acknowledged, nothing read); the Memory Stick is never touched. */
static int temporary_career;
void savedata_temporary_career(void) { temporary_career=1; }
int savedata_career_is_temporary(void) { return temporary_career; }
int savedata_write(const char *legacy,const void *data,int size,const char *detail) {
    if(temporary_career){last_error=0;return 1;}
    return backend_write(STORE_CAREER,legacy,data,size,detail,0);
}
/* One-way boot switch for runs nobody plays (the smokes, the boot-drive
 * flows): the manual saves go silent (AUTOSAVE), as before the native
 * dialog. Its dialog waits for a button no test presses: script 18's
 * Save held the game there until the emulator's deadline. */
void savedata_manual_silent(void) { manual_silent=1; }
int savedata_write_manual(const char *legacy,const void *data,int size,const char *detail) {
    if(temporary_career){last_error=0;return 1;}
    return backend_write(STORE_CAREER,legacy,data,size,detail,!manual_silent);
}
/* The background save (savedata.h): a test's RAM store at once, else the
 * utility left running (backend_write_async). */
static int async_ram;
int savedata_write_async(const char *legacy,const void *data,int size,const char *detail) {
    if(temporary_career){last_error=0;async_ram=1;return 1;}
    return backend_write_async(STORE_CAREER,legacy,data,size,detail);
}
int savedata_async_poll(void) {
    if(async_ram){async_ram=0;return 1;}
    return backend_async_poll();
}
int savedata_read(const char *legacy,void *data,int max) {
    if(temporary_career){last_error=0;return 0;}
    int got=backend_read(STORE_CAREER,legacy,data,max,0);
    return got>0?got:0; /* Keep the established CAREER return convention. */
}
int savedata_from_legacy(void) { return temporary_career?0:backend_from_legacy(); }
int savedata_mycars_write(const char *path,const void *data,int size,const char *detail) {
    if(temporary_career){last_error=0;return 1;}
    return backend_write(STORE_MYCARS,path,data,size,detail,0);
}
int savedata_mycars_read(const char *path,void *data,int max) {
    if(temporary_career){last_error=0;return 0;}
    return backend_read(STORE_MYCARS,path,data,max,0);
}
int savedata_profile_read(const char *legacy,void *data,int interactive) {
    if(temporary_career){last_error=0;return 0;}
    return backend_read(STORE_PROFILE,legacy,data,SAVEDATA_PROFILE_BYTES,interactive!=0);
}
int savedata_profile_write_auto(const char *legacy,const void *data) {
    if(temporary_career){last_error=0;return 1;}
    return backend_write(STORE_PROFILE,legacy,data,SAVEDATA_PROFILE_BYTES,"Player profile",0);
}
int savedata_profile_write(const char *legacy,const void *data) {
    if(temporary_career){last_error=0;return 1;}
    return backend_write(STORE_PROFILE,legacy,data,SAVEDATA_PROFILE_BYTES,"Player profile",!manual_silent);
}

/* The profile slots' lists (savedata.h). */
int savedata_list_pick(const char *legacy,int *chosen) {
    if(temporary_career){last_error=0;return 0;}
    return backend_list(0,legacy,NULL,0,NULL,chosen);
}
int savedata_list_save(const char *legacy,const void *data,int size,const char *detail,int *chosen) {
    if(temporary_career){last_error=0;if(chosen)*chosen=slot;return 1;}
    if(!data || size<=0 || size>SAVEDATA_CAREER_MAX_BYTES){last_error=-EINVAL;return -1;}
    return backend_list(1,legacy,data,size,detail,chosen);
}
int savedata_list_delete(const char *legacy) {
    if(temporary_career){last_error=0;return 0;}
    return backend_list(2,legacy,NULL,0,NULL,NULL);
}
int savedata_slot_present(const char *legacy,int k) {
    if(temporary_career){last_error=0;return 0;}
    if(k<0 || k>=SAVEDATA_SLOTS){last_error=-EINVAL;return -1;}
    return backend_slot_present(legacy,k);
}
int savedata_profile_delete(const char *legacy) {
    if(temporary_career){last_error=0;return 1;}
    return backend_store_delete(STORE_PROFILE,legacy);
}
int savedata_mycars_delete(const char *path) {
    if(temporary_career){last_error=0;return 1;}
    return backend_store_delete(STORE_MYCARS,path);
}
