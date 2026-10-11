#ifndef POINTY_PRESENTER_INTERNAL_H
#define POINTY_PRESENTER_INTERNAL_H
#include "anchors.h"
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
  PPPose ease_from, ease_target;
  double rotate_start, resize_start;
  bool rotating, resizing;
  bool sharp_pending;
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
};

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
