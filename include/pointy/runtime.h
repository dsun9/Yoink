#ifndef POINTY_RUNTIME_H
#define POINTY_RUNTIME_H
#include "pointy/app.h"
typedef struct PPRuntime PPRuntime;
typedef struct PPDrawer PPDrawer;
/* App, its assets and canonical scene outlive the runtime. Main thread only. */
bool pp_runtime_create(PPRuntime **out, PPApp *app, PPError *error);
void pp_runtime_event(PPRuntime *runtime, const PPPlatformEvent *event);
void pp_runtime_tick(PPRuntime *runtime, double now);
void pp_runtime_draw_ui(PPRuntime *runtime, double now);
void pp_runtime_destroy(PPRuntime *runtime);
bool pp_drawer_open(PPDrawer **out, PPApp *app, uint64_t instance_id,
                    PPError *error);
void pp_drawer_close(PPDrawer *drawer);
#endif
