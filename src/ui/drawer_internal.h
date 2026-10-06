#ifndef POINTY_DRAWER_INTERNAL_H
#define POINTY_DRAWER_INTERNAL_H
#include "pointy/runtime.h"
#include "pointy/ui.h"
typedef struct PPDrawerThumbnail {
  PPItemSnapshot *snapshot; /* Retained immutable content identity, not raster identity. */
  float scale;
  int width, height;
} PPDrawerThumbnail;
typedef struct PPDrawerCache {
  PPUIBackend *backend; /* Owned when no drawer has checked it out. */
  PPRenderer *renderer; /* Borrowed serialized runtime service. */
  bool in_use;
} PPDrawerCache;
bool pp_drawer_cache_warm(PPDrawerCache *cache,const PPDisplay *display,PPError *error);
void pp_drawer_cache_destroy(PPDrawerCache *cache);
bool pp_drawer_use_cache(PPDrawer *drawer,PPDrawerCache *cache,PPError *error);
struct PPDrawer {
  PPApp *app;
  uint64_t instance_id, generation;
  uint64_t focus_id;
  PPAnchor anchor;
  PPUIBackend *backend;
  PPRenderer *renderer;
  PPDrawerCache *cache; /* Borrowed until close; cache outlives this drawer. */
  unsigned
      textures[8]; /* Only visible rows; GPU textures, no retained CPU copy. */
  PPDrawerThumbnail thumbnails[8];
  PPRect bounds;
  float scale, raster_scale;
  bool dismissed, restore_focus, render_hidden;
  char diagnostic[512];
};
bool pp_drawer_valid(const PPDrawer *drawer);
size_t pp_drawer_count(const PPDrawer *drawer);
bool pp_drawer_select(PPDrawer *drawer, uint64_t item_id, double now,
                      PPError *error);
void pp_drawer_event(PPDrawer *drawer, const PPPlatformEvent *event);
void pp_drawer_layout(PPDrawer *drawer, const PPDisplay *displays,
                      size_t count);
bool pp_drawer_draw(PPDrawer *drawer, double now, PPError *error);
#endif
