#ifndef POINTY_RUNTIME_INTERNAL_H
#define POINTY_RUNTIME_INTERNAL_H
#include "anchors.h"
#include "pointy/runtime.h"
#include "scheduler.h"
struct PPRuntime {
  PPApp *app;
  PPRenderer *renderer;
  PPAnchorPresenter *presenter;
  PPDrawer *drawer;
  uint64_t generation;
  PPError error;
  bool failed;
};
PPSchedule pp_runtime_schedule(const PPRuntime *runtime, double now);
#endif
