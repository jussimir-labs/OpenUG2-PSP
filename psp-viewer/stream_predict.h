/* Bounded route prediction for streaming, in GU metres. No allocations. */
#ifndef NFSU2_STREAM_PREDICT_H
#define NFSU2_STREAM_PREDICT_H

typedef struct { float x,y,z; } StreamPoint;
/* A forecast is a hint for loading, never a change to driving or collision.
 * Returns 0 when the current segment cannot be trusted (output unchanged).
 * The path is already in driving order; idx is the tracked segment. Stop at
 * a non-circuit end. At most 64 segments, and at most 100 m of lookahead. */
int stream_predict_path(const float *xyz,int n,int idx,int circuit,
                        StreamPoint pos,float vx,float vz,float ahead,
                        StreamPoint *out);
/* Free roam: project onto a nearby road on the same level, follow bends,
 * and stop at the first fork. A fork has no known player intention. */
int stream_predict_road(StreamPoint pos,float vx,float vz,float ahead,
                        StreamPoint *out);

#endif
