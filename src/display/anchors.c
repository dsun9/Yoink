#include "pointy/display.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const int positions[8][2]={{0,0},{1,0},{2,0},{0,1},{2,1},{0,2},{1,2},{2,2}};
PPRect pp_anchor_bounds(const PPDisplay *d,PPAnchor a,double size) {
    if(!d||a<0||a>=PP_ANCHOR_COUNT||!isfinite(size)||size<=0) return (PPRect){0};
    double w=fmin(d->bounds.w,size*d->desktop_units_per_dip),h=fmin(d->bounds.h,size*d->desktop_units_per_dip);
    return (PPRect){d->bounds.x+(d->bounds.w-w)*positions[a][0]*.5,d->bounds.y+(d->bounds.h-h)*positions[a][1]*.5,w,h};
}
PPRect pp_anchor_preview_bounds(const PPDisplay *d,PPAnchor a,double size) {return pp_anchor_bounds(d,a,size);}
static bool contains(PPRect b,PPVec2 p) {return p.x>=b.x&&p.y>=b.y&&p.x<b.x+b.w&&p.y<b.y+b.h;}
void pp_anchors_enable(PPAnchorRuntime *r,PPAnchor a,bool enabled) {
    if(!r||a<0||a>=PP_ANCHOR_COUNT) return;
    if(!r->configured) {for(int i=0;i<PP_ANCHOR_COUNT;i++) r->enabled[i]=true;r->configured=true;}
    r->enabled[a]=enabled;
}
void pp_anchors_update(PPAnchorRuntime *r,const PPDisplay *d,size_t count,PPVec2 pointer,double now) {
    if(!r||(!d&&count)||!isfinite(now)) return;
    if(!r->configured) pp_anchors_enable(r,PP_ANCHOR_TOP_LEFT,true);
    if(count>SIZE_MAX/PP_ANCHOR_COUNT/sizeof(PPAnchorReveal)) {r->failed=true;return;}
    size_t n=count*PP_ANCHOR_COUNT;bool changed=n!=r->count;
    for(size_t i=0;!changed&&i<count;i++) if(r->entries[i*8].display_id!=d[i].id) changed=true;
    if(changed) {
        PPAnchorReveal *entries=n?calloc(n,sizeof(*entries)):NULL;if(n&&!entries) {r->failed=true;return;}
        for(size_t i=0;i<count;i++) for(int a=0;a<8;a++) {
            PPAnchorReveal *e=entries+i*8+a;e->display_id=d[i].id;e->anchor=(PPAnchor)a;
            for(size_t j=0;j<r->count;j++) if(r->entries[j].display_id==d[i].id&&r->entries[j].anchor==a) {*e=r->entries[j];break;}
        }free(r->entries);r->entries=entries;r->count=n;
    }
    r->failed=false;
    for(size_t i=0;i<n;i++) {
        PPAnchorReveal *e=r->entries+i;e->target=pp_anchor_bounds(d+i/8,e->anchor,16);e->preview=pp_anchor_preview_bounds(d+i/8,e->anchor,96);
        if(!r->enabled[e->anchor]) {e->revealed=e->leaving=e->held=false;e->opacity=0;continue;}
        if(e->held||contains(e->target,pointer)||(e->revealed&&contains(e->preview,pointer))) {e->revealed=true;e->leaving=false;e->opacity=.35;}
        else if(e->revealed) {
            if(!e->leaving) {e->leaving=true;e->leave_time=now;}
            e->opacity=.35*fmax(0,1-(now-e->leave_time)/.120);
            if(e->opacity<=0) {e->revealed=false;e->leaving=false;}
        }
    }
}
void pp_anchors_destroy(PPAnchorRuntime *r) {if(r) {free(r->entries);memset(r,0,sizeof(*r));}}
