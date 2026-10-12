#ifndef POINTY_UI_PREVIEW_H
#define POINTY_UI_PREVIEW_H
#include "pointy/ui.h"
#include "pointy/content.h"
typedef struct PPUIContentPreview {
  PPContentSources *sources;
  PPContentConsumer *consumer;
  PPRenderer *renderer;
  PPItemSnapshot *snapshot;
  PPContentFrame source;
  unsigned texture;
  int width,height;
  PPPose pose;
  double density;
  size_t source_bytes,target_bytes;
  uint64_t uploaded_id;
  int source_width,source_height;
  bool valid;
} PPUIContentPreview;
bool pp_ui_preview_update(PPUIBackend *ui,PPApp *app,PPUIContentPreview *preview,
    PPItemSnapshot *snapshot,double longest,double angle,double opacity,
    double density,double elapsed,double *next,PPError *error);
void pp_ui_preview_release(PPUIContentPreview *preview);
#endif
