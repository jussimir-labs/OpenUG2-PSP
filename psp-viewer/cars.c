/* Player cars (see cars.h). */
#include "cars.h"
#include <stdio.h>
#include <string.h>

static FleetCar fleet[FLEET_MAX];static int nfleet;

int fleet_load(const char *path) {
    nfleet=0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2CL",4) || fread(&version,4,1,f)!=1 || version!=1 || fread(&n,4,1,f)!=1 || n>FLEET_MAX){fclose(f);return 0;}
    for(uint32_t k=0;k<n;k++)if(fread(&fleet[k],sizeof(FleetCar),1,f)!=1){fclose(f);return 0;}
    fclose(f);
    for(uint32_t k=0;k<n;k++){fleet[k].model[15]=fleet[k].label[27]=fleet[k].shortname[11]=0;}
    nfleet=(int)n;return 1;
}
int fleet_count(void) { return nfleet; }
const FleetCar *fleet_car(int i) { return i>=0 && i<nfleet?&fleet[i]:NULL; }
const char *fleet_label(int i) { return i>=0 && i<nfleet?fleet[i].label:"Nissan 350Z"; }
const char *fleet_short(int i) { return i>=0 && i<nfleet?fleet[i].shortname:"350Z"; }
/* RideInfo::SetStockParts: the BASE_PAINT slot from the car record
 * (tools/convert_cars.py stock_paints); a table without it, the 206's
 * blue for the 206 (GLOSS_L2_COLOR03, the paint grid's 12) and the grid's
 * first for the others, as before. */
int fleet_stock_paint(int i) {
    if(i<0 || i>=nfleet)return 0;
    if(fleet[i].stock_paint)return fleet[i].stock_paint-1;
    return !strcmp(fleet[i].model,"PEUGOT")?12:0;
}
float fleet_top_ms(int i) { return i>=0 && i<nfleet?fleet[i].top_ms:61.f; }
void fleet_pack(int i,int detail,char *out,int n) {
    if(i>=0 && i<nfleet)snprintf(out,n,"car-%s%s.n2pv",fleet[i].model,detail?"-detail":"");
    else snprintf(out,n,"%s",detail?"car-detail.n2pv":"car-light.n2pv");   /* Rachel's 350Z (tools/convert_car.py) */
}
void fleet_phys(int i,char *out,int n) {
    if(i>=0 && i<nfleet)snprintf(out,n,"car-%s",fleet[i].model);else snprintf(out,n,"car");
}
