#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t derive_label(const struct nk_font *font, const char *text,
                      PPUILabelWhitespace whitespace, char *label, size_t capacity) {
  size_t length = 0, written = 0, remaining = text ? strlen(text) : 0;
  bool last_space = true, full = false;
  bool collapse = whitespace == PP_UI_LABEL_COLLAPSE_WHITESPACE;
  for (const char *p = text; p && *p;) {
    nk_rune rune;
    int used = nk_utf_decode(p, &rune, (int)remaining);
    if (used <= 0) break;
    const struct nk_font_glyph *glyph = nk_font_find_glyph(font, rune);
    char replacement[20];
    const char *part = p;
    size_t bytes = (size_t)used;
    if ((collapse && rune <= ' ') || rune == '\n' || rune == '\r' || rune == '\t') {
      part = " ";
      bytes = collapse && last_space ? 0 : 1;
    } else if (!glyph || glyph->codepoint != rune) {
      snprintf(replacement, sizeof(replacement), collapse ? "U+%04X" : "U+%04X ",
               (unsigned)rune);
      part = replacement; bytes = strlen(replacement);
    }
    if (label && !full && written + bytes < capacity) {
      memcpy(label + written, part, bytes);
      written += bytes;
    } else if (bytes) full = true;
    length += bytes;
    if (bytes) last_space = part[bytes - 1] == ' ';
    p += used;
    remaining -= (size_t)used;
  }
  if (collapse && length && last_space) length--;
  if (label && capacity) {
    if (collapse)
      while (written && label[written - 1] == ' ') written--;
    label[written] = 0;
  }
  return length;
}
void pp_ui_label_text(const struct nk_font *font, const char *text,
                      PPUILabelWhitespace whitespace, char *label, size_t capacity) {
  derive_label(font, text, whitespace, label, capacity);
}
char *pp_ui_item_label(const struct nk_font *font, const PPItem *item) {
  size_t length = item->kind == PP_ITEM_TEXT
      ? derive_label(font, item->text, PP_UI_LABEL_COLLAPSE_WHITESPACE, NULL, 0) : 0;
  size_t capacity = length ? length + 1 : 32;
  char *label = malloc(capacity);
  if (!label) return NULL;
  if (length)
    pp_ui_label_text(font, item->text, PP_UI_LABEL_COLLAPSE_WHITESPACE, label, capacity);
  else
    snprintf(label, capacity, "%s %llu", item->kind == PP_ITEM_IMAGE ? "Image" : "Text",
             (unsigned long long)item->id);
  return label;
}
size_t pp_ui_label_line(const struct nk_user_font *font, const char *text,
                        size_t length, float width) {
  size_t end = 0, space = 0;
  while (end < length) {
    nk_rune rune;
    int used = nk_utf_decode(text + end, &rune, (int)(length - end));
    if (used <= 0) break;
    size_t next = end + (size_t)used;
    if (font->width(font->userdata, font->height, text, (int)next) > width)
      return space ? space : end;
    end = next;
    if (rune == ' ') space = end;
  }
  return end;
}
size_t pp_ui_label_draw(struct nk_command_buffer *canvas, struct nk_rect bounds,
                        const struct nk_user_font *font, const char *text,
                        size_t length, struct nk_color background, struct nk_color ink) {
  size_t end = 0;
  float line_height = font->height + 4;
  for (float y = 0; y + font->height <= bounds.h && end < length; y += line_height) {
    size_t bytes = pp_ui_label_line(font, text + end, length - end, bounds.w);
    if (!bytes) break;
    nk_draw_text(canvas, nk_rect(bounds.x, bounds.y + y, bounds.w, font->height),
                   text + end, (int)bytes, font, background, ink);
    end += bytes;
  }
  return end;
}

size_t pp_ui_fit_label(const struct nk_user_font *font, char *label,
                       size_t capacity, float width) {
  if (!capacity) return 0;
  size_t length = strlen(label);
  bool abbreviated = font->width(font->userdata, font->height, label, (int)length) > width;
  float suffix = abbreviated ? font->width(font->userdata, font->height, "...", 3) : 0;
  if (abbreviated) {
    size_t end = 0;
    while (end < length) {
      nk_rune rune;
      int used = nk_utf_decode(label + end, &rune, (int)(length - end));
      if (used <= 0) break;
      size_t next = end + (size_t)used;
      if (next + 3 >= capacity ||
          font->width(font->userdata, font->height, label, (int)next) + suffix > width)
        break;
      end = next;
    }
    length = end;
  }
  if (abbreviated && width >= suffix && length + 3 < capacity) {
    memcpy(label + length, "...", 3);
    length += 3;
  }
  label[length] = 0;
  return length;
}
