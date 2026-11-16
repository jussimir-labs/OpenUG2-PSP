/* Named saves on the Memory Stick through the system's savedata
 * utility (ms0:/PSP/SAVEDATA/NFSU2VIEWCAREER: DATA.BIN, PARAM.SFO with the
 * title and a line of detail, ICON0.PNG: the EBOOT's icon0.png), as the
 * XMB lists and copies it.
 * Auto modes: no dialog; calls wait for completion up to a bounded timeout.
 * Main-thread calls must be serialized across all stores. A timed-out request
 * is polled by the next call and keeps a private copy of its payload.
 * Saves from before it (career.sav beside the EBOOT) are read once and
 * written back as savedata. The host simulation keeps the file. */
#ifndef NFSU2_SAVEDATA_H
#define NFSU2_SAVEDATA_H

/* Resource bounds, independent of versioned serializer layouts. */
enum { SAVEDATA_CAREER_MAX_BYTES=64*1024, SAVEDATA_MYCARS_MAX_BYTES=16*1024,
       SAVEDATA_PROFILE_BYTES=20 };

/* Boot-only opt-in: fresh transient CAREER, no native or legacy save I/O.
 * Writes acknowledge in-memory progress. One-way; suppresses CAREER, MYCARS
 * and PROFILE, including legacy reads and interactive dialogs. */
void savedata_temporary_career(void);
int savedata_career_is_temporary(void);

/* Write data (size bytes); detail: the line the XMB shows under the title.
 * legacy: the old file, the store on the host. 1 on success. */
int savedata_write(const char *legacy,const void *data,int size,const char *detail);
/* Explicit Save Game: same bytes/store, system SAVE confirmation UI. No human
 * response deadline. Cancellation/error returns 0 and never means saved. */
int savedata_write_manual(const char *legacy,const void *data,int size,const char *detail);
void savedata_manual_silent(void);   /* tests and boot-drive flows: the manual saves without the dialog (no one to press its buttons) */
/* Read up to max bytes: the savedata, else the legacy file; the bytes read,
 * 0 on failure or when there is no save. PSP legacy import occurs only when
 * native CAREER is absent, never after corruption/cancel/busy/timeout. */
int savedata_read(const char *legacy,void *data,int max);
/* 1 when the last read came from the legacy file (to be written back as
 * savedata); always 0 on the host, where the file is the store. */
int savedata_from_legacy(void);
/* Last write / PSP utility result; timeout and busy are explicit negative
 * errno values. A timed-out utility retains its private buffer until shutdown. */
int savedata_last_error(void);
/* The automatic saves (AUTOSAVE) so far: how many, the ms the caller was held in all and at most (viewer-stats street_stall). */
void savedata_block_stats(unsigned *n,unsigned *ms,unsigned *max);
/* The career's automatic save in the background: the utility started and
 * left running, the game going on (1 started, 0 refused: savedata_last_error);
 * savedata_async_poll once a frame: -1 none, 0 running, 1 done (its result in
 * savedata_last_error). A save that cannot wait (a dialog, a load) lets it
 * finish first. */
int savedata_write_async(const char *legacy,const void *data,int size,const char *detail);
int savedata_async_poll(void);
void savedata_async_stats(unsigned *n,unsigned *max_ms);   /* background saves so far, the longest (ms) */
/* main.c: called with 1 before the utility opens (0 refuses the request:
 * -EBUSY) and with 0 after it, whatever came of it; the volatile block
 * (volmem.h) is the utility's in between. */
extern int (*savedata_dialog_hook)(int begin);
/* Main-thread interactive ownership and rendering. Required for SAVE/LOAD;
 * begin must fence GE and hold the loader until end. frame(1) draws the
 * background before utility Update; frame(0) presents afterwards. AUTO modes
 * keep their existing silent path. The utility owns its private payload until
 * actual completion; an interactive request never returns while still live. */
