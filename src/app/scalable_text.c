#include "presenter_internal.h"
#include <math.h>
#include <string.h>

bool pp_snapshot_is_scalable_text(const PPItemSnapshot *snapshot) {
  const PPTextResult *text=pp_item_snapshot_text_result(snapshot);return text&&!text->artwork.bitmap_only;
}
bool pp_presenter_sources(PPAnchorPresenter *p,PPError *error) {
  if(!p->sources&&!pp_content_sources_create_worker(&p->sources,p->assets,p->worker,p->generation,error))return false;
  p->generation=pp_content_sources_generation(p->sources);p->worker=pp_content_sources_worker(p->sources);return true;
}
static void state(PPAnchorPresenter *p,PPSprite *s) {
  p->worker=pp_content_sources_worker(p->sources);s->raster_target=pp_content_target(s->content);
  s->raster_requested=pp_content_requested(s->content);s->raster_retry=pp_content_retry(s->content);
}
bool pp_scalable_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest,PPError *error) {
  if(!pp_snapshot_is_scalable(instance->snapshot))return true;
  if(!pp_presenter_sources(p,error)||(!s->content&&!pp_content_consumer_create(p->sources,&s->content,error)))return false;
  bool ok=pp_content_request(s->content,instance->snapshot,longest,display->drawable_pixels_per_dip,error);
  state(p,s);return ok;
}
bool pp_scalable_defer_if_busy(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest) {
  PPError error={0};return pp_scalable_request(p,s,instance,display,longest,&error)&&s->raster_retry;
}
void pp_scalable_cancel(PPAnchorPresenter *p,PPSprite *s) {
  (void)p;pp_content_cancel(s->content);s->raster_requested=s->raster_retry=false;s->raster_serial++;
}
void pp_scalable_release(PPAnchorPresenter *p,PPSprite *s) {
  (void)p;pp_content_consumer_destroy(s->content);s->content=NULL;
  s->scalable=(PPTextResult){0};s->scalable_bytes=0;s->raster_requested=s->raster_retry=false;
}
bool pp_scalable_source(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,PPSourcePresentation *out,PPError *error) {
  double target=s->resizing?s->ease_target.longest_dip:s->pose.longest_dip;
  if(!pp_scalable_request(p,s,instance,display,target,error))return false;
  double next;return pp_content_frame(s->content,instance->snapshot,target,display->drawable_pixels_per_dip,0,out,&next,error);
}
void pp_scalable_poll(PPAnchorPresenter *p) {
  /* Revalidate live consumers before the shared dispatcher consumes a result.
   * Entries have stable owner IDs, independent of sprite/item IDs. */
  for(size_t i=0;i<p->instance_count;i++) {
    PPSprite *s=p->instances+i;if(!s->content)continue;
    const PPSceneInstance *instance=pp_scene_get(p->scene,s->id);const PPDisplay *d=NULL;
    for(size_t j=0;j<p->display_count;j++)if(p->displays[j].id==s->display_id)d=p->displays+j;
    PPRasterKey key=pp_content_target(s->content);
    if(instance&&d&&pp_content_matches(s->content,instance->snapshot,s->resizing?s->ease_target.longest_dip:s->pose.longest_dip,d->drawable_pixels_per_dip)) {
      PPError ignored={0};pp_content_request(s->content,instance->snapshot,s->resizing?s->ease_target.longest_dip:s->pose.longest_dip,d->drawable_pixels_per_dip,&ignored);key=pp_content_target(s->content);
    }
    if(key.content_revision&&(!instance||!d||pp_item_snapshot_revision(instance->snapshot)!=key.content_revision||
        d->drawable_pixels_per_dip!=key.density||(s->resizing?s->ease_target.longest_dip:s->pose.longest_dip)!=key.longest_dip))pp_scalable_cancel(p,s);
  }
  pp_content_sources_poll(p->sources);
  for(size_t i=0;i<p->instance_count;) {
    PPSprite *s=p->instances+i;if(!s->content){i++;continue;}state(p,s);
    PPError error={0};if(pp_content_error(s->content,&error)){p->notice=error;s->sharp_pending=false;}
    uint64_t revision=0;const PPTextResult *candidate=pp_content_candidate(s->content,&revision);
    const PPSceneInstance *instance=pp_scene_get(p->scene,s->id);const PPDisplay *d=NULL;
    for(size_t j=0;j<p->display_count;j++)if(p->displays[j].id==s->display_id)d=p->displays+j;
    if(!candidate||!instance||!d){i++;continue;}
    PPRasterKey key=pp_content_target(s->content);
    PPSourcePresentation source={0};double next;
    if(!pp_content_frame(s->content,instance->snapshot,key.longest_dip,key.density,0,&source,&next,&error)){p->notice=error;i++;continue;}
    if(!pp_retained_present(p,s->surface,&s->retained,source,s->pose,d,&error)) {
      p->notice=error;pp_content_reject(s->content);
      if(p->failed_surface==s->surface) {
        pp_surface_cancel_input(s->surface);pp_scene_delete(p->scene,s->id);pp_sprite_release_presentation(p,s);
        pp_surface_destroy(s->surface);pp_item_snapshot_release(s->seen_snapshot);
        memmove(s,s+1,(p->instance_count-i-1)*sizeof(*s));p->instance_count--;p->failed_surface=NULL;continue;
      }
      s->sharp_pending=false;i++;continue;
    }
    pp_content_accept(s->content);size_t bytes=0;const PPTextResult *current=pp_content_current(s->content,&revision,&bytes);
    s->scalable=*current;s->scalable_bytes=bytes;s->scalable_identity=key.content_revision;s->scalable_revision=revision;s->dirty=true;
    if(s->sharp_pending) {
      p->metrics.sharp_publications++;p->metrics.last_sharp_input_timestamp=s->resize_start;
      p->metrics.last_sharp_instance_id=s->id;p->metrics.last_sharp_content_revision=key.content_revision;
      p->metrics.last_sharp_longest_dip=key.longest_dip;p->metrics.last_sharp_drawable_density=key.density;s->sharp_pending=false;
    }
    i++;
  }
}
