#include "edit_state.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static size_t minimum(size_t a, size_t b) { return a < b ? a : b; }
static size_t maximum(size_t a, size_t b) { return a > b ? a : b; }
static void snapshots_clear(PPUIEditSnapshot *snapshots, size_t *count) {
  for (size_t i = 0; i < *count; i++)
    free(snapshots[i].text);
  *count = 0;
}
void pp_ui_edit_cancel(PPUIEditState *s) {
  free(s->preedit);
  s->preedit = NULL;
  s->composing = false;
}
void pp_ui_edit_state_clear(PPUIEditState *s) {
  snapshots_clear(s->undo, &s->undo_count);
  snapshots_clear(s->redo, &s->redo_count);
  pp_ui_edit_cancel(s);
  memset(s, 0, sizeof(*s));
}
void pp_ui_edit_preedit(PPUIEditState *s, const char *text, int start,
                        int length) {
  if (!text || !*text) {
    pp_ui_edit_cancel(s);
    return;
  }
  if (!pp_ui_text_valid_utf8(text))
    return;
  char *copy = SDL_strdup(text);
  if (!copy)
    return;
  if (!s->composing) {
    s->replace_first = minimum(s->caret, s->anchor);
    s->replace_last = maximum(s->caret, s->anchor);
  }
  free(s->preedit);
  s->preedit = copy;
  s->composing = true;
  s->preedit_start = start;
  s->preedit_length = length;
}
static size_t character_byte(const char *text, int count) {
  const char *p = text;
  for (int i = 0; i < count && *p; i++)
    SDL_StepUTF8(&p, NULL);
  return (size_t)(p - text);
}
char *pp_ui_edit_display(const PPUIEditState *s, const char *draft,
                         size_t *first, size_t *last, size_t *caret) {
  *first = *last = 0;
  *caret = s->caret;
  if (!s->composing)
    return SDL_strdup(draft);
  size_t n = strlen(s->preedit), tail = strlen(draft + s->replace_last);
  char *display = malloc(s->replace_first + n + tail + 1);
  if (!display)
    return NULL;
  memcpy(display, draft, s->replace_first);
  memcpy(display + s->replace_first, s->preedit, n);
  memcpy(display + s->replace_first + n, draft + s->replace_last, tail + 1);
  size_t start = character_byte(s->preedit, s->preedit_start);
  size_t finish = start + character_byte(s->preedit + start, s->preedit_length);
  *first = s->replace_first + start;
  *last = s->replace_first + finish;
  *caret = *last;
  return display;
}
static bool push(PPUIEditSnapshot *items, size_t *count, const char *text,
                 size_t caret, size_t anchor) {
  char *copy = SDL_strdup(text);
  if (!copy)
    return false;
  if (*count == PP_UI_UNDO_LIMIT) {
    free(items[0].text);
    memmove(items, items + 1, (PP_UI_UNDO_LIMIT - 1) * sizeof(*items));
    (*count)--;
  }
  items[(*count)++] = (PPUIEditSnapshot){copy, caret, anchor};
  return true;
}
bool pp_ui_edit_replace(PPUIEditState *s, char *draft, size_t capacity,
                        const char *payload) {
  if (!pp_ui_text_valid_utf8(payload))
    return false;
  size_t first = s->composing ? s->replace_first : minimum(s->caret, s->anchor);
  size_t last = s->composing ? s->replace_last : maximum(s->caret, s->anchor);
  size_t length = strlen(draft), n = strlen(payload);
  if (first > last || last > length || n >= capacity ||
      length - (last - first) >= capacity - n)
    return false;
  if (first == last && !n) {
    pp_ui_edit_cancel(s);
    return false;
  }
  if (!push(s->undo, &s->undo_count, draft, s->caret, s->anchor))
    return false;
  snapshots_clear(s->redo, &s->redo_count);
  memmove(draft + first + n, draft + last, length - last + 1);
  memcpy(draft + first, payload, n);
  s->caret = s->anchor = first + n;
  s->preferred_valid = false;
  pp_ui_edit_cancel(s);
  return true;
}
static bool undo(PPUIEditState *s, char *draft, size_t capacity, bool redo) {
  PPUIEditSnapshot *from = redo ? s->redo : s->undo,
                   *to = redo ? s->undo : s->redo;
  size_t *source = redo ? &s->redo_count : &s->undo_count,
         *dest = redo ? &s->undo_count : &s->redo_count;
  if (!*source || strlen(from[*source - 1].text) >= capacity ||
      !push(to, dest, draft, s->caret, s->anchor))
    return false;
  PPUIEditSnapshot saved = from[--*source];
  strcpy(draft, saved.text);
  s->caret = saved.caret;
  s->anchor = saved.anchor;
  free(saved.text);
  s->preferred_valid = false;
  return true;
}
static size_t boundary(const PPUITextView *view, size_t byte, bool right) {
  size_t previous = 0;
  for (size_t i = 0; i < view->caret_count; i++) {
    size_t stop = view->carets[i].byte;
    if (stop == byte)
      return stop;
    if (stop > byte)
      return right ? stop : previous;
    previous = stop;
  }
  return previous;
}
void pp_ui_edit_reconcile(PPUIEditState *s, const PPUITextView *view) {
  if (s->composing || !view || !view->caret_count)
    return;
  bool collapsed = s->caret == s->anchor;
  bool forward = s->caret > s->anchor;
  s->caret = boundary(view, s->caret, collapsed || forward);
  s->anchor = boundary(view, s->anchor, collapsed || !forward);
}
bool pp_ui_edit_key(PPUIEditState *s, PPUITextLayout *layout, char *draft,
                    size_t capacity, PPKey key, unsigned modifiers,
                    double viewport_height) {
  bool ctrl = (modifiers & PP_MOD_CONTROL) != 0,
       shift = (modifiers & PP_MOD_SHIFT) != 0;
  if (s->composing) {
    if (key == PP_KEY_ESCAPE)
      pp_ui_edit_cancel(s);
    return false;
  }
  size_t length = strlen(draft), first = minimum(s->caret, s->anchor),
         last = maximum(s->caret, s->anchor);
  if (ctrl && key == PP_KEY_A) {
    s->anchor = 0;
    s->caret = length;
    return false;
  }
  if (ctrl && (key == PP_KEY_C || key == PP_KEY_X)) {
    if (last > first) {
      char *copy = malloc(last - first + 1);
      if (!copy)
        return false;
      memcpy(copy, draft + first, last - first);
      copy[last - first] = 0;
      bool copied = SDL_SetClipboardText(copy);
      free(copy);
      if (copied && key == PP_KEY_X)
        return pp_ui_edit_replace(s, draft, capacity, "");
    }
    return false;
  }
  if (ctrl && key == PP_KEY_V) {
    char *text = SDL_GetClipboardText();
    bool changed = text && pp_ui_edit_replace(s, draft, capacity, text);
    SDL_free(text);
    return changed;
  }
  if (ctrl && key == PP_KEY_Z)
    return undo(s, draft, capacity, shift);
  const PPUITextView *v = pp_ui_text_layout_view(layout);
  size_t index = 0;
  while (index + 1 < v->caret_count && v->carets[index + 1].byte <= s->caret)
    index++;
  size_t previous = index ? v->carets[index - 1].byte : 0;
  size_t next = index + 1 < v->caret_count ? v->carets[index + 1].byte : length;
  if (key == PP_KEY_BACKSPACE || key == PP_KEY_DELETE) {
    if (first == last) {
      if (key == PP_KEY_BACKSPACE)
        s->anchor = previous;
      else
        s->anchor = next;
    }
    return pp_ui_edit_replace(s, draft, capacity, "");
  }
  if (key == PP_KEY_ENTER && !ctrl)
    return pp_ui_edit_replace(s, draft, capacity, "\n");
  size_t target = s->caret;
  PPUITextCaret caret = pp_ui_text_layout_caret(layout, s->caret);
  if (key == PP_KEY_LEFT)
    target = !shift && first != last ? first : previous;
  else if (key == PP_KEY_RIGHT)
    target = !shift && first != last ? last : next;
  else if (key == PP_KEY_HOME)
    target = ctrl ? 0 : v->rows[caret.row].begin;
  else if (key == PP_KEY_END)
    target = ctrl ? length : v->rows[caret.row].end;
  else if (key == PP_KEY_UP || key == PP_KEY_DOWN || key == PP_KEY_PAGE_UP ||
           key == PP_KEY_PAGE_DOWN) {
    if (!s->preferred_valid) {
      s->preferred_x = caret.point.x;
      s->preferred_valid = true;
    }
    double lines = (key == PP_KEY_PAGE_UP || key == PP_KEY_PAGE_DOWN)
                       ? fmax(1, floor(viewport_height / v->line_height))
                       : 1;
    double delta = lines * v->line_height;
    if (key == PP_KEY_UP || key == PP_KEY_PAGE_UP)
      delta = -delta;
    target = pp_ui_text_layout_hit(
                 layout, (PPVec2){s->preferred_x,
                                  caret.point.y + delta + .5 * v->line_height})
                 .byte;
  } else
    return false;
  if (key != PP_KEY_UP && key != PP_KEY_DOWN && key != PP_KEY_PAGE_UP &&
      key != PP_KEY_PAGE_DOWN)
    s->preferred_valid = false;
  s->caret = target;
  if (!shift)
    s->anchor = target;
  return false;
}
