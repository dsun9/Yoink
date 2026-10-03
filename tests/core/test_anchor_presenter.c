/* Exercises the real presenter with deterministic presentation boundaries.
 * No windows, hooks, input injection, or desktop focus changes are made. */
#include "../../src/app/anchors.h"
#include "../test.h"
#include "../project_fixture.h"
#include "../../src/core/geometry_internal.h"
#include <math.h>
struct PPSurface {uint64_t id;PPRect bounds;bool shown;struct PPSurface *next;int source_width;uint8_t red,blue;};
static PPSurface *surfaces;
static int source_width;
bool pp_surface_create(PPSurface **out,PPError *error) {(void)error;*out=calloc(1,sizeof(**out));CHECK(*out);(*out)->next=surfaces;surfaces=*out;return true;}
bool pp_surface_present(PPSurface *s,const PPFrame *frame,PPRect bounds,PPError *error) {(void)error;CHECK(bounds.w==frame->width);s->bounds=bounds;s->shown=true;s->source_width=source_width;s->red=frame->pixels[0];s->blue=frame->pixels[2];return true;}
void pp_surface_destroy(PPSurface *s) {if(!s) return;PPSurface **link=&surfaces;while(*link!=s) link=&(*link)->next;*link=s->next;free(s);}
void pp_surface_hide(PPSurface *s) {s->shown=false;}
void pp_surface_set_instance_id(PPSurface *s,uint64_t id) {s->id=id;}
bool pp_platform_capture(PPSurface *s,PPError *error) {(void)s;(void)error;return true;}
void pp_platform_release_capture(void) {}
bool pp_platform_poll(PPPlatformEvent *out) {(void)out;return false;}
bool pp_platform_pointer(PPVec2 *out) {(void)out;return false;}
bool pp_platform_displays(PPDisplay **out,size_t *count,PPError *error) {(void)out;(void)count;(void)error;return false;}
void pp_displays_free(PPDisplay *d) {free(d);}
void pp_platform_gesture_config(PPGestureConfig *out) {*out=(PPGestureConfig){2,2,2,2,.5,10,1.1,.12};}
bool pp_renderer_create(PPRenderer **out,PPError *error) {(void)error;*out=(PPRenderer *)1;return true;}
void pp_renderer_destroy(PPRenderer *r) {(void)r;}
bool pp_renderer_transform(PPRenderer *r,const PPFrame *source,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
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
int main(void) {
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
    puts("PASS shared project availability, retained sprite and mixed-DPI presentation");return 0;
}
