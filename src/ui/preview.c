#include "preview.h"
#include "backend.h"
#include "../core/geometry_internal.h"
#include <math.h>
#include <string.h>
static void release_graphics(PPUIContentPreview *p) {
  pp_renderer_destroy(p->renderer);p->renderer=NULL;
  pp_content_gpu_release(p->sources,p->source_bytes+p->target_bytes);
  p->source_bytes=p->target_bytes=0;p->texture=0;p->width=p->height=0;p->valid=false;
  p->uploaded_id=0;p->source_width=p->source_height=0;
}
void pp_ui_preview_release(PPUIContentPreview *p) {
  if(!p)return;release_graphics(p);pp_content_consumer_destroy(p->consumer);
  pp_item_snapshot_release(p->snapshot);memset(p,0,sizeof(*p));
}
bool pp_ui_preview_update(PPUIBackend *ui,PPApp *app,PPUIContentPreview *p,
    PPItemSnapshot *snapshot,double longest,double angle,double opacity,double density,
    double elapsed,double *next,PPError *error) {
  if(!snapshot||opacity<=0) {pp_ui_preview_release(p);*next=HUGE_VAL;return true;}
  if(p->snapshot&&pp_item_snapshot_item(p->snapshot)->id!=pp_item_snapshot_item(snapshot)->id)
    pp_ui_preview_release(p);
  if(!p->sources)p->sources=pp_app_content_sources(app,error);
  if(!p->sources||(!p->consumer&&!pp_content_consumer_create(p->sources,&p->consumer,error)))return false;
  pp_content_sources_poll(p->sources);PPContentFrame source={0};
  if(!pp_content_frame(p->consumer,snapshot,longest,density,elapsed,&source,next,error))return false;
  if(!source.frame)return true;
  if(!source.visual_alpha){release_graphics(p);pp_content_accept(p->consumer);return true;}
  if(!source.source_rect.w&&!source.source_rect.h)source.source_rect=(PPRect){0,0,source.layout.x,source.layout.y};
  PPPose pose={{0,0},longest,angle,opacity};
  bool changed=source.content_id!=p->source.content_id||source.source_revision!=p->source.source_revision;
  if(p->valid&&!changed&&!memcmp(&pose,&p->pose,sizeof(pose))&&p->density==density&&
      !memcmp(&source.layout,&p->source.layout,sizeof(source.layout))&&
      !memcmp(&source.source_rect,&p->source.source_rect,sizeof(source.source_rect)))return true;
  int w,h;if(!pp_geometry_source_extent(source.layout,source.source_rect,longest,angle,density,&w,&h)||w>8192||h>8192) {
    if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"UI preview exceeds 8192 pixels per axis"};return false;
  }
  bool source_overlap=!p->renderer||source.content_id!=p->uploaded_id||
      source.frame->width!=p->source_width||source.frame->height!=p->source_height;
  size_t source_bytes=source_overlap?(size_t)source.frame->width*source.frame->height*4:0;
  size_t target_bytes=(size_t)w*h*4;
  if(!pp_content_gpu_reserve(p->sources,source_bytes+target_bytes,error)){pp_content_reject(p->consumer);return false;}
  PPRendererMetrics before=pp_renderer_metrics(p->renderer);
  bool ok=p->renderer||pp_renderer_create_for_ui(&p->renderer,SDL_GetWindowID(ui->window),ui->gl_context,error);
  if(ok)ok=pp_renderer_preview_source(p->renderer,source.frame,source.content_id,source.source_revision,
      source.layout,source.source_rect,longest,angle,opacity,density,&p->texture,&p->width,&p->height,error);
  bool uploaded=pp_renderer_metrics(p->renderer).source_uploads>before.source_uploads;
  if(uploaded){p->uploaded_id=source.content_id;p->source_width=source.frame->width;p->source_height=source.frame->height;}
  if(source_overlap&&uploaded){pp_content_gpu_release(p->sources,p->source_bytes);p->source_bytes=source_bytes;}
  else pp_content_gpu_release(p->sources,source_bytes);
  if(ok) {
    pp_content_gpu_release(p->sources,p->target_bytes);p->target_bytes=target_bytes;
    p->source=source;p->pose=pose;p->density=density;p->valid=true;pp_content_accept(p->consumer);
    if(p->snapshot!=snapshot){pp_item_snapshot_retain(snapshot);pp_item_snapshot_release(p->snapshot);p->snapshot=snapshot;}
  } else {pp_content_gpu_release(p->sources,target_bytes);pp_content_reject(p->consumer);}
  return ok;
}
