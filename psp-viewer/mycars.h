/* GameCube FEPlayerCarDB: twenty tuned cars, separate from career slots. */
#ifndef NFSU2_MYCARS_H
#define NFSU2_MYCARS_H
#include "garage.h"
#include "perf.h"
enum { MYCARS_MAX=20 };
/* Load once at the selector boundary. -1 invalid/unavailable store, 0 absent, 1 loaded. */
int mycars_load(void);
void mycars_forget(void);   /* the profile changed (its slot's MYCARS): loaded again at the next mycars_load */
int mycars_count(void);
int mycars_car(int slot);
const CarLook *mycars_look(int slot);
const CarPerf *mycars_perf(int slot);
/* slot -1 creates a tuned record; existing slot retains identity. Save failure
 * leaves both the record and caller's slot unchanged. Never touches career. */
int mycars_install(int *slot,int car,const CarLook *look);
/* Atomic record update, retaining the supplied fitted look and every other slot. */
int mycars_install_perf(int *slot,int car,const CarLook *look,const CarPerf *perf);
#endif
