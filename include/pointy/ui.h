#ifndef POINTY_UI_H
#define POINTY_UI_H
#include "pointy/app.h"
#include "pointy/platform.h"
typedef struct PPSettings PPSettings;
typedef struct PPUIBackend PPUIBackend;
typedef struct PPUIWindowOptions {
  const char *title;
  PPRect geometry;
  float scale; /* 0 follows the display. Nonzero is a controlled UI scale. */
  bool resizable;
  bool compact; /* Drawer: borderless and no settings minimum geometry. */
} PPUIWindowOptions;
/* Explicit independent context/window/GL/font ownership for settings and
 * drawer. Internal drawing helpers live in src/ui/backend.h; no global Nuklear
 * context. Windows begin hidden. Show explicitly after the first successful
 * frame. */
bool pp_ui_backend_create(PPUIBackend **out, const PPUIWindowOptions *options,
                          PPError *error);
void pp_ui_backend_destroy(PPUIBackend *backend);
void pp_ui_backend_event(PPUIBackend *backend, const PPPlatformEvent *event);
uint32_t pp_ui_backend_window_id(const PPUIBackend *backend);
/* Poll SDL UI events once at application level and route by window_id. TEXT
 * is one Unicode scalar per event; no borrowed SDL string escapes. */
bool pp_ui_poll(PPPlatformEvent *event);
bool pp_settings_create(PPSettings **out, PPApp *app, PPError *error);
void pp_settings_event(PPSettings *settings, const PPPlatformEvent *event);
void pp_settings_draw(PPSettings *settings);
void pp_settings_destroy(PPSettings *settings);
uint32_t pp_settings_window_id(const PPSettings *settings);
#endif
