#ifndef POINTY_UI_TEXT_LAYOUT_H
#define POINTY_UI_TEXT_LAYOUT_H
#include "pointy/platform.h"
#include "pointy/assets.h"

typedef struct PPUITextLayout PPUITextLayout;
typedef struct PPUITextCaret {
  size_t byte, row;
  PPVec2 point;
  double height;
} PPUITextCaret;
typedef struct PPUITextCluster {
  size_t begin, end, row;
  PPRect ink;
  unsigned direction;
} PPUITextCluster;
typedef struct PPUITextRow {
  size_t begin, end;
  PPFrame frame;
  PPVec2 origin_px;
  double y;
} PPUITextRow;
typedef struct PPUITextView {
  uint64_t generation;
  PPUITextCaret *carets;
  size_t caret_count;
  PPUITextCluster *clusters;
  size_t cluster_count;
  PPUITextRow *rows;
  size_t row_count;
  PPVec2 extent;
  double density, line_height;
} PPUITextView;
/* Owns font bytes and retained full hard-line layouts. View is valid until a
 * successful changed set or destruction; failure leaves the previous view. */
bool pp_ui_text_layout_create(PPUITextLayout **out, PPAssets *assets,
                              double font_dip, double density,
                              const uint8_t ink[4], PPError *error);
bool pp_ui_text_layout_set(PPUITextLayout *layout, const char *utf8,
                           PPError *error);
const PPUITextView *pp_ui_text_layout_view(const PPUITextLayout *layout);
PPUITextCaret pp_ui_text_layout_caret(const PPUITextLayout *layout,
                                      size_t byte);
PPUITextCaret pp_ui_text_layout_hit(const PPUITextLayout *layout, PPVec2 point);
void pp_ui_text_layout_destroy(PPUITextLayout *layout);
bool pp_ui_text_valid_utf8(const char *text);
#endif
