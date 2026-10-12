#ifndef POINTY_RUNTIME_INTERNAL_H
#define POINTY_RUNTIME_INTERNAL_H
#include "anchors.h"
#include "pointy/runtime.h"
#include "scheduler.h"
#include "../ui/drawer_internal.h"
typedef struct PPRasterWorker PPRasterWorker;
struct PPRuntime {
  PPApp *app;
  PPRenderer *renderer;
  PPAnchorPresenter *presenter;
  PPDrawer *drawer;
  PPDrawerCache drawer_cache;
  uint64_t generation;
  PPError error;
  bool failed;
  PPRasterWorker *worker;
};
PPSchedule pp_runtime_schedule(const PPRuntime *runtime, double now);
/* Startup-only warmup, before the first presenter update installs hooks. */
void pp_runtime_prepare_ui(PPRuntime *runtime);
#endif
