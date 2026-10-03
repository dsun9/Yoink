#ifndef POINTY_GEOMETRY_INTERNAL_H
#define POINTY_GEOMETRY_INTERNAL_H
#include "pointy/render.h"
/* Common ceil/limit convention for drawable rendering and inverse sampling. */
bool pp_geometry_extent(const PPFrame *source,double longest_dip,double angle_deg,
    double pixels_per_dip,int *width,int *height);
#endif
