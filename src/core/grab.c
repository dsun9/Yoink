#include "pointy/grab.h"
#include "pointy/assets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GrabNode { uint16_t child; uint8_t state, reserved; } GrabNode;
struct PPGrabArea { size_t refs,count; int width,height; PPAssets *owner; GrabNode nodes[]; };
typedef struct Grid { uint64_t rows[PP_GRAB_AXIS][2]; } Grid;
static bool fail(PPError *e,int code,const char *message) {
    if(e){e->code=code;snprintf(e->message,sizeof(e->message),"%s",message);}return false;
}
static bool bit(const Grid *g,int x,int y){return (g->rows[y][x/64]>>(x%64))&1;}
static void fill(Grid *g,int y,int first,int last) {
    if(first<0)first=0;if(last>PP_GRAB_AXIS)last=PP_GRAB_AXIS;
    for(int x=first;x<last;) {
        int end=(x/64+1)*64;if(end>last)end=last;
        int n=end-x;uint64_t mask=n==64?UINT64_MAX:((UINT64_C(1)<<n)-1)<<(x%64);
        g->rows[y][x/64]|=mask;x=end;
    }
}
static int state(const Grid *g,int x,int y,int side) {
    bool first=bit(g,x,y);
    for(int j=y;j<y+side;j++)for(int i=x;i<x+side;i++)if(bit(g,i,j)!=first)return 2;
    return first?1:0;
}
static size_t nodes(const Grid *g,int x,int y,int side) {
    if(state(g,x,y,side)!=2)return 1;
    size_t n=1;int half=side/2;
    for(int i=0;i<4;i++)n+=nodes(g,x+(i&1)*half,y+(i>>1)*half,half);
    return n;
}
static void tree(PPGrabArea *a,const Grid *g,size_t at,size_t *next,int x,int y,int side) {
    a->nodes[at].state=(uint8_t)state(g,x,y,side);
    if(a->nodes[at].state!=2)return;
    size_t children=*next;*next+=4;a->nodes[at].child=(uint16_t)children;int half=side/2;
    for(int i=0;i<4;i++)tree(a,g,children+i,next,x+(i&1)*half,y+(i>>1)*half,half);
}
static bool create(PPAssets *owner,const PPFrame *frames,size_t count,PPGrabArea **out,PPError *e) {
    if(!out)return fail(e,PP_ERROR_ARGUMENT,"Missing grab area output");*out=NULL;
    if(!frames||!count||frames[0].width<1||frames[0].height<1||frames[0].width>8192||frames[0].height>8192)
        return fail(e,PP_ERROR_ARGUMENT,"Invalid grab source");
    int w=frames[0].width,h=frames[0].height;Grid grid={0},cells={0};
    if(count>4096||(size_t)w*h>((size_t)256*1024*1024/5)/count)
        return fail(e,PP_ERROR_RESOURCE_LIMIT,"Grab source exceeds decoded resource limits");
    int nx=w<PP_GRAB_AXIS?w:PP_GRAB_AXIS,ny=h<PP_GRAB_AXIS?h:PP_GRAB_AXIS;size_t occupied_cells=0;
    for(size_t f=0;f<count;f++) {
        const PPFrame *frame=frames+f;
        if(frame->width!=w||frame->height!=h||!frame->pixels||frame->stride<(size_t)w*4)
            return fail(e,PP_ERROR_ARGUMENT,"Grab union needs matching composited frames");
        if(occupied_cells==(size_t)nx*ny)continue;
        /* Once a cell is occupied, further pixels/frames cannot change its
         * union. Stop at the first alpha instead of scanning every opaque run. */
        for(int gy=0;gy<ny;gy++)for(int gx=0;gx<nx;gx++) {
            if(bit(&cells,gx,gy))continue;
            int y0=gy*h/ny,y1=((gy+1)*h+ny-1)/ny;
            int x0=gx*w/nx,x1=((gx+1)*w+nx-1)/nx;
            bool occupied=false;
            for(int y=y0;y<y1&&!occupied;y++) {
                const uint8_t *alpha=frame->content_alpha?frame->content_alpha+(size_t)y*w:frame->pixels+(size_t)y*frame->stride+3;
                size_t step=frame->content_alpha?1:4;
                for(int x=x0;x<x1;x++)if(alpha[(size_t)x*step]){occupied=true;break;}
            }
            if(occupied) {
                fill(&cells,gy,gx,gx+1);occupied_cells++;
                int left=gx*PP_GRAB_AXIS/nx,right=((gx+1)*PP_GRAB_AXIS+nx-1)/nx;
                int top=gy*PP_GRAB_AXIS/ny,bottom=((gy+1)*PP_GRAB_AXIS+ny-1)/ny;
                for(int y=top;y<bottom;y++)fill(&grid,y,left,right);
            }
        }
    }
    size_t n=nodes(&grid,0,0,PP_GRAB_AXIS),bytes=sizeof(PPGrabArea)+n*sizeof(GrabNode);
    if(owner&&!pp_assets_reserve_grab(owner,bytes,e))return false;
    PPGrabArea *a=calloc(1,bytes);if(!a){if(owner)pp_assets_release_grab(owner,bytes);return fail(e,PP_ERROR_MEMORY,"Cannot allocate grab quadtree");}
    a->refs=1;a->count=n;a->width=w;a->height=h;a->owner=owner;size_t next=1;tree(a,&grid,0,&next,0,0,PP_GRAB_AXIS);*out=a;return true;
}
bool pp_grab_create(const PPFrame *frames,size_t count,PPGrabArea **out,PPError *e){return create(NULL,frames,count,out,e);}
bool pp_grab_create_cached(PPAssets *assets,const PPFrame *frames,size_t count,PPGrabArea **out,PPError *e) {
    if(!assets)return fail(e,PP_ERROR_ARGUMENT,"Missing grab cache owner");return create(assets,frames,count,out,e);
}
PPGrabArea *pp_grab_retain(PPGrabArea *a){if(a)a->refs++;return a;}
void pp_grab_release(PPGrabArea *a){if(a&&!--a->refs){if(a->owner)pp_assets_release_grab(a->owner,pp_grab_bytes(a));free(a);}}
size_t pp_grab_bytes(const PPGrabArea *a){return a?sizeof(*a)+a->count*sizeof(GrabNode):0;}
size_t pp_grab_nodes(const PPGrabArea *a){return a?a->count:0;}
bool pp_grab_empty(const PPGrabArea *a){return !a||a->nodes[0].state==0;}

