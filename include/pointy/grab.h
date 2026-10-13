#ifndef POINTY_GRAB_H
#define POINTY_GRAB_H
#include "pointy/render.h"
#define PP_GRAB_DEPTH 7
#define PP_GRAB_AXIS (1 << PP_GRAB_DEPTH)
#define PP_GRAB_OUTPUT_AXIS 64
#define PP_GRAB_RECT_LIMIT 1024
#define PP_GRAB_ANGLE_STEP .05
#define PP_GRAB_CACHE_LIMIT ((size_t)8 * 1024 * 1024)
typedef struct PPGrabArea PPGrabArea;
typedef struct PPGrabRect { int left, top, right, bottom; } PPGrabRect;
typedef struct PPGrabRegion {
    PPGrabRect rects[PP_GRAB_RECT_LIMIT];
    size_t count;
    unsigned axis; /* 64 normally; conservative 32-cell coarsening if needed. */
} PPGrabRegion;
/* Fixed-depth source quadtree, shared by immutable owners. Mixed leaves
 * conservatively contain any nonzero source alpha. Union uses composed frames. */
bool pp_grab_create(const PPFrame *frames,size_t count,PPGrabArea **out,PPError *error);
PPGrabArea *pp_grab_retain(PPGrabArea *area);
void pp_grab_release(PPGrabArea *area);
size_t pp_grab_bytes(const PPGrabArea *area);
size_t pp_grab_nodes(const PPGrabArea *area);
bool pp_grab_empty(const PPGrabArea *area);
/* Projection includes linear filtering and rounding margins, and returns
 * sorted nonoverlapping native rectangles with a cap after rotation. */
bool pp_grab_project(const PPGrabArea *area,PPVec2 layout,PPRect source_rect,
    double longest,double angle,double desktop_density,int width,int height,
    PPGrabRegion *out,PPError *error);
#endif
