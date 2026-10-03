#include "anchors.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct AnchorSurface {
    PPSurface *target,*preview;
    PPDisplay display;
    PPAnchor anchor;
    PPInteraction interaction;
    PPVec2 grab_dip;
    uint64_t pulled_id;
    double opacity,scale;
    bool shown;
} AnchorSurface;
typedef struct ProbeInstance {
    PPSurface *surface;
    uint64_t id,display_id;
    PPPose pose,drawn;
    PPInteraction interaction;
    PPVec2 grab_dip,drag_pointer;
    double scale;
    bool dirty,dragging;
} ProbeInstance;
struct PPAnchorPresenter {
    PPRenderer *renderer;
    PPFrame source,target_source;
    PPAnchorRuntime runtime;
    AnchorSurface *anchors;
    size_t anchor_count;
    PPDisplay *displays;
    size_t display_count;
    ProbeInstance *instances;
    size_t instance_count;
    uint64_t next_id;
    PPGestureConfig gestures;
};
static bool memory_error(PPError *error) {if(error) {error->code=PP_ERROR_MEMORY;snprintf(error->message,sizeof(error->message),"Cannot allocate anchor runtime");}return false;}
static bool present(PPAnchorPresenter *p,PPSurface *surface,const PPFrame *source,PPPose pose,double scale,PPError *error) {
    PPFrame frame={0};bool ok=pp_renderer_transform(p->renderer,source,pose.longest_dip,pose.angle_deg,pose.opacity,scale,&frame,error);
    if(ok) ok=pp_surface_present(surface,&frame,(PPRect){round(pose.center.x-frame.width*.5),round(pose.center.y-frame.height*.5),frame.width,frame.height},error);
    pp_frame_destroy(&frame);return ok;
}
bool pp_anchor_presenter_create(PPAnchorPresenter **out,PPRenderer *renderer,PPError *error) {
    if(!out||!renderer) {if(error) {error->code=PP_ERROR_ARGUMENT;snprintf(error->message,sizeof(error->message),"Missing anchor presenter output or renderer");}return false;}
    *out=NULL;PPAnchorPresenter *p=calloc(1,sizeof(*p));if(!p) return memory_error(error);
    p->renderer=renderer;p->next_id=1;pp_platform_gesture_config(&p->gestures);
    p->source=(PPFrame){calloc(64,256),calloc(64,64),64,64,256};
    p->target_source=(PPFrame){calloc(1,4),calloc(1,1),1,1,4};
    if(!p->source.pixels||!p->source.content_alpha||!p->target_source.pixels||!p->target_source.content_alpha) {pp_anchor_presenter_destroy(p);return memory_error(error);}
    for(int y=0;y<64;y++) for(int x=0;x<64;x++) {
        bool shaft=x>=8&&x<=37&&y>=26&&y<=37;
        bool head=x>=32&&x<=57&&abs(y-32)<=((57-x)*3)/4;
        if(shaft||head) {size_t i=(size_t)y*64+x;p->source.pixels[i*4]=32;p->source.pixels[i*4+1]=180;p->source.pixels[i*4+2]=255;p->source.pixels[i*4+3]=p->source.content_alpha[i]=255;}
    }
    p->target_source.pixels[0]=32;p->target_source.pixels[1]=180;p->target_source.pixels[2]=255;p->target_source.pixels[3]=p->target_source.content_alpha[0]=255;
    *out=p;return true;
}
static void close_anchors(PPAnchorPresenter *p) {
    for(size_t i=0;i<p->anchor_count;i++) {pp_surface_destroy(p->anchors[i].target);pp_surface_destroy(p->anchors[i].preview);}
    free(p->anchors);p->anchors=NULL;p->anchor_count=0;
}
static const PPDisplay *display_by_id(const PPDisplay *d,size_t count,uint64_t id) {
    for(size_t i=0;i<count;i++) if(d[i].id==id) return d+i;
    return NULL;
}
static bool sync_displays(PPAnchorPresenter *p,const PPDisplay *d,size_t count,PPError *error) {
    if(pp_displays_equal(p->displays,p->display_count,d,count)&&p->anchor_count==count*8) return true;
    bool changed=count*8!=p->anchor_count;
    for(size_t i=0;!changed&&i<count;i++) if(p->anchors[i*8].display.id!=d[i].id) changed=true;
    PPDisplay *copy=count?malloc(count*sizeof(*copy)):NULL;if(count&&!copy) return memory_error(error);
    if(count) memcpy(copy,d,count*sizeof(*copy));
    if(changed) {
        close_anchors(p);
        p->anchors=count?calloc(count*8,sizeof(*p->anchors)):NULL;if(count&&!p->anchors) {free(copy);return memory_error(error);}
        p->anchor_count=count*8;
        for(size_t i=0;i<p->anchor_count;i++) {
            AnchorSurface *a=p->anchors+i;a->display=d[i/8];a->anchor=(PPAnchor)(i%8);
            if(!pp_surface_create(&a->preview,error)||!pp_surface_create(&a->target,error)) {free(copy);close_anchors(p);return false;}
        }
        /* Native destruction cancels source gestures, even before the queued
         * cancellation is consumed. End transient holds on rebuilt anchors. */
        for(size_t i=0;i<p->runtime.count;i++) p->runtime.entries[i].held=false;
    }
    for(size_t i=0;i<p->anchor_count;i++) {
        AnchorSurface *a=p->anchors+i;
        if(!pp_displays_equal(&a->display,1,d+i/8,1)) a->scale=0; /* Invalidate native geometry, including origin/resolution. */
        a->display=d[i/8];
    }
    free(p->displays);p->displays=copy;p->display_count=count;
    for(size_t i=0;i<p->instance_count;i++) {
        ProbeInstance *instance=p->instances+i;
        if(changed&&instance->interaction.pressed) {pp_surface_hide(instance->surface);memset(&instance->interaction,0,sizeof(instance->interaction));instance->dirty=true;}
        if(changed) instance->dragging=false;
        const PPDisplay *active=display_by_id(d,count,instance->display_id);
        if(active&&instance->dragging) {instance->pose.center=pp_display_drag_center(active,instance->drag_pointer,instance->grab_dip);instance->dirty=true;}
        PPVec2 recovered=pp_recover_center(d,count,instance->pose.center,instance->pose.longest_dip);
        if(recovered.x!=instance->pose.center.x||recovered.y!=instance->pose.center.y) {instance->pose.center=recovered;memset(&instance->interaction,0,sizeof(instance->interaction));instance->dirty=true;}
    }return true;
}
bool pp_anchor_presenter_update(PPAnchorPresenter *p,const PPDisplay *d,size_t count,PPVec2 pointer,double now,PPError *error) {
    if(!p||(!d&&count)||count>SIZE_MAX/8/sizeof(AnchorSurface)) return false;
    if(!sync_displays(p,d,count,error)) return false;
    pp_anchors_update(&p->runtime,d,count,pointer,now);if(p->runtime.failed) return memory_error(error);
    for(size_t i=0;i<p->anchor_count;i++) {
        AnchorSurface *a=p->anchors+i;PPAnchorReveal *r=p->runtime.entries+i;
        if(!r->revealed) {if(a->shown) {pp_surface_hide(a->target);pp_surface_hide(a->preview);a->shown=false;a->interaction=(PPInteraction){0};a->pulled_id=0;}continue;}
        double scale=a->display.desktop_units_per_dip;
        if(!a->shown||a->opacity!=r->opacity||a->scale!=scale) {
            PPPose pose={{r->preview.x+r->preview.w*.5,r->preview.y+r->preview.h*.5},96,0,r->opacity};
            if(!present(p,a->preview,&p->source,pose,scale,error)) goto failed;
            pose=(PPPose){{r->target.x+r->target.w*.5,r->target.y+r->target.h*.5},16,0,r->opacity};
            if(!present(p,a->target,&p->target_source,pose,scale,error)) goto failed;
            a->opacity=r->opacity;a->scale=scale;a->shown=true;
        }
    }
    for(size_t i=0;i<p->instance_count;i++) {
        ProbeInstance *s=p->instances+i;const PPDisplay *display=display_by_id(d,count,s->display_id);
        if(!display) {display=pp_display_at(d,count,s->pose.center);if(display) s->display_id=display->id;}
        if(!display) {pp_surface_hide(s->surface);s->dirty=true;continue;}
        double scale=display->desktop_units_per_dip;
        if(s->dirty||s->scale!=scale) {if(!present(p,s->surface,&p->source,s->pose,scale,error)) goto failed;s->dirty=false;s->drawn=s->pose;s->scale=scale;}
    }return true;
failed:
    /* A presentation failure removes every interceptor owned by this probe.
     * Returning false lets the caller report and shut down the owner. */
    for(size_t i=0;i<p->anchor_count;i++) {pp_surface_hide(p->anchors[i].target);pp_surface_hide(p->anchors[i].preview);}
    for(size_t i=0;i<p->instance_count;i++) pp_surface_hide(p->instances[i].surface);
    return false;
}
static ProbeInstance *find_instance(PPAnchorPresenter *p,uint64_t id) {for(size_t i=0;i<p->instance_count;i++) if(p->instances[i].id==id) return p->instances+i;return NULL;}
static ProbeInstance *create_instance(PPAnchorPresenter *p,PPPose pose,PPError *error) {
    ProbeInstance *items=realloc(p->instances,(p->instance_count+1)*sizeof(*items));if(!items) {memory_error(error);return NULL;}p->instances=items;
    ProbeInstance *s=items+p->instance_count;memset(s,0,sizeof(*s));s->id=p->next_id++;s->pose=pose;s->dirty=true;
    if(!pp_surface_create(&s->surface,error)) return NULL;pp_surface_set_instance_id(s->surface,s->id);p->instance_count++;return s;
}
static PPVec2 target_grab(PPAnchorPresenter *p,AnchorSurface *a,PPVec2 point) {
    PPRect b=pp_anchor_preview_bounds(&a->display,a->anchor,96);
    double scale=a->display.desktop_units_per_dip,best=HUGE_VAL;PPVec2 grab={0};
    for(int y=0;y<64;y++) for(int x=0;x<64;x++) if(p->source.content_alpha[y*64+x]) {
        PPVec2 candidate={(x+.5)*1.5-48,(y+.5)*1.5-48};
        double dx=b.x+b.w*.5+candidate.x*scale-point.x,dy=b.y+b.h*.5+candidate.y*scale-point.y;
        if(dx*dx+dy*dy<best) {best=dx*dx+dy*dy;grab=candidate;}
    }return grab;
}
bool pp_anchor_presenter_event(PPAnchorPresenter *p,const PPPlatformEvent *event,PPError *error) {
    if(!p||!event) return false;
    for(size_t i=0;i<p->anchor_count;i++) {
        AnchorSurface *a=p->anchors+i;if(event->surface!=a->target&&event->surface!=a->preview) continue;
        PPAnchorReveal *reveal=p->runtime.entries+i;
        PPRect bounds=reveal->preview;
        a->interaction.displayed_pose=(PPPose){{bounds.x+bounds.w*.5,bounds.y+bounds.h*.5},96,0,1};
        if(event->kind==PP_EVENT_BUTTON_DOWN&&event->button==PP_BUTTON_LEFT) {
            if(!p->runtime.enabled[a->anchor]) return true;
            a->grab_dip=event->surface==a->target?target_grab(p,a,event->desktop_point):
                (PPVec2){(event->desktop_point.x-a->interaction.displayed_pose.center.x)/a->display.desktop_units_per_dip,(event->desktop_point.y-a->interaction.displayed_pose.center.y)/a->display.desktop_units_per_dip};
            reveal->held=true;a->pulled_id=0;
            /* A completed queued gesture can have already released its native
             * accepted press. Its remaining queued trajectory still runs. */
            pp_platform_capture(event->surface,NULL);
        }
        PPInteractionResult result;pp_interaction_step(&a->interaction,event,&p->gestures,&result);
        if(result.command==PP_COMMAND_BEGIN_DRAG||result.command==PP_COMMAND_MOVE_DRAG||result.command==PP_COMMAND_END_DRAG) {
            const PPDisplay *display=pp_display_at(p->displays,p->display_count,event->desktop_point);if(!display) return true;
            PPPose pose={pp_display_drag_center(display,event->desktop_point,a->grab_dip),96,0,1};
            ProbeInstance *s=find_instance(p,a->pulled_id);
            if(!s) {s=create_instance(p,pose,error);if(!s) return false;a->pulled_id=s->id;}
            s->pose=pose;s->display_id=display->id;s->grab_dip=a->grab_dip;s->drag_pointer=event->desktop_point;
            s->dragging=result.command!=PP_COMMAND_END_DRAG;s->dirty=true;
        }
        if(event->kind==PP_EVENT_CANCEL||(event->kind==PP_EVENT_BUTTON_UP&&event->button==PP_BUTTON_LEFT)) {
            ProbeInstance *s=find_instance(p,a->pulled_id);if(s) s->dragging=false;
            reveal->held=false;a->pulled_id=0;
        }
        return true;
    }
    ProbeInstance *s=find_instance(p,event->instance_id);if(!s||event->surface!=s->surface) return true;
    s->interaction.displayed_pose=s->pose;
    if(event->kind==PP_EVENT_BUTTON_DOWN&&event->button==PP_BUTTON_LEFT) {
        if(s->scale<=0) return true;
        /* The press belongs to the presented raster. Its center can be across
         * a seam from the display that supplied that raster's scale. */
        s->grab_dip=(PPVec2){(event->desktop_point.x-s->pose.center.x)/s->scale,(event->desktop_point.y-s->pose.center.y)/s->scale};
        pp_platform_capture(s->surface,NULL);
    }
    PPInteractionResult result;pp_interaction_step(&s->interaction,event,&p->gestures,&result);
    if(result.command==PP_COMMAND_BEGIN_DRAG||result.command==PP_COMMAND_MOVE_DRAG||result.command==PP_COMMAND_END_DRAG) {
        const PPDisplay *d=pp_display_at(p->displays,p->display_count,event->desktop_point);
        if(d) {s->pose.center=pp_display_drag_center(d,event->desktop_point,s->grab_dip);s->display_id=d->id;s->drag_pointer=event->desktop_point;s->dragging=result.command!=PP_COMMAND_END_DRAG;s->dirty=true;}
    }
    if(event->kind==PP_EVENT_CANCEL) s->dragging=false;
    if(result.command==PP_COMMAND_DELETE) {size_t index=(size_t)(s-p->instances);pp_surface_destroy(s->surface);memmove(s,s+1,(p->instance_count-index-1)*sizeof(*s));p->instance_count--;}
    return true;
}
void pp_anchor_presenter_enable(PPAnchorPresenter *p,PPAnchor a,bool enabled) {if(p) pp_anchors_enable(&p->runtime,a,enabled);}
size_t pp_anchor_presenter_instance_count(const PPAnchorPresenter *p) {return p?p->instance_count:0;}
bool pp_anchor_presenter_instance_pose(const PPAnchorPresenter *p,size_t i,PPPose *out) {if(!p||!out||i>=p->instance_count) return false;*out=p->instances[i].pose;return true;}
const PPAnchorRuntime *pp_anchor_presenter_reveals(const PPAnchorPresenter *p) {return p?&p->runtime:NULL;}
void pp_anchor_presenter_destroy(PPAnchorPresenter *p) {
    if(!p) return;close_anchors(p);for(size_t i=0;i<p->instance_count;i++) pp_surface_destroy(p->instances[i].surface);
    free(p->instances);free(p->displays);pp_anchors_destroy(&p->runtime);pp_frame_destroy(&p->source);pp_frame_destroy(&p->target_source);free(p);
}
int pp_anchors_probe(unsigned duration_ms) {
    PPRenderer *renderer=NULL;PPAnchorPresenter *p=NULL;PPDisplay *d=NULL;size_t count=0;PPError error={0};int result=1;
    if(!pp_renderer_create(&renderer,&error)||!pp_anchor_presenter_create(&p,renderer,&error)) goto done;
    Uint64 start=SDL_GetTicks(),next=0;
    puts("Eight anchors per display: approach an edge target, then drag the procedural preview. Right-click deletes a pulled sprite.");
    while(!duration_ms||SDL_GetTicks()-start<duration_ms) {
        Uint64 now=SDL_GetTicks();
        if(now>=next) {pp_displays_free(d);d=NULL;if(!pp_platform_displays(&d,&count,&error)) goto done;next=now+250;}
        PPPlatformEvent event;while(pp_platform_poll(&event)) {if(event.kind==PP_EVENT_QUIT) {result=0;goto done;}if(!pp_anchor_presenter_event(p,&event,&error)) goto done;}
        PPVec2 pointer;if(!pp_platform_pointer(&pointer)) {snprintf(error.message,sizeof(error.message),"Cannot read desktop cursor");goto done;}
        if(!pp_anchor_presenter_update(p,d,count,pointer,now/1000.0,&error)) goto done;
        SDL_Delay(8);
    }result=0;
done:
    if(result) fprintf(stderr,"Anchor probe failed: %s\n",error.message);
    else printf("Anchor probe completed with %zu temporary sprites\n",pp_anchor_presenter_instance_count(p));
    pp_displays_free(d);pp_anchor_presenter_destroy(p);pp_renderer_destroy(renderer);return result;
}
