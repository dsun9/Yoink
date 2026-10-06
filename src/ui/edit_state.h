#ifndef POINTY_UI_EDIT_STATE_H
#define POINTY_UI_EDIT_STATE_H
#include "text_layout.h"
#define PP_UI_UNDO_LIMIT 64
typedef struct PPUIEditSnapshot {
  char *text;
  size_t caret, anchor;
} PPUIEditSnapshot;
typedef struct PPUIEditState {
  size_t caret, anchor;
  double preferred_x;
  bool preferred_valid, composing;
  char *preedit;
  int preedit_start, preedit_length;
  size_t replace_first, replace_last;
  PPVec2 scroll;
  PPUIEditSnapshot undo[PP_UI_UNDO_LIMIT], redo[PP_UI_UNDO_LIMIT];
  size_t undo_count, redo_count;
} PPUIEditState;
void pp_ui_edit_state_clear(PPUIEditState *state);
void pp_ui_edit_preedit(PPUIEditState *state, const char *text, int start,
                        int length);
void pp_ui_edit_cancel(PPUIEditState *state);
char *pp_ui_edit_display(const PPUIEditState *state, const char *draft,
                         size_t *marked_first, size_t *marked_last,
                         size_t *caret);
bool pp_ui_edit_replace(PPUIEditState *state, char *draft, size_t capacity,
                        const char *payload);
/* Call only after a successful layout of the committed draft, never preedit
 * or a retained view of an older draft. Collapsed positions have right affinity;
 * a selection expands to whole clusters without reversing its direction. */
void pp_ui_edit_reconcile(PPUIEditState *state, const PPUITextView *view);
bool pp_ui_edit_key(PPUIEditState *state, PPUITextLayout *layout, char *draft,
                    size_t capacity, PPKey key, unsigned modifiers,
                    double viewport_height);
#endif
