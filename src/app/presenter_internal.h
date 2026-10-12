#ifndef POINTY_PRESENTER_INTERNAL_H
#define POINTY_PRESENTER_INTERNAL_H
#include "anchors.h"
#include "raster_worker.h"
typedef struct AnchorSurface {
  PPSurface *target, *preview;
  PPDisplay display;
  PPAnchor anchor;
  PPInteraction interaction;
  PPVec2 grab_dip;
  uint64_t pulled_id;
  double opacity, scale;
  double next_frame, animation_start;
  uint64_t revision;
  size_t target_bytes, preview_bytes;
  bool shown, failed;
} AnchorSurface;
/* Source acquisition policy stays outside retained placement/drawing. GIF
 * frame and scalable raster replacements provide their own source revision. */
typedef struct PPSourcePresentation {
  const PPFrame *frame;
  uint64_t content_id, source_revision;
  PPVec2 layout;
  bool visual_alpha;
  PPRect source_rect; /* Source texels mapped into canonical local DIPs. Zero means full layout. */
} PPSourcePresentation;
typedef struct PPRetainedPresentation {
  PPRenderer *renderer;
  PPItemSnapshot *snapshot;
  size_t gpu_bytes;
  int source_width, source_height;
  uint64_t content_id, source_revision;
  PPVec2 layout;
  PPPose drawn;
  PPRect bounds;
  double density, animation_start;
  bool visible, valid, visual_alpha;
  PPRect source_rect;
} PPRetainedPresentation;
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
  bool raster_requested;
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
  PPRendererMetrics retired_renderer_metrics;
  PPRasterWorker *worker;
  uint64_t generation;
  bool borrowed_worker;
};
bool pp_snapshot_is_scalable_text(const PPItemSnapshot *snapshot);
bool pp_scalable_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest,PPError *error);
bool pp_scalable_source(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,PPSourcePresentation *out,PPError *error);
void pp_scalable_poll(PPAnchorPresenter *p);
void pp_scalable_cancel(PPAnchorPresenter *p,PPSprite *s);
void pp_scalable_release(PPAnchorPresenter *p,PPSprite *s);
bool pp_snapshot_is_png(const PPItemSnapshot *snapshot);
bool pp_sprite_present(PPAnchorPresenter *p,PPSprite *sprite,
    const PPSceneInstance *instance,const PPDisplay *display,double elapsed,
    double *next,PPError *error);
void pp_sprite_release_presentation(PPAnchorPresenter *p,PPSprite *sprite);
void pp_sprite_hide(PPSprite *sprite);
bool pp_source_visual_alpha(const PPFrame *source);
bool pp_retained_present(PPAnchorPresenter *p,PPSurface *surface,
    PPRetainedPresentation *retained,PPSourcePresentation source,PPPose pose,
    const PPDisplay *display,PPError *error);
void pp_retained_release(PPAnchorPresenter *p,PPRetainedPresentation *retained);

PPSprite *pp_sprites_find(PPAnchorPresenter *p, uint64_t id);
PPSprite *pp_sprites_create(PPAnchorPresenter *p, PPAnchor anchor, PPPose pose,
                            double now, PPError *error);
bool pp_present(PPAnchorPresenter *p, PPSurface *surface, const PPFrame *source,
                PPPose pose, const PPDisplay *display, size_t *retained, PPError *error);
bool pp_present_content(PPAnchorPresenter *p, PPSurface *surface,
                        PPItemSnapshot *snapshot, PPPose pose,
                        const PPDisplay *display, double elapsed, double *next,
                        size_t *bytes, PPError *error);
bool pp_sprites_update(PPAnchorPresenter *p, double now, PPError *error);
bool pp_sprites_event(PPAnchorPresenter *p, const PPPlatformEvent *event,
                      PPError *error);
void pp_sprites_sync(PPAnchorPresenter *p);
void pp_sprites_clear(PPAnchorPresenter *p);
#endif
