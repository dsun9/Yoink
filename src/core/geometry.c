#include "pointy/interaction.h"
#include <math.h>
static int clamp(int x,int limit) { return x<0?0:(x>=limit?limit-1:x); }
bool pp_content_hit(const PPFrame *source,const PPPose *pose,double units,PPVec2 point) {
    if(!source || !source->content_alpha || source->width<=0 || source->height<=0 || !pose ||
        !isfinite(units) || units<=0 || !isfinite(pose->longest_dip) || pose->longest_dip<=0 ||
        !isfinite(pose->angle_deg) || !isfinite(pose->center.x) || !isfinite(pose->center.y) ||
        !isfinite(point.x) || !isfinite(point.y) || !isfinite(pose->opacity) || pose->opacity<=0 || pose->opacity>1) return false;
    double scale=pose->longest_dip*units/(source->width>source->height?source->width:source->height);
    double angle=fmod(pose->angle_deg,360)*3.14159265358979323846/180;
    double dx=point.x-pose->center.x,dy=point.y-pose->center.y;
    double sx=(cos(angle)*dx+sin(angle)*dy)/scale+source->width*.5;
    double sy=(-sin(angle)*dx+cos(angle)*dy)/scale+source->height*.5;
    if(!isfinite(sx)||!isfinite(sy)||sx<0||sy<0||sx>source->width||sy>source->height) return false;
    /* GL_LINEAR with CLAMP_TO_EDGE; texel centers are at half coordinates.
     * Match RGBA8 presentation quantization before testing visible coverage. */
    sx-=.5;sy-=.5;int x=(int)floor(sx),y=(int)floor(sy);
    double fx=sx-x,fy=sy-y;
    int x0=clamp(x,source->width),x1=clamp(x+1,source->width);
    int y0=clamp(y,source->height),y1=clamp(y+1,source->height);
    double top=source->content_alpha[(size_t)y0*source->width+x0]*(1-fx)+source->content_alpha[(size_t)y0*source->width+x1]*fx;
    double bottom=source->content_alpha[(size_t)y1*source->width+x0]*(1-fx)+source->content_alpha[(size_t)y1*source->width+x1]*fx;
    return floor((top*(1-fy)+bottom*fy)*pose->opacity+.5)>0;
}
