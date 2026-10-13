/* Exercises the real presenter with deterministic presentation boundaries.
 * No windows, hooks, input injection, or desktop focus changes are made. */
#include "../../src/app/anchors.h"
#include "../test.h"
#include "../project_fixture.h"
#include "../../src/core/geometry_internal.h"
#include "../../src/app/presenter_internal.h"
#include <math.h>
#include <SDL3/SDL.h>
#undef pp_text_render_result_bounded
#undef pp_svg_render_bounded
bool pp_text_render_result_bounded(PPAssets *,const char *,const PPTextStyle *,double,size_t,PPTextResult *,PPTextDiagnostics *,PPError *);
bool pp_svg_render_bounded(PPBytes,int,int,size_t,size_t,PPFrame *,PPError *);
static SDL_AtomicInt block_next_worker, fail_next_worker;
static SDL_Semaphore *worker_entered, *worker_continue;
/* Controls scheduling at the agreed font/content boundary; generation still
 * uses the production renderer and isolated worker manager. */
bool pp_test_worker_text(PPAssets *assets,const char *text,const PPTextStyle *style,double scale,size_t limit,PPTextResult *out,PPTextDiagnostics *diagnostics,PPError *error) {
    if(SDL_CompareAndSwapAtomicInt(&block_next_worker,1,0)) {SDL_SignalSemaphore(worker_entered);CHECK(SDL_WaitSemaphoreTimeout(worker_continue,5000));}
    if(SDL_CompareAndSwapAtomicInt(&fail_next_worker,1,0)) {if(error)*error=(PPError){PP_ERROR_MEMORY,"Injected candidate generation failure"};return false;}
    return pp_text_render_result_bounded(assets,text,style,scale,limit,out,diagnostics,error);
}
bool pp_test_worker_svg(PPBytes source,int w,int h,size_t limit,size_t scratch,PPFrame *out,PPError *error) {
    if(SDL_CompareAndSwapAtomicInt(&block_next_worker,1,0)){SDL_SignalSemaphore(worker_entered);CHECK(SDL_WaitSemaphoreTimeout(worker_continue,5000));}
    if(SDL_CompareAndSwapAtomicInt(&fail_next_worker,1,0)){if(error)*error=(PPError){PP_ERROR_MEMORY,"Injected SVG candidate generation failure"};return false;}
    return pp_svg_render_bounded(source,w,h,limit,scratch,out,error);
}
static void hold_next_worker(void) {
    worker_entered=SDL_CreateSemaphore(0);worker_continue=SDL_CreateSemaphore(0);CHECK(worker_entered&&worker_continue);
    SDL_SetAtomicInt(&block_next_worker,1);
}
static void release_worker(void) {SDL_SignalSemaphore(worker_continue);}
static void finish_worker_control(void) {SDL_DestroySemaphore(worker_entered);SDL_DestroySemaphore(worker_continue);worker_entered=worker_continue=NULL;}
struct PPSurface {uint64_t id;PPRect bounds;bool shown;struct PPSurface *next;int source_width, raster_width;uint8_t red,blue;uint32_t window_id;PPRect artwork_bounds;};
struct PPRenderer {PPSurface *surface;PPRendererMetrics metrics;uint64_t content_id,revision;PPRect alpha_bounds;};
struct PPSharedTexture {const PPFrame *frame;};
static PPSurface *surfaces;
static int source_width;
static unsigned cpu_transforms;
static uint32_t window_serial;
static PPSurface *capture_owner,*failed_gl_surface;
static bool fail_gl_upload;
static bool fail_gl_context;
static bool fail_shared_context;
static size_t native_planes = 4;
static PPRect drawn_source_rect;
size_t pp_surface_raster_bytes(int width,int height) {return (size_t)width*height*native_planes;}
bool pp_surface_create(PPSurface **out,PPError *error) {(void)error;*out=calloc(1,sizeof(**out));CHECK(*out);(*out)->next=surfaces;surfaces=*out;return true;}
bool pp_surface_present(PPSurface *s,const PPFrame *frame,PPRect bounds,PPError *error) {(void)error;s->raster_width=frame->width;s->bounds=bounds;s->shown=true;s->source_width=source_width;s->red=frame->pixels[0];s->blue=frame->pixels[2];return true;}
void pp_surface_destroy(PPSurface *s) {if(!s) return;if(capture_owner==s)capture_owner=NULL;PPSurface **link=&surfaces;while(*link!=s) link=&(*link)->next;*link=s->next;free(s);}
void pp_surface_hide(PPSurface *s) {s->shown=false;if(capture_owner==s)capture_owner=NULL;}
void pp_surface_set_instance_id(PPSurface *s,uint64_t id) {s->id=id;}
bool pp_platform_capture(PPSurface *s,PPError *error) {(void)error;capture_owner=s;return true;}
void pp_platform_release_capture(void) {capture_owner=NULL;}
void pp_surface_cancel_input(PPSurface *s) {if(capture_owner==s)capture_owner=NULL;}
void pp_surface_raise(PPSurface *s) {(void)s;}
bool pp_platform_poll(PPPlatformEvent *out) {(void)out;return false;}
bool pp_platform_pointer(PPVec2 *out) {(void)out;return false;}
bool pp_platform_displays(PPDisplay **out,size_t *count,PPError *error) {(void)out;(void)count;(void)error;return false;}
void pp_displays_free(PPDisplay *d) {free(d);}
void pp_platform_gesture_config(PPGestureConfig *out) {*out=(PPGestureConfig){2,2,2,2,.5,10,1.1,.12};}
bool pp_renderer_create(PPRenderer **out,PPError *error) {(void)error;*out=(PPRenderer *)1;return true;}
void pp_renderer_destroy(PPRenderer *r) {if(r && r!=(PPRenderer *)1) free(r);}
PPRendererMetrics pp_renderer_metrics(const PPRenderer *r) {return r==(PPRenderer *)1?(PPRendererMetrics){cpu_transforms,cpu_transforms,cpu_transforms*2}:r?r->metrics:(PPRendererMetrics){0};}
bool pp_surface_create_gl(PPSurface **out,PPError *error) {if(!pp_surface_create(out,error))return false;(*out)->window_id=++window_serial;return true;}
uint32_t pp_surface_window_id(const PPSurface *s) {return s?s->window_id:0;}
bool pp_surface_place_gl(PPSurface *s,PPRect b,bool interactive,PPError *error) {(void)error;s->bounds=b;s->shown=interactive;if(!interactive)pp_surface_cancel_input(s);return true;}
void pp_surface_set_rectangular(PPSurface *s,bool enabled) {(void)s;(void)enabled;}
void pp_surface_inherit_order(PPSurface *s,PPSurface *old) {(void)s;(void)old;}
bool pp_renderer_create_for_window(PPRenderer **out,uint32_t id,PPError *error) {
    (void)error;*out=calloc(1,sizeof(**out));CHECK(*out);
    for(PPSurface *s=surfaces;s;s=s->next) if(s->window_id==id) (*out)->surface=s;
    CHECK((*out)->surface);return true;
}
bool pp_renderer_create_texture_owner(PPRenderer **out,PPError *error) {(void)error;*out=calloc(1,sizeof(**out));return *out!=NULL;}
bool pp_renderer_create_shared_window(PPRenderer **out,uint32_t id,PPRenderer *owner,PPError *error) {(void)owner;return pp_renderer_create_for_window(out,id,error);}
bool pp_shared_texture_create(PPRenderer *owner,const PPFrame *frame,PPSharedTexture **out,PPError *error) {
    if(fail_shared_context){if(error)*error=(PPError){PP_ERROR_RENDER,"Injected producer context failure"};return false;}
    if(fail_gl_upload){if(error)*error=(PPError){PP_ERROR_MEMORY,"Injected shared allocation rejection"};return false;}
    *out=malloc(sizeof(**out));CHECK(*out);(*out)->frame=frame;owner->metrics.source_uploads++;return true;
}
bool pp_renderer_bind_shared(PPRenderer *r,PPSharedTexture *texture,uint64_t id,uint64_t revision,PPError *error) {
    if(fail_gl_context){if(error)*error=(PPError){PP_ERROR_RENDER,"Injected shared context failure"};return false;}
    r->content_id=id;r->revision=revision;r->surface->source_width=texture->frame->width;
    r->surface->red=texture->frame->pixels[0];r->surface->blue=texture->frame->pixels[2];return true;
}
void pp_shared_texture_destroy(PPSharedTexture *texture){free(texture);}
bool pp_renderer_upload_source(PPRenderer *r,const PPFrame *frame,uint64_t id,uint64_t revision,PPError *error) {
    if(fail_gl_upload) {if(error)*error=(PPError){PP_ERROR_MEMORY,"Injected GPU candidate allocation failure"};return false;}
    if(fail_gl_context) {if(error)*error=(PPError){PP_ERROR_RENDER,"Injected direct context failure"};return false;}
    if(r->content_id==id && r->revision==revision)return true;
    r->surface->source_width=frame->width;r->surface->red=frame->pixels[0];r->surface->blue=frame->pixels[2];
    int left=frame->width,top=frame->height,right=0,bottom=0;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)if(frame->pixels[(size_t)y*frame->stride+x*4+3]) {
        left=SDL_min(left,x);top=SDL_min(top,y);right=SDL_max(right,x+1);bottom=SDL_max(bottom,y+1);
    }
    r->alpha_bounds=(PPRect){(double)left/frame->width,(double)top/frame->height,
        (double)(right-left)/frame->width,(double)(bottom-top)/frame->height};
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++) {
        const uint8_t *pixel=frame->pixels+(size_t)y*frame->stride+x*4;
        if(pixel[3]==255){r->surface->red=pixel[0];r->surface->blue=pixel[2];y=frame->height;break;}
    }
    r->content_id=id;r->revision=revision;r->metrics.source_uploads++;return true;
}
bool pp_renderer_draw(PPRenderer *r,PPVec2 layout,double size,double angle,double opacity,double density,PPError *error) {
    (void)layout;(void)size;(void)angle;(void)opacity;(void)density;
    if(r->surface==failed_gl_surface) {if(error)*error=(PPError){PP_ERROR_RENDER,"Injected active direct draw failure"};return false;}
    r->metrics.transform_draws++;return true;
}
bool pp_renderer_draw_source(PPRenderer *r,PPVec2 layout,PPRect rect,double size,double angle,double opacity,double density,PPError *error) {
    /* The external renderer mock reports the placed nontransparent artwork,
     * using its uploaded alpha extent. Compare replacement with a fresh pull;
     * no private retained state supplies the expected geometry. */
    double scale=size*density/fmax(layout.x,layout.y),a=angle*3.14159265358979323846/180,c=cos(a),s=sin(a);
    double x=rect.x+(r->alpha_bounds.x+r->alpha_bounds.w*.5)*rect.w-layout.x*.5;
    double y=rect.y+(r->alpha_bounds.y+r->alpha_bounds.h*.5)*rect.h-layout.y*.5;
    double w=(fabs(c)*r->alpha_bounds.w*rect.w+fabs(s)*r->alpha_bounds.h*rect.h)*scale;
    double h=(fabs(s)*r->alpha_bounds.w*rect.w+fabs(c)*r->alpha_bounds.h*rect.h)*scale;
    r->surface->artwork_bounds=(PPRect){(c*x-s*y)*scale-w*.5, (s*x+c*y)*scale-h*.5,w,h};
    drawn_source_rect=rect;return pp_renderer_draw(r,layout,size,angle,opacity,density,error);
}
bool pp_renderer_transform(PPRenderer *r,const PPFrame *source,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
    cpu_transforms++;
    (void)r;(void)opacity;(void)error;
    int w,h;CHECK(pp_geometry_extent(source,longest,angle,scale,&w,&h));
    *out=(PPFrame){calloc((size_t)w*h,4),calloc((size_t)w*h,1),w,h,(size_t)w*4};
    source_width=source->width;
    for(int y=0;y<source->height;y++) for(int x=0;x<source->width;x++) {
        const uint8_t *pixel=source->pixels+(size_t)y*source->stride+x*4;
        if(pixel[3]==255) {out->pixels[0]=pixel[0];out->pixels[2]=pixel[2];return true;}
    }
    return true;
}
bool pp_renderer_transform_source(PPRenderer *r,const PPFrame *source,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
    (void)r;(void)opacity;(void)error;cpu_transforms++;int w,h;CHECK(pp_geometry_source_extent(layout,rect,longest,angle,scale,&w,&h));
    *out=(PPFrame){calloc((size_t)w*h,4),calloc((size_t)w*h,1),w,h,(size_t)w*4};source_width=source->width;
    for(int y=0;y<source->height;y++)for(int x=0;x<source->width;x++) {
        const uint8_t *pixel=source->pixels+(size_t)y*source->stride+x*4;
        if(pixel[3]==255){out->pixels[0]=pixel[0];out->pixels[2]=pixel[2];return true;}
    }
    return true;
}
bool pp_renderer_transform_color_source(PPRenderer *r,const PPFrame *source,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
    bool ok=pp_renderer_transform_source(r,source,layout,rect,longest,angle,opacity,scale,out,error);
    free(out->content_alpha);out->content_alpha=NULL;return ok;
}
void pp_frame_destroy(PPFrame *f) {free(f->pixels);free(f->content_alpha);*f=(PPFrame){0};}
static PPSurface *instance_surface(void) {for(PPSurface *s=surfaces;s;s=s->next) if(s->id==1) return s;return NULL;}
static PPItem gif_item(PPAssets *assets,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",PP_MEDIA_FIXTURES,name);
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long n=ftell(file);CHECK(n>0);rewind(file);
    uint8_t *bytes=malloc((size_t)n);CHECK(bytes);CHECK(fread(bytes,1,(size_t)n,file)==(size_t)n);fclose(file);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};PPError error={0};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)n},PP_ASSET_GIF,&item.image_asset,&error));free(bytes);return item;
}
static void test_gif_producer_failure_isolates_interceptor(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=gif_item(assets,"finite.gif");CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay d={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(d));*p->displays=d;p->display_count=1;
    PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error);CHECK(s);uint64_t a=s->id;
    CHECK(pp_sprites_update(p,1,&error));PPSurface *usable=pp_sprites_find(p,a)->surface;
    s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{400,200},96,0,1},.94,&error);CHECK(s);uint64_t b=s->id;
    fail_shared_context=true;CHECK(pp_sprites_update(p,1,&error));fail_shared_context=false;
    CHECK(!pp_scene_get(p->scene,b) && pp_scene_get(p->scene,a) && usable->shown);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS failed GIF producer context removes only the failed interceptor");
}
static void test_gif_independent_clocks_and_shared_frames(void) {
    PPAssets *assets=NULL;PPError error={0};CHECK(pp_assets_create(&assets,&error));
    FILE *file=fopen(PP_MEDIA_FIXTURES "/finite.gif","rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long n=ftell(file);rewind(file);uint8_t *bytes=malloc((size_t)n);CHECK(bytes);
    CHECK(fread(bytes,1,(size_t)n,file)==(size_t)n);fclose(file);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)n},PP_ASSET_GIF,&item.image_asset,&error));free(bytes);
    PPProject *project=NULL;CHECK(pp_project_create(assets,&project,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    PPAnchorPresenter *p=NULL;CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay d={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(d));*p->displays=d;p->display_count=1;
    uint64_t ids[3];for(int i=0;i<3;i++) {
        PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200+i*100,200},96,0,1},i?1.06:1,&error);CHECK(s);ids[i]=s->id;
    }
    uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
    CHECK(pp_sprites_update(p,1.08,&error));
    PPRendererMetrics first=pp_anchor_presenter_renderer_metrics(p);
    CHECK(first.source_uploads-before.source_uploads==2 && first.transform_draws-before.transform_draws==3 && first.readbacks==before.readbacks);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==24);
    CHECK(pp_sprites_find(p,ids[0])->surface->red==0 && pp_sprites_find(p,ids[1])->surface->red==255);
    CHECK(fabs(pp_sprites_find(p,ids[0])->next_frame-1.1)<1e-8 && fabs(pp_sprites_find(p,ids[1])->next_frame-1.11)<1e-8);
    PPSprite *s=pp_sprites_find(p,ids[1]);s->pose.center.x+=20;s->dirty=true;
    CHECK(pp_scene_set_pose(p->scene,s->id,s->pose,&error));CHECK(pp_sprites_update(p,1.081,&error));
    PPRendererMetrics moved=pp_anchor_presenter_renderer_metrics(p);
    CHECK(moved.source_uploads==first.source_uploads && moved.transform_draws==first.transform_draws);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
    s->pose.angle_deg=37;s->pose.longest_dip=192;s->dirty=true;d.drawable_pixels_per_dip=1.5;*p->displays=d;
    CHECK(pp_sprites_update(p,1.082,&error));PPRendererMetrics transformed=pp_anchor_presenter_renderer_metrics(p);
    CHECK(transformed.source_uploads==moved.source_uploads && transformed.transform_draws==moved.transform_draws+3 && transformed.readbacks==moved.readbacks);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
    CHECK(pp_sprites_update(p,1.28,&error));
    PPRendererMetrics stopped=pp_anchor_presenter_renderer_metrics(p);
    CHECK(pp_sprites_update(p,3,&error));
    CHECK(pp_anchor_presenter_renderer_metrics(p).transform_draws==stopped.transform_draws);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==stopped.source_uploads);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
}
static void event(PPAnchorPresenter *p,PPSurface *s,PPPlatformEventKind kind,double x,double y,double time) {
    PPError error={0};PPPlatformEvent e={s,s->id,kind,{x,y},PP_BUTTON_LEFT,0,0,time};CHECK(pp_anchor_presenter_event(p,&e,&error));
}
static void update(PPAnchorPresenter *p,PPDisplay *d,double x,double y,double time) {PPError error={0};CHECK(pp_anchor_presenter_update(p,d,2,(PPVec2){x,y},time,&error));}
static void check_grab(PPAnchorPresenter *p,double pointer_x,double expected_center,double width) {
    PPPose pose;CHECK(pp_anchor_presenter_instance_pose(p,0,&pose));CHECK(fabs(pose.center.x-expected_center)<1e-8);
    PPSurface *s=instance_surface();CHECK(s&&s->shown);CHECK(s->bounds.w==width);
    CHECK(fabs(pose.center.x+35*s->bounds.w/96-pointer_x)<1e-8);
}
static void test_unavailable_source_finishes_pull(void) {
    /* Each source mutation is exercised with update-before-cancel, a queued
     * release before update, and a queued cancel before update. A later scale
     * change exposes stale dragging through its old pointer/grab offset. */
    for (int mutation = 0; mutation < 4; mutation++) {
        for (int ending = 0; ending < 3; ending++) {
            PPError error = {0};
            PPAssets *assets = NULL;
            CHECK(pp_assets_create(&assets, &error));
            PPProject *project = test_arrow_project(assets);
            if (mutation == 3) {
                PPItem remaining = *pp_project_target(project, PP_ANCHOR_TOP_RIGHT);
                remaining.id = 0;
                CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_RIGHT, &remaining, NULL, &error));
            }
            PPAnchorPresenter *p = NULL;
            CHECK(pp_anchor_presenter_create(&p, (PPRenderer *)1, project, &error));
            PPDisplay d[] = {
                {1, {-1920,0,1920,1080}, 1.5, 1.5},
                {2, {0,0,1920,1080}, 1, 1}
            };
            update(p, d, -8, 8, 1);
            PPSurface *preview = NULL;
            for (PPSurface *s = surfaces; s; s = s->next)
                if (s->shown && s->bounds.x == -144 && s->bounds.w == 144) preview = s;
            CHECK(preview);
            event(p, preview, PP_EVENT_BUTTON_DOWN, -19.5, 72, 1.01);
            event(p, preview, PP_EVENT_POINTER_MOVE, 300, 200, 1.02);
            update(p, d, 300, 200, 1.02);
            PPSurface *sprite = instance_surface();
            CHECK(sprite && sprite->shown && sprite->red == 255);
            PPItem item = *pp_project_target(project, PP_ANCHOR_TOP_RIGHT);
            if (mutation == 0 || mutation == 3) {
                CHECK(pp_project_remove_item(project, PP_ANCHOR_TOP_RIGHT, item.id, &error));
            } else if (mutation == 1) {
                CHECK(pp_project_set_enabled(project, PP_ANCHOR_TOP_RIGHT, false, &error));
            } else {
                item.opacity = 0;
                CHECK(pp_project_edit_item(project, item.id, &item, &error));
            }
            double expected_center = 265;
            if (ending != 0) {
                PPPlatformEventKind kind = ending == 1 ? PP_EVENT_BUTTON_UP : PP_EVENT_CANCEL;
                event(p, preview, kind, 320, 200, 1.03);
                CHECK(!pp_anchor_presenter_reveals(p)->entries[PP_ANCHOR_TOP_RIGHT].held);
                if (ending == 1) expected_center = 285;
            }
            update(p, d, 320, 200, 1.04);
            if (mutation != 3) CHECK(!preview->shown);
            CHECK(!pp_anchor_presenter_reveals(p)->entries[PP_ANCHOR_TOP_RIGHT].held);
            /* Hiding may queue terminal events after the linkage is cleared. */
            event(p, preview, PP_EVENT_CANCEL, 400, 250, 1.05);
            event(p, preview, PP_EVENT_BUTTON_UP, 400, 250, 1.06);
            d[1].desktop_units_per_dip = 2;
            d[1].drawable_pixels_per_dip = 2;
            update(p, d, 400, 250, 1.07);
            PPPose pose;
            CHECK(pp_anchor_presenter_instance_pose(p, 0, &pose));
            CHECK(pose.center.x == expected_center && pose.center.y == 200);
            CHECK(pose.longest_dip == 96 && pose.angle_deg == 0 && pose.opacity == 1);
            CHECK(pp_anchor_presenter_instance_count(p) == 1 && instance_surface() == sprite);
            CHECK(sprite->shown && sprite->red == 255 && sprite->bounds.w == 192);
            pp_anchor_presenter_destroy(p);
            pp_project_destroy(project);
            CHECK(pp_assets_external_raster_bytes(assets) == 0);
            pp_assets_destroy(assets);
            CHECK(!surfaces);
        }
    }
}
static void test_native_retention_contract(void) {
    /* A backend retaining two four-byte planes must be charged for both.
     * Replacing a Retina raster releases the old charge only after commit. */
    PPAssets *assets = NULL;
    PPError error = {0};
    CHECK(pp_assets_create(&assets, &error));
    PPAnchorPresenter presenter = {.assets = assets, .renderer = (PPRenderer *)1};
    PPSurface *surface = NULL;
    CHECK(pp_surface_create(&surface, &error));
    uint8_t pixels[16] = {255, 0, 0, 255}, alpha[4] = {255};
    PPFrame source = {pixels, alpha, 2, 2, 8};
    PPDisplay display = {1, {0, 0, 1000, 1000}, 1, 2};
    PPPose pose = {{200, 200}, 64, 0, 1};
    size_t retained = 0;
    native_planes = 8;
    CHECK(pp_present(&presenter, surface, &source, pose, &display, &retained, &error));
    CHECK(surface->bounds.w == 64 && surface->raster_width == 128);
    CHECK(retained == 131072 && pp_assets_external_raster_bytes(assets) == 131072);
    pose.longest_dip = 32;
    CHECK(pp_present(&presenter, surface, &source, pose, &display, &retained, &error));
    CHECK(retained == 32768 && pp_assets_external_raster_bytes(assets) == 32768);
    pp_assets_release_raster(assets, retained);
    pp_surface_destroy(surface);
    CHECK(pp_assets_external_raster_bytes(assets) == 0);
    pp_assets_destroy(assets);
    native_planes = 4;
}
static void test_latest_resize_sharp_observation(void) {
    PPAssets *assets=NULL; PPError error={0}; CHECK(pp_assets_create(&assets,&error));
    PPProject *project=test_arrow_project(assets); PPAnchorPresenter *p=NULL;
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay d={1,{0,0,1920,1080},1,1};
    p->displays=malloc(sizeof(d)); *p->displays=d; p->display_count=1;
    PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error); CHECK(s);
    CHECK(pp_sprites_update(p,1,&error));
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++){SDL_Delay(1);CHECK(pp_sprites_update(p,1,&error));}
    CHECK(pp_sprites_update(p,1,&error));hold_next_worker();
    PPPlatformEvent resize={.kind=PP_EVENT_WHEEL,.surface=s->surface,.instance_id=s->id,.modifiers=PP_MOD_ALT,.wheel_delta=1,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&resize,&error));
    CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    resize.timestamp=2.01; CHECK(pp_anchor_presenter_event(p,&resize,&error));
    CHECK(pp_sprites_update(p,2.02,&error));
    CHECK(pp_anchor_presenter_metrics(p).sharp_publications==0);
    release_worker();
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++){SDL_Delay(1);CHECK(pp_sprites_update(p,2.2,&error));}
    CHECK(pp_sprites_update(p,2.2,&error));
    finish_worker_control();
    PPPresentationMetrics metrics=pp_anchor_presenter_metrics(p);
    CHECK(metrics.sharp_publications==1 && metrics.last_sharp_input_timestamp==2.01);
    CHECK(metrics.last_sharp_instance_id==s->id && metrics.last_sharp_drawable_density==1);
    CHECK(metrics.last_sharp_longest_dip>96);
    CHECK(pp_sprites_update(p,2.21,&error));
    CHECK(pp_anchor_presenter_metrics(p).sharp_publications==1);
    pp_anchor_presenter_destroy(p); pp_project_destroy(project); pp_assets_destroy(assets);
}
static void test_png_translation_reuses_presentation(void) {
    PPAssets *assets=NULL;PPError error={0};CHECK(pp_assets_create(&assets,&error));
    FILE *file=fopen(PP_MEDIA_FIXTURES "/alpha.png","rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0);rewind(file);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes);CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)length},PP_ASSET_PNG,&item.image_asset,&error));free(bytes);
    PPProject *project=NULL;CHECK(pp_project_create(assets,&project,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    PPAnchorPresenter *p=NULL;CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    update(p,displays,-8,8,1);PPSurface *preview=NULL;
    for(PPSurface *s=surfaces;s;s=s->next) if(s->shown&&!s->id&&s->source_width==3) preview=s;
    CHECK(preview);event(p,preview,PP_EVENT_BUTTON_DOWN,-120,24,1.01);
    event(p,preview,PP_EVENT_POINTER_MOVE,300,200,1.02);event(p,preview,PP_EVENT_BUTTON_UP,300,200,1.03);
    update(p,displays,300,200,1.3);PPSurface *sprite=instance_surface();CHECK(sprite&&sprite->shown);
    unsigned warm=cpu_transforms;
    update(p,displays,300,200,2);CHECK(cpu_transforms==warm);
    event(p,sprite,PP_EVENT_BUTTON_DOWN,300,200,3);
    event(p,sprite,PP_EVENT_POINTER_MOVE,400,300,3.01);event(p,sprite,PP_EVENT_BUTTON_UP,400,300,3.02);
    update(p,displays,400,300,3.03);
    CHECK(instance_surface()==sprite && cpu_transforms==warm);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS PNG static/translation reuses displayed source and native surface without a transform");
}
static PPItem png_item(PPAssets *assets,const char *fixture,double longest) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",PP_MEDIA_FIXTURES,fixture);
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0);rewind(file);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes);CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=longest,.opacity=1};PPError error={0};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)length},PP_ASSET_PNG,&item.image_asset,&error));free(bytes);return item;
}
static PPSurface *surface_for(uint64_t id) {for(PPSurface *s=surfaces;s;s=s->next)if(s->id==id)return s;return NULL;}
static uint64_t png_pull(PPAnchorPresenter *p,PPScene *scene,PPDisplay *displays,double now) {
    update(p,displays,-8,8,now);PPSurface *preview=NULL;
    for(PPSurface *s=surfaces;s;s=s->next)if(s->shown&&!s->id&&s->bounds.w>24)preview=s;CHECK(preview);
    event(p,preview,PP_EVENT_BUTTON_DOWN,-120,24,now+.01);
    event(p,preview,PP_EVENT_POINTER_MOVE,400,300,now+.02);event(p,preview,PP_EVENT_BUTTON_UP,400,300,now+.03);
    update(p,displays,400,300,now+.3);CHECK(pp_scene_count(scene));return pp_scene_at(scene,pp_scene_count(scene)-1)->id;
}
static uint64_t source_sprite(PPAnchorPresenter *p,PPScene *scene,PPDisplay *displays,double now) {
    /* Isolate source-worker observations from the independent anchor consumer.
     * The whole preview-to-pull route has its own retained-source regression. */
    update(p,displays,400,300,now);PPError error={0};
    const PPItem *item=pp_project_target(p->project,PP_ANCHOR_TOP_RIGHT);
    PPItemSnapshot *snapshot=pp_project_snapshot(p->project,item->id);PPVec2 layout=pp_item_snapshot_size(snapshot);pp_item_snapshot_release(snapshot);
    size_t count=pp_scene_count(scene);PPPose pose={{400,300},fmax(layout.x,layout.y),item->angle_deg,item->opacity};
    PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,pose,now,&error);CHECK(sprite);
    update(p,displays,400,300,now);CHECK(pp_scene_count(scene)==count+1);return pp_scene_at(scene,count)->id;
}
static void test_png_appearance_geometry_and_rollback(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=png_item(assets,"alpha.png",96);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=png_pull(p,scene,displays,1);PPSurface *sprite=surface_for(id);CHECK(sprite&&sprite->window_id&&sprite->source_width==3);
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);PPSourceMetrics builds=pp_assets_source_metrics(assets);
    PPPose pose=pp_scene_get(scene,id)->pose;pose.center=(PPVec2){400,300};pose.longest_dip=192;
    CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,2);
    CHECK(sprite==surface_for(id)&&sprite->bounds.w==192&&sprite->bounds.h==64);
    pose.angle_deg=90;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,3);
    CHECK(sprite->bounds.w==64&&sprite->bounds.h==192);
    pose.opacity=.5;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,4);
    PPRendererMetrics appearance=pp_anchor_presenter_renderer_metrics(p);
    CHECK(appearance.source_uploads==warm.source_uploads && appearance.transform_draws==warm.transform_draws+3 && appearance.readbacks==warm.readbacks);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds.raster_builds);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    event(p,sprite,PP_EVENT_BUTTON_DOWN,400,300,4.1);CHECK(capture_owner==sprite);
    pose.opacity=0;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,5);CHECK(!sprite->shown&&!capture_owner);
    pose.opacity=1;pose.angle_deg=0;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,6);
    displays[1].drawable_pixels_per_dip=1.5;update(p,displays,400,300,7);
    CHECK(sprite->bounds.w==192 && sprite->bounds.h==64 && pp_scene_get(scene,id)->pose.center.x==400);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads);
    /* Reject the drawable extent before native placement; preserve the last usable pose. */
    PPPose usable=pp_scene_get(scene,id)->pose;PPRect usable_bounds=sprite->bounds;
    pose.longest_dip=4096;pose.angle_deg=45;CHECK(pp_scene_set_pose(scene,id,pose,&error));
    displays[1].drawable_pixels_per_dip=3;update(p,displays,400,300,8);
    CHECK(pp_scene_get(scene,id) && !memcmp(&pp_scene_get(scene,id)->pose,&usable,sizeof(usable)) && !memcmp(&sprite->bounds,&usable_bounds,sizeof(usable_bounds)));
    displays[1].drawable_pixels_per_dip=1.5;
    uint64_t before_revision=pp_item_snapshot_revision(pp_scene_get(scene,id)->snapshot);
    PPItem blue=png_item(assets,"retained-blue.png",96);CHECK(pp_scene_replace(scene,id,&blue,9,&error));
    fail_gl_upload=true;update(p,displays,400,300,9);fail_gl_upload=false;
    CHECK(sprite==surface_for(id)&&sprite->shown&&pp_item_snapshot_revision(pp_scene_get(scene,id)->snapshot)==before_revision);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    CHECK(pp_scene_replace(scene,id,&blue,10,&error));update(p,displays,400,300,10);
    CHECK(sprite==surface_for(id)&&sprite->source_width==8&&sprite->blue==255&&sprite->bounds.w==192&&sprite->bounds.h==96);
    PPGpuResources gpu=pp_anchor_presenter_gpu_resources(p);CHECK(gpu.retained_bytes==128&&gpu.peak_bytes==140);
    /* Physical desktop size changes preserve logical pose and the grabbed DIP offset. */
    event(p,sprite,PP_EVENT_BUTTON_DOWN,420,300,10.1);event(p,sprite,PP_EVENT_POINTER_MOVE,500,300,10.2);
    displays[1].desktop_units_per_dip=1.5;update(p,displays,500,300,10.21);
    CHECK(pp_scene_get(scene,id)->pose.center.x==470 && sprite->bounds.w==288);
    event(p,sprite,PP_EVENT_BUTTON_UP,500,300,10.22);
    PPDisplay surviving={3,{-1000,0,1000,800},1,1};
    CHECK(pp_anchor_presenter_update(p,&surviving,1,(PPVec2){-500,400},11,&error));
    CHECK(pp_scene_get(scene,id)->pose.center.x<0 && pp_scene_get(scene,id)->pose.longest_dip==192);
    event(p,sprite,PP_EVENT_BUTTON_DOWN,pp_scene_get(scene,id)->pose.center.x,pp_scene_get(scene,id)->pose.center.y,11.1);
    CHECK(capture_owner==sprite);failed_gl_surface=sprite;pose=pp_scene_get(scene,id)->pose;pose.angle_deg=20;
    CHECK(pp_scene_set_pose(scene,id,pose,&error));CHECK(pp_anchor_presenter_update(p,&surviving,1,(PPVec2){-500,400},12,&error));failed_gl_surface=NULL;
    CHECK(!pp_scene_get(scene,id)&&!surface_for(id)&&!capture_owner&&pp_anchor_presenter_gpu_resources(p).retained_bytes==0);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS PNG cached appearance, opacity/capture, canonical density/recovery, transactional replacement/rollback and direct failure isolation");
}
static void test_png_gpu_limit_and_overlap(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=png_item(assets,"budget-0.png",96);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t first=0;
    p->displays=malloc(sizeof(displays));CHECK(p->displays);memcpy(p->displays,displays,sizeof(displays));p->display_count=2;
    for(int i=0;i<4;i++) {
      PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{400+i*100,300},96,0,1},1+i,&error);CHECK(sprite);
      if(!i)first=sprite->id;CHECK(pp_sprites_update(p,1+i,&error));CHECK(pp_scene_count(scene)==(size_t)i+1);
    }
    PPGpuResources full=pp_anchor_presenter_gpu_resources(p);CHECK(full.retained_bytes==268435456 && full.peak_bytes==full.retained_bytes && full.limit_bytes==268435456);
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);PPSurface *old=surface_for(first);
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(scene,first)->snapshot);
    PPItem replacement=png_item(assets,"budget-1.png",96);
    CHECK(pp_scene_replace(scene,first,&replacement,5,&error));update(p,displays,400,300,5);
    CHECK(surface_for(first)==old && pp_item_snapshot_revision(pp_scene_get(scene,first)->snapshot)==revision && pp_anchor_presenter_gpu_resources(p).retained_bytes==full.retained_bytes);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads);
    CHECK(pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{800,300},96,0,1},6,&error));
    CHECK(pp_sprites_update(p,6,&error));CHECK(pp_scene_count(scene)==4 && pp_anchor_presenter_instance_count(p)==4);
    pp_scene_delete(scene,pp_scene_at(scene,3)->id);update(p,displays,400,300,7);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==201326592);
    CHECK(pp_scene_replace(scene,first,&replacement,8,&error));update(p,displays,400,300,8);
    CHECK(pp_item_snapshot_revision(pp_scene_get(scene,first)->snapshot)!=revision && surface_for(first)==old);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==201326592 && pp_anchor_presenter_gpu_resources(p).peak_bytes==268435456);
    pp_scene_clear(scene);update(p,displays,400,300,9);CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==0);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS PNG aggregate GPU limit charges retained/candidate overlap, preserves rejected scene, and releases on deletion/clear");
}
static void test_png_broken_context_releases_interceptor(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=png_item(assets,"alpha.png",96);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t a=png_pull(p,scene,displays,1),b=png_pull(p,scene,displays,2);PPSurface *first=surface_for(a),*second=surface_for(b);
    event(p,first,PP_EVENT_BUTTON_DOWN,400,300,2.4);CHECK(capture_owner==first);
    PPItem replacement=png_item(assets,"retained-blue.png",96);
    CHECK(pp_scene_replace(scene,a,&replacement,3,&error));fail_gl_context=true;update(p,displays,400,300,3);fail_gl_context=false;
    CHECK(!surface_for(a)&&!pp_scene_get(scene,a)&&!capture_owner&&surface_for(b)==second&&second->shown);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS failed direct upload/context releases the active interceptor and isolates another usable PNG");
}
static void test_png_empty_source_releases_input(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=png_item(assets,"alpha.png",96);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=png_pull(p,scene,displays,1);PPSurface *surface=surface_for(id);
    event(p,surface,PP_EVENT_BUTTON_DOWN,400,300,2);CHECK(capture_owner==surface);
    PPItem empty=png_item(assets,"retained-empty.png",96);CHECK(pp_scene_replace(scene,id,&empty,3,&error));
    update(p,displays,400,300,3);CHECK(!surface->shown&&!capture_owner&&pp_scene_get(scene,id)->pose.opacity==1);
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);PPSourceMetrics source=pp_assets_source_metrics(assets);
    PPPose pose=pp_scene_get(scene,id)->pose;pose.center.x+=20;CHECK(pp_scene_set_pose(scene,id,pose,&error));
    update(p,displays,420,300,4);update(p,displays,420,300,5);
    PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(p);
    CHECK(after.source_uploads==warm.source_uploads&&after.transform_draws==warm.transform_draws&&after.readbacks==warm.readbacks);
    CHECK(pp_assets_source_metrics(assets).raster_builds==source.raster_builds&&!surface->shown);
    CHECK(pp_scene_replace(scene,id,&item,6,&error));update(p,displays,420,300,6);CHECK(surface->shown&&surface==surface_for(id));
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS fully transparent PNG source hides/releases accepted input; hidden translation/static reuses visibility metadata");
}
static void test_png_candidate_preserves_svg_clock(void) {
    PPAssets *assets=NULL;PPError error={0};CHECK(pp_assets_create(&assets,&error));
    PPProject *project=test_arrow_project(assets);PPAnchorPresenter *p=NULL;
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay display={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(display));*p->displays=display;p->display_count=1;
    PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error);CHECK(sprite);
    uint64_t id=sprite->id;PPSurface *surface=sprite->surface;CHECK(pp_sprites_update(p,1,&error));
    size_t previous_gpu=pp_anchor_presenter_gpu_resources(p).retained_bytes;
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot);
    PPItem png=png_item(assets,"alpha.png",96);CHECK(pp_scene_replace(p->scene,id,&png,3,&error));
    pp_sprites_sync(p);fail_gl_upload=true;CHECK(pp_sprites_update(p,3,&error));fail_gl_upload=false;
    CHECK(surface_for(id)==surface&&surface->shown&&surface->window_id);
    CHECK(pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot)==revision);
    CHECK(pp_scene_get(p->scene,id)->animation_start==1);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==previous_gpu);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS rejected PNG candidate preserves independent retained SVG surface, immutable source and animation start");
}
static void test_gif_budget_sharing_and_png_transitions(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem png=png_item(assets,"budget-0.png",96),gif=gif_item(assets,"finite.gif");
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&png,NULL,&error));CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    p->displays=malloc(sizeof(displays));CHECK(p->displays);memcpy(p->displays,displays,sizeof(displays));p->display_count=2;
    /* Fill exactly the sprite budget. A visible retained anchor now correctly
     * charges its own source/target overlap and cannot be a free pull fixture. */
    uint64_t ids[4];for(int i=0;i<4;i++) {
      PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{400+i*100,300},96,0,1},1+i,&error);CHECK(sprite);
      ids[i]=sprite->id;CHECK(pp_sprites_update(p,1+i,&error));CHECK(pp_scene_count(scene)==(size_t)i+1);
    }
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(scene,ids[0])->snapshot);
    double accepted_start=pp_scene_get(scene,ids[0])->animation_start;
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);
    CHECK(pp_scene_replace(scene,ids[0],&gif,5,&error));update(p,displays,400,300,5);
    CHECK(pp_item_snapshot_revision(pp_scene_get(scene,ids[0])->snapshot)==revision && pp_scene_get(scene,ids[0])->animation_start==accepted_start);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==268435456 && pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads);
    pp_scene_delete(scene,ids[3]);update(p,displays,400,300,6);
    CHECK(pp_scene_replace(scene,ids[0],&gif,7,&error));CHECK(pp_scene_replace(scene,ids[1],&gif,7,&error));update(p,displays,400,300,7);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==67108876);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads+1);
    pp_scene_delete(scene,ids[0]);update(p,displays,400,300,7.01);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==67108876);
    PPItem small=png_item(assets,"retained-blue.png",96);CHECK(pp_scene_replace(scene,ids[1],&small,7.02,&error));update(p,displays,400,300,7.02);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==67108992 && surface_for(ids[1])->blue==255);
    pp_scene_clear(scene);update(p,displays,400,300,8);CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==0);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS bounded GIF candidate overlap, PNG/GIF transitions, unique sharing and reference cleanup");
}
static void test_gif_transparent_frame_releases_capture(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=gif_item(assets,"changing-alpha.gif");CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay d={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(d));*p->displays=d;p->display_count=1;
    PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error);CHECK(s);
    CHECK(pp_sprites_update(p,1,&error));PPSurface *surface=s->surface;CHECK(surface->shown);
    event(p,surface,PP_EVENT_BUTTON_DOWN,200,200,1.05);CHECK(capture_owner==surface);
    CHECK(pp_sprites_update(p,1.11,&error));CHECK(!surface->shown && !capture_owner && fabs(s->next_frame-1.2)<1e-8);
    event(p,surface,PP_EVENT_CANCEL,200,200,1.11);
    PPRendererMetrics hidden=pp_anchor_presenter_renderer_metrics(p);
    s->pose.center.x+=10;s->dirty=true;CHECK(pp_sprites_update(p,1.12,&error));
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==hidden.source_uploads && pp_anchor_presenter_renderer_metrics(p).transform_draws==hidden.transform_draws);
    CHECK(pp_sprites_update(p,1.21,&error));CHECK(surface->shown && surface->red==255 && fabs(s->next_frame-1.3)<1e-8);
    CHECK(!s->dragging && !s->interaction.pressed);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS transparent GIF frame hides/releases input and retains the next independent animation deadline");
}
static void test_gif_rejected_replacement_preserves_clock(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=gif_item(assets,"finite.gif");CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay d={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(d));*p->displays=d;p->display_count=1;
    PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error);CHECK(s);uint64_t id=s->id;
    CHECK(pp_sprites_update(p,1,&error));PPSurface *surface=s->surface;
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot);
    PPItem replacement=gif_item(assets,"changing-alpha.gif");CHECK(pp_scene_replace(p->scene,id,&replacement,1.02,&error));
    pp_sprites_sync(p);fail_gl_upload=true;CHECK(pp_sprites_update(p,1.02,&error));fail_gl_upload=false;
    CHECK(pp_scene_get(p->scene,id)->animation_start==1 && pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot)==revision);
    CHECK(surface==s->surface && surface->shown && surface->red==255 && fabs(s->next_frame-1.05)<1e-8);
    CHECK(pp_sprites_update(p,1.08,&error));CHECK(surface->red==0 && s->retained.source_revision==2 && fabs(s->next_frame-1.1)<1e-8);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==12);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS rejected GIF replacement retains old source/start and continues its accepted deadlines");
}
static void test_canonical_source_mapping(void) {
    PPError error={0};PPAnchorPresenter p={0};PPSurface *surface=NULL;
    CHECK(pp_assets_create(&p.assets,&error));
    CHECK(pp_surface_create_gl(&surface,&error));PPRetainedPresentation retained={0};
    uint8_t pixels[4]={255,0,0,255};PPFrame frame={pixels,NULL,1,1,4};
    PPSourcePresentation source={.frame=&frame,.content_id=1,.source_revision=1,
        .layout={50,100},.visual_alpha=true,.source_rect={-5,1,50,99}};
    PPDisplay display={1,{0,0,1000,1000},1,1};PPPose pose={{200,300},100,0,1};
    CHECK(pp_retained_present(&p,surface,&retained,source,pose,&display,&error));
    CHECK(drawn_source_rect.x==-5 && drawn_source_rect.y==1 && drawn_source_rect.w==50 && drawn_source_rect.h==99);
    CHECK(surface->bounds.x==170 && surface->bounds.y==250 && surface->bounds.w==60 && surface->bounds.h==100);
    CHECK(retained.drawn.center.x==200 && retained.drawn.center.y==300 && retained.layout.x==50);
    source.frame=NULL;source.source_rect=(PPRect){0,0,50,100};
    CHECK(pp_retained_present(&p,surface,&retained,source,pose,&display,&error));
    CHECK(drawn_source_rect.x==0 && drawn_source_rect.y==0 && drawn_source_rect.h==100);
    CHECK(surface->bounds.x==175 && surface->bounds.w==50 && retained.drawn.longest_dip==100);
    CHECK(pp_renderer_metrics(retained.renderer).source_uploads==1);
    pp_retained_release(&p,&retained);pp_surface_destroy(surface);
    pp_content_sources_destroy(p.sources);pp_assets_destroy(p.assets);
    puts("PASS source origin mapping preserves canonical center/aspect and includes overhang without reupload");
}
static bool svg_lifecycle;
static PPItem svg_item(PPAssets *assets) {
    const char *svg="<svg width='13' height='7' viewBox='0 0 13 7'><rect x='2' y='1' width='9' height='5' fill='red'/></svg>";
    PPError error={0};PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));return item;
}
static PPItem scalable_item(PPAssets *assets) {
    if(svg_lifecycle)return svg_item(assets);
    FILE *file=fopen(PP_FONT_FIXTURES "/PointyOverhang.ttf","rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0);rewind(file);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes);CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    PPError error={0};PPItem item={.kind=PP_ITEM_TEXT,.text="A\xcc\x81\nAj",.text_style={.requested_family="Pointy Overhang",.font_size_dip=96,.color={30,120,230,210}},.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)length},PP_ASSET_FONT,&item.text_style.primary_asset,&error));free(bytes);return item;
}
static void test_async_latest_text_resize(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);PPSurface *sprite=surface_for(id);CHECK(sprite&&sprite->shown);
    PPSourceMetrics before=pp_assets_source_metrics(assets);PPPresentationMetrics metrics=pp_anchor_presenter_metrics(p);
    PPRendererMetrics renderer_before=pp_anchor_presenter_renderer_metrics(p);
    PPPlatformEvent wheel={.surface=sprite,.instance_id=id,.kind=PP_EVENT_WHEEL,.desktop_point={400,300},.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));wheel.timestamp=2.01;CHECK(pp_anchor_presenter_event(p,&wheel,&error));
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==metrics.worker_requests+2);
    CHECK(pp_assets_source_metrics(assets).raster_builds==before.raster_builds);
    update(p,displays,400,300,2.015);
    for(int i=0;i<1000 && pp_anchor_presenter_metrics(p).sharp_publications==metrics.sharp_publications;i++) {SDL_Delay(1);update(p,displays,400,300,2.015);}
    PPPresentationMetrics sharp=pp_anchor_presenter_metrics(p);CHECK(sharp.sharp_publications==metrics.sharp_publications+1);
    CHECK(sharp.last_sharp_input_timestamp==2.01 && sharp.last_sharp_instance_id==id && sharp.last_sharp_drawable_density==1);
    CHECK(pp_scene_get(scene,id)->pose.longest_dip<sharp.last_sharp_longest_dip);
    CHECK(sharp.worker_source_builds>metrics.worker_source_builds && sharp.worker_pending_jobs==0);
    CHECK(sprite==surface_for(id) && sprite->shown && sharp.scalable_retained_bytes>0);
    CHECK(pp_anchor_presenter_renderer_metrics(p).readbacks==renderer_before.readbacks);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(pp_assets_external_raster_bytes(assets)==0);pp_assets_destroy(assets);
    puts("PASS latest resize starts during input, uses isolated source work and publishes before easing settles");
}
static void wait_worker_idle(PPAnchorPresenter *p,PPDisplay *displays,double now) {
    unsigned idle=0;
    for(int i=0;i<2000&&idle<2;i++) {SDL_Delay(1);update(p,displays,400,300,now);idle=pp_anchor_presenter_metrics(p).worker_pending_jobs?0:idle+1;}
    CHECK(idle==2);
}
static void test_new_scalable_pull_survives_busy_worker(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));hold_next_worker();
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t first=source_sprite(p,scene,displays,1);CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    uint64_t second=source_sprite(p,scene,displays,2);CHECK(pp_scene_count(scene)==2);
    /* Equal targets now join one preparation. Give the second consumer a
     * distinct endpoint to exercise a genuinely occupied waiting slot. */
    PPPose second_pose=pp_scene_get(scene,second)->pose;second_pose.longest_dip=128;
    CHECK(pp_scene_set_pose(scene,second,second_pose,&error));pp_sprites_sync(p);CHECK(pp_sprites_update(p,2.3,&error));
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
    uint64_t third=source_sprite(p,scene,displays,3);
    CHECK(pp_scene_count(scene)==3&&first!=second&&second!=third);
    CHECK(surface_for(first)->shown&&surface_for(second)->shown&&surface_for(third)->shown);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==before.source_uploads+1);
    CHECK(pp_anchor_presenter_metrics(p).worker_pending_jobs==2);
    update(p,displays,400,300,3.5);
    PPRendererMetrics waiting=pp_anchor_presenter_renderer_metrics(p);
    for(int i=0;i<8;i++)update(p,displays,400,300,3.5+i*.01);
    PPRendererMetrics unchanged=pp_anchor_presenter_renderer_metrics(p);
    CHECK(unchanged.source_uploads==waiting.source_uploads&&unchanged.transform_draws==waiting.transform_draws);
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==2);
    double latest=pp_scene_get(scene,third)->pose.longest_dip*1.21;
    PPPlatformEvent wheel={.surface=surface_for(third),.instance_id=third,.kind=PP_EVENT_WHEEL,
      .wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=3.8};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));wheel.timestamp=3.81;CHECK(pp_anchor_presenter_event(p,&wheel,&error));
    update(p,displays,400,300,3.94);
    CHECK(fabs(pp_scene_get(scene,third)->pose.longest_dip-latest)<1e-7);
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==2&&pp_anchor_presenter_metrics(p).worker_pending_jobs==2);
    release_worker();
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_source_builds<3;i++){SDL_Delay(1);update(p,displays,400,300,3.94);}
    CHECK(pp_anchor_presenter_metrics(p).worker_source_builds==3);
    wait_worker_idle(p,displays,3.94);finish_worker_control();
    CHECK(pp_scene_count(scene)==3&&surface_for(third)->shown);
    PPPresentationMetrics sharp=pp_anchor_presenter_metrics(p);
    CHECK(sharp.last_sharp_instance_id==third&&sharp.last_sharp_input_timestamp==3.81&&fabs(sharp.last_sharp_longest_dip-latest)<1e-7);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);
    CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS new scalable pull keeps canonical artwork when the worker is busy");
}
static void test_zero_opacity_cancels_only_its_source_target(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t hidden=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);
    PPSprite *other=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{600,300},96,0,1},1.4,&error);CHECK(other);
    uint64_t sibling=other->id;CHECK(pp_sprites_update(p,1.4,&error));
    hold_next_worker();PPPlatformEvent wheel={.surface=other->surface,.instance_id=sibling,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    event(p,other->surface,PP_EVENT_BUTTON_DOWN,600,300,2.01);CHECK(capture_owner==other->surface);
    uint64_t requests=pp_anchor_presenter_metrics(p).worker_requests;
    PPPose pose=pp_scene_get(scene,hidden)->pose;pose.opacity=0;pose.longest_dip=384;CHECK(pp_scene_set_pose(scene,hidden,pose,&error));
    pp_sprites_sync(p);CHECK(pp_sprites_update(p,2.01,&error));
    CHECK(!surface_for(hidden)->shown&&capture_owner==surface_for(sibling));
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==requests);
    CHECK(pp_content_requested(pp_sprites_find(p,sibling)->content));
    release_worker();wait_worker_idle(p,displays,2.2);finish_worker_control();
    CHECK(surface_for(sibling)->shown&&!surface_for(hidden)->shown);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS zero-opacity size target starts no preparation and preserves another consumer's pending source and accepted input");
}
static void test_cached_source_survives_compatibility_request(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));hold_next_worker();
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPSprite *s=pp_sprites_find(p,id);PPRect mapping=s->retained.source_rect;PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
    const PPFrame *frame=NULL;double next;CHECK(pp_item_snapshot_frame(pp_scene_get(scene,id)->snapshot,0,3,&frame,&next,&error));
    PPPose pose=pp_scene_get(scene,id)->pose;pose.angle_deg=23.5;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,2);
    CHECK(!memcmp(&mapping,&s->retained.source_rect,sizeof(mapping)));
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==before.source_uploads);
    release_worker();wait_worker_idle(p,displays,2);finish_worker_control();
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS available scalable raster keeps its own mapping after compatibility snapshot raster changes");
}
static void test_completed_target_is_revalidated_after_external_resize(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);PPPresentationMetrics metrics=pp_anchor_presenter_metrics(p);
    hold_next_worker();PPPlatformEvent wheel={.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));release_worker();
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++)SDL_Delay(1);
    CHECK(!pp_anchor_presenter_metrics(p).worker_pending_jobs);
    PPPose pose=pp_scene_get(scene,id)->pose;pose.longest_dip=64;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,2.01);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==before.source_uploads);
    CHECK(pp_anchor_presenter_metrics(p).worker_stale_results==metrics.worker_stale_results+1);
    wait_worker_idle(p,displays,2.01);finish_worker_control();
    CHECK(pp_scene_get(scene,id)->pose.longest_dip==64);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS completed raster cannot publish after an externally replaced endpoint");
}
static void test_scalable_rapid_resize_drag_density_and_cancellation(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);
    PPRasterStatus initial=pp_raster_worker_status(p->worker);PPRendererMetrics renderer=pp_anchor_presenter_renderer_metrics(p);
    uint64_t owner_builds=pp_assets_source_metrics(assets).raster_builds;
    hold_next_worker();PPPlatformEvent wheel={.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    for(int i=1;i<20;i++) {wheel.timestamp=2+i*.001;CHECK(pp_anchor_presenter_event(p,&wheel,&error));}
    PPRasterStatus blocked=pp_raster_worker_status(p->worker);CHECK(blocked.pending_jobs==2&&blocked.requests==initial.requests+20&&blocked.capture_bytes<=64u*1024*1024);
    event(p,surface,PP_EVENT_BUTTON_DOWN,pp_scene_get(scene,id)->pose.center.x+10,pp_scene_get(scene,id)->pose.center.y,2.02);event(p,surface,PP_EVENT_POINTER_MOVE,430,320,2.025);
    update(p,displays,430,320,2.04);PPSprite *s=pp_sprites_find(p,id);double logical_grab=10*s->pose.longest_dip/s->ease_from.longest_dip;
    CHECK(fabs(s->pose.center.x+logical_grab-430)<1e-7&&capture_owner==surface);
    displays[1].desktop_units_per_dip=1.5;displays[1].drawable_pixels_per_dip=1.5;update(p,displays,430,320,2.04);
    CHECK(fabs(s->pose.center.x+logical_grab*1.5-430)<1e-7&&capture_owner==surface);
    CHECK(pp_assets_source_metrics(assets).raster_builds==owner_builds);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==renderer.source_uploads);
    release_worker();wait_worker_idle(p,displays,2.04);finish_worker_control();
    PPPresentationMetrics ready=pp_anchor_presenter_metrics(p);CHECK(ready.last_sharp_drawable_density==1.5&&ready.last_sharp_input_timestamp==2.019);
    CHECK(ready.worker_stale_results>=1&&ready.worker_source_builds==initial.source_builds+2&&ready.worker_reserved_bytes==0);
    event(p,surface,PP_EVENT_BUTTON_UP,430,320,2.05);CHECK(!capture_owner);
    /* Replacement invalidates an executing capture without sharing the edited
     * snapshot, and deletion releases every queued owner reservation. */
    hold_next_worker();wheel.timestamp=3;CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPItem replacement=scalable_item(assets);replacement.text="jA";replacement.text_style.color[0]=220;
    if(svg_lifecycle) {
        const char *blue="<svg width='13' height='7'><rect x='2' y='1' width='9' height='5' fill='blue'/></svg>";
        CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)blue,strlen(blue)},PP_ASSET_SVG,&replacement.image_asset,&error));
    }
    CHECK(pp_scene_replace(scene,id,&replacement,3.01,&error));update(p,displays,430,320,3.01);
    uint64_t revision=pp_item_snapshot_content_id(pp_scene_get(scene,id)->snapshot);
    release_worker();wait_worker_idle(p,displays,3.01);finish_worker_control();
    CHECK(pp_sprites_find(p,id)->retained.content_id==revision);
    if(svg_lifecycle)CHECK(surface_for(id)->blue==255&&surface_for(id)->red==0);
    CHECK(pp_anchor_presenter_metrics(p).worker_source_builds==ready.worker_source_builds+2);
    hold_next_worker();wheel.timestamp=4;CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPPlatformEvent remove={.surface=surface,.instance_id=id,.kind=PP_EVENT_BUTTON_DOWN,.button=PP_BUTTON_RIGHT,.timestamp=4.01};CHECK(pp_anchor_presenter_event(p,&remove,&error));
    CHECK(!pp_scene_count(scene)&&!pp_anchor_presenter_gpu_resources(p).retained_bytes&&pp_raster_worker_status(p->worker).reserved_bytes==0);
    release_worker();wait_worker_idle(p,displays,4.01);finish_worker_control();
    CHECK(pp_anchor_presenter_metrics(p).worker_source_builds==ready.worker_source_builds+3);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS twenty immediate wheel targets coalesce while dragging/density changes, stale success counts survive replacement/deletion, and reservations release");
}
static void test_scalable_budget_failure_and_project_teardown(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);PPSprite *s=pp_sprites_find(p,id);
    /* Let the source anchor finish its fade before measuring the stable scene.
     * Hidden previews release their owned raster/binding resources. */
    update(p,displays,400,300,1.5);
    size_t retained=s->scalable_bytes,baseline=pp_assets_external_raster_bytes(assets);PPPose usable=s->pose;PPRendererMetrics renderer=pp_anchor_presenter_renderer_metrics(p);
    /* Fill the combined retained CPU budget using valid reservations. */
    size_t fill=512u*1024*1024-pp_assets_raster_bytes(assets);CHECK(fill>256u*1024*1024);
    CHECK(pp_assets_reserve_raster(assets,256u*1024*1024,&error));CHECK(pp_assets_reserve_raster(assets,fill-256u*1024*1024,&error));
    PPPlatformEvent wheel={.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(p->notice.code==PP_ERROR_RESOURCE_LIMIT);
    update(p,displays,400,300,2.01);CHECK(surface->shown&&!memcmp(&usable,&s->pose,sizeof(usable))&&s->scalable_bytes==retained);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==renderer.source_uploads&&pp_raster_worker_status(p->worker).reserved_bytes==0);
    pp_assets_release_raster(assets,fill);CHECK(pp_assets_external_raster_bytes(assets)==baseline);
    /* Failed source generation preserves the usable CPU/GPU source and does
     * not turn an existing visible sprite into an interceptor without pixels. */
    SDL_SetAtomicInt(&fail_next_worker,1);wheel.timestamp=3;CHECK(pp_anchor_presenter_event(p,&wheel,&error));
    CHECK(fabs(s->raster_target.longest_dip-usable.longest_dip*1.1)<1e-7);wait_worker_idle(p,displays,3.2);
    CHECK(p->notice.code==PP_ERROR_MEMORY&&surface->shown&&s->scalable_bytes==retained);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==renderer.source_uploads&&pp_raster_worker_status(p->worker).reserved_bytes==0);
    /* Runtime ownership survives a project change. Presenter teardown cancels
     * and releases its owner state while a detached immutable capture is held. */
    hold_next_worker();wheel.timestamp=4;CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPRasterWorker *service=p->worker;p->borrowed_worker=true;
    event(p,surface,PP_EVENT_BUTTON_DOWN,400,300,4.01);CHECK(capture_owner==surface);
    pp_anchor_presenter_destroy(p);p=NULL;CHECK(!capture_owner&&!surfaces&&pp_raster_worker_status(service).reserved_bytes==0);
    pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);assets=NULL;
    release_worker();pp_raster_worker_destroy(service);finish_worker_control();
    puts("PASS candidate pressure and generation failure preserve usable source, project teardown releases capture and assets before worker completion");
}
static void test_scalable_publication_failure_releases_interceptor(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);
    hold_next_worker();PPPlatformEvent wheel={.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    event(p,surface,PP_EVENT_BUTTON_DOWN,400,300,2.01);CHECK(capture_owner==surface);fail_gl_context=true;
    release_worker();wait_worker_idle(p,displays,2.01);finish_worker_control();fail_gl_context=false;
    CHECK(!capture_owner&&!pp_scene_get(scene,id)&&!pp_anchor_presenter_instance_count(p)&&!pp_anchor_presenter_gpu_resources(p).retained_bytes);
    CHECK(pp_raster_worker_status(p->worker).reserved_bytes==0);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS failed asynchronous GL publication deletes the failed interceptor and releases input, source and candidate budgets");
}
static PPAssetId font_asset(PPAssets *assets,const char *path) {
    size_t length=0;void *bytes=SDL_LoadFile(path,&length);CHECK(bytes&&length);PPAssetId id={0};PPError error={0};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,length},PP_ASSET_FONT,&id,&error));SDL_free(bytes);return id;
}
static void test_settled_worker_matches_fresh_text(void) {
    for(int kind=0;kind<3;kind++) {
        PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
        CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
        PPItem item=scalable_item(assets);
        if(kind==0) {item.text="\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd\n\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";item.text_style.requested_family="Twemoji Mozilla";
            item.text_style.primary_asset=font_asset(assets,PP_MEDIA_FIXTURES "/../../../resources/optional-fonts/Twemoji.Mozilla-v0.7.0.ttf");}
        if(kind==1) {item.text="A\xcc\x81j\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\nA";
            item.text_style.fallback_asset=font_asset(assets,PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf");}
        if(kind==2) {item.text="AB\nA";item.text_style.requested_family="Pointy Hybrid";item.text_style.font_size_dip=16;
            item.text_style.primary_asset=font_asset(assets,PP_FONT_FIXTURES "/PointyHybrid.ttf");}
        CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
        PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1.5}};
        uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);
        PPPose pose=pp_scene_get(scene,id)->pose;pose.longest_dip=768;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,2);wait_worker_idle(p,displays,2);
        const PPSceneInstance *instance=pp_scene_get(scene,id);PPSprite *sprite=pp_sprites_find(p,id);PPVec2 canonical=pp_item_snapshot_size(instance->snapshot);
        PPTextResult fresh={0};CHECK(pp_text_render_result(assets,item.text,&item.text_style,768*1.5/fmax(canonical.x,canonical.y),&fresh,NULL,&error));
        const PPTextResult *cached=&sprite->scalable;CHECK(cached->frame.width==fresh.frame.width&&cached->frame.height==fresh.frame.height);
        CHECK(!memcmp(cached->frame.pixels,fresh.frame.pixels,fresh.frame.stride*fresh.frame.height));CHECK(!memcmp(cached->frame.content_alpha,fresh.frame.content_alpha,(size_t)fresh.frame.width*fresh.frame.height));
        CHECK(!memcmp(&cached->layout_size,&canonical,sizeof(canonical))&&!memcmp(&cached->layout_origin,&fresh.layout_origin,sizeof(fresh.layout_origin))&&!memcmp(&cached->raster_origin_px,&fresh.raster_origin_px,sizeof(fresh.raster_origin_px)));
        CHECK(cached->artwork.scalable_glyphs&&((kind==0)==(cached->artwork.bitmap_glyphs==0)));
        size_t edge=0;for(size_t i=0;i<(size_t)fresh.frame.width*fresh.frame.height;i++) {const uint8_t *pixel=fresh.frame.pixels+i*4;CHECK(pixel[0]<=pixel[3]&&pixel[1]<=pixel[3]&&pixel[2]<=pixel[3]&&pixel[3]==fresh.frame.content_alpha[i]);edge+=pixel[3]>0&&pixel[3]<item.text_style.color[3];}CHECK(edge);
        PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);PPRasterStatus worker=pp_raster_worker_status(p->worker);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
        pose.angle_deg=17.25;pose.opacity=.63;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,400,300,3);
        pose.center.x+=37;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,437,300,4);update(p,displays,437,300,5);
        PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(p);CHECK(after.source_uploads==before.source_uploads&&after.transform_draws==before.transform_draws+1&&after.readbacks==before.readbacks);
        CHECK(pp_raster_worker_status(p->worker).source_builds==worker.source_builds&&pp_assets_source_metrics(assets).raster_builds==builds);
        CHECK(pp_scene_get(scene,id)->pose.center.x==pose.center.x&&pp_item_snapshot_size(instance->snapshot).y==canonical.y);
        pp_frame_destroy(&fresh.frame);pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    }
    puts("PASS settled isolated color, mixed cluster/overhang and hybrid outline/bitmap sources exactly match fresh requested-resolution RGBA/coverage and reuse rotation/opacity/translation");
}
static void test_bitmap_text_transform_reuses_useful_source(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    FILE *file=fopen(PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf","rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0);rewind(file);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes);CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    PPItem item={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,
        .text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)length},PP_ASSET_FONT,&item.text_style.fallback_asset,&error));free(bytes);
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    PPSprite *created=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{400,300},1536,0,1},1,&error);CHECK(created);uint64_t id=created->id;
    PPPose pose=pp_scene_get(scene,id)->pose;pose.longest_dip=1536;CHECK(pp_scene_set_pose(scene,id,pose,&error));
    uint64_t prepared=pp_assets_source_metrics(assets).raster_builds;update(p,displays,400,300,1);
    PPSurface *sprite=surface_for(id);CHECK(sprite&&sprite->window_id&&sprite->source_width==136);
    CHECK(pp_assets_source_metrics(assets).raster_builds==prepared+1);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==69632);
    CHECK(pp_assets_external_raster_bytes(assets)==137437); /* Snapshot, useful RGBA and presenter's five-byte anchor target. */
    CHECK(fabs(drawn_source_rect.w-119.77981651376147)<1e-9&&fabs(drawn_source_rect.h-112.73394495412845)<1e-9);
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    update(p,displays,400,300,1.1);
    pose.center.x+=10;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,410,300,1.2);
    PPRendererMetrics moved=pp_anchor_presenter_renderer_metrics(p);
    CHECK(moved.source_uploads==warm.source_uploads&&moved.transform_draws==warm.transform_draws&&moved.readbacks==warm.readbacks);
    PPPlatformEvent wheel={.surface=sprite,.instance_id=id,.kind=PP_EVENT_WHEEL,.modifiers=PP_MOD_ALT,.wheel_delta=1,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));
    for(int i=1;i<=14;i++) update(p,displays,410,300,2+i*.01);
    CHECK(pp_scene_get(scene,id)->pose.longest_dip>1536&&sprite->source_width==136);
    PPRendererMetrics eased=pp_anchor_presenter_renderer_metrics(p);
    CHECK(eased.source_uploads==warm.source_uploads&&eased.transform_draws>warm.transform_draws&&eased.readbacks==warm.readbacks);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds&&pp_assets_external_raster_bytes(assets)==137437);
    event(p,sprite,PP_EVENT_BUTTON_DOWN,430,300,3);CHECK(capture_owner==sprite);
    event(p,sprite,PP_EVENT_POINTER_MOVE,600,300,3.01);displays[1].desktop_units_per_dip=1.5;displays[1].drawable_pixels_per_dip=1.5;
    update(p,displays,600,300,3.02);CHECK(fabs(pp_scene_get(scene,id)->pose.center.x-570)<1e-8);
    event(p,sprite,PP_EVENT_BUTTON_UP,600,300,3.03);CHECK(!capture_owner);
    pose=pp_scene_get(scene,id)->pose;pose.angle_deg=23.5;pose.opacity=.5;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,600,300,4);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds&&pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads);
    PPVec2 canonical=pp_item_snapshot_size(pp_scene_get(scene,id)->snapshot);CHECK(canonical.x==120&&canonical.y==113);
    CHECK(pp_scene_get(scene,id)->pose.angle_deg==23.5&&pp_scene_get(scene,id)->pose.opacity==.5);
    PPPose usable=pp_scene_get(scene,id)->pose;PPRect usable_bounds=sprite->bounds;
    pose.longest_dip=4096;pose.angle_deg=45;displays[1].drawable_pixels_per_dip=3;
    CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,600,300,5);
    CHECK(!memcmp(&pp_scene_get(scene,id)->pose,&usable,sizeof(usable))&&!memcmp(&sprite->bounds,&usable_bounds,sizeof(usable_bounds)));
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds&&pp_anchor_presenter_gpu_resources(p).retained_bytes==69632);
    displays[1].drawable_pixels_per_dip=1.5;uint64_t revision=pp_item_snapshot_revision(pp_scene_get(scene,id)->snapshot);
    item.text="\xf0\x9f\x91\x88";
    CHECK(pp_scene_replace(scene,id,&item,6,&error));fail_gl_upload=true;update(p,displays,600,300,6);fail_gl_upload=false;
    CHECK(pp_item_snapshot_revision(pp_scene_get(scene,id)->snapshot)==revision&&surface_for(id)==sprite&&sprite->shown);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==69632&&pp_assets_external_raster_bytes(assets)==137437);
    for(int i=0;i<16;i++) {
        item.text=i%2?"\xf0\x9f\x91\x89":"\xf0\x9f\x91\x88";
        CHECK(pp_scene_replace(scene,id,&item,7+i,&error));update(p,displays,600,300,7+i);
        CHECK(surface_for(id)==sprite&&sprite->source_width==136&&pp_anchor_presenter_gpu_resources(p).retained_bytes==69632);
        CHECK(pp_assets_external_raster_bytes(assets)==274869); /* Project's shared original and one live replacement. */
    }
    CHECK(pp_anchor_presenter_gpu_resources(p).peak_bytes==139264);
    pose=pp_scene_get(scene,id)->pose;event(p,sprite,PP_EVENT_BUTTON_DOWN,pose.center.x,pose.center.y,24);CHECK(capture_owner==sprite);
    pose.opacity=0;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,600,300,25);CHECK(!sprite->shown&&!capture_owner);
    pose.opacity=1;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,600,300,26);
    event(p,sprite,PP_EVENT_BUTTON_DOWN,pose.center.x,pose.center.y,26.1);CHECK(capture_owner==sprite);
    failed_gl_surface=sprite;pose.angle_deg+=10;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,600,300,27);failed_gl_surface=NULL;
    CHECK(!pp_scene_get(scene,id)&&!surface_for(id)&&!capture_owner&&pp_anchor_presenter_gpu_resources(p).retained_bytes==0);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS bitmap text useful source survives large fractional easing, static/translation, rotation, density and grabbed-point changes without raster/upload/readback work");
}
static void test_bitmap_replacement_matches_fresh_canonical_source(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    size_t length=0;void *bytes=SDL_LoadFile(PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf",&length);CHECK(bytes);
    PPItem item={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,
        .text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,length},PP_ASSET_FONT,&item.text_style.fallback_asset,&error));SDL_free(bytes);
    uint64_t item_id=0;CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,&item_id,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1.5,1.5}};
    uint64_t id=png_pull(p,scene,displays,1);
    update(p,displays,500,400,1);
    for(int change=0;change<2;change++) {
        item.id=item_id;item.text_style.font_size_dip=change?96:192;
        CHECK(pp_project_edit_item(project,item_id,&item,&error));
        PPItemSnapshot *replacement=pp_project_snapshot(project,item_id);CHECK(replacement);
        const PPTextResult *fresh=NULL;CHECK(pp_item_snapshot_bitmap_source(replacement,&fresh,&error));
        CHECK(fresh->frame.width==136&&fresh->frame.height==128);
        for(int rotation=0;rotation<2;rotation++) {
            PPPose pose={{500,400},384,rotation?17.25:0,.5};
            CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,500,400,2+change*3+rotation);
            PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
            CHECK(pp_scene_replace_snapshot(scene,id,replacement,2+change*3+rotation,&error));
            update(p,displays,500,400,2+change*3+rotation);
            CHECK(!memcmp(&pp_scene_get(scene,id)->pose,&pose,sizeof(pose)));
            CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==before.source_uploads);
            PPRect replaced_bounds=surface_for(id)->bounds,replaced_artwork=surface_for(id)->artwork_bounds;
            uint64_t fresh_id=png_pull(p,scene,displays,2+change*3+rotation);
            CHECK(pp_scene_set_pose(scene,fresh_id,pose,&error));update(p,displays,500,400,2+change*3+rotation);
            PPRect fresh_bounds=surface_for(fresh_id)->bounds,fresh_artwork=surface_for(fresh_id)->artwork_bounds;
            printf("BITMAP replacement font=%.0f angle=%.2f replaced_artwork=%.3f,%.3f %.3fx%.3f fresh_artwork=%.3f,%.3f %.3fx%.3f retained_source=136x128 uploads_unchanged=1\n",
                item.text_style.font_size_dip,pose.angle_deg,replaced_artwork.x,replaced_artwork.y,replaced_artwork.w,replaced_artwork.h,
                fresh_artwork.x,fresh_artwork.y,fresh_artwork.w,fresh_artwork.h);
            CHECK(!memcmp(&replaced_bounds,&fresh_bounds,sizeof(fresh_bounds)));
            CHECK(!memcmp(&replaced_artwork,&fresh_artwork,sizeof(replaced_artwork)));
            pp_scene_delete(scene,fresh_id);update(p,displays,500,400,2+change*3+rotation);
        }
        pp_item_snapshot_release(replacement);
    }
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS same-artwork bitmap replacement preserves pose/detail/upload identity and matches fresh canonical placement at both font sizes and rotations");
}
static void test_bitmap_candidate_upload_preserves_usable_source(void) {
    PPError error={0};PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));PPProject *project=test_arrow_project(assets);PPAnchorPresenter *p=NULL;
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    PPSprite *created=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{400,300},96,0,1},1,&error);CHECK(created);uint64_t id=created->id;
    update(p,displays,400,300,1);wait_worker_idle(p,displays,1);PPSurface *usable=surface_for(id);CHECK(usable&&usable->shown&&usable->window_id);
    size_t previous_gpu=pp_anchor_presenter_gpu_resources(p).retained_bytes;
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot);
    FILE *file=fopen(PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf","rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);rewind(file);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes&&fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    PPItem bitmap={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,.text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)length},PP_ASSET_FONT,&bitmap.text_style.fallback_asset,&error));free(bytes);
    CHECK(pp_scene_replace(p->scene,id,&bitmap,2,&error));fail_gl_upload=true;update(p,displays,400,300,2);fail_gl_upload=false;
    CHECK(pp_scene_get(p->scene,id)&&surface_for(id)==usable&&usable->shown);
    CHECK(pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot)==revision&&pp_scene_get(p->scene,id)->animation_start==1);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==previous_gpu);
    CHECK(pp_scene_replace(p->scene,id,&bitmap,3,&error));update(p,displays,400,300,3);CHECK(surface_for(id)->window_id&&surface_for(id)->source_width==136);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS rejected bitmap GPU candidate preserves independent usable SVG source/clock and releases candidate ownership before successful retry");
}
static void test_svg_endpoint_worker_reuses_cached_artwork(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    const char *svg="<svg width='13' height='7' viewBox='0 0 13 7'><rect x='2' y='1' width='9' height='5' fill='red'/></svg>";
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);
    CHECK(surface->window_id&&pp_anchor_presenter_metrics(p).worker_source_builds==1);
    PPSprite *sprite=pp_sprites_find(p,id);PPPose pose=sprite->pose;PPVec2 canonical=pp_item_snapshot_size(pp_scene_get(scene,id)->snapshot);
    CHECK(sprite->scalable.frame.width==96&&sprite->scalable.frame.height==52);
    PPRendererMetrics renderer=pp_anchor_presenter_renderer_metrics(p);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    PPPlatformEvent wheel={.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=.5,.modifiers=PP_MOD_ALT,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));update(p,displays,400,300,2.01);update(p,displays,400,300,2.02);
    wait_worker_idle(p,displays,2.02);
    CHECK(pp_anchor_presenter_metrics(p).sharp_publications==1&&sprite->resizing);
    CHECK(pp_anchor_presenter_metrics(p).last_sharp_input_timestamp==2);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
    CHECK(sprite->retained.layout.x==canonical.x&&sprite->retained.layout.y==canonical.y);
    CHECK(sprite->retained.source_rect.x==0&&sprite->retained.source_rect.y==0&&sprite->retained.source_rect.w==canonical.x&&sprite->retained.source_rect.h==canonical.y);
    CHECK(sprite->pose.center.x==pose.center.x&&sprite->pose.center.y==pose.center.y);
    CHECK(pp_anchor_presenter_renderer_metrics(p).readbacks==renderer.readbacks);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS SVG endpoint worker publishes during easing while preserving cached viewport, canonical aspect, center and owner source reuse");
}
static void test_svg_shared_asset_keeps_independent_sizes(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=svg_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t first=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);
    uint64_t second=source_sprite(p,scene,displays,2);wait_worker_idle(p,displays,2.3);
    PPPose pose=pp_scene_get(scene,second)->pose;pose.longest_dip=337;CHECK(pp_scene_set_pose(scene,second,pose,&error));
    update(p,displays,400,300,3);wait_worker_idle(p,displays,3);
    PPSprite *a=pp_sprites_find(p,first),*b=pp_sprites_find(p,second);
    CHECK(a->scalable.frame.width==96&&a->scalable.frame.height==52&&b->scalable.frame.width==337&&b->scalable.frame.height==182);
    CHECK(a->scalable.frame.pixels!=b->scalable.frame.pixels&&a->scalable_bytes==24960&&b->scalable_bytes==306670);
    const PPFrame *fresh=NULL;double next;CHECK(pp_item_snapshot_frame(pp_scene_get(scene,first)->snapshot,0,337.0/96,&fresh,&next,&error));
    CHECK(!memcmp(b->scalable.frame.pixels,fresh->pixels,fresh->stride*fresh->height));
    CHECK(a->scalable.frame.width==96&&surface_for(first)->source_width==96&&surface_for(second)->source_width==337);
    uint64_t owner_builds=pp_assets_source_metrics(assets).raster_builds;PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
    pose.angle_deg=17.25;pose.opacity=.63;CHECK(pp_scene_set_pose(scene,second,pose,&error));update(p,displays,400,300,4);
    pose.center.x+=37;CHECK(pp_scene_set_pose(scene,second,pose,&error));update(p,displays,437,300,5);
    CHECK(pp_assets_source_metrics(assets).raster_builds==owner_builds&&pp_anchor_presenter_renderer_metrics(p).source_uploads==before.source_uploads);
    CHECK(b->retained.layout.x==a->retained.layout.x&&b->retained.layout.y==a->retained.layout.y);
    /* A candidate of different dimensions overlaps the old CPU and GPU state.
     * A bounded GPU rejection leaves both old sources and native windows usable. */
    size_t baseline=pp_assets_external_raster_bytes(assets);
    hold_next_worker();PPPlatformEvent wheel={.surface=surface_for(second),.instance_id=second,.kind=PP_EVENT_WHEEL,.wheel_delta=1,.modifiers=PP_MOD_ALT,.timestamp=6};
    CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    CHECK(pp_assets_external_raster_bytes(assets)==baseline+pp_raster_worker_status(p->worker).reserved_bytes);
    size_t old_gpu=pp_anchor_presenter_gpu_resources(p).retained_bytes;CHECK(old_gpu==265304);
    p->gpu.limit_bytes=old_gpu;release_worker();wait_worker_idle(p,displays,6.01);finish_worker_control();
    CHECK(p->notice.code==PP_ERROR_RESOURCE_LIMIT&&b->scalable.frame.width==337&&a->scalable.frame.width==96);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==old_gpu&&!pp_raster_worker_status(p->worker).reserved_bytes);
    CHECK(surface_for(first)->shown&&surface_for(second)->shown&&pp_assets_external_raster_bytes(assets)==baseline);
    p->gpu.limit_bytes=PP_RETAINED_GPU_LIMIT;
    p->notice=(PPError){0};hold_next_worker();wheel.timestamp=7;CHECK(pp_anchor_presenter_event(p,&wheel,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPPlatformEvent first_wheel=wheel;first_wheel.surface=surface_for(first);first_wheel.instance_id=first;first_wheel.timestamp=7.001;
    CHECK(pp_anchor_presenter_event(p,&first_wheel,&error));wheel.timestamp=7.002;CHECK(pp_anchor_presenter_event(p,&wheel,&error));
    CHECK(!p->notice.code&&b->raster_retry&&pp_raster_worker_status(p->worker).pending_jobs==2);
    CHECK(a->scalable.frame.width==96&&b->scalable.frame.width==337&&surface_for(first)->shown&&surface_for(second)->shown);
    release_worker();wait_worker_idle(p,displays,7.02);finish_worker_control();CHECK(!pp_raster_worker_status(p->worker).reserved_bytes);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS shared SVG sprites retain independent sharp sizes and mappings across shared-cache edits, transforms and rejected CPU/GPU overlap");
}
static void test_empty_svg_releases_input(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=svg_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t id=source_sprite(p,scene,displays,1);wait_worker_idle(p,displays,1.3);PPSurface *surface=surface_for(id);
    event(p,surface,PP_EVENT_BUTTON_DOWN,400,300,2);CHECK(capture_owner==surface);
    const char *empty="<svg width='13' height='7'><rect width='13' height='7' fill='none'/></svg>";
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)empty,strlen(empty)},PP_ASSET_SVG,&item.image_asset,&error));
    CHECK(pp_scene_replace(scene,id,&item,2.01,&error));update(p,displays,400,300,2.01);wait_worker_idle(p,displays,2.01);
    CHECK(!capture_owner&&!surface->shown&&!pp_sprites_find(p,id)->retained.visual_alpha);
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    PPPose pose=pp_scene_get(scene,id)->pose;pose.center.x+=37;CHECK(pp_scene_set_pose(scene,id,pose,&error));update(p,displays,437,300,3);update(p,displays,437,300,4);
    CHECK(!surface->shown&&pp_anchor_presenter_renderer_metrics(p).transform_draws==before.transform_draws&&pp_assets_source_metrics(assets).raster_builds==builds);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS empty SVG hides and releases input after replacement, with retained hidden translation reuse");
}
static void test_svg_initial_source_is_cached(void) {
  for(int evicted=0;evicted<2;evicted++) {
    PPError error={0};PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPItem item=svg_item(assets);if(evicted)pp_assets_trim_cache(assets);
    PPProject *project=NULL;CHECK(pp_project_create(assets,&project,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    PPScene *scene=NULL;CHECK(pp_scene_create(&scene,&error));PPAnchorPresenter *p=NULL;
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    PPSprite *created=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,(PPPose){{400,300},96,0,1},1,&error);CHECK(created);
    hold_next_worker();uint64_t id=created->id;update(p,displays,400,300,1);CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    CHECK(pp_assets_source_metrics(assets).raster_builds==1);
    CHECK(surface_for(id)->shown==!evicted);release_worker();
    wait_worker_idle(p,displays,1);CHECK(surface_for(id)->shown&&pp_sprites_find(p,id)->scalable.frame.width==96);
    finish_worker_control();
    CHECK(pp_assets_source_metrics(assets).raster_builds==1);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
  }
    puts("PASS initial SVG sprite uses the available asset raster while its isolated requested-resolution endpoint prepares");
}
static void test_pending_svg_seed_releases_after_png_replacement(void) {
  for(int independent=0;independent<2;independent++) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=svg_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    hold_next_worker();uint64_t id=source_sprite(p,scene,displays,1);
    CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPSurface *surface=surface_for(id);CHECK(surface&&surface->shown&&surface->red==255);
    /* The 13 by 7 immutable SVG seed owns 364 RGBA + 91 alpha bytes. The
     * endpoint is still blocked, so this checks pre-endpoint ownership. */
    CHECK(pp_anchor_presenter_metrics(p).scalable_retained_bytes==455);
    PPItemSnapshot *old=pp_item_snapshot_retain(pp_scene_get(scene,id)->snapshot);
    uint64_t old_revision=pp_item_snapshot_revision(old);PPPose old_pose=pp_scene_get(scene,id)->pose;
    PPContentConsumer *observer=NULL;const PPFrame *observed=NULL;uint8_t old_pixels[364],old_alpha[91];
    if(independent) {
      CHECK(pp_content_consumer_create(p->sources,&observer,&error));PPContentFrame frame={0};double next;
      CHECK(pp_content_frame(observer,old,old_pose.longest_dip,1,0,&frame,&next,&error));pp_content_accept(observer);
      /* Keep the accepted seed without requesting an independent new endpoint. */
      pp_content_cancel(observer);
      observed=frame.frame;CHECK(observed&&observed->width==13&&observed->height==7);
      memcpy(old_pixels,observed->pixels,sizeof(old_pixels));memcpy(old_alpha,observed->content_alpha,sizeof(old_alpha));
      CHECK(pp_anchor_presenter_metrics(p).scalable_retained_bytes==455);
    }
    PPItem png=png_item(assets,"retained-blue.png",96);
    CHECK(pp_scene_replace(scene,id,&png,2,&error));fail_gl_upload=true;
    update(p,displays,400,300,2);fail_gl_upload=false;
    CHECK(surface_for(id)==surface&&surface->shown&&surface->red==255);
    CHECK(pp_item_snapshot_revision(pp_scene_get(scene,id)->snapshot)==old_revision);
    CHECK(!memcmp(&pp_scene_get(scene,id)->pose,&old_pose,sizeof(old_pose)));
    CHECK(pp_anchor_presenter_metrics(p).scalable_retained_bytes==455);
    CHECK(pp_scene_replace(scene,id,&png,3,&error));update(p,displays,400,300,3);
    CHECK(surface_for(id)==surface&&surface->shown&&surface->blue==255);
    release_worker();wait_worker_idle(p,displays,3);finish_worker_control();
    CHECK(pp_anchor_presenter_metrics(p).worker_reserved_bytes==0);
    CHECK(pp_anchor_presenter_metrics(p).scalable_retained_bytes==(independent?455:0));
    if(independent) {
      const PPTextResult *current=pp_content_current(observer,NULL,NULL);CHECK(current&&&current->frame==observed);
      CHECK(!memcmp(old_pixels,current->frame.pixels,sizeof(old_pixels))&&!memcmp(old_alpha,current->frame.content_alpha,sizeof(old_alpha)));
      pp_content_consumer_destroy(observer);CHECK(pp_anchor_presenter_metrics(p).scalable_retained_bytes==0);
    }
    pp_item_snapshot_release(old);pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);
    CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
  }
  puts("PASS pending SVG seed survives rejected PNG and releases after accepted replacement; independent old consumer keeps exact pixels and charge");
}
static void test_svg_full_queue_keeps_new_cached_sprite(void) {
    PPError error={0};PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem item=svg_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    hold_next_worker();uint64_t ids[3];
    for(int i=0;i<3;i++) {
        /* Three distinct endpoint sizes require three preparations; equal
         * targets are covered by the shared-consumer regression. */
        PPPose pose={{400+i*30,300},96+i*48,0,1};
        PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_RIGHT,pose,1,&error);CHECK(s);ids[i]=s->id;
        CHECK(pp_scene_set_pose(scene,s->id,pose,&error));
        update(p,displays,400,300,1);if(!i)CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    }
    CHECK(pp_scene_get(scene,ids[2])&&pp_scene_count(scene)==3&&pp_anchor_presenter_instance_count(p)==3);
    CHECK(surface_for(ids[2])->shown&&surface_for(ids[2])->source_width==13);
    CHECK(!p->notice.code&&pp_raster_worker_status(p->worker).requests==2&&pp_raster_worker_status(p->worker).pending_jobs==2);
    CHECK(pp_assets_source_metrics(assets).raster_builds==1);
    release_worker();wait_worker_idle(p,displays,1);finish_worker_control();
    CHECK(pp_scene_count(scene)==3&&surface_for(ids[2])->shown&&!pp_raster_worker_status(p->worker).reserved_bytes);
    CHECK(surface_for(ids[2])->source_width==192&&pp_raster_worker_status(p->worker).source_builds==3);
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS full SVG worker queue preserves a newly pulled cached sprite and bounds admitted captures");
}
static void test_svg_cpu_adapter_keeps_canonical_aspect(void) {
    PPError error={0};PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    const char *svg="<svg width='13.5' height='7.5'><rect width='13.5' height='7.5' fill='red'/></svg>";
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
    PPItemSnapshot *snapshot=NULL;CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error));
    PPVec2 canonical=pp_item_snapshot_size(snapshot);CHECK(canonical.x==96&&fabs(canonical.y-53.333333333333333)<1e-12);
    PPAnchorPresenter p={.assets=assets,.renderer=(PPRenderer *)1};PPSurface *surface=NULL;CHECK(pp_surface_create(&surface,&error));
    PPDisplay display={.drawable_pixels_per_dip=1,.desktop_units_per_dip=1};size_t retained=0;double next;
    CHECK(pp_capture_present_content(&p,surface,snapshot,(PPPose){{400,300},337,17.25,.63},&display,0,&next,&retained,&error));
    /* Worked viewport extent: ceil(337*cos + (337*5/9)*sin),
     * ceil(337*sin + (337*5/9)*cos). Rounded source337x188 would be378x280. */
    CHECK(surface->bounds.w==378&&surface->bounds.h==279&&retained==421848);
    pp_assets_release_raster(assets,retained);pp_surface_destroy(surface);pp_item_snapshot_release(snapshot);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS CPU compatibility SVG transform keeps fractional canonical aspect independently of rounded source texels");
}
static void test_anchor_retains_gif_clock_across_unrelated_edits(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=gif_item(assets,"finite.gif");uint64_t id;
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,&id,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay display={1,{0,0,1920,1080},1,1};
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1.08,&error));
    PPSurface *preview=p->anchors[PP_ANCHOR_TOP_LEFT].preview;
    CHECK(preview->shown&&preview->red==0);
    double deadline=p->anchors[PP_ANCHOR_TOP_LEFT].next_frame;
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(p);
    CHECK(pp_project_set_name(project,"Presentation",&error));
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1.081,&error));
    CHECK(preview->red==0&&p->anchors[PP_ANCHOR_TOP_LEFT].next_frame==deadline);
    PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(p);
    CHECK(after.source_uploads==before.source_uploads&&after.transform_draws==before.transform_draws&&after.readbacks==before.readbacks);
    CHECK(preview->window_id&&before.readbacks==0);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);
    CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS retained anchor keeps GIF clock and native artwork through an unrelated project edit");
}
static void test_equal_consumers_share_requested_source_preparation(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=scalable_item(assets);CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay display={1,{0,0,1920,1080},1,1.5};p->displays=malloc(sizeof(display));CHECK(p->displays);*p->displays=display;p->display_count=1;
    hold_next_worker();
    PPSprite *first=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{300,300},384,0,1},1,&error);CHECK(first);uint64_t a=first->id;
    CHECK(pp_sprites_update(p,1,&error));CHECK(SDL_WaitSemaphoreTimeout(worker_entered,2000));
    PPSprite *second=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{800,300},384,0,1},1.02,&error);CHECK(second);uint64_t b=second->id;
    CHECK(pp_sprites_update(p,1.02,&error));release_worker();
    for(int i=0;i<2000&&pp_raster_worker_status(p->worker).pending_jobs;i++){SDL_Delay(1);CHECK(pp_sprites_update(p,1.02,&error));}
    CHECK(pp_sprites_update(p,1.02,&error));finish_worker_control();
    CHECK(surface_for(a)->shown&&surface_for(b)->shown&&surface_for(a)->bounds.w==surface_for(b)->bounds.w);
    CHECK(pp_anchor_presenter_metrics(p).worker_source_builds==1);
    PPRendererMetrics drawn=pp_anchor_presenter_renderer_metrics(p);
    CHECK(pp_sprites_update(p,2,&error));CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==drawn.source_uploads);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS two retained consumers share one requested-resolution preparation without sharing their placement");
}
static void test_scalable_anchor_pull_reuses_prepared_content(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    const char *svg="<svg width='13.5' height='7.5'><rect x='2' y='1' width='9' height='5' fill='blue'/></svg>";
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay display={1,{0,0,1920,1080},1,1};
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));
    PPSurface *preview=p->anchors[PP_ANCHOR_TOP_LEFT].preview;
    CHECK(preview->window_id&&preview->shown);
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++){SDL_Delay(1);CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));}
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));
    CHECK(preview->bounds.w==96&&preview->bounds.h==54&&preview->source_width==96);
    PPPresentationMetrics prepared=pp_anchor_presenter_metrics(p);PPRendererMetrics rendered=pp_anchor_presenter_renderer_metrics(p);
    CHECK(prepared.worker_source_builds==1&&rendered.readbacks==0);
    /* This is transparent padding inside the native preview rectangle, beyond
     * the16x16activation region. Its logical offset remains the grab. */
    event(p,preview,PP_EVENT_BUTTON_DOWN,8,24,1.01);
    PPItemSnapshot *pressed=pp_item_snapshot_retain(p->anchors[0].snapshot);
    const char *replacement_svg="<svg width='7' height='13'><rect width='7' height='13' fill='red'/></svg>";
    PPItem replacement=*pp_project_target(project,PP_ANCHOR_TOP_LEFT);
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)replacement_svg,strlen(replacement_svg)},PP_ASSET_SVG,&replacement.image_asset,&error));
    CHECK(pp_project_edit_item(project,replacement.id,&replacement,&error));
    event(p,preview,PP_EVENT_POINTER_MOVE,400,300,1.02);
    event(p,preview,PP_EVENT_BUTTON_UP,400,300,1.03);
    CHECK(pp_sprites_update(p,1.04,&error));
    CHECK(pp_scene_count(p->scene)==1);const PPSceneInstance *sprite=pp_scene_at(p->scene,0);
    CHECK(sprite->pose.center.x==440&&sprite->pose.center.y==324&&sprite->pose.longest_dip==96);
    CHECK(sprite->snapshot==pressed);pp_item_snapshot_release(pressed);
    CHECK(surface_for(sprite->id)->shown&&surface_for(sprite->id)->bounds.w==96&&surface_for(sprite->id)->bounds.h==54);
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==prepared.worker_requests&&pp_anchor_presenter_metrics(p).worker_source_builds==1);
    CHECK(pp_anchor_presenter_renderer_metrics(p).readbacks==0);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS direct fractional-viewport anchor shares prepared content with a padding grab and pulled sprite");
}
static void test_anchor_pose_edits_keep_artwork_preparation(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=scalable_item(assets);uint64_t id;CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,&id,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));PPDisplay d={1,{0,0,1920,1080},1,1};
    CHECK(pp_anchor_presenter_update(p,&d,1,(PPVec2){8,8},1,&error));
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++){SDL_Delay(1);CHECK(pp_anchor_presenter_update(p,&d,1,(PPVec2){8,8},1,&error));}
    CHECK(pp_anchor_presenter_update(p,&d,1,(PPVec2){8,8},1,&error));
    PPPresentationMetrics prepared=pp_anchor_presenter_metrics(p);PPRendererMetrics drawn=pp_anchor_presenter_renderer_metrics(p);
    uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    PPItem changed=*pp_project_item(project,id);changed.angle_deg=17.25;changed.opacity=.63;
    CHECK(pp_project_edit_item(project,id,&changed,&error));
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
    CHECK(pp_anchor_presenter_update(p,&d,1,(PPVec2){8,8},1.1,&error));
    CHECK(pp_anchor_presenter_metrics(p).worker_requests==prepared.worker_requests);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==drawn.source_uploads);
    CHECK(p->anchors[0].preview->shown&&p->anchors[0].retained.drawn.angle_deg==17.25);
    CHECK(fabs(p->anchors[0].retained.drawn.opacity-.63*.35)<1e-12);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS anchor rotation/opacity edits redraw the pose while retaining prepared artwork");
}
static void test_removed_pressed_anchor_cancels_pending_pull(void) {
  for(int unrelated=0;unrelated<2;unrelated++) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    PPItem item=svg_item(assets);uint64_t removed,other;
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,&removed,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,&other,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));PPDisplay display={1,{0,0,1920,1080},1,1};
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));
    size_t before=0;PPSurface *independent=NULL;uint64_t independent_id=0;
    if(unrelated) {
      PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{800,500},96,0,1},1,&error);CHECK(s);
      independent=s->surface;independent_id=s->id;CHECK(pp_sprites_update(p,1,&error));before=1;
    }
    AnchorSurface *anchor=p->anchors;event(p,anchor->preview,PP_EVENT_BUTTON_DOWN,8,24,1.01);
    CHECK(anchor->interaction.pressed&&anchor->pull_snapshot&&capture_owner==anchor->preview);
    if(unrelated)event(p,independent,PP_EVENT_BUTTON_DOWN,800,500,1.015);
    CHECK(pp_project_remove_item(project,PP_ANCHOR_TOP_LEFT,removed,&error));
    CHECK(pp_project_target(project,PP_ANCHOR_TOP_LEFT)->id==other);
    event(p,anchor->preview,PP_EVENT_POINTER_MOVE,400,300,1.02);
    CHECK(pp_scene_count(p->scene)==before);
    CHECK(!anchor->interaction.pressed&&!anchor->pull_snapshot&&!p->runtime.entries[0].held);
    CHECK(capture_owner==independent);
    if(unrelated)CHECK(pp_sprites_find(p,independent_id)->interaction.pressed&&pp_scene_get(p->scene,independent_id));
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
  }
  puts("PASS removing a pressed source cancels its pending pull without spawning removed content or clearing another owner");
}
static void test_hidden_obsolete_source_releases_its_reference(void) {
  for(int sibling=0;sibling<2;sibling++)for(int replacement=0;replacement<2;replacement++) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));
    const char *red="<svg width='13.5' height='7.5'><rect width='13.5' height='7.5' fill='red'/></svg>";
    const char *blue="<svg width='7.5' height='13.5'><rect width='7.5' height='13.5' fill='blue'/></svg>";
    PPItem a={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1},b=a;uint64_t removed;
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)red,strlen(red)},PP_ASSET_SVG,&a.image_asset,&error));
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)blue,strlen(blue)},PP_ASSET_SVG,&b.image_asset,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&a,&removed,&error));
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&b,NULL,&error));
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));PPDisplay display={1,{0,0,1920,1080},1,1};
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));
    for(int i=0;i<2000&&pp_anchor_presenter_metrics(p).worker_pending_jobs;i++){SDL_Delay(1);CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));}
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){8,8},1,&error));uint64_t id=0;
    if(sibling){PPSprite *s=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{500,500},96,0,1},1,&error);CHECK(s);id=s->id;CHECK(pp_sprites_update(p,1,&error));}
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){500,500},2,&error));
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){500,500},2.5,&error));
    CHECK(pp_assets_external_raster_bytes(assets)==25925);
    uint64_t requests=pp_anchor_presenter_metrics(p).worker_requests;
    CHECK(pp_project_set_name(project,"Hidden artwork",&error));
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){500,500},2.75,&error));
    CHECK(pp_assets_external_raster_bytes(assets)==25925&&pp_anchor_presenter_metrics(p).worker_requests==requests);
    if(replacement)CHECK(pp_project_edit_item(project,removed,&b,&error));
    else CHECK(pp_project_remove_item(project,PP_ANCHOR_TOP_LEFT,removed,&error));
    CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){500,500},3,&error));
    CHECK(pp_assets_external_raster_bytes(assets)==(sibling?25925:5));
    if(sibling){CHECK(surface_for(id)->shown&&surface_for(id)->red==255);pp_scene_delete(p->scene,id);CHECK(pp_anchor_presenter_update(p,&display,1,(PPVec2){500,500},3.1,&error));CHECK(pp_assets_external_raster_bytes(assets)==5);}
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
  }
  puts("PASS hidden removed/replaced artwork releases obsolete ownership while a live sibling keeps its shared source");
}
static void test_bitmap_consumers_recover_after_shared_rejection(void) {
  for(int destroy_first=0;destroy_first<2;destroy_first++) {
    PPError error={0};PPAssets *assets=NULL;PPContentSources *sources=NULL;PPItemSnapshot *snapshot=NULL;
    PPContentConsumer *rejecting=NULL,*fresh=NULL,*sibling=NULL;
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_content_sources_create(&sources,assets,&error));
    size_t length=0;void *bytes=SDL_LoadFile(PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf",&length);CHECK(bytes);
    PPItem item={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,
      .text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,length},PP_ASSET_FONT,&item.text_style.fallback_asset,&error));SDL_free(bytes);
    CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error));
    CHECK(pp_item_snapshot_text_result(snapshot)->artwork.bitmap_only);
    CHECK(pp_content_consumer_create(sources,&rejecting,&error));PPContentFrame frame={0};double next;
    CHECK(pp_content_frame(rejecting,snapshot,96,1,0,&frame,&next,&error));
    CHECK(frame.frame&&frame.visual_alpha&&frame.frame->width==136&&frame.frame->height==128);
    size_t pixels=frame.frame->stride*(size_t)frame.frame->height,retained=pp_assets_external_raster_bytes(assets);
    uint8_t *expected=malloc(pixels);CHECK(expected);memcpy(expected,frame.frame->pixels,pixels);
    pp_content_reject(rejecting);
    for(int tick=0;tick<3;tick++) {
      CHECK(pp_content_frame(rejecting,snapshot,96,1,0,&frame,&next,&error));CHECK(!frame.frame&&!error.code);
    }
    if(destroy_first){pp_content_consumer_destroy(rejecting);rejecting=NULL;}
    CHECK(pp_content_consumer_create(sources,&fresh,&error));
    CHECK(pp_content_frame(fresh,snapshot,96,1,0,&frame,&next,&error));
    CHECK(frame.frame&&frame.visual_alpha&&frame.frame->width==136&&frame.frame->height==128);
    CHECK(!memcmp(frame.frame->pixels,expected,pixels));pp_content_accept(fresh);
    if(rejecting) {
      CHECK(pp_content_frame(rejecting,snapshot,96,1,0,&frame,&next,&error));CHECK(!frame.frame);
      pp_content_consumer_destroy(rejecting);rejecting=NULL;
    }
    CHECK(pp_content_frame(fresh,snapshot,96,1,0,&frame,&next,&error));
    CHECK(frame.frame&&frame.visual_alpha&&!memcmp(frame.frame->pixels,expected,pixels));
    CHECK(pp_content_consumer_create(sources,&sibling,&error));
    CHECK(pp_content_frame(sibling,snapshot,96,1,0,&frame,&next,&error));CHECK(frame.frame);pp_content_reject(sibling);
    CHECK(pp_content_frame(sibling,snapshot,96,1,0,&frame,&next,&error));CHECK(!frame.frame);
    CHECK(pp_content_frame(fresh,snapshot,96,1,0,&frame,&next,&error));
    CHECK(frame.frame&&frame.visual_alpha&&!memcmp(frame.frame->pixels,expected,pixels));
    CHECK(pp_assets_external_raster_bytes(assets)==retained&&pp_content_sources_raster_bytes(sources)==0);
    pp_content_cancel(sibling);CHECK(pp_content_frame(sibling,snapshot,96,1,0,&frame,&next,&error));CHECK(frame.frame);
    pp_content_cancel(fresh);CHECK(pp_content_current(fresh,NULL,NULL)->frame.pixels);
    pp_content_consumer_destroy(sibling);pp_content_consumer_destroy(fresh);pp_content_sources_destroy(sources);
    CHECK(pp_assets_external_raster_bytes(assets)==retained);pp_item_snapshot_release(snapshot);
    CHECK(!pp_assets_external_raster_bytes(assets));free(expected);pp_assets_destroy(assets);
  }
  puts("PASS fresh bitmap consumers recover after shared rejection and rejecting-owner teardown, with local suppression and bounded snapshot ownership");
}
int main(void) {
    test_bitmap_consumers_recover_after_shared_rejection();
    test_bitmap_replacement_matches_fresh_canonical_source();
    test_hidden_obsolete_source_releases_its_reference();
    test_removed_pressed_anchor_cancels_pending_pull();
    test_zero_opacity_cancels_only_its_source_target();
    test_anchor_pose_edits_keep_artwork_preparation();
    test_scalable_anchor_pull_reuses_prepared_content();
    test_equal_consumers_share_requested_source_preparation();
    test_anchor_retains_gif_clock_across_unrelated_edits();
    test_svg_cpu_adapter_keeps_canonical_aspect();
    test_svg_full_queue_keeps_new_cached_sprite();
    test_svg_initial_source_is_cached();
    test_pending_svg_seed_releases_after_png_replacement();
    test_svg_endpoint_worker_reuses_cached_artwork();
    test_svg_shared_asset_keeps_independent_sizes();
    test_empty_svg_releases_input();
    svg_lifecycle=true;
    test_scalable_publication_failure_releases_interceptor();
    test_scalable_budget_failure_and_project_teardown();
    test_scalable_rapid_resize_drag_density_and_cancellation();
    test_completed_target_is_revalidated_after_external_resize();
    svg_lifecycle=false;
    test_new_scalable_pull_survives_busy_worker();
    test_settled_worker_matches_fresh_text();
    test_scalable_publication_failure_releases_interceptor();
    test_scalable_budget_failure_and_project_teardown();
    test_scalable_rapid_resize_drag_density_and_cancellation();
    test_completed_target_is_revalidated_after_external_resize();
    test_cached_source_survives_compatibility_request();
    test_async_latest_text_resize();
    test_gif_transparent_frame_releases_capture();
#if defined(_WIN32)
    test_gif_budget_sharing_and_png_transitions();
    test_gif_rejected_replacement_preserves_clock();
    test_gif_producer_failure_isolates_interceptor();
    test_gif_independent_clocks_and_shared_frames();
#endif
    test_canonical_source_mapping();
#if defined(_WIN32)
    test_bitmap_text_transform_reuses_useful_source();
    test_bitmap_candidate_upload_preserves_usable_source();
    test_png_translation_reuses_presentation();
    test_png_appearance_geometry_and_rollback();
    test_png_gpu_limit_and_overlap();
    test_png_broken_context_releases_interceptor();
    test_png_empty_source_releases_input();
    test_png_candidate_preserves_svg_clock();
#endif
    test_latest_resize_sharp_observation();
    test_native_retention_contract();
    test_unavailable_source_finishes_pull();
    PPDisplay d[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    PPError error={0};PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPProject *project=test_arrow_project(assets);
    PPAnchorPresenter *p=NULL;CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    update(p,d,-8,8,1);PPSurface *preview=NULL;
    for(PPSurface *s=surfaces;s;s=s->next) if(s->shown&&s->bounds.x==-144&&s->bounds.w==144) preview=s;CHECK(preview);
    event(p,preview,PP_EVENT_BUTTON_DOWN,-19.5,72,1.01);event(p,preview,PP_EVENT_POINTER_MOVE,10,200,1.02);
    update(p,d,10,200,1.02);check_grab(p,10,-25,96); /* Pointer right, center left. */
    PPItem while_dragging=*pp_project_target(project,PP_ANCHOR_TOP_RIGHT);
    while_dragging.angle_deg=30;while_dragging.opacity=.2;
    CHECK(pp_project_edit_item(project,while_dragging.id,&while_dragging,&error));
    event(p,preview,PP_EVENT_BUTTON_UP,10,200,1.03);update(p,d,10,200,1.03);check_grab(p,10,-25,96);
    PPPose unchanged;CHECK(pp_anchor_presenter_instance_pose(p,0,&unchanged));
    CHECK(unchanged.angle_deg==0 && unchanged.opacity==1);
    while_dragging.angle_deg=0;while_dragging.opacity=1;
    CHECK(pp_project_edit_item(project,while_dragging.id,&while_dragging,&error));
    PPSurface *sprite=instance_surface();event(p,sprite,PP_EVENT_BUTTON_DOWN,10,200,1.04);
    event(p,sprite,PP_EVENT_POINTER_MOVE,-10,200,1.05);update(p,d,-10,200,1.05);check_grab(p,-10,-62.5,144);
    event(p,sprite,PP_EVENT_POINTER_MOVE,10,200,1.06);update(p,d,10,200,1.06);check_grab(p,10,-25,96);
    event(p,sprite,PP_EVENT_BUTTON_UP,10,200,1.07);update(p,d,10,200,1.07);check_grab(p,10,-25,96);
    wait_worker_idle(p,d,1.07);
    CHECK(sprite->red==255 && sprite->blue==0 && sprite->source_width==96);
    d[1].drawable_pixels_per_dip=2;
    update(p,d,10,200,1.08);wait_worker_idle(p,d,1.08);
    CHECK(sprite->source_width==192 && sprite->bounds.w==96);
    CHECK(sprite->window_id); /* Requested Retina source is retained by direct GL. */
    PPItem replacement=*pp_project_target(project,PP_ANCHOR_TOP_RIGHT);
    const char *blue="<svg width='200' height='100'><rect width='200' height='100' fill='blue'/></svg>";
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)blue,strlen(blue)},PP_ASSET_SVG,&replacement.image_asset,&error));
    replacement.image_longest_dip=200;
    CHECK(pp_project_edit_item(project,replacement.id,&replacement,&error));
    update(p,d,-8,8,1.09);wait_worker_idle(p,d,1.09);
    bool blue_preview=false;
    for(PPSurface *s=surfaces;s;s=s->next) if(s->shown&&!s->id&&s->blue==255&&s->red==0) {
        CHECK(s->bounds.w==300 && s->bounds.h==150 && s->source_width==300);blue_preview=true;
    }
    CHECK(blue_preview);
    CHECK(sprite->red==255 && sprite->blue==0);
    FILE *gif=fopen(PP_MEDIA_FIXTURES "/finite.gif","rb");CHECK(gif);
    CHECK(!fseek(gif,0,SEEK_END));long length=ftell(gif);CHECK(length>0);rewind(gif);
    uint8_t *data=malloc((size_t)length);CHECK(data);
    CHECK(fread(data,1,(size_t)length,gif)==(size_t)length);fclose(gif);
    CHECK(pp_asset_import(assets,(PPBytes){data,(size_t)length},PP_ASSET_GIF,&replacement.image_asset,&error));free(data);
    CHECK(pp_project_edit_item(project,replacement.id,&replacement,&error));
    update(p,d,-8,8,10);
    bool initial_frame=false;
    for(PPSurface *s=surfaces;s;s=s->next) if(s->shown&&!s->id&&s->source_width==3&&s->red==255) initial_frame=true;
    CHECK(initial_frame); /* A finite GIF preview starts at reveal, not app uptime. */
    update(p,d,-8,8,10.26);
    for(PPSurface *s=surfaces;s;s=s->next) if(s->shown&&!s->id&&s->source_width==3) CHECK(s->red==0);
    CHECK(pp_project_set_enabled(project,PP_ANCHOR_TOP_RIGHT,false,&error));
    update(p,d,-8,8,2);
    const PPAnchorRuntime *runtime=pp_anchor_presenter_reveals(p);
    CHECK(!runtime->entries[PP_ANCHOR_TOP_RIGHT].revealed);
    CHECK(!runtime->entries[8+PP_ANCHOR_TOP_RIGHT].revealed);
    CHECK(pp_project_set_enabled(project,PP_ANCHOR_TOP_RIGHT,true,&error));
    uint64_t id=pp_project_target(project,PP_ANCHOR_TOP_RIGHT)->id;
    CHECK(pp_project_remove_item(project,PP_ANCHOR_TOP_RIGHT,id,&error));
    update(p,d,-8,8,3);
    CHECK(!runtime->entries[PP_ANCHOR_TOP_RIGHT].revealed);
    CHECK(pp_anchor_presenter_instance_count(p)==1);
    pp_anchor_presenter_destroy(p);CHECK(!surfaces);
    pp_project_destroy(project);CHECK(pp_assets_external_raster_bytes(assets)==0);pp_assets_destroy(assets);
    puts("PASS shared content, source removal/disable/zero-opacity pull cancellation, queued terminal events and mixed-DPI presentation");return 0;
}
/* These boundary mocks have no GL output storage. Native desktop tests cover
 * retained color allocation and publication. */
size_t pp_renderer_output_candidate_bytes(const PPRenderer *r,int w,int h){(void)r;(void)w;(void)h;return 0;}
size_t pp_renderer_output_bytes(const PPRenderer *r){(void)r;return 0;}
bool pp_renderer_prepare_source_output(PPRenderer *r,const PPFrame *frame,PPSharedTexture *shared,
    uint64_t id,uint64_t revision,int w,int h,PPError *error) {
  (void)w;(void)h;
  if(shared)return pp_renderer_bind_shared(r,shared,id,revision,error);
  if(frame)return pp_renderer_upload_source(r,frame,id,revision,error);
  return true;
}
void pp_renderer_release_output(PPRenderer *r){(void)r;}
bool pp_renderer_refresh(PPRenderer *r,PPError *error){(void)error;r->metrics.presentation_refreshes++;return true;}
bool pp_renderer_destroy_retained(PPRenderer *r){pp_renderer_destroy(r);return true;}
