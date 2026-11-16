/* The player cars: the US release's 29 stock cars (tools/convert_cars.py
 * -> cars.n2cl) and Rachel's tuned 350Z, the car the career starts in. */
#ifndef NFSU2_CARS_H
#define NFSU2_CARS_H
#include <stdint.h>

enum { FLEET_RACHEL=-1, FLEET_MAX=40 };
enum { FLEET_OPEN=1, FLEET_BY_RACE=2 };   /* flags: open from the start; opened by winning the stage's need-th row */
typedef struct {
    char model[16],label[28],shortname[12];   /* "350Z", "Nissan 350Z", "350Z" */
    uint32_t logo;                            /* bin hash of its SECONDARY_LOGO_ texture */
    uint8_t stage,need,flags;                 /* unlock: events finished in that career stage */
    uint8_t stock_paint;                      /* its stock paint's place in the paint grid + 1 (0: an older table's none) */
    float accel,top,handling;                 /* the car lot's bars, 0..1 */
    float top_ms;                             /* top speed for the physics, m/s */
} FleetCar;

int fleet_load(const char *path);
int fleet_stock_paint(int i);   /* the car's stock paint (garage_paint's index): the game's, its record's BASE_PAINT */
int fleet_count(void);
const FleetCar *fleet_car(int i);       /* NULL for FLEET_RACHEL or out of range */
const char *fleet_label(int i);         /* Rachel's: "Nissan 350Z" */
const char *fleet_short(int i);
float fleet_top_ms(int i);
/* Pack file (detail: the garage's) and physics profile names. */
void fleet_pack(int i,int detail,char *out,int n);
void fleet_phys(int i,char *out,int n);

#endif
