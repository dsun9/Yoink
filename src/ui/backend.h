#ifndef POINTY_UI_BACKEND_H
#define POINTY_UI_BACKEND_H
#include "../render/gl_api.h"
#include "nuklear_config.h"
#include "pointy/ui.h"
#include "pointy/content.h"
typedef struct PPUIControl {
  uint64_t id;
  struct nk_rect bounds;
  char label[96];
  bool enabled, edit, in_popup;
} PPUIControl;
typedef struct PPUITheme {
  struct nk_color canvas, surface, ink, muted, accent, danger;
  struct nk_color field, border, selection, accent_hover, accent_pressed;
  struct nk_color disabled_field, disabled_border, disabled_ink;
  float compact, control, panel, control_radius, panel_radius;
  float body_size, heading_size, utility_size, row_height;
} PPUITheme;
extern const PPUITheme pp_ui_theme;
/* Keep the stroke and its antialiasing fringe inside the widget's clip. */
void pp_ui_control_border(struct nk_command_buffer *canvas, struct nk_rect bounds,
                          float rounding, float thickness, struct nk_color color);
typedef enum PPUIIcon {
  PP_UI_ICON_NEW, PP_UI_ICON_OPEN, PP_UI_ICON_SAVE, PP_UI_ICON_SAVE_AS,
  PP_UI_ICON_CHEVRON_DOWN, PP_UI_ICON_MONITOR, PP_UI_ICON_STAR, PP_UI_ICON_STAR_OUTLINE,
  PP_UI_ICON_DELETE, PP_UI_ICON_CLOSE, PP_UI_ICON_DRAG,
  PP_UI_ICON_NORTHWEST, PP_UI_ICON_NORTH, PP_UI_ICON_NORTHEAST,
  PP_UI_ICON_WEST, PP_UI_ICON_EAST, PP_UI_ICON_SOUTHWEST,
  PP_UI_ICON_SOUTH, PP_UI_ICON_SOUTHEAST
} PPUIIcon;
struct PPUIBackend {
  struct PPUIShapedEdit *shaped;
  PPAssets *text_assets; /* Borrowed app raster budget; outlives this field. */
  PPContentSources *content_sources;
  struct PPUITextureCharge *texture_charges;
  GLuint premultiplied_textures[16];
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
  bool edit_blurred;
  PPKey navigation;
  unsigned modifiers;
  int tab_direction;
  uint64_t focus_id, restore_focus;
  uint64_t edit_owner;
  PPUIControl *controls;
  size_t control_count, control_capacity;
  uint64_t popup_id;
  uint64_t dismissed_popup;
  struct nk_rect popup_bounds;
  int popup_index;
  bool in_popup;
  bool modal, last_modal;
  SDL_Cursor *arrow_cursor, *hand_cursor, *text_cursor;
  bool pointer_target, text_target;
  char font_diagnostic[160];
  char *tooltip_text;
  size_t tooltip_capacity, tooltip_end;
  const char *tooltip_more;
  struct nk_rect tooltip_anchor, tooltip_bounds;
  bool tooltip_focus;
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
void pp_ui_secondary(PPUIBackend *ui, const char *text);
typedef enum PPUILabelWhitespace {
  PP_UI_LABEL_COLLAPSE_WHITESPACE,
  PP_UI_LABEL_PRESERVE_WHITESPACE
} PPUILabelWhitespace;
/* Derive a control-font label without changing the item's full Unicode text.
 * Menus retain ownership of their empty-text and image fallback labels. */
void pp_ui_label_text(const struct nk_font *font, const char *text,
                      PPUILabelWhitespace whitespace, char *label, size_t capacity);
/* Caller owns the complete derived Settings label, before presentation fitting. */
char *pp_ui_item_label(const struct nk_font *font, const PPItem *item);
/* Return the next measured line's byte length, including any break space.
 * Every returned boundary is a complete UTF-8 codepoint. */
size_t pp_ui_label_line(const struct nk_user_font *font, const char *text,
                        size_t length, float width);
/* Draw measured lines inside bounds. Return the number of source bytes drawn. */
size_t pp_ui_label_draw(struct nk_command_buffer *canvas, struct nk_rect bounds,
                        const struct nk_user_font *font, const char *text,
                        size_t length, struct nk_color background, struct nk_color ink);
/* Fit in place using the drawing font's measured width, retaining whole UTF-8
 * codepoints and reserving the actual ellipsis width when it fits. */
size_t pp_ui_fit_label(const struct nk_user_font *font, char *label,
                       size_t capacity, float width);
bool pp_ui_button(PPUIBackend *ui, const char *id, const char *label,
                  bool enabled);
void pp_ui_draw_icon(struct nk_command_buffer *canvas, struct nk_rect bounds,
                     PPUIIcon icon, struct nk_color color);
bool pp_ui_icon_button(PPUIBackend *ui, const char *id, PPUIIcon icon,
                       const char *action, const char *tooltip, bool enabled,
                       bool primary);
/* Attach help to the preceding control or a noninteractive display region.
 * Disabled controls still explain their state on pointer hover. */
bool pp_ui_tooltip(PPUIBackend *ui, struct nk_rect bounds, const char *id,
                   const char *text);
void pp_ui_tooltip_clear(PPUIBackend *ui);
bool pp_ui_switch(PPUIBackend *ui, const char *id, const char *label,
                  bool *value, bool enabled);
bool pp_ui_select(PPUIBackend *ui, const char *id, const char *label,
                  bool selected, bool enabled);
bool pp_ui_tab(PPUIBackend *ui, const char *id, const char *label,
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
void pp_ui_release_texture_charges(PPUIBackend *ui,size_t count,const GLuint *textures);
void pp_ui_premultiplied_texture(PPUIBackend *ui,GLuint previous,GLuint current);
/* Test instrumentation and later drawer layout inspection; copies control
 * geometry in logical units, does not synthesize or execute any action. */
bool pp_ui_control_rect(const PPUIBackend *ui, const char *id, PPRect *bounds);
bool pp_ui_focused(const PPUIBackend *ui, const char *id);
bool pp_ui_tooltip_visible(const PPUIBackend *ui, const char *text);
void pp_ui_focus(PPUIBackend *ui, const char *id);
bool pp_ui_unicode_notice(const PPUIBackend *ui, const char *text, char *out,
                          size_t capacity);
bool pp_ui_capture(PPUIBackend *ui, const char *path, PPError *error);
#endif
