#ifndef POINTY_APP_ANCHORS_H
#define POINTY_APP_ANCHORS_H
#include "pointy/interaction.h"
#include "pointy/scene.h"
#include "pointy/presentation_metrics.h"
#include "pointy/retained.h"
typedef struct PPAnchorPresenter PPAnchorPresenter;
PPPresentationMetrics pp_anchor_presenter_metrics(const PPAnchorPresenter *presenter);
/* Renderer and shared project are borrowed and outlive the presenter. Windows
 * PNG sprites retain native sources; compatibility consumers acquire drawable
 * sources before another SVG request. Active display selection
 * stays attached to a drag through release, including mixed-scale seams. */
bool pp_anchor_presenter_create(PPAnchorPresenter **out,PPRenderer *renderer,PPProject *project,PPError *error);
bool pp_anchor_presenter_create_shared(PPAnchorPresenter **out,PPRenderer *renderer,const PPProject *project,PPScene *scene,PPError *error);
bool pp_anchor_presenter_update(PPAnchorPresenter *p,const PPDisplay *displays,size_t count,PPVec2 pointer,double now,PPError *error);
bool pp_anchor_presenter_event(PPAnchorPresenter *p,const PPPlatformEvent *event,PPError *error);
void pp_anchor_presenter_enable(PPAnchorPresenter *p,PPAnchor anchor,bool enabled);
size_t pp_anchor_presenter_instance_count(const PPAnchorPresenter *p);
bool pp_anchor_presenter_instance_pose(const PPAnchorPresenter *p,size_t index,PPPose *out);
const PPAnchorRuntime *pp_anchor_presenter_reveals(const PPAnchorPresenter *p);
PPRendererMetrics pp_anchor_presenter_renderer_metrics(const PPAnchorPresenter *p);
PPGpuResources pp_anchor_presenter_gpu_resources(const PPAnchorPresenter *p);
void pp_anchor_presenter_destroy(PPAnchorPresenter *p);
int pp_anchors_probe(unsigned duration_ms);
#endif
