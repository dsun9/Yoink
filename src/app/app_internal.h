#ifndef POINTY_APP_INTERNAL_H
#define POINTY_APP_INTERNAL_H
#include "pointy/app.h"
struct PPApp {
  PPAssets *assets;
  PPProject *project;
  PPScene *scene;
  char *path, *preferences_file, *remembered_path, *pending_path;
  uint64_t saved_revision, generation;
  PPAppUIState ui;
  char diagnostic[512];
  PPTextStyle default_style;
  char *default_notice;
  PPContentSources *sources;
};
char *pp_app_copy(const char *text);
bool pp_app_error(PPError *error, int code, const char *message);
bool pp_preferences_load(PPApp *app, const char *directory, PPError *error);
bool pp_preferences_save(PPApp *app, PPError *error);
#endif
