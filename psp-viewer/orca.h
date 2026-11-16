/* Optimal Reciprocal Collision Avoidance (ORCA), 2D, ported to C from the
 * RVO2 Library (https://github.com/snape/RVO2, Agent.cc). Copyright 2008
 * University of North Carolina at Chapel Hill; Apache License 2.0, see
 * LICENSE-RVO2. Changes: plain C, agents only (no static obstacles - the
 * race driver keeps to the road itself), neighbours passed in, and a
 * per-neighbour share of the avoidance (RVO2 always splits it 0.5/0.5;
 * a car that will not react, such as traffic or the player, gets 1.0).
 *
 * van den Berg, Guy, Lin, Manocha, "Reciprocal n-body collision
 * avoidance", ISRR 2011. */
#ifndef NFSU2_ORCA_H
#define NFSU2_ORCA_H

typedef struct { float px,py,vx,vy,radius,share; } OrcaNeighbour;

/* The velocity closest to (pref_x, pref_y), no faster than max_speed, that
 * keeps clear of every neighbour for time_horizon seconds. (px, py) and
 * (vx, vy): this agent's position and current velocity; dt: the step. */
void orca_velocity(float px,float py,float vx,float vy,float radius,float max_speed,
                   float pref_x,float pref_y,float time_horizon,float dt,
                   const OrcaNeighbour *nb,int n,float *out_x,float *out_y);

#endif
