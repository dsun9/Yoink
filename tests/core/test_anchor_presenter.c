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
static PPSurface *surfaces;
static int source_width;
static unsigned cpu_transforms;
static uint32_t window_serial;
static PPSurface *capture_owner,*failed_gl_surface;
static bool fail_gl_upload;
static bool fail_gl_context;
static size_t native_planes = 4;
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
bool pp_surface_place_gl(PPSurface *s,PPRect b,bool interactive,PPError *error) {(void)error;s->bounds=b;s->shown=interactive;return true;}
void pp_surface_set_rectangular(PPSurface *s,bool enabled) {(void)s;(void)enabled;}
void pp_surface_inherit_order(PPSurface *s,PPSurface *old) {(void)s;(void)old;}
bool pp_renderer_create_for_window(PPRenderer **out,uint32_t id,PPError *error) {
    (void)error;*out=calloc(1,sizeof(**out));CHECK(*out);
    for(PPSurface *s=surfaces;s;s=s->next) if(s->window_id==id) (*out)->surface=s;
    CHECK((*out)->surface);return true;
}
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
int main(void) {
#if defined(_WIN32)
    test_png_translation_reuses_presentation();
    test_png_appearance_geometry_and_rollback();
    test_png_gpu_limit_and_overlap();
    test_png_broken_context_releases_interceptor();
    test_png_empty_source_releases_input();
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