extern int (*savedata_interactive_hook)(int begin);
extern void (*savedata_frame_hook)(int before_update);
int savedata_idle(void);   /* no request and the utility at none: the block can be taken back */
/* Each utility request once it is over (diagnostics): the store (0 CAREER,
 * 1 MYCARS, 2 PROFILE), its mode (PSP_UTILITY_SAVEDATA_*), what
 * sceUtilitySavedataInitStart returned (1 when the request was refused
 * before it) and the request's result (0 success); also once the utility
 * has started, with result 1 (still open). Optional. */
extern void (*savedata_trace_hook)(int store,int mode,int init,int result);

/* Separate NFSU2VIEWMYCARS/DATA.BIN; never imports a legacy PSP file.
 * HOST_SIM uses the supplied separate path, with atomic replace on write.
 * Read: positive byte count; 0 ONLY when absent; -1 on failure (including
 * empty/oversized data, cancellation, busy and timeout). Failed reads leave
 * the caller's buffer unchanged. Write: 1 success, 0 failure. A timeout is
 * an unknown persistent outcome: bytes may complete later; it is not rollback.
 * Version/checksum/content validation belongs to the caller's serializer. */
int savedata_mycars_write(const char *path,const void *data,int size,const char *detail);
int savedata_mycars_read(const char *path,void *data,int max);

/* Separate NFSU2VIEWPROFILE, exact legacy N2PF+16B payload. Native first;
 * silent boot read may fall back read-only to legacy ONLY when absent, never
 * after cancellation/corruption/busy. No implicit migration write/deletion.
 * interactive LOAD is available to an explicit future profile-load action;
 * it does not fall back to a different file. Return positive/0 absent/-1 error.
 * PROFILE save is explicitly interactive. Existing CAREER/MYCARS bytes stay. */
int savedata_profile_read(const char *legacy,void *data,int interactive);
int savedata_profile_write(const char *legacy,const void *data);
/* The same bytes silently (AUTOSAVE): the name beside a career the player
 * just saved on the system's list. */
int savedata_profile_write_auto(const char *legacy,const void *data);

/* Profile slots (beta #136, the original's several profiles on the card):
 * slot 0 is the CAREER and PROFILE every build so far wrote (and the only
 * one with the legacy files); slot k (1..7) NFSU2VIEWCAREERk and
 * NFSU2VIEWPROFILEk and NFSU2VIEWMYCARSk, the same payloads: each profile
 * its own career, name and My Cars, as the original's save (UserProfile:
 * FEPlayerCarDB and PlayerCareerState). Slot 0's MYCARS is the one every
 * build so far shared. Every request goes to the slot set here. */
enum { SAVEDATA_SLOTS=8 };
void savedata_set_slot(int slot);
int savedata_slot(void);
/* The profile's name in the saves' SFO titles ("Career: NAME"), as the lists show them. */
void savedata_set_owner(const char *name);
/* Slot 0 deleted: its legacy files are not read again this session. */
void savedata_legacy_off(void);
/* The system's lists over the careers, interactive as an explicit Save
 * (without a dialog after savedata_manual_silent: what PPSSPP_DIALOG_AUTO=yes
 * picks). Each returns 1 done (*chosen: the slot), 0 cancelled or nothing
 * listed, -1 an error (savedata_last_error).
 * pick: LISTLOAD, the cursor on the newest; only chooses (read the slot after).
 * save: LISTSAVE of a career's bytes, the cursor on the first empty slot
 *       ("New Data"); an existing one is overwritten after the system asks.
 * delete: LISTDELETE, the cursor on the last; which one went is not told
 *       (savedata_slot_present). legacy: the host's file, as the others. */
int savedata_list_pick(const char *legacy,int *chosen);
int savedata_list_save(const char *legacy,const void *data,int size,const char *detail,int *chosen);
int savedata_list_delete(const char *legacy);
/* Slot k's career there (a silent AUTOLOAD): 1, 0 absent, -1 error. */
int savedata_slot_present(const char *legacy,int k);
/* The slot's PROFILE removed silently (AUTODELETE): 1 gone or absent. */
int savedata_profile_delete(const char *legacy);
int savedata_mycars_delete(const char *path);   /* the slot's MYCARS, the same way */

#endif
