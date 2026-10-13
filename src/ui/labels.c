#include "backend.h"
#include <stdio.h>
#include <string.h>

void pp_ui_label_text(const struct nk_font *font, const char *text,
                      PPUILabelWhitespace whitespace, char *label, size_t capacity) {
  if (!capacity) return;
  size_t length = 0;
  bool collapse = whitespace == PP_UI_LABEL_COLLAPSE_WHITESPACE;
  for (const char *p = text; p && *p;) {
    nk_rune rune;
    int used = nk_utf_decode(p, &rune, (int)strlen(p));
    if (used <= 0) break;
    const struct nk_font_glyph *glyph = nk_font_find_glyph(font, rune);
    char replacement[20];
    const char *part = p;
    size_t bytes = (size_t)used;
    if ((collapse && rune <= ' ') || rune == '\n' || rune == '\r' || rune == '\t') {
      part = " ";
      bytes = collapse && (!length || label[length - 1] == ' ') ? 0 : 1;
    } else if (!glyph || glyph->codepoint != rune) {
      snprintf(replacement, sizeof(replacement), collapse ? "U+%04X" : "U+%04X ",
               (unsigned)rune);
      part = replacement; bytes = strlen(replacement);
    }
    if (length + bytes >= capacity) break;
    memcpy(label + length, part, bytes);
    length += bytes;
    p += used;
  }
  if (collapse)
    while (length && label[length - 1] == ' ') length--;
  label[length] = 0;
}

size_t pp_ui_fit_label(const struct nk_user_font *font, char *label,
                       size_t capacity, float width) {
  if (!capacity) return 0;
  size_t length = strlen(label);
  bool abbreviated = font->width(font->userdata, font->height, label, (int)length) > width;
  float suffix = abbreviated ? font->width(font->userdata, font->height, "...", 3) : 0;
  while (length && (font->width(font->userdata, font->height, label, (int)length) + suffix > width ||
                    (abbreviated && length + 3 >= capacity))) {
    length--;
    while (length && ((unsigned char)label[length] & 0xc0) == 0x80) length--;
  }
  if (abbreviated && width >= suffix && length + 3 < capacity) {
    memcpy(label + length, "...", 3);
    length += 3;
  }
  label[length] = 0;
  return length;
}
