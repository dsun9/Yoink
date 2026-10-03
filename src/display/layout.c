#include "pointy/display.h"
#include <math.h>
#include <float.h>
static double clamp(double v,double lo,double hi) {return fmax(lo,fmin(hi,v));}
bool pp_displays_equal(const PPDisplay *a,size_t na,const PPDisplay *b,size_t nb) {
    if(na!=nb||(!a&&na)||(!b&&nb)) return false;
    for(size_t i=0;i<na;i++) {
        if(a[i].id!=b[i].id||a[i].bounds.x!=b[i].bounds.x||a[i].bounds.y!=b[i].bounds.y||a[i].bounds.w!=b[i].bounds.w||a[i].bounds.h!=b[i].bounds.h||a[i].desktop_units_per_dip!=b[i].desktop_units_per_dip||a[i].drawable_pixels_per_dip!=b[i].drawable_pixels_per_dip) return false;
    }return true;
}
PPVec2 pp_display_to_local(const PPDisplay *d,PPVec2 p) {return (PPVec2){(p.x-d->bounds.x)/d->desktop_units_per_dip,(p.y-d->bounds.y)/d->desktop_units_per_dip};}
PPVec2 pp_display_to_desktop(const PPDisplay *d,PPVec2 p) {return (PPVec2){d->bounds.x+p.x*d->desktop_units_per_dip,d->bounds.y+p.y*d->desktop_units_per_dip};}
const PPDisplay *pp_display_at(const PPDisplay *d,size_t n,PPVec2 p) {
    const PPDisplay *nearest=NULL;double best=DBL_MAX;
    for(size_t i=0;i<n;i++) {
        PPRect b=d[i].bounds;
        if(p.x>=b.x&&p.x<b.x+b.w&&p.y>=b.y&&p.y<b.y+b.h) return d+i;
        double dx=p.x-clamp(p.x,b.x,b.x+b.w),dy=p.y-clamp(p.y,b.y,b.y+b.h),distance=dx*dx+dy*dy;
        if(distance<best) {best=distance;nearest=d+i;}
    }return nearest;
}
PPVec2 pp_display_drag_center(const PPDisplay *d,PPVec2 pointer,PPVec2 grab) {
    return (PPVec2){pointer.x-grab.x*d->desktop_units_per_dip,pointer.y-grab.y*d->desktop_units_per_dip};
}
PPVec2 pp_recover_center(const PPDisplay *d,size_t n,PPVec2 p,double longest) {
    for(size_t i=0;i<n;i++) {PPRect b=d[i].bounds;if(p.x>=b.x&&p.x<b.x+b.w&&p.y>=b.y&&p.y<b.y+b.h) return p;}
    const PPDisplay *chosen=pp_display_at(d,n,p);if(!chosen) return p;
    PPRect b=chosen->bounds;double margin=fmax(0,longest)*chosen->desktop_units_per_dip*.5;
    double mx=fmin(b.w*.5,margin),my=fmin(b.h*.5,margin);
    return (PPVec2){clamp(p.x,b.x+mx,b.x+b.w-mx),clamp(p.y,b.y+my,b.y+b.h-my)};
}
