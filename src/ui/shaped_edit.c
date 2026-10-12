#include "shaped_edit.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct EditEvent {
  PPPlatformEvent event;
  char *payload;
  struct EditEvent *next;
} EditEvent;
struct PPUIShapedEdit {
  PPUITextLayout *layout;
  PPUIEditState state;
  GLuint *textures;
  size_t texture_count;
  uint64_t texture_generation;
  EditEvent *head, *tail;
  struct nk_rect bounds, clip;
  bool enabled, dragging;
  char *last_draft;
  char error[512];
  char configuration_error[512];
  char *attempted_draft;
  float attempted_density;
  size_t attempted_budget;
  /* The layout owns immutable fonts, size, ink and density. This signature is
   * invalidated on every layout swap, so it includes that full configuration. */
  char *failed_display;
  size_t failed_display_budget;
};
static void clear_textures(PPUIBackend *ui,PPUIShapedEdit *e) {
  pp_ui_release_texture_charges(ui,e->texture_count,e->textures);
  if (e->textures)
    glDeleteTextures((GLsizei)e->texture_count, e->textures);
  free(e->textures);
  e->textures = NULL;
  e->texture_count = 0;
  e->texture_generation = 0;
}
static void dispose(PPUIBackend *ui, bool bound) {
  PPUIShapedEdit *e = ui->shaped;
  if (!e)
    return;
  if (bound)
    clear_textures(ui,e);
  else
    {pp_ui_release_texture_charges(ui,e->texture_count,e->textures);free(e->textures);} /* Context reclaims GPU objects. */
  pp_ui_text_layout_destroy(e->layout);
  pp_ui_edit_state_clear(&e->state);
  while (e->head) {
    EditEvent *event = e->head;
    e->head = event->next;
    free(event->payload);
    free(event);
  }
  free(e->last_draft);
  free(e->attempted_draft);
  free(e->failed_display);
  free(e);
  ui->shaped = NULL;
}
bool pp_ui_shaped_reset(PPUIBackend *ui) {
  if (!ui->shaped)
    return true;
  if (!SDL_GL_MakeCurrent(ui->window, ui->gl_context))
    return false;
  dispose(ui, true);
  return true;
}
void pp_ui_shaped_destroy(PPUIBackend *ui) {
  if (!ui || !ui->shaped)
    return;
  dispose(ui, SDL_GL_MakeCurrent(ui->window, ui->gl_context));
}
static void configure(PPUIBackend *ui, PPUIShapedEdit *e, const char *draft) {
  const PPUITextView *view = pp_ui_text_layout_view(e->layout);
  if (view && fabs(view->density - ui->scale) <= .001) {
    e->configuration_error[0] = 0;
    return;
  }
  size_t budget = pp_assets_raster_bytes(ui->text_assets);
  /* Failed configuration attempts retry only for changed input/configuration
   * or shared-budget occupancy. Blink/caret frames retain the error and image.
   */
  if (e->configuration_error[0] && e->attempted_density == ui->scale &&
      e->attempted_budget == budget && e->attempted_draft &&
      !strcmp(e->attempted_draft, draft))
    return;
  free(e->attempted_draft);
  e->attempted_draft = SDL_strdup(draft);
  e->attempted_density = ui->scale;
  PPUITextLayout *candidate = NULL;
  PPError error = {0};
  uint8_t ink[] = {pp_ui_theme.ink.r, pp_ui_theme.ink.g, pp_ui_theme.ink.b,
                   255};
  if (pp_ui_text_layout_create(&candidate, ui->text_assets,
                               pp_ui_theme.body_size, ui->scale, ink, &error) &&
      pp_ui_text_layout_set(candidate, draft, &error)) {
    pp_ui_text_layout_destroy(e->layout);
    e->layout = candidate;
    free(e->failed_display);
    e->failed_display = NULL;
    clear_textures(ui,e);
    e->configuration_error[0] = 0;
  } else {
    pp_ui_text_layout_destroy(candidate);
    SDL_strlcpy(e->configuration_error,
                error.message[0] ? error.message
                                 : "Cannot configure editor text",
                sizeof(e->configuration_error));
  }
  e->attempted_budget = pp_assets_raster_bytes(ui->text_assets);
}
static bool contains(struct nk_rect r, double x, double y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
bool pp_ui_shaped_event(PPUIBackend *ui, const PPPlatformEvent *event) {
  PPUIShapedEdit *e = ui->shaped;
  if (!e)
    return false;
  bool focused = pp_ui_focused(ui, "item-text");
  bool cancel =
      event->kind == PP_EVENT_CANCEL || event->kind == PP_EVENT_FOCUS_LOST;
  double x = event->window_point.x / ui->window_scale,
         y = event->window_point.y / ui->window_scale;
  bool inside = contains(e->bounds, x, y) && contains(e->clip, x, y);
  bool pointer = event->kind == PP_EVENT_BUTTON_DOWN ||
                 event->kind == PP_EVENT_BUTTON_UP ||
                 event->kind == PP_EVENT_POINTER_MOVE;
  if (event->kind == PP_EVENT_BUTTON_DOWN && event->button == PP_BUTTON_LEFT) {
    if (inside && e->enabled && !ui->modal && !ui->popup_id) {
      pp_ui_focus(ui, "item-text");
      focused = true;
      e->dragging = true;
    } else {
      cancel = true;
      e->dragging = false;
    }
  }
  bool route = cancel || (pointer && e->dragging) ||
               (event->kind == PP_EVENT_WHEEL && inside) ||
               (focused && (event->kind == PP_EVENT_TEXT ||
                            event->kind == PP_EVENT_COMPOSITION ||
                            event->kind == PP_EVENT_KEY_DOWN));
  if (!route || (!cancel && (!e->enabled || ui->modal || ui->popup_id)))
    return false;
  if (event->kind == PP_EVENT_KEY_DOWN &&
      (event->key == PP_KEY_TAB ||
       ((event->modifiers & PP_MOD_CONTROL) &&
        (event->key == PP_KEY_S || event->key == PP_KEY_N ||
         event->key == PP_KEY_O))))
    return false;
  if (event->kind == PP_EVENT_TEXT && event->text_continuation)
    return true;
  EditEvent *q = calloc(1, sizeof(*q));
  if (!q) {
    ui->failed = true;
    return true;
  }
  q->event = *event;
  if (pointer)
    q->event.modifiers |= ui->modifiers;
  if (cancel) {
    q->event.kind = PP_EVENT_CANCEL;
    e->dragging = false;
  }
  if (event->text_utf8) {
    q->payload = SDL_strdup(event->text_utf8);
    if (!q->payload) {
      free(q);
      ui->failed = true;
      return true;
    }
    q->event.text_utf8 = q->payload;
  }
  if (e->tail)
    e->tail->next = q;
  else
    e->head = q;
  e->tail = q;
  if (event->kind == PP_EVENT_BUTTON_UP)
    e->dragging = false;
  if (event->kind == PP_EVENT_COMPOSITION)
    ui->composing = event->composing;
  if (event->kind == PP_EVENT_TEXT)
    ui->composing = false;
  if (event->kind == PP_EVENT_KEY_DOWN) {
    ui->modifiers = event->modifiers;
    ui->activate = false;
  }
  return !pointer && !cancel;
}
static bool layout_display(PPUIBackend *ui, PPUIShapedEdit *e, const char *draft,
                           size_t *first, size_t *last, size_t *caret) {
  *first = *last = 0;
  *caret = e->state.caret;
  if (!e->layout)
    return false;
  char *display = pp_ui_edit_display(&e->state, draft, first, last, caret);
  size_t budget = pp_assets_raster_bytes(ui->text_assets);
  if (display && e->failed_display && e->failed_display_budget == budget &&
      !strcmp(display, e->failed_display)) {
    free(display);
    return false;
  }
  PPError error = {0};
  bool ok = display && pp_ui_text_layout_set(e->layout, display, &error);
  free(e->failed_display);
  e->failed_display = NULL;
  if (!ok) {
    e->failed_display = display;
    e->failed_display_budget = pp_assets_raster_bytes(ui->text_assets);
    SDL_strlcpy(e->error,
                error.message[0] ? error.message
                                 : "Cannot allocate editor display",
                sizeof(e->error));
  } else {
    free(display);
    e->error[0] = 0;
    if (!e->state.composing) {
      pp_ui_edit_reconcile(&e->state, pp_ui_text_layout_view(e->layout));
      *caret = e->state.caret;
    }
  }
  return ok;
}
static PPVec2 content_point(PPUIShapedEdit *e, PPUIBackend *ui, PPVec2 point) {
  return (PPVec2){
      point.x / ui->window_scale - e->bounds.x - 6 + e->state.scroll.x,
      point.y / ui->window_scale - e->bounds.y - 6 + e->state.scroll.y};
}
static void range(struct nk_command_buffer *canvas, const PPUITextView *v,
                  size_t first, size_t last, float x, float y,
                  struct nk_color color, bool underline) {
  if (first > last) {
    size_t swap = first;
    first = last;
    last = swap;
  }
  for (size_t i = 0; i < v->caret_count; i++) {
    PPUITextCaret a = v->carets[i];
    if (a.byte < first || a.byte >= last)
      continue;
    double width = 5;
    if (i + 1 < v->caret_count && v->carets[i + 1].row == a.row)
      width = fmax(1, v->carets[i + 1].point.x - a.point.x);
    struct nk_rect r = nk_rect(x + (float)a.point.x, y + (float)a.point.y,
                               (float)width, (float)a.height);
    if (underline) {
      r.y += r.h - 2;
      r.h = 2;
    }
    nk_fill_rect(canvas, r, 0, color);
  }
}
bool pp_ui_shaped_draw(PPUIBackend *ui, char *draft, int capacity,
                       struct nk_rect bounds, bool focused, bool enabled) {
  if (!ui->shaped)
    ui->shaped = calloc(1, sizeof(*ui->shaped));
  PPUIShapedEdit *e = ui->shaped;
  if (!e) {
    ui->failed = true;
    return false;
  }
  e->bounds = bounds;
  e->clip = ui->context.current->layout->clip;
  e->enabled = enabled;
  if (e->last_draft && strcmp(e->last_draft, draft))
    pp_ui_edit_state_clear(&e->state);
  if (!focused && !e->head)
    pp_ui_edit_cancel(&e->state);
  configure(ui, e, draft);
  struct nk_rect allocated;
  nk_widget(&allocated, &ui->context);
  struct nk_command_buffer *canvas = nk_window_get_canvas(&ui->context);
  nk_fill_rect(canvas, bounds, pp_ui_theme.control_radius, pp_ui_theme.surface);
  nk_stroke_rect(canvas, bounds, pp_ui_theme.control_radius, 1,
                 focused ? pp_ui_theme.accent : pp_ui_theme.muted);
  size_t marked_first, marked_last, display_caret;
  bool changed = false;
  bool reveal = false;
  bool layout_ok =
      layout_display(ui, e, draft, &marked_first, &marked_last, &display_caret);
  while (e->head) {
    EditEvent *q = e->head;
    PPPlatformEvent event = q->event;
    if (event.kind != PP_EVENT_WHEEL)
      reveal = true;
    e->head = q->next;
    if (!e->head)
      e->tail = NULL;
    if (event.kind == PP_EVENT_CANCEL) {
      pp_ui_edit_cancel(&e->state);
      ui->composing = false;
    } else if (event.kind == PP_EVENT_TEXT) {
      char utf8[5] = {0};
      const char *payload = event.text_utf8;
      if (!payload) {
        nk_utf_encode(event.codepoint, utf8, 4);
        payload = utf8;
      }
      changed =
          pp_ui_edit_replace(&e->state, draft, (size_t)capacity, payload) ||
          changed;
    } else if (event.kind == PP_EVENT_COMPOSITION) {
      pp_ui_edit_preedit(&e->state, event.text_utf8, event.composition_start,
                         event.composition_length);
    } else if (event.kind == PP_EVENT_KEY_DOWN) {
      /* A retained invalid/oversized draft must still allow selecting all,
       * replacement, clipboard and undo while the previous image is shown. */
      bool recovery = (event.modifiers & PP_MOD_CONTROL) &&
                      (event.key == PP_KEY_A || event.key == PP_KEY_C ||
                       event.key == PP_KEY_X || event.key == PP_KEY_V ||
                       event.key == PP_KEY_Z);
      if (layout_ok || recovery)
        changed = pp_ui_edit_key(&e->state, e->layout, draft, (size_t)capacity,
                                 event.key, event.modifiers, bounds.h - 12) ||
                  changed;
    } else if (event.kind == PP_EVENT_WHEEL && e->layout) {
      e->state.scroll.y =
          fmax(0, e->state.scroll.y -
                      event.wheel_delta *
                          pp_ui_text_layout_view(e->layout)->line_height * 3);
    } else if (!e->state.composing && layout_ok) {
      PPUITextCaret hit = pp_ui_text_layout_hit(
          e->layout, content_point(e, ui, event.window_point));
      e->state.caret = hit.byte;
      if (event.kind == PP_EVENT_BUTTON_DOWN &&
          !(event.modifiers & PP_MOD_SHIFT))
        e->state.anchor = hit.byte;
      e->state.preferred_valid = false;
    }
    free(q->payload);
    free(q);
    configure(ui, e, draft);
    layout_ok =
        layout_display(ui, e, draft, &marked_first, &marked_last, &display_caret);
  }
  const PPUITextView *v = pp_ui_text_layout_view(e->layout);
  if (!v)
    goto done;
  if (e->texture_generation != v->generation) {
    clear_textures(ui,e);
    e->textures = calloc(v->row_count, sizeof(*e->textures));
    if (!e->textures) {
      ui->failed = true;
      goto done;
    }
    e->texture_count = v->row_count;
    for (size_t i = 0; i < v->row_count; i++)
      if (v->rows[i].frame.pixels) {
        PPError error = {0};
        if (!pp_ui_upload_frame(ui, &e->textures[i], &v->rows[i].frame,
                                &error)) {
          SDL_strlcpy(e->error, error.message, sizeof(e->error));
          goto done;
        }
      }
    e->texture_generation = v->generation;
  }
  PPUITextCaret caret = pp_ui_text_layout_caret(e->layout, display_caret);
  double width = bounds.w - 12, height = bounds.h - 12;
  if (focused && reveal) {
    if (caret.point.x < e->state.scroll.x)
      e->state.scroll.x = caret.point.x;
    if (caret.point.x > e->state.scroll.x + width - 2)
      e->state.scroll.x = caret.point.x - width + 2;
    if (caret.point.y < e->state.scroll.y)
      e->state.scroll.y = caret.point.y;
    if (caret.point.y + caret.height > e->state.scroll.y + height)
      e->state.scroll.y = caret.point.y + caret.height - height;
  }
  e->state.scroll.x = fmax(0, e->state.scroll.x);
  e->state.scroll.y =
      fmax(0, fmin(e->state.scroll.y, fmax(0, v->extent.y - height)));
  float x = bounds.x + 6 - (float)e->state.scroll.x,
        y = bounds.y + 6 - (float)e->state.scroll.y;
  struct nk_rect clip = canvas->clip;
  float left = fmaxf(bounds.x + 2, clip.x), top = fmaxf(bounds.y + 2, clip.y);
  nk_push_scissor(
      canvas,
      nk_rect(left, top,
              fmaxf(0, fminf(bounds.x + bounds.w - 2, clip.x + clip.w) - left),
              fmaxf(0, fminf(bounds.y + bounds.h - 2, clip.y + clip.h) - top)));
  if (focused && !e->state.composing)
    range(canvas, v, e->state.anchor, e->state.caret, x, y,
          nk_rgba(pp_ui_theme.accent.r, pp_ui_theme.accent.g,
                  pp_ui_theme.accent.b, 60),
          false);
  if (e->state.composing)
    range(canvas, v, marked_first, marked_last, x, y, pp_ui_theme.canvas,
          false);
  for (size_t i = 0; i < v->row_count; i++)
    if (e->textures[i]) {
      const PPUITextRow *row = &v->rows[i];
      struct nk_image image = nk_image_id((int)e->textures[i]);
      nk_draw_image(
          canvas,
          nk_rect(x - (float)(row->origin_px.x / v->density),
                  y + (float)row->y - (float)(row->origin_px.y / v->density),
                  (float)(row->frame.width / v->density),
                  (float)(row->frame.height / v->density)),
          &image, nk_rgba(255, 255, 255, 255));
    }
  if (e->state.composing)
    range(canvas, v, e->state.replace_first,
          e->state.replace_first + strlen(e->state.preedit), x, y,
          pp_ui_theme.accent, true);
  if (focused) {
    if ((SDL_GetTicks() / 500) % 2 == 0 || e->state.composing)
      nk_fill_rect(canvas,
                   nk_rect(x + (float)caret.point.x, y + (float)caret.point.y,
                           1 / ui->scale, (float)caret.height),
                   0, pp_ui_theme.ink);
    SDL_Rect area = {(int)((x + caret.point.x) * ui->window_scale),
                     (int)((y + caret.point.y) * ui->window_scale), 1,
                     (int)(caret.height * ui->window_scale)};
    SDL_SetTextInputArea(ui->window, &area, 0);
  }
  nk_push_scissor(canvas, clip);
done:
  ; /* A C11 label must precede a statement. */
  const char *diagnostic =
      e->configuration_error[0] ? e->configuration_error : e->error;
  if (diagnostic[0]) {
    struct nk_rect notice =
        nk_rect(bounds.x + 4, bounds.y + bounds.h - 22, bounds.w - 8, 20);
    nk_draw_text(canvas, notice, diagnostic, (int)strlen(diagnostic),
                 &ui->body_font->handle, pp_ui_theme.surface,
                 pp_ui_theme.danger);
  }
  if (focused)
    ui->composing = e->state.composing;
  free(e->last_draft);
  e->last_draft = SDL_strdup(draft);
  return changed;
}
const PPUITextView *pp_ui_shaped_view(const PPUIBackend *ui) {
  return ui->shaped ? pp_ui_text_layout_view(ui->shaped->layout) : NULL;
}
const PPUIEditState *pp_ui_shaped_state(const PPUIBackend *ui) {
  return ui->shaped ? &ui->shaped->state : NULL;
}
