#include "backend.h"
/* Canonical runtime tokens. DESIGN.md mirrors this owner; all shared widgets
 * consume this adapter. No screen-local palettes. */
const PPUITheme pp_ui_theme = {{238, 243, 247, 255},
                               {255, 255, 255, 255},
                               {32, 53, 71, 255},
                               {82, 106, 123, 255},
                               {24, 92, 131, 255},
                               {164, 43, 52, 255},
                               6,
                               12,
                               18,
                               4,
                               8,
                               16,
                               21,
                               14,
                               32};
void pp_ui_apply_theme(PPUIBackend *ui) {
  struct nk_color colors[NK_COLOR_COUNT];
  const PPUITheme *t = &pp_ui_theme;
  for (int i = 0; i < NK_COLOR_COUNT; i++)
    colors[i] = t->surface;
  colors[NK_COLOR_TEXT] = t->ink;
  colors[NK_COLOR_WINDOW] = t->canvas;
  colors[NK_COLOR_HEADER] = t->surface;
  colors[NK_COLOR_BORDER] = t->muted;
  colors[NK_COLOR_BUTTON] = t->surface;
  colors[NK_COLOR_BUTTON_HOVER] = t->canvas;
  colors[NK_COLOR_BUTTON_ACTIVE] = t->accent;
  colors[NK_COLOR_TOGGLE] = t->surface;
  colors[NK_COLOR_TOGGLE_HOVER] = t->canvas;
  colors[NK_COLOR_TOGGLE_CURSOR] = t->accent;
  colors[NK_COLOR_SELECT] = t->canvas;
  colors[NK_COLOR_SELECT_ACTIVE] = t->accent;
  colors[NK_COLOR_SLIDER] = t->canvas;
  colors[NK_COLOR_SLIDER_CURSOR] = t->muted;
  colors[NK_COLOR_SLIDER_CURSOR_HOVER] = t->accent;
  colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = t->accent;
  colors[NK_COLOR_EDIT] = t->surface;
  colors[NK_COLOR_EDIT_CURSOR] = t->accent;
  colors[NK_COLOR_COMBO] = t->surface;
  colors[NK_COLOR_CHART] = t->canvas;
  colors[NK_COLOR_CHART_COLOR] = t->accent;
  colors[NK_COLOR_CHART_COLOR_HIGHLIGHT] = t->danger;
  colors[NK_COLOR_SCROLLBAR] = t->canvas;
  colors[NK_COLOR_SCROLLBAR_CURSOR] = t->muted;
  colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = t->accent;
  colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = t->accent;
  colors[NK_COLOR_TAB_HEADER] = t->surface;
  nk_style_from_table(&ui->context, colors);
  struct nk_style *s = &ui->context.style;
  s->window.padding = nk_vec2(t->panel, t->control);
  s->window.group_padding = nk_vec2(t->control, t->control);
  s->window.spacing = nk_vec2(t->compact, t->compact);
  s->window.rounding = t->panel_radius;
  s->window.scrollbar_size = nk_vec2(12, 12);
  s->button.rounding = t->control_radius;
  s->button.border = 1;
  s->button.text_active = t->surface;
  s->button.padding = nk_vec2(t->compact, t->compact);
  s->edit.rounding = t->control_radius;
  s->edit.border = 1;
  s->edit.cursor_normal = t->accent;
  s->edit.cursor_hover = t->accent;
  s->selectable.text_normal = t->ink;
  s->selectable.text_normal_active = t->surface;
  s->selectable.text_hover_active = t->surface;
  s->selectable.text_pressed_active = t->surface;
  s->selectable.rounding = t->control_radius;
  s->combo.rounding = t->control_radius;
}
