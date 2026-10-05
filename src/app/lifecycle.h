#ifndef POINTY_LIFECYCLE_H
#define POINTY_LIFECYCLE_H
#include "pointy/runtime.h"
#include "pointy/ui.h"
typedef struct PPLifecycle {
  PPApp *app;
  PPRuntime *runtime;
  PPSettings *settings;
  PPTray *tray;
} PPLifecycle;
bool pp_lifecycle_start(PPLifecycle *life, const PPAppOptions *options, PPError *error);
void pp_lifecycle_stop(PPLifecycle *life);
bool pp_lifecycle_action(PPLifecycle *life, PPCommandKind command, PPError *error);
/* One serialized native/UI/dialog/runtime iteration, including deadline wait. */
bool pp_lifecycle_iteration(PPLifecycle *life, PPError *error);
int pp_app_run(int argc, char **argv);
#endif
