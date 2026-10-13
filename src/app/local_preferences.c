#include "app_internal.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

bool pp_preferences_load(PPApp *app, const char *directory, PPError *error) {
  if (directory && !*directory)
    return true;
  /* Public branding is Yoink; retain the installed app's preference identity. */
  char *owned =
      directory ? NULL : SDL_GetPrefPath("PointyPointer", "PointyPointer");
  if (!directory)
    directory = owned;
  if (!directory)
    return pp_app_error(error, PP_ERROR_PLATFORM,
                        "Cannot find the preferences directory");
  size_t length = strlen(directory) + 32;
  app->preferences_file = malloc(length);
  if (!app->preferences_file) {
    SDL_free(owned);
    return pp_app_error(error, PP_ERROR_MEMORY,
                        "Cannot allocate preferences path");
  }
  snprintf(app->preferences_file, length, "%s/preferences.json", directory);
  SDL_free(owned);
  FILE *file = pp_platform_open_file(app->preferences_file, "rb", NULL);
  if (!file)
    return true;
  char data[65537];
  size_t size = fread(data, 1, sizeof(data), file);
  bool ok = !ferror(file) && size < sizeof(data);
  fclose(file);
  yyjson_doc *doc = ok ? yyjson_read(data, size, 0) : NULL;
  if (!doc)
    return pp_app_error(
        error, PP_ERROR_FORMAT,
        "Preferences could not be read; using default window placement");
  yyjson_val *root = yyjson_doc_get_root(doc);
  const char *path = yyjson_get_str(yyjson_obj_get(root, "last_project"));
  if (path) {
    app->remembered_path = pp_app_copy(path);
    if (!app->remembered_path) {
      yyjson_doc_free(doc);
      return pp_app_error(error, PP_ERROR_MEMORY,
                          "Cannot copy remembered project path");
    }
  }
  yyjson_val *geometry = yyjson_obj_get(root, "geometry");
  if (yyjson_arr_size(geometry) == 4) {
    PPRect value = {yyjson_get_num(yyjson_arr_get(geometry, 0)),
                    yyjson_get_num(yyjson_arr_get(geometry, 1)),
                    yyjson_get_num(yyjson_arr_get(geometry, 2)),
                    yyjson_get_num(yyjson_arr_get(geometry, 3))};
    if (isfinite(value.x) && isfinite(value.y) && value.w >= 640 &&
        value.w <= 7680 && value.h >= 480 && value.h <= 4320)
      app->ui.geometry = value;
  }
  yyjson_doc_free(doc);
  return true;
}

bool pp_preferences_save(PPApp *app, PPError *error) {
  if (!app->preferences_file)
    return true;
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  if (!doc)
    return pp_app_error(error, PP_ERROR_MEMORY,
                        "Cannot allocate local preferences");
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  bool ok = root && yyjson_mut_obj_add_str(
                        doc, root, "last_project",
                        app->remembered_path ? app->remembered_path : "");
  double bounds[] = {app->ui.geometry.x, app->ui.geometry.y, app->ui.geometry.w,
                     app->ui.geometry.h};
  yyjson_mut_val *geometry = yyjson_mut_arr_with_real(doc, bounds, 4);
  ok =
      ok && geometry && yyjson_mut_obj_add_val(doc, root, "geometry", geometry);
  size_t size = 0;
  char *json = ok ? yyjson_mut_write(doc, 0, &size) : NULL;
  yyjson_mut_doc_free(doc);
  if (!json)
    return pp_app_error(error, PP_ERROR_MEMORY,
                        "Cannot encode local preferences");
  char *temporary = NULL;
  FILE *file =
      pp_platform_temporary_file(app->preferences_file, &temporary, error);
  ok = file && fwrite(json, 1, size, file) == size;
  free(json);
  if (ok)
    ok = pp_platform_flush_file(file, error);
  if (file && fclose(file))
    ok = false;
  if (ok)
    ok = pp_platform_replace_file(temporary, app->preferences_file, error);
  if (!ok && temporary)
    pp_platform_remove_file(temporary, NULL);
  free(temporary);
  if (!ok && error && !error->code)
    pp_app_error(error, PP_ERROR_PLATFORM, "Could not save local preferences");
  return ok;
}
