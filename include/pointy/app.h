#ifndef POINTY_APP_H
#define POINTY_APP_H
#include "pointy/scene.h"

typedef struct PPApp PPApp;
typedef enum PPCommandKind {
  PP_COMMAND_NEW,
  PP_COMMAND_OPEN,
  PP_COMMAND_SAVE,
  PP_COMMAND_SAVE_AS,
  PP_COMMAND_QUIT,
  PP_COMMAND_RESOLVE_TRANSITION,
  PP_COMMAND_ADD_ITEM,
  PP_COMMAND_EDIT_ITEM,
  PP_COMMAND_REMOVE_ITEM,
  PP_COMMAND_SET_DEFAULT,
  PP_COMMAND_REORDER_ITEM,
  PP_COMMAND_SET_ENABLED,
  PP_COMMAND_SET_INTERACTION,
  PP_COMMAND_SELECT_ANCHOR,
  PP_COMMAND_SELECT_ITEM,
  PP_COMMAND_SHOW_SETTINGS,
  PP_COMMAND_CLOSE_SETTINGS,
  PP_COMMAND_SET_GEOMETRY,
  PP_COMMAND_IMPORT_IMAGE,
  PP_COMMAND_IMPORT_FONT,
  PP_COMMAND_RENAME_PROJECT,
  PP_COMMAND_ADD_TEXT
} PPCommandKind;
typedef enum PPUnsavedChoice {
  PP_UNSAVED_ASK,
  PP_UNSAVED_SAVE,
  PP_UNSAVED_DISCARD,
  PP_UNSAVED_CANCEL
} PPUnsavedChoice;
typedef struct PPCommand {
  PPCommandKind kind;
  PPAnchor anchor;
  uint64_t item_id;
  size_t index;
  bool enabled;
  const char
      *path; /* Borrowed for this synchronous call; NULL Save As cancels. */
  const char *name;
  const PPItem *item;
  PPProjectSettings interaction;
  PPUnsavedChoice choice;
  PPRect geometry;
  int font_face; /* Literal collection face, preserved in project packages. */
} PPCommand;
typedef struct PPAppOptions {
  /* NULL uses SDL's preference directory; "" disables all preference I/O.
   * Tests supply a private directory. restore_last controls automatic Open. */
  const char *preferences_directory;
  bool restore_last;
} PPAppOptions;
typedef struct PPAppUIState {
  PPAnchor anchor;
  uint64_t selected_item;
  bool settings_open, quit_requested, transition_pending, needs_save_path;
  PPCommandKind pending_kind;
  PPRect geometry;
} PPAppUIState;
bool pp_app_create(PPApp **out, const PPAppOptions *options, PPError *error);
void pp_app_destroy(PPApp *app);
/* Sole project and UI mutation boundary. Synchronous on the application thread;
 * serialize with every snapshot/raster operation, including preview rendering.
 * Failed edits/imports and failed load preserve the active project and scene;
 * imports may leave unused manager-owned source bytes. A successful Save before
 * a failed Open remains saved. Preference I/O failure may follow a geometry
 * change. Error text is also retained in app diagnostic. */
bool pp_app_command(PPApp *app, const PPCommand *command, PPError *error);
/* Project views expire on a successful New/Open; path views also expire on Save
 * or Save As. Item/list views expire on
 * any project mutation. generation changes on replacement; revision changes on
 * edits. Assets and scene identities stay valid until app destruction. Runtime
 * owns scene mutation, and must drop preview/drawer project borrows on
 * generation change before processing more input. Scene IDs are never reused
 * across clears. */
const PPProject *pp_app_project(const PPApp *app);
PPAssets *pp_app_assets(PPApp *app);
PPScene *pp_app_scene(PPApp *app);
const char *pp_app_path(const PPApp *app);
const char *pp_app_name(const PPApp *app);
const char *pp_app_diagnostic(const PPApp *app);
const PPAppUIState *pp_app_ui(const PPApp *app);
bool pp_app_dirty(const PPApp *app);
uint64_t pp_app_generation(const PPApp *app);
/* Retain a runtime diagnostic for settings without changing project revision. */
void pp_app_report(PPApp *app, const PPError *error);
#endif
