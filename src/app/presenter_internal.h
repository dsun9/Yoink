#ifndef POINTY_PRESENTER_INTERNAL_H
#define POINTY_PRESENTER_INTERNAL_H
#include "anchors.h"
#include "raster_worker.h"
#include "content_sources.h"
/* Source acquisition policy stays outside retained placement/drawing. GIF
 * frame and scalable raster replacements provide their own source revision. */
typedef PPContentFrame PPSourcePresentation;
typedef struct PPRetainedPresentation {
  PPRenderer *renderer;
  PPItemSnapshot *snapshot;
  size_t gpu_bytes,output_bytes;
  int source_width, source_height;
  uint64_t content_id, source_revision;
  PPVec2 layout;
  PPPose drawn;
  PPRect bounds;
  double density, animation_start;
  bool visible, valid, visual_alpha;
  struct PPGifEntry *gif_entry;
  PPRect source_rect;
} PPRetainedPresentation;
typedef struct AnchorSurface {
  PPSurface *target, *preview;
  PPDisplay display;
  PPAnchor anchor;
  PPInteraction interaction;
  PPVec2 grab_dip;
  uint64_t pulled_id;
  double opacity, scale, density;
  double next_frame, animation_start;
  uint64_t revision;
  size_t target_bytes, preview_bytes;
  bool shown, failed;
  PPRetainedPresentation retained, target_retained;
  PPContentConsumer *content;
  PPItemSnapshot *snapshot,*pull_snapshot;
  PPSourcePresentation source;
} AnchorSurface;
typedef struct PPSprite {
  PPSurface *surface;
  uint64_t id, display_id;
  PPPose pose, drawn;
  PPInteraction interaction;
  PPVec2 grab_dip, drag_pointer;
  double scale;
  double next_frame;
  size_t raster_bytes;
  bool dirty, dragging, replaced;
  PPItemSnapshot *seen_snapshot; /* Retained until mapping is synchronized. */
  double seen_animation_start;
  PPPose ease_from, ease_target;
  double rotate_start, resize_start;
  bool rotating, resizing;
  bool sharp_pending;
  PPRetainedPresentation retained;
  double drawable_density;
  PPTextResult scalable;
  size_t scalable_bytes;
  uint64_t scalable_identity, scalable_revision, raster_serial;
  PPRasterKey raster_target;
  bool raster_requested, raster_retry;
  PPContentConsumer *content;
} PPSprite;
struct PPAnchorPresenter {
  PPRenderer *renderer;
  PPProject *project;
  PPScene *scene;
  PPAssets *assets;
  PPFrame target_source;
  PPAnchorRuntime runtime;
  AnchorSurface *anchors;
  size_t anchor_count;
  PPDisplay *displays;
  size_t display_count;
  PPSprite *instances;
  size_t instance_count;
  PPGestureConfig gestures;
  bool owns_scene;
  uint64_t drawer_request;
  PPSurface *failed_surface;
  PPError notice;
  PPPresentationMetrics metrics;
  PPGpuResources gpu;
  size_t retired_gpu_bytes;
  PPRendererMetrics retired_renderer_metrics;
  PPRasterWorker *worker;
  uint64_t generation;
  bool borrowed_worker;
  PPRenderer *texture_owner;
  struct PPGifEntry *gif_entries;
  PPContentSources *sources;
  bool borrowed_sources;
};
bool pp_presenter_sources(PPAnchorPresenter *p,PPError *error);
bool pp_snapshot_is_scalable_text(const PPItemSnapshot *snapshot);
bool pp_snapshot_is_svg(const PPItemSnapshot *snapshot);
bool pp_snapshot_is_scalable(const PPItemSnapshot *snapshot);
bool pp_scalable_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest,PPError *error);
bool pp_scalable_source(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,PPSourcePresentation *out,PPError *error);
void pp_scalable_poll(PPAnchorPresenter *p);
void pp_scalable_cancel(PPAnchorPresenter *p,PPSprite *s);
void pp_scalable_release(PPAnchorPresenter *p,PPSprite *s);
bool pp_snapshot_is_gif(const PPItemSnapshot *snapshot);
bool pp_presenter_texture_owner(PPAnchorPresenter *p,PPError *error);
bool pp_gif_acquire(PPAnchorPresenter *p,PPItemSnapshot *snapshot,double elapsed,PPSourcePresentation *source,double *next,struct PPGifEntry **out,PPError *error);
void pp_gif_release(PPAnchorPresenter *p,struct PPGifEntry *entry);
void pp_gif_clear(PPAnchorPresenter *p);
bool pp_snapshot_is_png(const PPItemSnapshot *snapshot);
bool pp_snapshot_is_bitmap_text(const PPItemSnapshot *snapshot);
bool pp_retained_bitmap_text_source(PPItemSnapshot *snapshot,PPSourcePresentation *source,PPError *error);
bool pp_sprite_present(PPAnchorPresenter *p,PPSprite *sprite,
    const PPSceneInstance *instance,const PPDisplay *display,double elapsed,
    double *next,PPError *error);
void pp_sprite_release_presentation(PPAnchorPresenter *p,PPSprite *sprite);
void pp_sprite_hide(PPAnchorPresenter *p,PPSprite *sprite);
void pp_retained_hide(PPAnchorPresenter *p,PPSurface *surface,PPRetainedPresentation *retained);
bool pp_source_visual_alpha(const PPFrame *source);
bool pp_present_source(PPAnchorPresenter *p,PPSurface *surface,PPSourcePresentation source,
    PPPose pose,const PPDisplay *display,size_t *retained,PPError *error);
bool pp_retained_present(PPAnchorPresenter *p,PPSurface *surface,
    PPRetainedPresentation *retained,PPSourcePresentation source,PPPose pose,
    const PPDisplay *display,PPError *error);
void pp_retained_release(PPAnchorPresenter *p,PPRetainedPresentation *retained);
bool pp_presenter_gpu_reserve(PPAnchorPresenter *p,size_t bytes,PPError *error);
void pp_presenter_gpu_release(PPAnchorPresenter *p,size_t bytes);

PPSprite *pp_sprites_find(PPAnchorPresenter *p, uint64_t id);
PPSprite *pp_sprites_create(PPAnchorPresenter *p, PPAnchor anchor, PPPose pose,
                            double now, PPError *error);
PPSprite *pp_sprites_create_snapshot(PPAnchorPresenter *p,PPItemSnapshot *snapshot,
    PPAnchor anchor,PPPose pose,double now,PPError *error);
bool pp_present(PPAnchorPresenter *p, PPSurface *surface, const PPFrame *source,
                PPPose pose, const PPDisplay *display, size_t *retained, PPError *error);
/* Explicit legacy comparison/capture adapter; no normal production callers.
 * Native compatibility consumers use shared PPSourcePresentation instead. */
bool pp_capture_present_content(PPAnchorPresenter *p, PPSurface *surface,
                        PPItemSnapshot *snapshot, PPPose pose,
                        const PPDisplay *display, double elapsed, double *next,
                        size_t *bytes, PPError *error);
bool pp_sprites_update(PPAnchorPresenter *p, double now, PPError *error);
bool pp_sprites_event(PPAnchorPresenter *p, const PPPlatformEvent *event,
                      PPError *error);
void pp_sprites_sync(PPAnchorPresenter *p);
void pp_sprites_clear(PPAnchorPresenter *p);
#endif
