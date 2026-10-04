#include "app_internal.h"
#include "pointy/platform.h"
#include "pointy/project_store.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *pp_app_copy(const char *text) {
  if (!text)
    text = "";
  size_t size = strlen(text) + 1;
  char *copy = malloc(size);
  if (copy)
    memcpy(copy, text, size);
  return copy;
}
bool pp_app_error(PPError *error, int code, const char *message) {
  if (error) {
    error->code = code;
    snprintf(error->message, sizeof(error->message), "%s", message);
  }
  return false;
}
static bool same_string(const char *a, const char *b) {
  return !strcmp(a ? a : "", b ? b : "");
}
static bool same_item(const PPItem *a, const PPItem *b) {
  if (!a || !b || a->kind != b->kind || a->angle_deg != b->angle_deg ||
      a->opacity != b->opacity)
    return false;
  if (a->kind == PP_ITEM_IMAGE)
    return !memcmp(&a->image_asset, &b->image_asset, sizeof(PPAssetId)) &&
           a->image_longest_dip == b->image_longest_dip;
  return same_string(a->text, b->text) &&
         same_string(a->text_style.requested_family,
                     b->text_style.requested_family) &&
         !memcmp(&a->text_style.primary_asset, &b->text_style.primary_asset,
                 sizeof(PPAssetId)) &&
         !memcmp(&a->text_style.fallback_asset, &b->text_style.fallback_asset,
                 sizeof(PPAssetId)) &&
         a->text_style.primary_face == b->text_style.primary_face &&
         a->text_style.fallback_face == b->text_style.fallback_face &&
         a->text_style.font_size_dip == b->text_style.font_size_dip &&
         !memcmp(a->text_style.color, b->text_style.color, 4);
}
static bool same_settings(const PPProjectSettings *a,
                          const PPProjectSettings *b) {
  return a->activation_dip == b->activation_dip &&
         a->rotation_step_deg == b->rotation_step_deg &&
         a->resize_factor == b->resize_factor &&
         a->easing_seconds == b->easing_seconds;
}
static void reset_selection(PPApp *app) {
  const PPAnchorList *list = pp_project_anchor(app->project, app->ui.anchor);
  app->ui.selected_item = list ? list->default_id : 0;
}
static void clear_transition(PPApp *app) {
  app->ui.transition_pending = app->ui.needs_save_path = false;
  free(app->pending_path);
  app->pending_path = NULL;
}
static void remember(PPApp *app, char *copy) {
  free(app->remembered_path);
  app->remembered_path = copy;
  PPError error = {0};
  if (!pp_preferences_save(app, &error))
    snprintf(app->diagnostic, sizeof(app->diagnostic),
             "Project succeeded. Local preferences: %.460s", error.message);
}
static bool save(PPApp *app, const char *path, PPError *error) {
  if (!path || !*path)
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Choose a project file with Save As");
  char *copy = pp_app_copy(path), *remembered = pp_app_copy(path);
  if (!copy || !remembered) {
    free(copy);
    free(remembered);
    return pp_app_error(error, PP_ERROR_MEMORY, "Cannot copy project path");
  }
  if (!pp_project_save(app->project, path, error)) {
    free(copy);
    free(remembered);
    return false;
  }
  free(app->path);
  app->path = copy;
  app->saved_revision = pp_project_revision(app->project);
  remember(app, remembered);
  return true;
}
static bool transition(PPApp *app, PPCommandKind kind, const char *path,
                       PPError *error) {
  if (kind == PP_COMMAND_QUIT) {
    app->ui.quit_requested = true;
    pp_scene_clear(app->scene);
    return true;
  }
  PPProject *candidate = NULL;
  char *copy = pp_app_copy(kind == PP_COMMAND_OPEN ? path : "");
  char *remembered = kind == PP_COMMAND_OPEN ? pp_app_copy(path) : NULL;
  if (!copy || (kind == PP_COMMAND_OPEN && !remembered)) {
    free(copy);
    free(remembered);
    return pp_app_error(error, PP_ERROR_MEMORY, "Cannot copy project path");
  }
  bool ok = kind == PP_COMMAND_OPEN
                ? pp_project_load(path, app->assets, &candidate, error)
                : pp_project_default(app->assets, &candidate, error);
  if (!ok) {
    free(copy);
    free(remembered);
    return false;
  }
  /* Keep the bundled fallback's real notice available for Add text even when
   * an opened package initially contains no text. Existing project notices
   * win; unused notice records never become orphan package resources. */
  if (!*pp_project_notice(candidate, app->default_style.fallback_asset) &&
      !pp_project_set_notice(candidate, app->default_style.fallback_asset,
                             app->default_notice, error)) {
    pp_project_destroy(candidate);
    free(copy);
    free(remembered);
    return false;
  }
  pp_scene_clear(app->scene);
  pp_project_destroy(app->project);
  app->project = candidate;
  free(app->path);
  app->path = copy;
  app->saved_revision = pp_project_revision(candidate);
  app->generation++;
  reset_selection(app);
  if (remembered)
    remember(app, remembered);
  return true;
}
static bool request_transition(PPApp *app, const PPCommand *command,
                               PPError *error) {
  if (command->kind == PP_COMMAND_OPEN && (!command->path || !*command->path))
    return true;
  if (!pp_app_dirty(app))
    return transition(app, command->kind, command->path, error);
  char *path = command->path ? pp_app_copy(command->path) : NULL;
  if (command->path && !path)
    return pp_app_error(error, PP_ERROR_MEMORY,
                        "Cannot retain pending project path");
  clear_transition(app);
  app->pending_path = path;
  app->ui.pending_kind = command->kind;
  app->ui.transition_pending = true;
  return true;
}
static bool resolve_transition(PPApp *app, const PPCommand *command,
                               PPError *error) {
  if (!app->ui.transition_pending)
    return true;
  if (command->choice == PP_UNSAVED_CANCEL) {
    clear_transition(app);
    return true;
  }
  if (command->choice == PP_UNSAVED_ASK)
    return true;
  if (command->choice == PP_UNSAVED_SAVE) {
    const char *path = command->path ? command->path : app->path;
    if (!*path) {
      app->ui.needs_save_path = true;
      return true;
    }
    if (!save(app, path, error)) {
      clear_transition(app);
      return false;
    }
  } else if (command->choice != PP_UNSAVED_DISCARD) {
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Unknown unsaved-change choice");
  }
  bool ok = transition(app, app->ui.pending_kind, app->pending_path, error);
  clear_transition(app);
  return ok;
}
static bool import(PPApp *app, const PPCommand *command, PPError *error) {
  if (!command->path)
    return true;
  const PPItem *original =
      command->item_id ? pp_project_item(app->project, command->item_id) : NULL;
  if (command->kind == PP_COMMAND_IMPORT_FONT &&
      (!original || original->kind != PP_ITEM_TEXT))
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Select a text item before importing a font");
  const char *extension = strrchr(command->path, '.');
  PPAssetKind kind = PP_ASSET_FONT;
  if (command->kind == PP_COMMAND_IMPORT_IMAGE) {
    if (extension && !SDL_strcasecmp(extension, ".png"))
      kind = PP_ASSET_PNG;
    else if (extension && !SDL_strcasecmp(extension, ".gif"))
      kind = PP_ASSET_GIF;
    else if (extension && !SDL_strcasecmp(extension, ".svg"))
      kind = PP_ASSET_SVG;
    else
      return pp_app_error(error, PP_ERROR_FORMAT,
                          "Image: choose a PNG, GIF, or static SVG file");
  } else if (!extension || (SDL_strcasecmp(extension, ".ttf") &&
                            SDL_strcasecmp(extension, ".otf") &&
                            SDL_strcasecmp(extension, ".ttc") &&
                            SDL_strcasecmp(extension, ".otc"))) {
    return pp_app_error(error, PP_ERROR_FORMAT,
                        "Font: choose a TTF, OTF, TTC, or OTC file");
  }
  FILE *file = pp_platform_open_file(command->path, "rb", error);
  if (!file)
    return false;
  uint64_t size = 0;
  if (!pp_platform_file_size(file, &size) || !size ||
      size > 256u * 1024u * 1024u) {
    fclose(file);
    return pp_app_error(error, PP_ERROR_RESOURCE_LIMIT,
                        "Import: source must be nonempty and at most 256 MiB");
  }
  uint8_t *bytes = malloc((size_t)size);
  if (!bytes) {
    fclose(file);
    return pp_app_error(error, PP_ERROR_MEMORY, "Cannot allocate import bytes");
  }
  bool ok = fread(bytes, 1, (size_t)size, file) == size;
  fclose(file);
  PPFontInfo info = {0};
  if (!ok)
    pp_app_error(error, PP_ERROR_PLATFORM,
                 "Could not read the complete import file");
  if (ok && kind == PP_ASSET_FONT) {
    ok = pp_font_inspect((PPBytes){bytes, (size_t)size}, command->font_face,
                         &info, error);
    if (ok && ((size_t)command->font_face >= info.face_count ||
               info.face_count > 4096))
      ok = pp_app_error(error, PP_ERROR_FORMAT,
                        "Font: choose a literal collection face from 0 to "
                        "count minus one (at most 4096 faces)");
    for (size_t face = 0; ok && face < info.face_count; face++) {
      PPFontInfo sibling;
      ok = pp_font_inspect((PPBytes){bytes, (size_t)size}, (int)face, &sibling,
                           error);
      if (ok && !sibling.embeddable)
        ok = pp_app_error(error, PP_ERROR_FORMAT,
                          "Font: a collection face does not permit embedding");
    }
  }
  PPAssetId asset;
  if (ok)
    ok = pp_asset_import(app->assets, (PPBytes){bytes, (size_t)size}, kind,
                         &asset, error);
  free(bytes);
  if (!ok)
    return false;
  PPItem item = original ? *original
                         : (PPItem){.kind = PP_ITEM_IMAGE,
                                    .image_longest_dip = 96,
                                    .opacity = 1};
  if (kind == PP_ASSET_FONT) {
    item.text_style.primary_asset = asset;
    item.text_style.primary_face = command->font_face;
    item.text_style.requested_family = info.family;
  } else {
    item.kind = PP_ITEM_IMAGE;
    item.image_asset = asset;
    if (original && original->kind == PP_ITEM_TEXT)
      item.image_longest_dip = original->text_style.font_size_dip;
  }
  if (original)
    return same_item(original, &item) ||
           pp_project_edit_item(app->project, original->id, &item, error);
  return pp_project_add_item(app->project, command->anchor, &item,
                             &app->ui.selected_item, error);
}

