#include "pointy/assets.h"
#include "pointy/interaction.h"
#include "../../src/core/geometry_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
static PPFrame frame(int w,int h){PPFrame f={0};f.width=w;f.height=h;f.stride=(size_t)w*4;f.pixels=calloc(h,f.stride);CHECK(f.pixels);return f;}
static bool contains(const PPGrabRegion *r,int x,int y){for(size_t i=0;i<r->count;i++){PPGrabRect b=r->rects[i];if(x>=b.left&&x<b.right&&y>=b.top&&y<b.bottom)return true;}return false;}
static void destroy(PPFrame *f){free(f->pixels);free(f->content_alpha);}
static void validate(const PPGrabRegion *r,int w,int h) {
    CHECK(r->count<=PP_GRAB_RECT_LIMIT);
    for(size_t i=0;i<r->count;i++) {
        PPGrabRect a=r->rects[i];CHECK(a.left>=0&&a.top>=0&&a.right<=w&&a.bottom<=h&&a.left<a.right&&a.top<a.bottom);
        if(i){PPGrabRect b=r->rects[i-1];CHECK(a.top>b.top||(a.top==b.top&&a.left>=b.right));}
        for(size_t j=0;j<i;j++){PPGrabRect b=r->rects[j];CHECK(a.right<=b.left||a.left>=b.right||a.bottom<=b.top||a.top>=b.bottom);}
    }
}
static void coverage(void) {
    PPFrame f=frame(128,128);PPError e={0};
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){double d=hypot(x-63.5,y-63.5);if(d>29&&d<52)f.pixels[(size_t)y*f.stride+x*4+3]=255;}
    /* Very faint, thin disconnected diagonal must never be clipped. */
    for(int x=5;x<20;x++)f.pixels[(size_t)x*f.stride+x*4+3]=1;
    PPGrabArea *a=NULL;CHECK(pp_grab_create(&f,1,&a,&e));CHECK(!pp_grab_empty(a)&&pp_grab_nodes(a)>1);
    double angles[]={0,17.25,37,90,17.274,37.026};
    for(int k=0;k<6;k++)for(int size=0;size<2;size++) {
        PPPose pose={{0,0},size?384:96,angles[k],1};int w,h;CHECK(pp_geometry_extent(&f,pose.longest_dip,pose.angle_deg,1.5,&w,&h));
        PPGrabRegion r={0};CHECK(pp_grab_project(a,(PPVec2){128,128},(PPRect){0,0,128,128},pose.longest_dip,pose.angle_deg,1.5,w,h,&r,&e));validate(&r,w,h);
        CHECK(!contains(&r,w/2,h/2));
        uint8_t *covered=calloc((size_t)w,h);CHECK(covered);
        for(size_t i=0;i<r.count;i++){PPGrabRect b=r.rects[i];for(int y=b.top;y<b.bottom;y++)memset(covered+(size_t)y*w+b.left,1,b.right-b.left);}
        PPSamplingGrid grid={{0,0},1};
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(pp_content_hit_on_grid(&f,&pose,1.5,&grid,(PPVec2){x,y}))CHECK(covered[(size_t)y*w+x]);
        free(covered);
    }
    pp_grab_release(a);destroy(&f);
    puts("PASS ring holes and conservative exact-alpha coverage at six rotations/two sizes, including alpha1 diagonals");
}
static void union_and_bounds(void) {
    PPFrame f[3]={frame(128,64),frame(128,64),frame(128,64)};PPError e={0};
    for(int y=16;y<48;y++)for(int x=8;x<24;x++)f[0].pixels[(size_t)y*f[0].stride+x*4+3]=255;
    for(int y=16;y<48;y++)for(int x=104;x<120;x++)f[2].pixels[(size_t)y*f[2].stride+x*4+3]=255;
    PPGrabArea *a=NULL;CHECK(pp_grab_create(f,3,&a,&e));PPGrabRegion r={0};
    CHECK(pp_grab_project(a,(PPVec2){128,64},(PPRect){0,0,128,64},128,0,1,128,64,&r,&e));
    CHECK(contains(&r,16,32)&&contains(&r,112,32)&&!contains(&r,64,32));
    /* All-occupied source collapses to one quadtree node, but rotation must
     * still reject empty corners rather than taking that leaf's outer AABB. */
    memset(f[1].pixels,255,f[1].stride*f[1].height);PPGrabArea *solid=NULL;CHECK(pp_grab_create(f+1,1,&solid,&e));CHECK(pp_grab_nodes(solid)==1);
    CHECK(pp_grab_project(solid,(PPVec2){128,64},(PPRect){0,0,128,64},128,37,1,142,130,&r,&e));CHECK(!contains(&r,0,0));
    pp_grab_release(solid);pp_grab_release(a);for(int i=0;i<3;i++)destroy(f+i);
    puts("PASS stable union includes blank-frame positions, preserves common holes, and rotated solid interiors compress without rectangular corner interception");
}
static void fragmented(void) {
    PPFrame f=frame(2048,2048);unsigned seed=17;
    for(int y=32;y<128;y++)for(int x=0;x<128;x++){seed=1664525*seed+1013904223;if(seed%20==0)f.pixels[(size_t)(y*16+8)*f.stride+(x*16+8)*4+3]=255;}
    PPGrabArea *a=NULL;PPError e={0};CHECK(pp_grab_create(&f,1,&a,&e));
    PPGrabRegion r={0};CHECK(pp_grab_project(a,(PPVec2){2048,2048},(PPRect){0,0,2048,2048},1536,17.25,1.5,2888,2888,&r,&e));validate(&r,2888,2888);
    CHECK(r.axis==PP_GRAB_OUTPUT_AXIS||r.axis==32);CHECK(!contains(&r,0,0));
    printf("PASS fragmented projection bounded to %zu native rectangles at %u cells, %zu nodes/%zu bytes\n",r.count,r.axis,pp_grab_nodes(a),pp_grab_bytes(a));
    pp_grab_release(a);destroy(&f);
}
static void overhang(void) {
    PPFrame f=frame(137,61);PPError e={0};
    for(int y=0;y<61;y++)for(int x=0;x<137;x++)if(x==0||y==0||x==136||y==60||x==y)f.pixels[(size_t)y*f.stride+x*4+3]=1;
    PPGrabArea *a=NULL;CHECK(pp_grab_create(&f,1,&a,&e));
    PPVec2 layout={96,48};PPRect source={-12.3,-8.1,137.4,61.7};
    double angles[]={0,17.25,37,90};
    for(int k=0;k<4;k++) {
        int w,h;CHECK(pp_geometry_source_extent(layout,source,384,angles[k],1.5,&w,&h));PPGrabRegion r={0};
        CHECK(pp_grab_project(a,layout,source,384,angles[k],1.5,w,h,&r,&e));validate(&r,w,h);
        double radians=angles[k]*3.14159265358979323846/180,c=cos(radians),s=sin(radians),scale=384*1.5/96;
        for(int y=0;y<61;y++)for(int x=0;x<137;x++)if(f.pixels[(size_t)y*f.stride+x*4+3]) {
            double sx=source.x+source.w*(x+.5)/137-layout.x*.5,sy=source.y+source.h*(y+.5)/61-layout.y*.5;
            int px=(int)floor(w*.5+(c*sx-s*sy)*scale),py=(int)floor(h*.5+(s*sx+c*sy)*scale);
            CHECK(contains(&r,px,py));
        }
    }
    pp_grab_release(a);destroy(&f);puts("PASS faint boundary texels and canonical overhang mapping at four rotations");
}
static int compare_time(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void cache_and_tiny_union(void) {
    PPFrame f=frame(1,1);PPError e={0};PPFrame *frames=malloc(4096*sizeof(*frames));CHECK(frames);
    for(int i=0;i<4096;i++)frames[i]=f;PPGrabArea *empty=NULL;CHECK(pp_grab_create(frames,4096,&empty,&e));CHECK(pp_grab_empty(empty));pp_grab_release(empty);
    f.pixels[3]=255;PPGrabArea *solid=NULL;CHECK(pp_grab_create(frames,4096,&solid,&e));CHECK(pp_grab_nodes(solid)==1);pp_grab_release(solid);free(frames);
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&e));PPGrabArea *a=NULL,*candidate=NULL;
    CHECK(pp_grab_create_cached(assets,&f,1,&a,&e));size_t bytes=pp_grab_bytes(a);CHECK(pp_assets_grab_bytes(assets)==bytes);
    pp_grab_retain(a);pp_grab_release(a);CHECK(pp_assets_grab_bytes(assets)==bytes);
    size_t remaining=PP_GRAB_CACHE_LIMIT-bytes;CHECK(pp_assets_reserve_grab(assets,remaining,&e));
    CHECK(!pp_grab_create_cached(assets,&f,1,&candidate,&e)&&!candidate&&e.code==PP_ERROR_RESOURCE_LIMIT&&pp_assets_grab_bytes(assets)==PP_GRAB_CACHE_LIMIT);
    pp_assets_release_grab(assets,remaining);pp_grab_release(a);CHECK(!pp_assets_grab_bytes(assets));pp_assets_destroy(assets);destroy(&f);
    puts("PASS 4096 tiny blank/full frame unions and exact allocation admission/refcount lifetime under the grab budget");
}
static void preparation_timing(void) {
    PPFrame f=frame(8192,6144);size_t pixels=(size_t)f.width*f.height;f.content_alpha=calloc(pixels,1);CHECK(f.content_alpha);PPError e={0};
    const char *names[]={"empty","opaque","checker"};
    for(int kind=0;kind<3;kind++) {
        if(kind==1)memset(f.content_alpha,255,pixels);
        if(kind==2)for(size_t i=0;i<pixels;i++)f.content_alpha[i]=(i&1)?255:0;
        double times[20];size_t count=0,bytes=0;
        for(int sample=0;sample<23;sample++) {
            Uint64 start=SDL_GetPerformanceCounter();PPGrabArea *a=NULL;CHECK(pp_grab_create(&f,1,&a,&e));
            double elapsed=(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
            count=pp_grab_nodes(a);bytes=pp_grab_bytes(a);pp_grab_release(a);if(sample>=3)times[sample-3]=elapsed;
        }
        qsort(times,20,sizeof(double),compare_time);
        printf("GRAB PREPARATION source=%s pixels=%zu raster_bytes=%zu alpha_cached=1 samples=20 median_ms=%.3f p95_ms=%.3f maximum_ms=%.3f nodes=%zu retained_bytes=%zu\n",names[kind],pixels,pixels*5,times[10],times[18],times[19],count,bytes);
    }
    PPFrame *tiny=malloc(4096*sizeof(*tiny));CHECK(tiny);uint8_t zero[4]={0};
    for(int i=0;i<4096;i++)tiny[i]=(PPFrame){.pixels=zero,.width=1,.height=1,.stride=4};
    Uint64 start=SDL_GetPerformanceCounter();PPGrabArea *tiny_union=NULL;CHECK(pp_grab_create(tiny,4096,&tiny_union,&e));
    printf("GRAB PREPARATION source=4096-empty-tiny-frames source_pixels=4096 elapsed_ms=%.3f nodes=%zu retained_bytes=%zu\n",(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency(),pp_grab_nodes(tiny_union),pp_grab_bytes(tiny_union));
    pp_grab_release(tiny_union);free(tiny);
    destroy(&f);
}
int main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"--timing")){preparation_timing();return 0;}CHECK(argc==1);coverage();union_and_bounds();fragmented();overhang();cache_and_tiny_union();return 0;}
