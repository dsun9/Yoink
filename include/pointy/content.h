#ifndef POINTY_CONTENT_H
#define POINTY_CONTENT_H
#include "pointy/model.h"
#include "pointy/retained.h"

typedef struct PPContentSources PPContentSources;
typedef struct PPContentConsumer PPContentConsumer;
typedef struct PPContentFrame {
  const PPFrame *frame;
  uint64_t content_id,source_revision;
  PPVec2 layout;
  bool visual_alpha;
  PPRect source_rect;
  PPSharedTexture *shared_texture; /* Filled only by a compatible native GL adapter. */
  const PPGrabArea *grab; /* Immutable source shape; GIF is an animation union. */
} PPContentFrame;
/* Owner-thread source service. Assets outlive the service and its consumers.
 * Each consumer has its own lifetime, target and clock. Equal immutable
 * content/size/density requests share one charged CPU result. */
bool pp_content_sources_create(PPContentSources **out,PPAssets *assets,PPError *error);
void pp_content_sources_destroy(PPContentSources *sources);
bool pp_content_consumer_create(PPContentSources *sources,PPContentConsumer **out,PPError *error);
void pp_content_consumer_destroy(PPContentConsumer *consumer);
bool pp_content_request(PPContentConsumer *consumer,PPItemSnapshot *snapshot,
    double longest_dip,double density,PPError *error);
/* PNG/GIF keep native bytes; bitmap-only text keeps useful strike detail;
 * scalable/mixed text and SVG keep canonical mapping while preparing the
 * endpoint. Elapsed time belongs to this caller; drawer passes zero. */
bool pp_content_frame(PPContentConsumer *consumer,PPItemSnapshot *snapshot,
    double longest_dip,double density,double elapsed,PPContentFrame *out,
    double *next_change,PPError *error);
/* This is the only completion dispatcher. It publishes CPU entries only;
 * native/UI owners validate and commit their own GPU bindings afterwards. */
void pp_content_sources_poll(PPContentSources *sources);
const PPTextResult *pp_content_candidate(const PPContentConsumer *consumer,uint64_t *revision);
const PPTextResult *pp_content_current(const PPContentConsumer *consumer,uint64_t *revision,size_t *bytes);
void pp_content_accept(PPContentConsumer *consumer);
void pp_content_reject(PPContentConsumer *consumer);
void pp_content_cancel(PPContentConsumer *consumer); /* Cancels only this consumer's desired work. */
size_t pp_content_sources_raster_bytes(const PPContentSources *sources);
/* App aggregate includes native/UI source and UI output textures. Shared
 * textures charge once; callers keep old allocations charged until commit. */
bool pp_content_gpu_reserve(PPContentSources *sources,size_t bytes,PPError *error);
void pp_content_gpu_release(PPContentSources *sources,size_t bytes);
PPGpuResources pp_content_gpu_resources(const PPContentSources *sources);
#endif
