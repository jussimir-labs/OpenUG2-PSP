/* Audio-only adapters for AI tyre emitters; never mutates a Car. */
#ifndef NFSU2_RIVAL_TYRES_H
#define NFSU2_RIVAL_TYRES_H
#include "vehicle.h"
enum { RIVAL_TYRE_SLOTS=3 };
typedef struct { const Car *car;int big,rail; } RivalTyreSource;
void rival_tyres_reset(void);
void rival_tyres_update(float dt,const Car *listener,const RivalTyreSource source[RIVAL_TYRE_SLOTS],int drift,int drag,float rain,float volume,int active);
#endif