bool pp_app_create(PPApp **out, const PPAppOptions *options, PPError *error) {
  if (out)
    *out = NULL;
  if (!out)
    return pp_app_error(error, PP_ERROR_ARGUMENT, "Missing application output");
  PPApp *app = calloc(1, sizeof(*app));
  if (!app)
    return pp_app_error(error, PP_ERROR_MEMORY, "Cannot allocate application");
  app->ui.geometry = (PPRect){100, 100, 1040, 780};
  app->path = pp_app_copy("");
  if (!app->path || !pp_assets_create(&app->assets, error) ||
      !pp_scene_create(&app->scene, error) ||
      !pp_project_default(app->assets, &app->project, error)) {
    pp_app_destroy(app);
    return false;
  }
  app->generation = 1;
  app->default_style =
      pp_project_target(app->project, PP_ANCHOR_TOP_LEFT)->text_style;
  app->default_style.requested_family = "Aptos";
  app->default_notice = pp_app_copy(
      pp_project_notice(app->project, app->default_style.fallback_asset));
  if (!app->default_notice) {
    pp_app_destroy(app);
    return pp_app_error(error, PP_ERROR_MEMORY,
                        "Cannot retain the bundled font notice");
  }
  app->saved_revision = pp_project_revision(app->project);
  reset_selection(app);
  PPError preference_error = {0};
  if (!pp_preferences_load(app, options ? options->preferences_directory : NULL,
                           &preference_error))
    snprintf(app->diagnostic, sizeof(app->diagnostic), "%s",
             preference_error.message);
  if ((!options || options->restore_last) && app->remembered_path &&
      *app->remembered_path) {
    PPError load_error = {0};
    if (!transition(app, PP_COMMAND_OPEN, app->remembered_path, &load_error))
      snprintf(
          app->diagnostic, sizeof(app->diagnostic),
          "Last project could not open. Built-in defaults are active. %.425s",
          load_error.message);
  }
  *out = app;
  return true;
}
void pp_app_destroy(PPApp *app) {
  if (!app)
    return;
  pp_scene_destroy(app->scene);
  pp_project_destroy(app->project);
  pp_assets_destroy(app->assets);
  free(app->path);
  free(app->pending_path);
  free(app->remembered_path);
  free(app->preferences_file);
  free(app->default_notice);
  free(app);
}
static bool execute(PPApp *app, const PPCommand *command, PPError *error) {
  if (app->ui.transition_pending &&
      command->kind != PP_COMMAND_RESOLVE_TRANSITION &&
      command->kind != PP_COMMAND_SHOW_SETTINGS &&
      command->kind != PP_COMMAND_CLOSE_SETTINGS &&
      command->kind != PP_COMMAND_SET_GEOMETRY)
    return pp_app_error(
        error, PP_ERROR_ARGUMENT,
        "Resolve the pending Save, Discard, or Cancel choice first");
  const PPAnchorList *list = pp_project_anchor(app->project, command->anchor);
  switch (command->kind) {
  case PP_COMMAND_NEW:
  case PP_COMMAND_OPEN:
  case PP_COMMAND_QUIT:
    return request_transition(app, command, error);
  case PP_COMMAND_RESOLVE_TRANSITION:
    return resolve_transition(app, command, error);
  case PP_COMMAND_SAVE:
    if (!*app->path) {
      app->ui.needs_save_path = true;
      return true;
    }
    return save(app, app->path, error);
  case PP_COMMAND_SAVE_AS:
    app->ui.needs_save_path = false;
    return !command->path || save(app, command->path, error);
  case PP_COMMAND_ADD_ITEM:
    return pp_project_add_item(app->project, command->anchor, command->item,
                               &app->ui.selected_item, error);
  case PP_COMMAND_ADD_TEXT: {
    const PPItem *selected =
        pp_project_item(app->project, app->ui.selected_item);
    PPItem item = {.kind = PP_ITEM_TEXT,
                   .text = "\xf0\x9f\x91\x89",
                   .text_style = app->default_style,
                   .opacity = 1};
    if (selected && selected->kind == PP_ITEM_TEXT) {
      item = *selected;
      item.id = 0;
    }
    return pp_project_add_item(app->project, command->anchor, &item,
                               &app->ui.selected_item, error);
  }
  case PP_COMMAND_EDIT_ITEM:
    return same_item(pp_project_item(app->project, command->item_id),
                     command->item) ||
           pp_project_edit_item(app->project, command->item_id, command->item,
                                error);
  case PP_COMMAND_REMOVE_ITEM:
    if (!pp_project_remove_item(app->project, command->anchor, command->item_id,
                                error))
      return false;
    if (app->ui.selected_item == command->item_id)
      reset_selection(app);
    return true;
  case PP_COMMAND_SET_DEFAULT:
    return (list && list->default_id == command->item_id && command->item_id) ||
           pp_project_set_default(app->project, command->anchor,
                                  command->item_id, error);
  case PP_COMMAND_REORDER_ITEM:
    return (list && command->index < list->count &&
            list->item_ids[command->index] == command->item_id) ||
           pp_project_reorder_item(app->project, command->anchor,
                                   command->item_id, command->index, error);
  case PP_COMMAND_SET_ENABLED:
    return (list && list->enabled == command->enabled) ||
           pp_project_set_enabled(app->project, command->anchor,
                                  command->enabled, error);
  case PP_COMMAND_SET_INTERACTION:
    return same_settings(pp_project_settings(app->project),
                         &command->interaction) ||
           pp_project_set_settings(app->project, &command->interaction, error);
  case PP_COMMAND_SELECT_ANCHOR:
    if (!list)
      return pp_app_error(error, PP_ERROR_ARGUMENT, "Unknown anchor");
    app->ui.anchor = command->anchor;
    reset_selection(app);
    return true;
  case PP_COMMAND_SELECT_ITEM:
    list = pp_project_anchor(app->project, app->ui.anchor);
    for (size_t i = 0; i < list->count; i++) {
      if (list->item_ids[i] == command->item_id) {
        app->ui.selected_item = command->item_id;
        return true;
      }
    }
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Selected item is not in this anchor");
  case PP_COMMAND_SHOW_SETTINGS:
    app->ui.settings_open = true;
    return true;
  case PP_COMMAND_CLOSE_SETTINGS:
    app->ui.settings_open = false;
    return true;
  case PP_COMMAND_SET_GEOMETRY:
    if (!isfinite(command->geometry.x) || !isfinite(command->geometry.y) ||
        !isfinite(command->geometry.w) || !isfinite(command->geometry.h) ||
        command->geometry.w < 640 || command->geometry.h < 480 ||
        command->geometry.w > 7680 || command->geometry.h > 4320)
      return pp_app_error(error, PP_ERROR_ARGUMENT,
                          "Invalid settings window geometry");
    app->ui.geometry = command->geometry;
    return pp_preferences_save(app, error);
  case PP_COMMAND_IMPORT_IMAGE:
  case PP_COMMAND_IMPORT_FONT:
    return import(app, command, error);
  case PP_COMMAND_RENAME_PROJECT:
    return pp_project_set_name(app->project, command->name, error);
  default:
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Unknown application command");
  }
}
bool pp_app_command(PPApp *app, const PPCommand *command, PPError *error) {
  PPError local = {0};
  if (!error)
    error = &local;
  *error = (PPError){0};
  if (!app || !command)
    return pp_app_error(error, PP_ERROR_ARGUMENT,
                        "Missing application or command");
  bool ok = execute(app, command, error);
  if (!ok)
    snprintf(app->diagnostic, sizeof(app->diagnostic), "%s", error->message);
  return ok;
}
const PPProject *pp_app_project(const PPApp *app) {
  return app ? app->project : NULL;
}
PPAssets *pp_app_assets(PPApp *app) { return app ? app->assets : NULL; }
PPScene *pp_app_scene(PPApp *app) { return app ? app->scene : NULL; }
const char *pp_app_path(const PPApp *app) {
  return app && app->path ? app->path : "";
}
const char *pp_app_name(const PPApp *app) {
  return app ? pp_project_name(app->project) : "";
}
const char *pp_app_diagnostic(const PPApp *app) {
  return app ? app->diagnostic : "";
}
const PPAppUIState *pp_app_ui(const PPApp *app) {
  return app ? &app->ui : NULL;
}
bool pp_app_dirty(const PPApp *app) {
  return app && app->saved_revision != pp_project_revision(app->project);
}
uint64_t pp_app_generation(const PPApp *app) {
  return app ? app->generation : 0;
}