typedef struct Projection { Grid grid; PPVec2 layout,center; PPRect rect; double scale,c,s; int w,h; const PPGrabArea *area; } Projection;
static void polygon(Projection *p,int x,int y,int side) {
    /* Half a source texel covers linear filtering; two native pixels cover
     * integer region rounding and float shader differences. */
    double angle_margin=hypot(p->w,p->h)*.5*(PP_GRAB_ANGLE_STEP*.5*3.14159265358979323846/180);
    double mx=.5*p->rect.w/p->area->width,my=.5*p->rect.h/p->area->height,rounding=(2+angle_margin)/p->scale;
    /* The shader rejects UVs outside the source rectangle, so filtering does
     * not extend the artwork past its outer texel boundary. */
    double l=p->rect.x+fmax(0,p->rect.w*x/PP_GRAB_AXIS-mx)-p->layout.x*.5-rounding;
    double r=p->rect.x+fmin(p->rect.w,p->rect.w*(x+side)/PP_GRAB_AXIS+mx)-p->layout.x*.5+rounding;
    double t=p->rect.y+fmax(0,p->rect.h*y/PP_GRAB_AXIS-my)-p->layout.y*.5-rounding;
    double b=p->rect.y+fmin(p->rect.h,p->rect.h*(y+side)/PP_GRAB_AXIS+my)-p->layout.y*.5+rounding;
    PPVec2 v[4];int order[4]={0,1,3,2};double low=PP_GRAB_AXIS,high=0;
    for(int i=0;i<4;i++) {
        double sx=order[i]&1?r:l,sy=order[i]&2?b:t;
        v[i]=(PPVec2){((p->c*sx-p->s*sy)*p->scale+p->center.x)*PP_GRAB_AXIS/p->w,
                      ((p->s*sx+p->c*sy)*p->scale+p->center.y)*PP_GRAB_AXIS/p->h};
        low=fmin(low,v[i].y);high=fmax(high,v[i].y);
    }
    int first=(int)fmax(0,floor(low)),last=(int)fmin(PP_GRAB_AXIS,ceil(high));
    for(int row=first;row<last;row++) {
        double xmin=HUGE_VAL,xmax=-HUGE_VAL;
        for(int i=0;i<4;i++) {
            PPVec2 a=v[i],b2=v[(i+1)%4];
            if(a.y>=row&&a.y<=row+1){xmin=fmin(xmin,a.x);xmax=fmax(xmax,a.x);}
            if(a.y!=b2.y)for(int edge=0;edge<2;edge++) {
                double q=(row+edge-a.y)/(b2.y-a.y);
                if(q>=0&&q<=1){double xx=a.x+q*(b2.x-a.x);xmin=fmin(xmin,xx);xmax=fmax(xmax,xx);}
            }
        }
        if(xmin<=xmax)fill(&p->grid,row,(int)fmax(0,floor(xmin)),(int)fmin(PP_GRAB_AXIS,ceil(xmax)));
    }
}
static void visit(Projection *p,size_t at,int x,int y,int side) {
    const GrabNode *n=p->area->nodes+at;
    if(n->state==1)polygon(p,x,y,side);
    else if(n->state==2){int half=side/2;for(int i=0;i<4;i++)visit(p,n->child+i,x+(i&1)*half,y+(i>>1)*half,half);}
}
static bool rectangles(const Grid *g,int axis,int w,int h,PPGrabRegion *out) {
    out->count=0;out->axis=(unsigned)axis;int factor=PP_GRAB_AXIS/axis;
    uint64_t rows[PP_GRAB_AXIS][2]={0};
    for(int y=0;y<PP_GRAB_AXIS;y++)for(int x=0;x<PP_GRAB_AXIS;x++)if(bit(g,x,y))rows[y/factor][(x/factor)/64]|=UINT64_C(1)<<((x/factor)%64);
    for(int y=0;y<axis;) {
        int end=y+1;while(end<axis&&rows[end][0]==rows[y][0]&&rows[end][1]==rows[y][1])end++;
        int top=y*h/axis,bottom=end*h/axis;
        if(top<bottom)for(int x=0;x<axis;) {
            if(!((rows[y][x/64]>>(x%64))&1)){x++;continue;}
            int start=x++;while(x<axis&&((rows[y][x/64]>>(x%64))&1))x++;
            int left=start*w/axis,right=x*w/axis;
            if(left<right){if(out->count==PP_GRAB_RECT_LIMIT)return false;out->rects[out->count++]=(PPGrabRect){left,top,right,bottom};}
        }
        y=end;
    }
    return true;
}
bool pp_grab_project(const PPGrabArea *a,PPVec2 layout,PPRect rect,double longest,double angle,double density,int w,int h,PPGrabRegion *out,PPError *e) {
    return pp_grab_project_at(a,layout,rect,longest,angle,density,w,h,(PPVec2){w*.5,h*.5},out,e);
}
bool pp_grab_project_at(const PPGrabArea *a,PPVec2 layout,PPRect rect,double longest,double angle,double density,int w,int h,PPVec2 center,PPGrabRegion *out,PPError *e) {
    if(!a||!out||!isfinite(layout.x)||!isfinite(layout.y)||layout.x<=0||layout.y<=0||
       !isfinite(rect.x)||!isfinite(rect.y)||!isfinite(rect.w)||!isfinite(rect.h)||rect.w<=0||rect.h<=0||
       !isfinite(longest)||longest<=0||!isfinite(angle)||!isfinite(density)||density<=0||w<1||h<1||w>16384||h>16384||
       !isfinite(center.x)||!isfinite(center.y)||center.x<0||center.x>w||center.y<0||center.y>h)
        return fail(e,PP_ERROR_ARGUMENT,"Invalid grab projection");
    /* Bound tiny easing updates at the input approximation's precision. The
     * angular envelope above contains every visual pose in the bucket. */
    double radians=round(fmod(angle,360)/PP_GRAB_ANGLE_STEP)*PP_GRAB_ANGLE_STEP*3.14159265358979323846/180;
    Projection p={0};p.area=a;p.layout=layout;p.center=center;p.rect=rect;p.scale=longest*density/fmax(layout.x,layout.y);p.c=cos(radians);p.s=sin(radians);p.w=w;p.h=h;
    if(!isfinite(p.scale)||p.scale<=0||!isfinite(2/p.scale))return fail(e,PP_ERROR_ARGUMENT,"Invalid grab scale");
    visit(&p,0,0,0,PP_GRAB_AXIS);
    for(int axis=PP_GRAB_OUTPUT_AXIS;axis>=32;axis/=2)if(rectangles(&p.grid,axis,w,h,out))return true;
    return fail(e,PP_ERROR_RESOURCE_LIMIT,"Grab region exceeds its bounded approximation");
}
