#ifndef POINTY_APP_ANCHORS_H
#define POINTY_APP_ANCHORS_H
#include "pointy/interaction.h"
typedef struct PPAnchorPresenter PPAnchorPresenter;
/* Procedural default for the display/input probe. Renderer is borrowed. */
bool pp_anchor_presenter_create(PPAnchorPresenter **out,PPRenderer *renderer,PPError *error);
bool pp_anchor_presenter_update(PPAnchorPresenter *p,const PPDisplay *displays,size_t count,PPVec2 pointer,double now,PPError *error);
bool pp_anchor_presenter_event(PPAnchorPresenter *p,const PPPlatformEvent *event,PPError *error);
void pp_anchor_presenter_enable(PPAnchorPresenter *p,PPAnchor anchor,bool enabled);
size_t pp_anchor_presenter_instance_count(const PPAnchorPresenter *p);
bool pp_anchor_presenter_instance_pose(const PPAnchorPresenter *p,size_t index,PPPose *out);
const PPAnchorRuntime *pp_anchor_presenter_reveals(const PPAnchorPresenter *p);
void pp_anchor_presenter_destroy(PPAnchorPresenter *p);
int pp_anchors_probe(unsigned duration_ms);
#endif
