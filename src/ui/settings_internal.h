#ifndef POINTY_SETTINGS_INTERNAL_H
#define POINTY_SETTINGS_INTERNAL_H
#include "backend.h"
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
  GLuint preview_texture;
  int preview_width, preview_height;
  double animation_start, next_frame;
  bool changed, preview_dirty;
} PPItemEditor;
struct PPSettings {
  PPApp *app; /* Borrowed, must outlive settings. */
  PPUIBackend *backend;
  PPItemEditor editor;
  char project_name[PP_PROJECT_NAME_MAX + 5], name_error[512];
  char activation[32], rotation[32], resize[32], easing[32],
      interaction_error[512];
  char status[512];
  uint64_t generation, revision;
  bool dialog_pending, save_for_transition, failed, remove_confirmation;
  bool render_hidden; /* Harness-only: exercise the actual renderer without
                         showing a window. */
  PPDialogKind dialog_kind;
  PPAnchor dialog_anchor;
  uint64_t dialog_item, dialog_generation;
};
bool pp_settings_command(PPSettings *settings, PPCommand command);
void pp_project_actions(PPSettings *settings);
void pp_project_confirmation(PPSettings *settings);
void pp_settings_dialog(PPSettings *settings, PPDialogKind kind,
                        bool add_image);
void pp_item_editor(PPSettings *settings);
void pp_item_editor_destroy(PPSettings *settings);
void pp_item_editor_sync(PPSettings *settings);
bool pp_ui_number(const char *text, double low, double high, double *out);
bool pp_settings_invalid(const PPSettings *settings);
void pp_settings_revert(PPSettings *settings);
bool pp_settings_create_scaled(PPSettings **out, PPApp *app, float scale,
                               PPError *error);
#endif
