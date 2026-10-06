#include "text_layout.h"
#include "../media/resources.h"
#include "../media/text_raster.h"
#include <SDL3_ttf/SDL_textengine.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PPUITextLayout {
  PPBlob bytes[2];
  SDL_IOStream *io[2];
  TTF_Font *font[2];
  TTF_Text **lines;
  char *text;
  double size, shaping;
  uint8_t ink[4];
  PPUITextView view;
  bool initialized;
  PPAssets *assets;
  bool own_assets;
  size_t raster_bytes;
};
static bool fail(PPError *error, const char *message) {
  if (error) {
    error->code = PP_ERROR_RENDER;
    snprintf(error->message, sizeof(error->message), "%s", message);
  }
  return false;
}
static void clear_view(PPUITextView *view, TTF_Text **lines) {
  for (size_t i = 0; i < view->row_count; i++) {
    TTF_DestroyText(lines ? lines[i] : NULL);
    pp_frame_destroy(&view->rows[i].frame);
  }
  free(lines);
  free(view->rows);
  free(view->clusters);
  free(view->carets);
  memset(view, 0, sizeof(*view));
}
void pp_ui_text_layout_destroy(PPUITextLayout *layout) {
  if (!layout)
    return;
  clear_view(&layout->view, layout->lines);
  pp_assets_release_raster(layout->assets, layout->raster_bytes);
  if (layout->own_assets)
    pp_assets_destroy(layout->assets);
  if (layout->font[0])
    TTF_ClearFallbackFonts(layout->font[0]);
  for (int i = 0; i < 2; i++) {
    TTF_CloseFont(layout->font[i]);
    if (layout->io[i])
      SDL_CloseIO(layout->io[i]);
    pp_blob_destroy(&layout->bytes[i]);
  }
  free(layout->text);
  if (layout->initialized)
    TTF_Quit();
  free(layout);
}
bool pp_ui_text_layout_create(PPUITextLayout **out, PPAssets *assets,
                              double size, double density, const uint8_t ink[4],
                              PPError *error) {
  PPUITextLayout *l = calloc(1, sizeof(*l));
  if (!l)
    return fail(error, "Cannot allocate editor layout");
  l->size = size;
  l->view.density = density;
  memcpy(l->ink, ink, 4);
  l->assets = assets;
  if (!assets) {
    if (!pp_assets_create(&l->assets, error))
      goto failed;
    l->own_assets = true;
  }
  if (!(l->initialized = TTF_Init()))
    goto failed;
#ifdef __APPLE__
  const char *family = "Helvetica";
#else
  const char *family = "Segoe UI";
#endif
  if (!pp_platform_find_font(family, &l->bytes[0], error))
    goto failed;
  l->bytes[1].data =
      pp_resource_load("fonts/NotoColorEmoji.ttf", &l->bytes[1].size);
  PPFontInfo info;
  if (!l->bytes[1].data ||
      !pp_font_inspect((PPBytes){l->bytes[1].data, l->bytes[1].size}, 0, &info,
                       error))
    goto failed;
  l->shaping = info.bitmap_em_size > 0 ? info.bitmap_em_size : size * density;
  for (int i = 0; i < 2; i++) {
    l->io[i] = SDL_IOFromConstMem(l->bytes[i].data, l->bytes[i].size);
    if (!l->io[i] ||
        !(l->font[i] = TTF_OpenFontIO(l->io[i], false, (float)l->shaping)))
      goto failed;
  }
  if (!TTF_AddFallbackFont(l->font[0], l->font[1]))
    goto failed;
  l->view.line_height =
      fmax(TTF_GetFontLineSkip(l->font[0]), TTF_GetFontHeight(l->font[1])) *
      size / l->shaping;
  *out = l;
  return true;
failed:
  fail(error, SDL_GetError());
  pp_ui_text_layout_destroy(l);
  return false;
}
static int caret_order(const void *a, const void *b) {
  size_t x = ((const PPUITextCaret *)a)->byte,
         y = ((const PPUITextCaret *)b)->byte;
  return x < y ? -1 : x > y ? 1 : 0;
}
bool pp_ui_text_layout_set(PPUITextLayout *l, const char *utf8,
                           PPError *error) {
  if (!pp_ui_text_valid_utf8(utf8))
    return fail(error, "Editor text must be valid UTF-8");
  if (l->text && !strcmp(l->text, utf8))
    return true;
  size_t length = strlen(utf8), rows = 1;
  for (size_t i = 0; i < length; i++)
    if (utf8[i] == '\n')
      rows++;
  PPUITextView v = {.density = l->view.density,
                    .line_height = l->view.line_height,
                    .generation = l->view.generation + 1};
  v.rows = calloc(rows, sizeof(*v.rows));
  v.carets = calloc(length + rows + 1, sizeof(*v.carets));
  v.clusters = calloc(length + 1, sizeof(*v.clusters));
  TTF_Text **lines = calloc(rows, sizeof(*lines));
  char *copy = SDL_strdup(utf8);
  size_t raster_bytes = 0;
  if (!v.rows || !v.carets || !v.clusters || !lines || !copy)
    goto failed;
  v.row_count = rows;
  double ratio = l->size / l->shaping;
  PPTextRasterFont fonts[2] = {
      {l->font[0], {l->bytes[0].data, l->bytes[0].size}, 0},
      {l->font[1], {l->bytes[1].data, l->bytes[1].size}, 0}};
  size_t start = 0;
  for (size_t row = 0; row < rows; row++) {
    size_t end = start;
    while (end < length && utf8[end] != '\n')
      end++;
    size_t content_end = end;
    if (content_end > start && utf8[content_end - 1] == '\r')
      content_end--;
    PPUITextRow *r = &v.rows[row];
    *r = (PPUITextRow){
        .begin = start, .end = content_end, .y = row * v.line_height};
    size_t first = v.caret_count;
    v.carets[v.caret_count++] =
        (PPUITextCaret){start, row, {0, r->y}, v.line_height};
    double terminal = 0;
    if (content_end > start) {
      lines[row] =
          TTF_CreateText(NULL, l->font[0], utf8 + start, content_end - start);
      if (!lines[row] || !TTF_SetTextWrapWhitespaceVisible(lines[row], true) ||
          !TTF_UpdateText(lines[row]))
        goto failed;
      /* Copy once before any dependency setter may invalidate cluster data. */
      for (int i = 0; i < lines[row]->internal->num_clusters; i++) {
        TTF_SubString c = lines[row]->internal->clusters[i];
        size_t byte = start + (size_t)c.offset;
        if (byte > content_end)
          continue;
        double x = c.rect.x * ratio;
        if (c.flags & TTF_SUBSTRING_TEXT_END) {
          terminal = x;
          continue;
        }
        if (c.length <= 0)
          continue;
        size_t finish = byte + (size_t)c.length;
        if (finish > content_end)
          finish = content_end;
        v.clusters[v.cluster_count++] = (PPUITextCluster){
            byte,
            finish,
            row,
            {x, r->y + c.rect.y * ratio, c.rect.w * ratio, c.rect.h * ratio},
            c.flags & TTF_SUBSTRING_DIRECTION_MASK};
        if (byte == start)
          v.carets[first].point.x = x;
        else
          v.carets[v.caret_count++] =
              (PPUITextCaret){byte, row, {x, r->y}, v.line_height};
        terminal = fmax(terminal, x + c.rect.w * ratio);
      }
      if (lines[row]->internal->w > 0 && lines[row]->internal->h > 0 &&
          lines[row]->internal->num_ops > 0 &&
          !pp_text_raster_origin(lines[row], fonts, l->shaping,
                                 l->size * v.density, l->ink, &r->frame,
                                 &r->origin_px, error))
        goto failed;
      size_t bytes =
          (r->frame.stride + (size_t)r->frame.width) * r->frame.height;
      if (bytes) {
        if (bytes > 256u * 1024 * 1024 - raster_bytes ||
            !pp_assets_reserve_raster(l->assets, bytes, error))
          goto failed;
        raster_bytes += bytes;
      }
    }
    if (content_end > start)
      v.carets[v.caret_count++] =
          (PPUITextCaret){content_end, row, {terminal, r->y}, v.line_height};
    v.extent.x = fmax(v.extent.x, terminal);
    start = end + 1;
  }
  qsort(v.carets, v.caret_count, sizeof(*v.carets), caret_order);
  /* Metadata can include shared-offset sentinels; editing stops are unique. */
  size_t n = 0;
  for (size_t i = 0; i < v.caret_count; i++)
    if (!n || v.carets[i].byte != v.carets[n - 1].byte)
      v.carets[n++] = v.carets[i];
  v.caret_count = n;
  v.extent.y = rows * v.line_height;
  clear_view(&l->view, l->lines);
  free(l->text);
  pp_assets_release_raster(l->assets, l->raster_bytes);
  l->raster_bytes = raster_bytes;
  l->view = v;
  l->lines = lines;
  l->text = copy;
  return true;
failed:
  pp_assets_release_raster(l->assets, raster_bytes);
  clear_view(&v, lines);
  free(copy);
  return fail(error, "Cannot shape or rasterize editor text");
}
bool pp_ui_text_valid_utf8(const char *text) {
  if (!text)
    return false;
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    unsigned first = *p++, count = 0;
    uint32_t code = 0, minimum = 0;
    if (first < 0x80)
      continue;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1;
      code = first & 31;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2;
      code = first & 15;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3;
      code = first & 7;
      minimum = 0x10000;
    } else
      return false;
    for (unsigned i = 0; i < count; i++) {
      if ((*p & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (*p++ & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}
const PPUITextView *pp_ui_text_layout_view(const PPUITextLayout *l) {
  return l ? &l->view : NULL;
}
PPUITextCaret pp_ui_text_layout_caret(const PPUITextLayout *l, size_t byte) {
  PPUITextCaret c = {0};
  for (size_t i = 0; i < l->view.caret_count; i++) {
    if (l->view.carets[i].byte > byte)
      break;
    c = l->view.carets[i];
  }
  return c;
}
PPUITextCaret pp_ui_text_layout_hit(const PPUITextLayout *l, PPVec2 point) {
  const PPUITextView *v = &l->view;
  size_t row = (size_t)fmax(0, floor(point.y / v->line_height));
  if (row >= v->row_count)
    row = v->row_count - 1;
  PPUITextCaret best = {0};
  double distance = HUGE_VAL;
  for (size_t i = 0; i < v->caret_count; i++)
    if (v->carets[i].row == row) {
      double d = fabs(v->carets[i].point.x - point.x);
      if (d < distance) {
        distance = d;
        best = v->carets[i];
      }
    }
  return best;
}
