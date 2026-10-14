#include "backend.h"
/* Canonical runtime tokens. DESIGN.md mirrors this owner; all shared widgets
 * consume this adapter. No screen-local palettes. */
const PPUITheme pp_ui_theme = {
    .canvas = {240, 244, 249, 255}, .surface = {255, 255, 255, 255},
    .ink = {24, 34, 48, 255}, .muted = {98, 115, 135, 255},
    .accent = {38, 122, 192, 255}, .danger = {164, 43, 52, 255},
    .field = {245, 248, 252, 255}, .border = {223, 231, 240, 255},
    .selection = {231, 242, 252, 255}, .accent_hover = {30, 106, 170, 255},
    .accent_pressed = {24, 87, 143, 255},
    .compact = 6, .control = 12, .panel = 18,
    .control_radius = 8, .panel_radius = 12,
    .body_size = 16, .heading_size = 21, .utility_size = 14, .row_height = 36};
void pp_ui_apply_theme(PPUIBackend *ui) {
  struct nk_color colors[NK_COLOR_COUNT];
  const PPUITheme *t = &pp_ui_theme;
  for (int i = 0; i < NK_COLOR_COUNT; i++) colors[i] = t->surface;
  colors[NK_COLOR_TEXT] = t->ink;
  colors[NK_COLOR_WINDOW] = t->canvas;
  colors[NK_COLOR_HEADER] = t->surface;
  colors[NK_COLOR_BORDER] = t->border;
  colors[NK_COLOR_BUTTON] = t->field;
  colors[NK_COLOR_BUTTON_HOVER] = t->selection;
  colors[NK_COLOR_BUTTON_ACTIVE] = t->accent;
  colors[NK_COLOR_TOGGLE] = t->border;
  colors[NK_COLOR_TOGGLE_HOVER] = t->selection;
  colors[NK_COLOR_TOGGLE_CURSOR] = t->accent;
  colors[NK_COLOR_SELECT] = t->field;
  colors[NK_COLOR_SELECT_ACTIVE] = t->selection;
  colors[NK_COLOR_SLIDER] = t->field;
  colors[NK_COLOR_SLIDER_CURSOR] = t->muted;
  colors[NK_COLOR_SLIDER_CURSOR_HOVER] = t->accent;
  colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = t->accent;
  colors[NK_COLOR_EDIT] = t->field;
  colors[NK_COLOR_EDIT_CURSOR] = t->accent;
  colors[NK_COLOR_COMBO] = t->field;
  colors[NK_COLOR_CHART] = t->canvas;
  colors[NK_COLOR_CHART_COLOR] = t->accent;
  colors[NK_COLOR_CHART_COLOR_HIGHLIGHT] = t->danger;
  colors[NK_COLOR_SCROLLBAR] = t->canvas;
  colors[NK_COLOR_SCROLLBAR_CURSOR] = t->border;
  colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = t->muted;
  colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = t->accent;
  colors[NK_COLOR_TAB_HEADER] = t->surface;
  nk_style_from_table(&ui->context, colors);
  struct nk_style *s = &ui->context.style;
  s->window.padding = nk_vec2(t->panel, t->control);
  s->window.group_padding = nk_vec2(t->control, t->control);
  s->window.spacing = nk_vec2(t->compact, t->compact);
  s->window.rounding = t->panel_radius;
  s->window.scrollbar_size = nk_vec2(10, 10);
  s->button.rounding = t->control_radius;
  s->button.border = 1;
  s->button.text_active = t->surface;
  s->button.padding = nk_vec2(t->compact, t->compact);
  /* Readable disabled labels instead of Nuklear's default low-alpha text. */
  s->button.color_factor_background = 1;
  s->button.color_factor_text = 1;
  s->edit.rounding = t->control_radius;
  s->edit.border = 1;
  s->edit.cursor_normal = t->accent;
  s->edit.cursor_hover = t->accent;
  s->edit.selected_normal = t->selection;
  s->edit.selected_hover = t->selection;
  s->edit.selected_text_normal = t->ink;
  s->edit.selected_text_hover = t->ink;
  s->selectable.text_normal = t->ink;
  s->selectable.text_normal_active = t->accent_hover;
  s->selectable.text_hover_active = t->accent_hover;
  s->selectable.text_pressed_active = t->surface;
  s->selectable.rounding = t->control_radius;
  s->combo.rounding = t->control_radius;
}
