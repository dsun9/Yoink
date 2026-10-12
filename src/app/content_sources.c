#include "content_sources.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct ContentEntry {
  struct ContentEntry *next;
  PPItemSnapshot *snapshot;
  PPRasterKey key;
  PPTextResult result;
  PPError error;
  size_t references,bytes;
  bool requested,retry,done;
} ContentEntry;
struct PPContentConsumer {
  struct PPContentConsumer *next;
  PPContentSources *sources;
  ContentEntry *desired,*current;
  uint64_t rejected,reported;
};
struct PPContentSources {
  PPAssets *assets;
  PPRasterWorker *worker;
  uint64_t generation,next_owner;
  bool owns_worker;
  ContentEntry *entries;
  PPContentConsumer *consumers;
};
static bool fail(PPError *error,int code,const char *message) {
  if(error){error->code=code;snprintf(error->message,sizeof(error->message),"%s",message);}return false;
}
typedef struct ContentCapture {
  PPTextStyle style;
  char *text,*family;
  uint8_t *primary,*fallback,*svg;
  size_t primary_bytes,fallback_bytes,svg_bytes;
  PPVec2 layout;
  double scale;
  int width,height;
} ContentCapture;
static void destroy_capture(void *data) {
  ContentCapture *c=data;if(!c)return;
  free(c->text);free(c->family);free(c->primary);free(c->fallback);free(c->svg);free(c);
}
static bool generate(void *data,size_t limit,PPTextResult *out,PPSourceMetrics *delta,PPError *error) {
  ContentCapture *c=data;
  if(c->svg) {
    if(!pp_svg_render_bounded((PPBytes){c->svg,c->svg_bytes},c->width,c->height,limit,PP_WORKER_SCRATCH_LIMIT,&out->frame,error))return false;
    out->layout_size=c->layout;out->raster_density=c->scale;delta->raster_builds=1;return true;
  }
  PPAssets *assets=NULL;bool ok=pp_assets_create(&assets,error);PPTextStyle style=c->style;style.requested_family=c->family;
  if(ok&&c->primary_bytes)ok=pp_asset_import(assets,(PPBytes){c->primary,c->primary_bytes},PP_ASSET_FONT,&style.primary_asset,error);
  if(ok&&c->fallback_bytes)ok=pp_asset_import(assets,(PPBytes){c->fallback,c->fallback_bytes},PP_ASSET_FONT,&style.fallback_asset,error);
  if(ok)ok=pp_text_render_result_bounded(assets,c->text,&style,c->scale,limit,out,NULL,error);
  *delta=pp_assets_source_metrics(assets);pp_assets_destroy(assets);return ok;
}
static bool copy_font(PPAssets *assets,PPAssetId id,uint8_t **out,size_t *bytes,PPError *error) {
  PPAsset *asset=pp_asset_retain(assets,id);if(!asset)return true;
  PPBytes source=pp_asset_source(asset);*bytes=source.size;
  if(source.size>16u*1024*1024){pp_asset_release(asset);return fail(error,PP_ERROR_RESOURCE_LIMIT,"Worker font source exceeds 16 MiB");}
  *out=malloc(source.size);if(*out)memcpy(*out,source.data,source.size);pp_asset_release(asset);
  return *out!=NULL||fail(error,PP_ERROR_MEMORY,"Cannot copy worker font bytes");
}
static bool target_valid(PPItemSnapshot *snapshot,double longest,double density,PPError *error) {
  const PPItem *item=pp_item_snapshot_item(snapshot);PPVec2 layout=pp_item_snapshot_size(snapshot);
  double scale=longest*density/fmax(layout.x,layout.y),w=ceil(layout.x*scale),h=ceil(layout.y*scale);
  if(item&&item->kind==PP_ITEM_TEXT){w+=64;h+=64;}
  if(!item||!isfinite(scale)||scale<=0||w<1||h<1||w>8192||h>8192||w*h>PP_WORKER_CANDIDATE_LIMIT/5.0)
    return fail(error,PP_ERROR_RESOURCE_LIMIT,"Scalable target exceeds bounded worker raster limits");
  if(item->kind==PP_ITEM_TEXT&&(strlen(item->text)>65536||strlen(item->text_style.requested_family)>4096))
    return fail(error,PP_ERROR_RESOURCE_LIMIT,"Worker text or family exceeds capture limits");
  return true;
}
bool pp_content_capture(PPItemSnapshot *snapshot,double longest,double density,PPRasterCapture *out,size_t *limit,PPError *error) {
  *out=(PPRasterCapture){0};*limit=0;
  const PPItem *item=pp_item_snapshot_item(snapshot);PPVec2 layout=pp_item_snapshot_size(snapshot);
  double scale=longest*density/fmax(layout.x,layout.y);double w=ceil(layout.x*scale),h=ceil(layout.y*scale);
  bool svg=item&&item->kind==PP_ITEM_IMAGE;
  if(!svg){w+=64;h+=64;}
  if(!item||!isfinite(scale)||scale<=0||w<1||h<1||w>8192||h>8192||w*h>PP_WORKER_CANDIDATE_LIMIT/5.0)
    return fail(error,PP_ERROR_RESOURCE_LIMIT,"Scalable target exceeds bounded worker raster limits");
  ContentCapture *c=calloc(1,sizeof(*c));if(!c)return fail(error,PP_ERROR_MEMORY,"Cannot allocate immutable content capture");
  c->scale=scale;c->layout=layout;c->width=(int)w;c->height=(int)h;size_t bytes=sizeof(*c);
  if(svg) {
    PPAsset *asset=pp_asset_retain(pp_item_snapshot_assets(snapshot),item->image_asset);PPBytes source=pp_asset_source(asset);
    if(!asset||pp_asset_kind(asset)!=PP_ASSET_SVG||!source.size||source.size>32u*1024*1024-sizeof(*c)) {
      pp_asset_release(asset);destroy_capture(c);return fail(error,PP_ERROR_RESOURCE_LIMIT,"SVG source exceeds worker capture limit");
    }
    c->svg=malloc(source.size);if(c->svg)memcpy(c->svg,source.data,source.size);c->svg_bytes=source.size;pp_asset_release(asset);
    if(!c->svg){destroy_capture(c);return fail(error,PP_ERROR_MEMORY,"Cannot copy immutable SVG source");}bytes+=c->svg_bytes;
  } else {
    if(strlen(item->text)>65536||strlen(item->text_style.requested_family)>4096) {
      destroy_capture(c);return fail(error,PP_ERROR_RESOURCE_LIMIT,"Worker text or family exceeds capture limits");
    }
    size_t text_bytes=strlen(item->text)+1,family_bytes=strlen(item->text_style.requested_family)+1;
    c->style=item->text_style;c->style.requested_family=NULL;c->text=malloc(text_bytes);c->family=malloc(family_bytes);
    if(c->text)memcpy(c->text,item->text,text_bytes);if(c->family)memcpy(c->family,item->text_style.requested_family,family_bytes);
    if(!c->text||!c->family){destroy_capture(c);return fail(error,PP_ERROR_MEMORY,"Cannot copy worker text or family");}
    if(!copy_font(pp_item_snapshot_assets(snapshot),c->style.primary_asset,&c->primary,&c->primary_bytes,error)||
       !copy_font(pp_item_snapshot_assets(snapshot),c->style.fallback_asset,&c->fallback,&c->fallback_bytes,error)){destroy_capture(c);return false;}
    bytes+=text_bytes+family_bytes+c->primary_bytes+c->fallback_bytes;
  }
  *limit=(size_t)w*(size_t)h*5;*out=(PPRasterCapture){c,bytes,generate,destroy_capture};return true;
}
bool pp_content_sources_create_worker(PPContentSources **out,PPAssets *assets,PPRasterWorker *worker,uint64_t generation,PPError *error) {
  if(out)*out=NULL;if(!out||!assets)return fail(error,PP_ERROR_ARGUMENT,"Content sources require assets and output");
  PPContentSources *s=calloc(1,sizeof(*s));if(!s)return fail(error,PP_ERROR_MEMORY,"Cannot allocate content sources");
  static uint64_t next_generation;s->assets=assets;s->worker=worker;s->generation=generation?generation:++next_generation;
  s->owns_worker=worker==NULL;s->next_owner=1;*out=s;return true;
}
bool pp_content_sources_create(PPContentSources **out,PPAssets *assets,PPError *error) {return pp_content_sources_create_worker(out,assets,NULL,0,error);}
PPRasterWorker *pp_content_sources_worker(const PPContentSources *s){return s?s->worker:NULL;}
uint64_t pp_content_sources_generation(const PPContentSources *s){return s?s->generation:0;}
void pp_content_sources_detach_worker(PPContentSources *s){if(s)s->owns_worker=false;}
static void release_entry(PPContentSources *s,ContentEntry *e) {
  if(!e||--e->references)return;
  pp_raster_worker_cancel(s->worker,s->generation,e->key.instance_id);
  ContentEntry **link=&s->entries;while(*link&&*link!=e)link=&(*link)->next;if(*link)*link=e->next;
  pp_frame_destroy(&e->result.frame);pp_assets_release_raster(s->assets,e->bytes);pp_item_snapshot_release(e->snapshot);free(e);
}
void pp_content_cancel(PPContentConsumer *c) {
  if(!c)return;release_entry(c->sources,c->desired);c->desired=NULL;c->rejected=c->reported=0;
}
void pp_content_consumer_destroy(PPContentConsumer *c) {
  if(!c)return;PPContentSources *s=c->sources;pp_content_cancel(c);release_entry(s,c->current);
  PPContentConsumer **link=&s->consumers;while(*link&&*link!=c)link=&(*link)->next;if(*link)*link=c->next;free(c);
}
void pp_content_sources_destroy(PPContentSources *s) {
  if(!s)return;while(s->consumers)pp_content_consumer_destroy(s->consumers);
  if(s->owns_worker)pp_raster_worker_destroy(s->worker);free(s);
}
bool pp_content_consumer_create(PPContentSources *s,PPContentConsumer **out,PPError *error) {
  if(out)*out=NULL;if(!s||!out)return fail(error,PP_ERROR_ARGUMENT,"Content consumer requires a source owner");
  PPContentConsumer *c=calloc(1,sizeof(*c));if(!c)return fail(error,PP_ERROR_MEMORY,"Cannot allocate content consumer");
  c->sources=s;c->next=s->consumers;s->consumers=c;*out=c;return true;
}
static bool start_entry(PPContentSources *s,ContentEntry *e,PPError *error) {
  /* Admission precedes copying fonts/source or reserving candidate bytes. */
  if(!pp_raster_worker_can_request(s->worker,s->generation,e->key.instance_id)){e->retry=true;return true;}
  PPRasterCapture capture={0};size_t limit;
  if(!pp_content_capture(e->snapshot,e->key.longest_dip,e->key.density,&capture,&limit,error))return false;
  if(!s->worker&&!pp_raster_worker_create(&s->worker,error)){capture.destroy(capture.data);return false;}
  if(!pp_raster_worker_request(s->worker,e->key,capture,s->assets,limit,error))return false;
  e->requested=true;e->retry=false;return true;
}
bool pp_content_request(PPContentConsumer *c,PPItemSnapshot *snapshot,double longest,double density,PPError *error) {
  if(!c||!snapshot)return fail(error,PP_ERROR_ARGUMENT,"Content request requires a consumer and snapshot");
  if(!target_valid(snapshot,longest,density,error))return false;
  PPContentSources *s=c->sources;uint64_t identity=pp_item_snapshot_revision(snapshot);ContentEntry *e=c->desired;
  if(e&&e->key.content_revision==identity&&e->key.longest_dip==longest&&e->key.density==density) {
    if(e->retry&&!start_entry(s,e,error))return false;return true;
  }
  pp_content_cancel(c);
  for(e=s->entries;e;e=e->next)if(e->key.content_revision==identity&&e->key.longest_dip==longest&&e->key.density==density)break;
  if(!e) {
    if(s->next_owner==UINT64_MAX)return fail(error,PP_ERROR_RESOURCE_LIMIT,"Content ownership IDs exhausted");
    e=calloc(1,sizeof(*e));if(!e)return fail(error,PP_ERROR_MEMORY,"Cannot allocate shared content entry");
    e->snapshot=pp_item_snapshot_retain(snapshot);e->key=(PPRasterKey){s->generation,s->next_owner++,identity,1,longest,density};
    e->next=s->entries;s->entries=e;
  }
  e->references++;c->desired=e;
  if(!e->done&&!e->requested&&!start_entry(s,e,error)){pp_content_cancel(c);return false;}return true;
}
void pp_content_sources_poll(PPContentSources *s) {
  if(!s)return;PPRasterCompletion completion={0};
  /* Bounded work per owner dispatch; requests never create a second poller. */
  for(unsigned count=0;count<32&&pp_raster_worker_poll(s->worker,s->generation,&completion);count++) {
    ContentEntry *e=s->entries;while(e&&(e->key.instance_id!=completion.key.instance_id||e->key.serial!=completion.key.serial))e=e->next;
    if(e&&e->requested) {
      e->requested=false;e->done=true;e->error=completion.error;
      if(completion.success){e->bytes=pp_raster_completion_take(&completion);e->result=completion.result;completion.result=(PPTextResult){0};}
    }
    pp_raster_completion_release(&completion);
  }
}
PPRasterKey pp_content_target(const PPContentConsumer *c){return c&&c->desired?c->desired->key:(PPRasterKey){0};}
bool pp_content_requested(const PPContentConsumer *c){return c&&c->desired&&c->desired->requested;}
bool pp_content_retry(const PPContentConsumer *c){return c&&c->desired&&c->desired->retry;}
bool pp_content_error(PPContentConsumer *c,PPError *error) {
  if(!c||!c->desired||!c->desired->done||!c->desired->error.code||c->reported==c->desired->key.instance_id)return false;
  c->reported=c->desired->key.instance_id;if(error)*error=c->desired->error;return true;
}
const PPTextResult *pp_content_candidate(const PPContentConsumer *c,uint64_t *revision) {
  ContentEntry *e=c?c->desired:NULL;
  if(!e||!e->result.frame.pixels||e==c->current||c->rejected==e->key.instance_id)return NULL;
  if(revision)*revision=e->key.instance_id+2;return &e->result;
}
const PPTextResult *pp_content_current(const PPContentConsumer *c,uint64_t *revision,size_t *bytes) {
  ContentEntry *e=c?c->current:NULL;if(!e)return NULL;
  if(revision)*revision=e->key.instance_id+2;if(bytes)*bytes=e->bytes;return &e->result;
}
void pp_content_accept(PPContentConsumer *c) {
  if(!c||!c->desired||!c->desired->result.frame.pixels||c->desired==c->current)return;
  release_entry(c->sources,c->current);c->current=c->desired;c->current->references++;c->rejected=0;
}
void pp_content_reject(PPContentConsumer *c) {
  if(!c||!c->desired)return;
  ContentEntry *e=c->desired;c->rejected=e->key.instance_id;
  bool useful=false;
  for(PPContentConsumer *other=c->sources->consumers;other;other=other->next)
    if(other->current==e||(other->desired==e&&other->rejected!=e->key.instance_id)){useful=true;break;}
  /* A publication rejected by every owner has no useful CPU candidate. Keep
   * the failed target identity to avoid resubmitting it on every UI tick. */
  if(!useful){pp_frame_destroy(&e->result.frame);pp_assets_release_raster(c->sources->assets,e->bytes);e->bytes=0;}
}
size_t pp_content_sources_raster_bytes(const PPContentSources *s) {
  size_t bytes=0;for(ContentEntry *e=s?s->entries:NULL;e;e=e->next)bytes+=e->bytes;return bytes;
}
