#ifndef POINTY_UI_BACKEND_H
#define POINTY_UI_BACKEND_H
#include "../render/gl_api.h"
#include "nuklear_config.h"
#include "pointy/ui.h"
typedef struct PPUIControl {
  uint64_t id;
  struct nk_rect bounds;
  char label[96];
  bool enabled, edit;
} PPUIControl;
typedef struct PPUITheme {
  struct nk_color canvas, surface, ink, muted, accent, danger;
  float compact, control, panel, control_radius, panel_radius;
  float body_size, heading_size, utility_size, row_height;
} PPUITheme;
extern const PPUITheme pp_ui_theme;
struct PPUIBackend {
  struct PPUIShapedEdit *shaped;
  PPAssets *text_assets; /* Borrowed app raster budget; outlives this field. */
  SDL_Window *window;
  SDL_GLContext gl_context;
  PPGL gl;
  struct nk_context context;
  struct nk_font_atlas atlas;
  struct nk_font *body_font, *heading_font, *utility_font;
  struct nk_draw_null_texture null_texture;
  struct nk_buffer commands, vertices, elements;
  GLuint program, vao, vbo, ebo, font_texture;
  GLint projection_location;
  float scale, window_scale, width, height;
  bool input_started, failed, composing, visible, keyboard_focus, activate,
      escape;
  PPKey navigation;
  unsigned modifiers;
  int tab_direction;
  uint64_t focus_id, restore_focus;
  PPUIControl *controls;
  size_t control_count, control_capacity;
  uint64_t popup_id;
  int popup_index;
  bool in_popup;
  bool modal, last_modal;
  SDL_Cursor *arrow_cursor, *hand_cursor, *text_cursor;
  bool pointer_target, text_target;
  char font_diagnostic[160];
  const char
      *capture_path; /* Test capture on next completed frame, before swap. */
};
void pp_ui_apply_theme(PPUIBackend *ui);
bool pp_ui_begin(PPUIBackend *ui, PPError *error);
bool pp_ui_end(PPUIBackend *ui, PPError *error);
void pp_ui_show(PPUIBackend *ui, bool show);
/* Reset per-open UI state while retaining the window, GL objects and fonts. */
bool pp_ui_backend_reset(PPUIBackend *ui, PPError *error);
void pp_ui_poll_reset(uint32_t window_id);
void pp_ui_modal(PPUIBackend *ui, bool modal);
void pp_ui_heading(PPUIBackend *ui, const char *text);
bool pp_ui_button(PPUIBackend *ui, const char *id, const char *label,
                  bool enabled);
bool pp_ui_select(PPUIBackend *ui, const char *id, const char *label,
                  bool selected, bool enabled);
bool pp_ui_edit(PPUIBackend *ui, const char *id, char *value, int capacity,
                bool multiline, bool enabled);
bool pp_ui_choice(PPUIBackend *ui, const char *id, const char *const *options,
                  int count, int *selected, bool enabled);
void pp_ui_reveal_focus(PPUIBackend *ui, const char *group,
                        struct nk_rect viewport, size_t first_control);
void pp_ui_error(PPUIBackend *ui, const char *text);
bool pp_ui_upload_frame(PPUIBackend *ui, GLuint *texture, const PPFrame *frame,
                        PPError *error);
/* Test instrumentation and later drawer layout inspection; copies control
 * geometry in logical units, does not synthesize or execute any action. */
bool pp_ui_control_rect(const PPUIBackend *ui, const char *id, PPRect *bounds);
bool pp_ui_focused(const PPUIBackend *ui, const char *id);
void pp_ui_focus(PPUIBackend *ui, const char *id);
bool pp_ui_unicode_notice(const PPUIBackend *ui, const char *text, char *out,
                          size_t capacity);
bool pp_ui_capture(PPUIBackend *ui, const char *path, PPError *error);
#endif
