#ifndef POINTY_SETTINGS_INTERNAL_H
#define POINTY_SETTINGS_INTERNAL_H
#include "backend.h"
#include "preview.h"
typedef struct PPItemEditor {
  uint64_t id, generation, revision;
  char *text;
  int text_capacity;
  char family[256], size[32], angle[32], opacity[32], color[16], face[32];
  char content_error[512], appearance_error[512], font_error[512],
      diagnostics[512];
  PPItem draft;
  PPItemSnapshot *preview;
  PPRenderer *renderer;
  PPUIContentPreview retained;
  GLuint preview_texture;
  int preview_width, preview_height;
  double animation_start, next_frame;
  bool changed, preview_dirty;
} PPItemEditor;
typedef enum PPProjectAction {
  PP_PROJECT_ACTION_NONE,
  PP_PROJECT_ACTION_NEW,
  PP_PROJECT_ACTION_OPEN,
  PP_PROJECT_ACTION_SAVE,
  PP_PROJECT_ACTION_SAVE_AS,
  PP_PROJECT_ACTION_CLOSE,
  PP_PROJECT_ACTION_REMOVE,
  PP_PROJECT_ACTION_DIALOG,
  PP_PROJECT_ACTION_COMMAND
} PPProjectAction;
struct PPSettings {
  PPApp *app; /* Borrowed, must outlive settings. */
  PPUIBackend *backend;
  PPItemEditor editor;
  char project_name[PP_PROJECT_NAME_MAX + 5], name_error[512];
  char activation[32], rotation[32], resize[32], easing[32],
      interaction_error[512];
  char status[512];
  char render_failure[512];
  uint64_t generation, revision;
  bool dialog_pending, save_for_transition, failed, remove_confirmation;
  bool render_hidden; /* Harness-only: exercise the actual renderer without
                         showing a window. */
  PPDialogKind dialog_kind;
  PPAnchor dialog_anchor;
  uint64_t dialog_item, dialog_generation;
  PPProjectAction pending_action;
  PPCommand pending_command; /* Only value-only selection/list commands. */
  bool drawing_fields;
};
bool pp_settings_command(PPSettings *settings, PPCommand command);
void pp_project_actions(PPSettings *settings);
void pp_project_request_action(PPSettings *settings, PPProjectAction action);
void pp_project_apply_action(PPSettings *settings);
void pp_settings_close(PPSettings *settings);
void pp_project_confirmation(PPSettings *settings);
void pp_settings_dialog(PPSettings *settings, PPDialogKind kind,
                        bool add_image);
void pp_item_editor(PPSettings *settings);
void pp_item_editor_destroy(PPSettings *settings);
void pp_item_editor_sync(PPSettings *settings);
void pp_item_editor_release_preview(PPSettings *settings);
bool pp_ui_number(const char *text, double low, double high, double *out);
bool pp_settings_invalid(const PPSettings *settings);
void pp_settings_revert(PPSettings *settings);
bool pp_settings_create_scaled(PPSettings **out, PPApp *app, float scale,
                               PPError *error);
#endif
