/* Exercises the real presenter with deterministic presentation boundaries.
 * No windows, hooks, input injection, or desktop focus changes are made. */
#include "../../src/app/anchors.h"
#include "../test.h"
#include "../project_fixture.h"
#include "../../src/core/geometry_internal.h"
#include "../../src/app/presenter_internal.h"
#include <math.h>
struct PPSurface {uint64_t id;PPRect bounds;bool shown;struct PPSurface *next;int source_width, raster_width;uint8_t red,blue;uint32_t window_id;};
struct PPRenderer {PPSurface *surface;PPRendererMetrics metrics;uint64_t content_id,revision;};
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
    r->content_id=id;r->revision=revision;r->metrics.source_uploads++;return true;
}
bool pp_renderer_draw(PPRenderer *r,PPVec2 layout,double size,double angle,double opacity,double density,PPError *error) {
    (void)layout;(void)size;(void)angle;(void)opacity;(void)density;
    if(r->surface==failed_gl_surface) {if(error)*error=(PPError){PP_ERROR_RENDER,"Injected active direct draw failure"};return false;}
    r->metrics.transform_draws++;return true;
}
bool pp_renderer_draw_source(PPRenderer *r,PPVec2 layout,PPRect rect,double size,double angle,double opacity,double density,PPError *error) {
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
    PPPlatformEvent resize={.kind=PP_EVENT_WHEEL,.surface=s->surface,.instance_id=s->id,.modifiers=PP_MOD_ALT,.wheel_delta=1,.timestamp=2};
    CHECK(pp_anchor_presenter_event(p,&resize,&error));
    resize.timestamp=2.01; CHECK(pp_anchor_presenter_event(p,&resize,&error));
    CHECK(pp_sprites_update(p,2.02,&error));
    CHECK(pp_anchor_presenter_metrics(p).sharp_publications==0);
    CHECK(pp_sprites_update(p,2.2,&error));
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
    for(int i=0;i<4;i++) {uint64_t id=png_pull(p,scene,displays,1+i);if(!i)first=id;CHECK(pp_scene_count(scene)==(size_t)i+1);}
    PPGpuResources full=pp_anchor_presenter_gpu_resources(p);CHECK(full.retained_bytes==268435456 && full.peak_bytes==full.retained_bytes && full.limit_bytes==268435456);
    PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(p);PPSurface *old=surface_for(first);
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(scene,first)->snapshot);
    CHECK(pp_scene_replace(scene,first,&item,5,&error));update(p,displays,400,300,5);
    CHECK(surface_for(first)==old && pp_item_snapshot_revision(pp_scene_get(scene,first)->snapshot)==revision && pp_anchor_presenter_gpu_resources(p).retained_bytes==full.retained_bytes);
    CHECK(pp_anchor_presenter_renderer_metrics(p).source_uploads==warm.source_uploads);
    png_pull(p,scene,displays,6);CHECK(pp_scene_count(scene)==4 && pp_anchor_presenter_instance_count(p)==4);
    pp_scene_delete(scene,pp_scene_at(scene,3)->id);update(p,displays,400,300,7);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==201326592);
    CHECK(pp_scene_replace(scene,first,&item,8,&error));update(p,displays,400,300,8);
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
    CHECK(pp_scene_replace(scene,a,&item,3,&error));fail_gl_context=true;update(p,displays,400,300,3);fail_gl_context=false;
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
static void test_png_candidate_preserves_compatibility_clock(void) {
    PPAssets *assets=NULL;PPError error={0};CHECK(pp_assets_create(&assets,&error));
    PPProject *project=test_arrow_project(assets);PPAnchorPresenter *p=NULL;
    CHECK(pp_anchor_presenter_create(&p,(PPRenderer *)1,project,&error));
    PPDisplay display={1,{0,0,1920,1080},1,1};p->displays=malloc(sizeof(display));*p->displays=display;p->display_count=1;
    PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,(PPPose){{200,200},96,0,1},1,&error);CHECK(sprite);
    uint64_t id=sprite->id;PPSurface *surface=sprite->surface;CHECK(pp_sprites_update(p,1,&error));
    uint64_t revision=pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot);
    PPItem png=png_item(assets,"alpha.png",96);CHECK(pp_scene_replace(p->scene,id,&png,3,&error));
    pp_sprites_sync(p);fail_gl_upload=true;CHECK(pp_sprites_update(p,3,&error));fail_gl_upload=false;
    CHECK(surface_for(id)==surface&&surface->shown&&!surface->window_id);
    CHECK(pp_item_snapshot_revision(pp_scene_get(p->scene,id)->snapshot)==revision);
    CHECK(pp_scene_get(p->scene,id)->animation_start==1);
    CHECK(pp_anchor_presenter_gpu_resources(p).retained_bytes==0);
    pp_anchor_presenter_destroy(p);pp_project_destroy(project);CHECK(!surfaces);pp_assets_destroy(assets);
    puts("PASS rejected PNG candidate preserves independent compatibility surface, immutable source and animation start");
}
static void test_gif_budget_sharing_and_png_transitions(void) {
    PPAssets *assets=NULL;PPProject *project=NULL;PPScene *scene=NULL;PPAnchorPresenter *p=NULL;PPError error={0};
    CHECK(pp_assets_create(&assets,&error));CHECK(pp_project_create(assets,&project,&error));CHECK(pp_scene_create(&scene,&error));
    PPItem png=png_item(assets,"budget-0.png",96),gif=gif_item(assets,"finite.gif");
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_RIGHT,&png,NULL,&error));CHECK(pp_anchor_presenter_create_shared(&p,(PPRenderer *)1,project,scene,&error));
    PPDisplay displays[]={{1,{-1920,0,1920,1080},1.5,1.5},{2,{0,0,1920,1080},1,1}};
    uint64_t ids[4];for(int i=0;i<4;i++)ids[i]=png_pull(p,scene,displays,1+i);
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
    puts("PASS source origin mapping preserves canonical center/aspect and includes overhang without reupload");
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
    pp_anchor_presenter_destroy(p);pp_scene_destroy(scene);pp_project_destroy(project);CHECK(!surfaces);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS bitmap text useful source survives large fractional easing, static/translation, rotation, density and grabbed-point changes without raster/upload/readback work");
}
int main(void) {
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
    test_png_translation_reuses_presentation();
    test_png_appearance_geometry_and_rollback();
    test_png_gpu_limit_and_overlap();
    test_png_broken_context_releases_interceptor();
    test_png_empty_source_releases_input();
    test_png_candidate_preserves_compatibility_clock();
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
    CHECK(sprite->red==255 && sprite->blue==0 && sprite->source_width==96);
    d[1].drawable_pixels_per_dip=2;
    update(p,d,10,200,1.08);
    CHECK(sprite->source_width==192 && sprite->bounds.w==96);
    CHECK(sprite->raster_width==192); /* Retina must survive the final transform. */
    PPItem replacement=*pp_project_target(project,PP_ANCHOR_TOP_RIGHT);
    const char *blue="<svg width='200' height='100'><rect width='200' height='100' fill='blue'/></svg>";
    CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)blue,strlen(blue)},PP_ASSET_SVG,&replacement.image_asset,&error));
    replacement.image_longest_dip=200;
    CHECK(pp_project_edit_item(project,replacement.id,&replacement,&error));
    update(p,d,-8,8,1.09);
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
