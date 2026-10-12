#include "pointy/interaction.h"
#include "geometry_internal.h"
#include <math.h>
static int clamp(int x,int limit) { return x<0?0:(x>=limit?limit-1:x); }
bool pp_geometry_source_extent(PPVec2 layout,PPRect rect,double longest,double angle_deg,
        double density,int *width,int *height) {
    if(!isfinite(layout.x)||!isfinite(layout.y)||layout.x<=0||layout.y<=0||
        !isfinite(rect.x)||!isfinite(rect.y)||!isfinite(rect.w)||!isfinite(rect.h)||rect.w<=0||rect.h<=0||
        !isfinite(longest)||longest<=0||!isfinite(angle_deg)||!isfinite(density)||density<=0) return false;
    double left=fmin(0,rect.x)-layout.x*.5,top=fmin(0,rect.y)-layout.y*.5;
    double right=fmax(layout.x,rect.x+rect.w)-layout.x*.5,bottom=fmax(layout.y,rect.y+rect.h)-layout.y*.5;
    double angle=fmod(angle_deg,360)*3.14159265358979323846/180,c=cos(angle),s=sin(angle);
    double mx=0,my=0,scale=longest*density/fmax(layout.x,layout.y);
    for(int i=0;i<4;i++) {
        double x=i&1?right:left,y=i&2?bottom:top;
        mx=fmax(mx,fabs(c*x-s*y));my=fmax(my,fabs(s*x+c*y));
    }
    double w=ceil(2*mx*scale-1e-9),h=ceil(2*my*scale-1e-9);
    if(!isfinite(w)||!isfinite(h)||w<1||h<1||w>16384||h>16384) return false;
    *width=(int)w;*height=(int)h;return true;
}
bool pp_geometry_extent(const PPFrame *source,double longest,double angle_deg,double pixels_per_dip,int *width,int *height) {
    if(!source || source->width<=0 || source->height<=0 || source->width>16384 || source->height>16384 ||
        !isfinite(longest) || longest<=0 || !isfinite(angle_deg) || !isfinite(pixels_per_dip) || pixels_per_dip<=0) return false;
    double scale=longest*pixels_per_dip/(source->width>source->height?source->width:source->height);
    double angle=fmod(angle_deg,360)*3.14159265358979323846/180;
    double w=ceil(scale*(fabs(cos(angle))*source->width+fabs(sin(angle))*source->height)-1e-9);
    double h=ceil(scale*(fabs(sin(angle))*source->width+fabs(cos(angle))*source->height)-1e-9);
    if(!isfinite(w)||!isfinite(h)||w<1||h<1||w>16384||h>16384) return false;
    *width=(int)w;*height=(int)h;return true;
}
bool pp_content_hit_on_grid(const PPFrame *source,const PPPose *pose,double units,const PPSamplingGrid *grid,PPVec2 point) {
    if(!source || !source->pixels || source->stride<(size_t)source->width*4 || !pose || !grid ||
        !isfinite(units) || units<=0 || !isfinite(grid->drawable_pixels_per_desktop_unit) || grid->drawable_pixels_per_desktop_unit<=0 ||
        !isfinite(grid->desktop_origin.x) || !isfinite(grid->desktop_origin.y) ||
        !isfinite(point.x) || !isfinite(point.y) || !isfinite(pose->opacity) || pose->opacity<=0 || pose->opacity>1) return false;
    double pixels_per_dip=units*grid->drawable_pixels_per_desktop_unit;
    int w,h;if(!pp_geometry_extent(source,pose->longest_dip,pose->angle_deg,pixels_per_dip,&w,&h)) return false;
    double x_pixel=floor((point.x-grid->desktop_origin.x)*grid->drawable_pixels_per_desktop_unit);
    double y_pixel=floor((point.y-grid->desktop_origin.y)*grid->drawable_pixels_per_desktop_unit);
    if(!isfinite(x_pixel)||!isfinite(y_pixel)||x_pixel<0||y_pixel<0||x_pixel>=w||y_pixel>=h) return false;
    double scale=pose->longest_dip*pixels_per_dip/(source->width>source->height?source->width:source->height);
    double angle=fmod(pose->angle_deg,360)*3.14159265358979323846/180;
    double dx=x_pixel+.5-w*.5,dy=y_pixel+.5-h*.5;
    double sx=(cos(angle)*dx+sin(angle)*dy)/scale+source->width*.5;
    double sy=(-sin(angle)*dx+cos(angle)*dy)/scale+source->height*.5;
    if(sx<0||sy<0||sx>source->width||sy>source->height) return false;
    /* GL_LINEAR with CLAMP_TO_EDGE; source texel centers are at half
     * coordinates. Sample RGBA alpha, which is the native presented coverage. */
    sx-=.5;sy-=.5;int x=(int)floor(sx),y=(int)floor(sy);
    double fx=sx-x,fy=sy-y;
    int x0=clamp(x,source->width),x1=clamp(x+1,source->width);
    int y0=clamp(y,source->height),y1=clamp(y+1,source->height);
    double top=source->pixels[(size_t)y0*source->stride+x0*4+3]*(1-fx)+source->pixels[(size_t)y0*source->stride+x1*4+3]*fx;
    double bottom=source->pixels[(size_t)y1*source->stride+x0*4+3]*(1-fx)+source->pixels[(size_t)y1*source->stride+x1*4+3]*fx;
    return floor((top*(1-fy)+bottom*fy)*pose->opacity+.5)>0;
}
bool pp_content_hit(const PPFrame *source,const PPPose *pose,double units,PPVec2 point) {
    if(!pose || !isfinite(pose->center.x) || !isfinite(pose->center.y)) return false;
    int w,h;if(!pp_geometry_extent(source,pose->longest_dip,pose->angle_deg,units,&w,&h)) return false;
    PPSamplingGrid grid={{round(pose->center.x-w*.5),round(pose->center.y-h*.5)},1};
    return pp_content_hit_on_grid(source,pose,units,&grid,point);
}
