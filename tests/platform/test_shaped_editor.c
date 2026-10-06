#include "../../src/ui/shaped_edit.h"
#include "../../src/media/resources.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef _WIN32
#include <windows.h>
#endif

#undef SDL_GL_MakeCurrent
#undef pp_ui_text_layout_create
#undef pp_ui_text_layout_set
#undef glDeleteTextures
bool SDLCALL SDL_GL_MakeCurrent(SDL_Window *, SDL_GLContext);
bool pp_ui_text_layout_create(PPUITextLayout **, PPAssets *, double, double,
                              const uint8_t[4], PPError *);
bool pp_ui_text_layout_set(PPUITextLayout *, const char *, PPError *);
void APIENTRY glDeleteTextures(GLsizei, const GLuint *);
static bool deny_context;
static unsigned layout_creates, foreign_deletes;
static unsigned layout_sets;
static SDL_GLContext expected_context;
static float forced_density;
static bool error_drawn;
static float field_height = 78;
bool pp_test_layout_set(PPUITextLayout *layout, const char *text, PPError *e) {
  layout_sets++;
  return pp_ui_text_layout_set(layout, text, e);
}
bool SDLCALL pp_test_make_current(SDL_Window *window, SDL_GLContext context) {
  return !deny_context && SDL_GL_MakeCurrent(window, context);
}
bool pp_test_layout_create(PPUITextLayout **out, PPAssets *assets, double size,
                           double density, const uint8_t ink[4], PPError *e) {
  layout_creates++;
  return pp_ui_text_layout_create(out, assets, size, density, ink, e);
}
void APIENTRY pp_test_delete_textures(GLsizei count, const GLuint *textures) {
  if (expected_context && SDL_GL_GetCurrentContext() != expected_context)
    foreign_deletes++;
  glDeleteTextures(count, textures);
}

static PPUIBackend *ui;
static PPError error;
static char draft[16384];
/* Test the exact SDL boundary bytes without replacing any user clipboard
 * format. The native input mode does not synthesize copy/paste. */
static char *clipboard_text;
bool SDLCALL pp_test_set_clipboard(const char *text) {
  char *copy = SDL_strdup(text);
  if (!copy)
    return false;
  SDL_free(clipboard_text);
  clipboard_text = copy;
  return true;
}
char *SDLCALL pp_test_get_clipboard(void) {
  return SDL_strdup(clipboard_text ? clipboard_text : "");
}
static bool draw(const char *capture) {
  CHECK(pp_ui_begin(ui, &error));
  if (forced_density)
    ui->scale = forced_density;
  CHECK(nk_begin(&ui->context, "field", nk_rect(0, 0, 600, 240),
                 NK_WINDOW_NO_SCROLLBAR));
  nk_layout_row_dynamic(&ui->context, field_height, 1);
  bool changed = pp_ui_edit(ui, "item-text", draft, sizeof(draft), true, true);
  nk_end(&ui->context);
  error_drawn = false;
  const struct nk_command *command;
  nk_foreach(command, &ui->context) if (command->type == NK_COMMAND_TEXT) {
    const struct nk_command_text *text =
        (const struct nk_command_text *)command;
    if (text->length >= 6 && !memcmp(text->string, "Cannot", 6))
      error_drawn = true;
  }
  ui->capture_path = capture;
  CHECK(pp_ui_end(ui, &error));
  return changed;
}
static void event(PPPlatformEvent e) {
  e.window_id = pp_ui_backend_window_id(ui);
  pp_ui_backend_event(ui, &e);
}
static void key(PPKey code, unsigned mods) {
  event((PPPlatformEvent){
      .kind = PP_EVENT_KEY_DOWN, .key = code, .modifiers = mods});
  event((PPPlatformEvent){
      .kind = PP_EVENT_KEY_UP, .key = code, .modifiers = mods});
}
static void payload(const char *text) {
  SDL_Event source = {.type = SDL_EVENT_TEXT_INPUT};
  source.text.windowID = pp_ui_backend_window_id(ui);
  source.text.text = text;
  CHECK(SDL_PushEvent(&source));
  for (int pass = 0; pass < 3; pass++) {
    PPPlatformEvent e;
    while (pp_ui_poll(&e)) {
      if (e.kind == PP_EVENT_KEY_DOWN)
        fprintf(stderr, "OS normalized key=%d mods=%u\n", e.key, e.modifiers);
      pp_ui_backend_event(ui, &e);
    }
  }
}
static void click_byte(size_t byte) {
  const PPUITextView *v = pp_ui_shaped_view(ui);
  PPUITextCaret caret = {0};
  for (size_t i = 0; i < v->caret_count; i++)
    if (v->carets[i].byte == byte)
      caret = v->carets[i];
  PPRect bounds;
  CHECK(pp_ui_control_rect(ui, "item-text", &bounds));
  PPVec2 p = {(bounds.x + 6 + caret.point.x) * ui->window_scale,
              (bounds.y + 6 + caret.point.y + caret.height / 2) *
                  ui->window_scale};
  event((PPPlatformEvent){.kind = PP_EVENT_BUTTON_DOWN,
                          .button = PP_BUTTON_LEFT,
                          .window_point = p});
  event((PPPlatformEvent){
      .kind = PP_EVENT_BUTTON_UP, .button = PP_BUTTON_LEFT, .window_point = p});
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == byte);
}
static void fixture(const char *text) {
  pp_ui_shaped_reset(ui);
  strcpy(draft, text);
  draw(NULL);
  pp_ui_focus(ui, "item-text");
  draw(NULL);
}
static void field_pixels(float density) {
  PPUIWindowOptions options = {
      "Hidden shaped editor", {0, 0, 600, 240}, density, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  strcpy(draft, "A\xf0\x9f\x91\x89"
                "B");
  const char *path = density == 1
                         ? PP_EDITOR_OUTPUT "/shaped-editor-field-100.bmp"
                         : PP_EDITOR_OUTPUT "/shaped-editor-field-150.bmp";
  draw(path);
  PPRect bounds;
  CHECK(pp_ui_control_rect(ui, "item-text", &bounds));
  SDL_Surface *bmp = SDL_LoadBMP(path);
  CHECK(bmp);
  unsigned colored = 0;
  for (int y = (int)((bounds.y + 5) * ui->scale);
       y < (int)((bounds.y + bounds.h - 5) * ui->scale); y++)
    for (int x = (int)((bounds.x + 5) * ui->scale);
         x < (int)((bounds.x + bounds.w - 5) * ui->scale); x++) {
      Uint8 r, g, b, a;
      CHECK(SDL_ReadSurfacePixel(bmp, x, y, &r, &g, &b, &a));
      if (r > b + 45 && g > b + 20 && r > 110)
        colored++;
    }
  fprintf(stderr, "field-only density=%.2f colored pixels=%u\n", ui->scale,
          colored);
  SDL_DestroySurface(bmp);
  CHECK(colored > 15);
  const PPUITextView *v = pp_ui_shaped_view(ui);
  CHECK(v && v->caret_count == 4 && v->carets[1].byte == 1 &&
        v->carets[2].byte == 5);
  uint64_t generation = v->generation;
  pp_ui_focus(ui, "item-text");
  key(PP_KEY_END, 0);
  draw(NULL);
  key(PP_KEY_LEFT, PP_MOD_SHIFT);
  draw(NULL);
  CHECK(pp_ui_shaped_view(ui)->generation == generation);
  CHECK(pp_ui_backend_reset(ui, &error));
  CHECK(!ui->shaped);
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void cluster_edits(void) {
  PPUIWindowOptions options = {
      "Hidden editing contracts", {0, 0, 600, 240}, 1, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  const char *clusters[] = {
      "\xf0\x9f\x91\x89", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
      "\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd", "\xf0\x9f\x91\x89\xef\xb8\x8f",
      "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"};
  for (size_t i = 0; i < sizeof(clusters) / sizeof(*clusters); i++) {
    char text[128];
    snprintf(text, sizeof(text), "A%sB", clusters[i]);
    fixture(text);
    const PPUITextView *v = pp_ui_shaped_view(ui);
    fprintf(stderr, "cluster fixture %zu carets=%zu bytes=%zu\n", i,
            v->caret_count, strlen(text));
    CHECK(v->caret_count == 4);
    size_t after = 1 + strlen(clusters[i]);
    CHECK(v->carets[1].byte == 1 && v->carets[2].byte == after);
    click_byte(after);
    key(PP_KEY_LEFT, 0);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == 1);
    key(PP_KEY_RIGHT, PP_MOD_SHIFT);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == after &&
          pp_ui_shaped_state(ui)->anchor == 1);
    uint64_t generation = pp_ui_shaped_view(ui)->generation;
    key(PP_KEY_C, PP_MOD_CONTROL);
    draw(NULL);
    char *clipboard = SDL_GetClipboardText();
    CHECK(clipboard && !strcmp(clipboard, clusters[i]));
    SDL_free(clipboard);
    CHECK(pp_ui_shaped_view(ui)->generation == generation);
    payload("XY");
    CHECK(draw(NULL));
    CHECK(!strcmp(draft, "AXYB"));
    key(PP_KEY_Z, PP_MOD_CONTROL);
    CHECK(draw(NULL));
    CHECK(!strcmp(draft, text));
    key(PP_KEY_Z, PP_MOD_CONTROL | PP_MOD_SHIFT);
    CHECK(draw(NULL));
    CHECK(!strcmp(draft, "AXYB"));
    key(PP_KEY_Z, PP_MOD_CONTROL);
    draw(NULL);
    key(PP_KEY_BACKSPACE, 0);
    draw(NULL);
    CHECK(!strcmp(draft, "AB"));
    key(PP_KEY_Z, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(!strcmp(draft, text));
    click_byte(1);
    key(PP_KEY_DELETE, 0);
    draw(NULL);
    CHECK(!strcmp(draft, "AB"));
    key(PP_KEY_Z, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(!strcmp(draft, text));
    click_byte(after);
    key(PP_KEY_BACKSPACE, 0);
    draw(NULL);
    CHECK(!strcmp(draft, "AB"));
    key(PP_KEY_V, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(!strcmp(draft, text));
  }
  fixture("A\xcc\x81"
          "B");
  CHECK(pp_ui_shaped_view(ui)->caret_count == 3);
  click_byte(3);
  key(PP_KEY_BACKSPACE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "B"));
  const char *blank[] = {"", " ", "  ", "\n", "A\n\n", "A  ", "A\r\nB"};
  const size_t rows[] = {1, 1, 1, 2, 3, 1, 2};
  for (size_t i = 0; i < 7; i++) {
    fixture(blank[i]);
    const PPUITextView *v = pp_ui_shaped_view(ui);
    CHECK(!strcmp(draft, blank[i]) && v->row_count == rows[i]);
    CHECK(v->carets[v->caret_count - 1].byte == strlen(draft));
    CHECK(v->line_height >= 16);
    if (i == 1 || i == 2)
      CHECK(v->extent.x > 0);
  }
  fixture("ABCDEFGHIJ\nAB\nABCDEFGHIJ");
  click_byte(8);
  key(PP_KEY_DOWN, 0);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == 13);
  key(PP_KEY_DOWN, 0);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == 22);
  fixture("a\na\na\na\na\na\na\na\na\na");
  key(PP_KEY_END, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->scroll.y > 0);
  key(PP_KEY_HOME, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->scroll.y == 0);
  fixture("AB");
  click_byte(1);
  event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION,
                          .composing = true,
                          .text_utf8 =
                              "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
                              "xy",
                          .composition_start = 3,
                          .composition_length = 1});
  draw(NULL);
  CHECK(!strcmp(draft, "AB") && pp_ui_shaped_state(ui)->composing);
  CHECK(pp_ui_shaped_state(ui)->preedit_start == 3 &&
        pp_ui_shaped_state(ui)->preedit_length == 1);
  CHECK(pp_ui_shaped_view(ui)->carets[2].byte == 12);
  key(PP_KEY_ENTER, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB") && !ui->activate);
  event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .text_utf8 = ""});
  draw(NULL);
  CHECK(!strcmp(draft, "AB") && !pp_ui_shaped_state(ui)->composing);
  event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION,
                          .composing = true,
                          .text_utf8 = "marked",
                          .composition_start = -1,
                          .composition_length = -1});
  draw(NULL);
  payload("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
          "xy");
  draw(NULL);
  CHECK(!strcmp(draft, "A\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
                       "xyB"));
  CHECK(pp_ui_shaped_state(ui)->undo_count == 1 &&
        !pp_ui_shaped_state(ui)->composing && !ui->composing);
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  event((PPPlatformEvent){
      .kind = PP_EVENT_COMPOSITION, .composing = true, .text_utf8 = "cancel"});
  draw(NULL);
  event((PPPlatformEvent){.kind = PP_EVENT_FOCUS_LOST});
  draw(NULL);
  CHECK(!pp_ui_shaped_state(ui)->composing && !strcmp(draft, "AB"));
  payload("kept");
  event((PPPlatformEvent){.kind = PP_EVENT_FOCUS_LOST});
  draw(NULL);
  CHECK(!strcmp(draft, "AkeptB"));
  fixture("AB");
  char oversized[1300];
  memset(oversized, 'W', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  key(PP_KEY_A, PP_MOD_CONTROL);
  payload(oversized);
  draw(NULL);
  CHECK(!strcmp(draft, oversized));
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  /* A queued borrowed payload is owned before the next SDL poll/reset. */
  payload("queued");
  CHECK(pp_ui_backend_reset(ui, &error));
  CHECK(!ui->shaped);
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void joined_cluster_edits(void) {
  PPUIWindowOptions options = {
      "Hidden joined clusters", {0, 0, 600, 240}, 1, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  const char *separate = "A\xf0\x9f\x91\xa9\xf0\x9f\x92\xbb" "B";
  const char *joined = "A\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb" "B";
  fixture(separate);
  click_byte(5);
  payload("\xe2\x80\x8d");
  draw(NULL);
  CHECK(!strcmp(draft, joined));
  fprintf(stderr, "join insertion raw caret=%zu expected=12\n",
          pp_ui_shaped_state(ui)->caret);
  key(PP_KEY_BACKSPACE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, joined) && pp_ui_shaped_state(ui)->caret == 12);
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, separate) && pp_ui_shaped_state(ui)->caret == 5);
  key(PP_KEY_Z, PP_MOD_CONTROL | PP_MOD_SHIFT);
  draw(NULL);
  CHECK(!strcmp(draft, joined) && pp_ui_shaped_state(ui)->caret == 12);
  /* Undo/redo restores the selection used by the following deletion. */
  key(PP_KEY_RIGHT, 0);
  draw(NULL);
  key(PP_KEY_LEFT, PP_MOD_SHIFT);
  draw(NULL);
  fprintf(stderr, "joined shift-left caret=%zu anchor=%zu\n", pp_ui_shaped_state(ui)->caret, pp_ui_shaped_state(ui)->anchor);
  CHECK(pp_ui_shaped_state(ui)->caret == 1 &&
        pp_ui_shaped_state(ui)->anchor == 12);
  key(PP_KEY_RIGHT, 0);
  key(PP_KEY_LEFT, 0);
  key(PP_KEY_RIGHT, PP_MOD_SHIFT);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == 12 &&
        pp_ui_shaped_state(ui)->anchor == 1);
  key(PP_KEY_LEFT, 0);
  key(PP_KEY_DELETE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  /* Multiple queued edits must reshape and reconcile between payloads. */
  fixture(separate);
  click_byte(5);
  payload("\xe2\x80\x8d");
  payload("X");
  key(PP_KEY_BACKSPACE, 0);
  key(PP_KEY_BACKSPACE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  CHECK(pp_ui_shaped_state(ui)->undo_count == 4);
  /* Removing the separator makes the two regional indicators one flag. */
  fixture("A\xf0\x9f\x87\xba" "x\xf0\x9f\x87\xb8" "B");
  click_byte(6);
  key(PP_KEY_BACKSPACE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "A\xf0\x9f\x87\xba\xf0\x9f\x87\xb8" "B"));
  CHECK(pp_ui_shaped_state(ui)->caret == 9);
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, "A\xf0\x9f\x87\xba" "x\xf0\x9f\x87\xb8" "B"));
  key(PP_KEY_Z, PP_MOD_CONTROL | PP_MOD_SHIFT);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == 9);
  key(PP_KEY_BACKSPACE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  key(PP_KEY_Z, PP_MOD_CONTROL);
  draw(NULL);
  CHECK(!strcmp(draft, "A\xf0\x9f\x87\xba\xf0\x9f\x87\xb8" "B"));
  key(PP_KEY_RIGHT, 0);
  key(PP_KEY_LEFT, PP_MOD_SHIFT);
  draw(NULL);
  CHECK(pp_ui_shaped_state(ui)->caret == 1 && pp_ui_shaped_state(ui)->anchor == 9);
  key(PP_KEY_LEFT, 0);
  key(PP_KEY_DELETE, 0);
  draw(NULL);
  CHECK(!strcmp(draft, "AB"));
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void page_navigation(void) {
  PPUIWindowOptions options = {
      "Hidden pages", {0, 0, 600, 240}, 1, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  const float heights[] = {30, 78, 190};
  for (size_t i = 0; i < 3; i++) {
    field_height = heights[i];
    fixture("ABCDE\nA\nABCDE\nABCDE\nABCDE\nABCDE\nABCDE\nABCDE\nABCDE\nABCDE");
    click_byte(4);
    const PPUITextView *v = pp_ui_shaped_view(ui);
    size_t rows = (size_t)fmax(1, floor((field_height - 12) / v->line_height));
    CHECK(i == 0 ? rows == 1 : rows > 1);
    size_t expected = v->rows[rows].begin + (rows == 1 ? 1 : 4);
    key(PP_KEY_PAGE_DOWN, PP_MOD_SHIFT);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == expected &&
          pp_ui_shaped_state(ui)->anchor == 4);
    key(PP_KEY_PAGE_UP, PP_MOD_SHIFT);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == 4 &&
          pp_ui_shaped_state(ui)->anchor == 4);
    key(PP_KEY_PAGE_UP, 0);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == 4);
    for (int j = 0; j < 10; j++) key(PP_KEY_PAGE_DOWN, 0);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == v->rows[9].begin + 4);
    key(PP_KEY_UP, 0);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == v->rows[8].begin + 4);
  }
  field_height = 78;
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void overhang_placement(void) {
  PPBlob font = {0};
#ifdef __APPLE__
  CHECK(pp_platform_find_font("Helvetica", &font, &error));
#else
  CHECK(pp_platform_find_font("Segoe UI", &font, &error));
#endif
  FT_Library library;
  FT_Face face;
  CHECK(!FT_Init_FreeType(&library));
  CHECK(!FT_New_Memory_Face(library, font.data, (FT_Long)font.size, 0, &face));
  CHECK(!FT_Set_Char_Size(face, 0, 24 * 64, 72, 72));
  CHECK(!FT_Load_Char(face, 'j', FT_LOAD_RENDER));
  CHECK(face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);
  int native_left = (int)face->glyph->bitmap.width;
  int native_right = -1;
  for (unsigned y = 0; y < face->glyph->bitmap.rows; y++)
    for (unsigned x = 0; x < face->glyph->bitmap.width; x++)
      if (face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x] > 64) {
        if ((int)x < native_left) native_left = (int)x;
        if ((int)x > native_right) native_right = (int)x;
      }
  native_left += face->glyph->bitmap_left;
  native_right += face->glyph->bitmap_left;
  CHECK(native_left < 0); /* This fixture must actually overhang its pen. */
  PPBlob emoji = {0};
  emoji.data = pp_resource_load("fonts/NotoColorEmoji.ttf", &emoji.size);
  PPFontInfo info;
  CHECK(emoji.data && pp_font_inspect((PPBytes){emoji.data, emoji.size}, 0, &info, &error));
  CHECK(info.bitmap_em_size > 0);
  CHECK(!FT_Set_Char_Size(face, 0, (FT_F26Dot6)(info.bitmap_em_size * 64), 72, 72));
  CHECK(!FT_Load_Char(face, 'j', FT_LOAD_DEFAULT));
  /* SDL_ttf offsets an initial negative shaping bearing to fit its line box.
   * Obtain that padding independently from the font, not the raster origin. */
  double leading = fmax(0, -floor(face->glyph->metrics.horiBearingX / 64.0));
  int baseline = (int)round(leading * 24 / info.bitmap_em_size);
  native_left += baseline;
  native_right += baseline;
  CHECK(native_left < 0); /* Native hinting still extends past the layout box. */
  pp_blob_destroy(&emoji);
  PPUIWindowOptions options = {
      "Hidden overhang placement", {0, 0, 600, 240}, 1.5f, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  forced_density = 1.5f;
  strcpy(draft, "j");
  const char *path = PP_EDITOR_OUTPUT "/shaped-editor-overhang.bmp";
  draw(path);
  const PPUITextView *v = pp_ui_shaped_view(ui);
  CHECK(v->caret_count == 2 && v->carets[0].byte == 0 && v->carets[1].byte == 1);
  CHECK(v->carets[0].point.x == 0 && v->carets[1].point.x > 0);
  CHECK(v->carets[1].point.x * v->density > native_right);
  const PPUITextRow *row = &v->rows[0];
  int raster_left = row->frame.width, raster_right = -1;
  for (int y = 0; y < row->frame.height; y++)
    for (int x = 0; x < row->frame.width; x++)
      if (row->frame.content_alpha[(size_t)y * row->frame.width + x] > 64) {
        if (x < raster_left) raster_left = x;
        if (x > raster_right) raster_right = x;
      }
  fprintf(stderr, "j independent FT bounds=[%d,%d] raster=[%d,%d] origin=%.1f caret=%.3f..%.3f\n",
          native_left, native_right, raster_left, raster_right, row->origin_px.x,
          v->carets[0].point.x, v->carets[1].point.x);
  CHECK(raster_left - row->origin_px.x == native_left);
  CHECK(raster_right - row->origin_px.x == native_right);
  PPRect bounds;
  CHECK(pp_ui_control_rect(ui, "item-text", &bounds));
  SDL_Surface *bmp = SDL_LoadBMP(path);
  CHECK(bmp);
  int field_left = bmp->w, field_right = -1;
  for (int y = (int)((bounds.y + 4) * ui->scale);
       y < (int)((bounds.y + 6 + v->line_height) * ui->scale); y++)
    for (int x = (int)((bounds.x + 3) * ui->scale);
         x < (int)((bounds.x + 24) * ui->scale); x++) {
      Uint8 r, g, b, a;
      CHECK(SDL_ReadSurfacePixel(bmp, x, y, &r, &g, &b, &a));
      if (r < 200 && g < 210 && b < 220) {
        if (x < field_left) field_left = x;
        if (x > field_right) field_right = x;
      }
    }
  double pen = (bounds.x + 6) * ui->scale;
  fprintf(stderr, "j field bounds=[%d,%d] expected=[%.1f,%.1f]\n", field_left,
          field_right, pen + native_left, pen + native_right);
  CHECK(fabs(field_left - (pen + native_left)) <= 1);
  CHECK(fabs(field_right - (pen + native_right)) <= 1);
  SDL_DestroySurface(bmp);
  FT_Done_Face(face);
  FT_Done_FreeType(library);
  pp_blob_destroy(&font);
  forced_density = 0;
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void layout_contracts(void) {
  PPAssets *assets = NULL;
  CHECK(pp_assets_create(&assets, &error));
  PPUITextLayout *layout = NULL;
  const uint8_t ink[] = {32, 53, 71, 255};
  CHECK(pp_ui_text_layout_create(&layout, assets, 16, 1.5, ink, &error));
  CHECK(pp_ui_text_layout_set(layout, "jA  \n\n", &error));
  const PPUITextView *view = pp_ui_text_layout_view(layout);
  CHECK(view->row_count == 3 && view->carets[view->caret_count - 1].byte == 6);
  PPUITextCaret copied = view->carets[1];
  uint64_t generation = view->generation;
  size_t bytes = pp_assets_external_raster_bytes(assets);
  CHECK(bytes > 0);
  for (size_t row = 0; row < view->row_count; row++) {
    CHECK(view->rows[row].origin_px.x >= 0 && view->rows[row].origin_px.y >= 0);
  }
  CHECK(pp_ui_text_layout_set(layout, "jA  \n\n", &error));
  CHECK(pp_ui_text_layout_view(layout)->generation == generation);
  CHECK(pp_assets_external_raster_bytes(assets) == bytes);
  const size_t resource = 256u * 1024 * 1024;
  CHECK(pp_assets_reserve_raster(assets, resource, &error));
  CHECK(pp_assets_reserve_raster(assets, resource - bytes, &error));
  CHECK(!pp_ui_text_layout_set(layout, "budget blocked", &error));
  CHECK(pp_ui_text_layout_view(layout)->generation == generation);
  CHECK(pp_ui_text_layout_view(layout)->carets[1].byte == copied.byte);
  CHECK(pp_assets_external_raster_bytes(assets) == resource * 2);
  pp_assets_release_raster(assets, resource);
  pp_assets_release_raster(assets, resource - bytes);
  CHECK(pp_ui_text_layout_set(layout,
                              "A\xf0\x9f\x91\x89"
                              "B",
                              &error));
  CHECK(pp_ui_text_layout_view(layout)->generation == generation + 1);
  CHECK(copied.byte == 1 && copied.point.x > 0);
  CHECK(!pp_ui_text_layout_set(layout, "\xc0\xaf", &error));
  pp_ui_text_layout_destroy(layout);
  CHECK(pp_assets_external_raster_bytes(assets) == 0);
  pp_assets_destroy(assets);
}
static int native_observe(void) {
  PPUIWindowOptions options = {"PointyPointer emoji field observation",
                               {120, 120, 720, 300},
                               1,
                               true,
                               true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  strcpy(draft, "A\xf0\x9f\x91\x89"
                "B\nA\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
                "B");
  draw(NULL);
  pp_ui_focus(ui, "item-text");
  pp_ui_show(ui, true);
  SDL_RaiseWindow(ui->window);
  key(PP_KEY_END, PP_MOD_CONTROL);
  draw(NULL);
  puts("READY native field, 20-second bounded observation; no app, surfaces or "
       "hooks. Windows emoji picker / installed IME may input here.");
  fflush(stdout);
  Uint64 deadline = SDL_GetTicks() + 20000;
  unsigned commits = 0, preedits = 0;
  while (SDL_GetTicks() < deadline) {
    PPPlatformEvent e;
    while (pp_ui_poll(&e)) {
      if (e.kind == PP_EVENT_QUIT || e.kind == PP_EVENT_WINDOW_CLOSE)
        deadline = 0;
      if (e.kind == PP_EVENT_TEXT && e.text_utf8) {
        commits++;
        fprintf(stderr, "Native SDL commit bytes=%zu: %s\n",
                strlen(e.text_utf8), e.text_utf8);
      }
      if (e.kind == PP_EVENT_COMPOSITION) {
        preedits++;
        fprintf(stderr, "Native SDL preedit start=%d length=%d text=%s\n",
                e.composition_start, e.composition_length,
                e.text_utf8 ? e.text_utf8 : "");
      }
      pp_ui_backend_event(ui, &e);
    }
    draw(NULL);
    SDL_Delay(10);
  }
  draw(PP_EDITOR_OUTPUT "/shaped-editor-native.bmp");
  printf("Native observation complete: SDL commits=%u preedits=%u draft=%s\n",
         commits, preedits, draft);
  pp_ui_backend_destroy(ui);
  ui = NULL;
  return 0;
}
static void failure_case(const char *which) {
  PPUIWindowOptions options = {
      "Hidden failures", {0, 0, 600, 240}, 1, false, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  PPAssets *assets = NULL;
  CHECK(pp_assets_create(&assets, &error));
  ui->text_assets = assets;
  const size_t block = 256u * 1024 * 1024;
  if (!strcmp(which, "initial")) {
    memset(draft, 'W', 1300);
    draft[1300] = 0;
    draw(NULL);
    CHECK(error_drawn);
    pp_ui_focus(ui, "item-text");
    key(PP_KEY_A, PP_MOD_CONTROL);
    payload("recovered");
    CHECK(draw(NULL));
    CHECK(!strcmp(draft, "recovered"));
    CHECK(pp_ui_shaped_view(ui));
    pp_ui_shaped_reset(ui);
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    strcpy(draft, "budget denied");
    draw(NULL);
    CHECK(error_drawn);
    pp_ui_focus(ui, "item-text");
    key(PP_KEY_A, PP_MOD_CONTROL);
    payload("retained");
    CHECK(draw(NULL));
    CHECK(!strcmp(draft, "retained"));
    pp_assets_release_raster(assets, block);
    pp_assets_release_raster(assets, block);
    draw(NULL);
    CHECK(pp_ui_shaped_view(ui) && !error_drawn);
  } else if (!strcmp(which, "display")) {
    fixture("AB");
    click_byte(1);
    const PPUITextView *old = pp_ui_shaped_view(ui);
    uint64_t generation = old->generation;
    size_t held = pp_assets_external_raster_bytes(assets);
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    CHECK(pp_assets_reserve_raster(assets, block - held, &error));
    payload("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
    draw(NULL);
    CHECK(error_drawn && pp_ui_shaped_view(ui)->generation == generation);
    CHECK(pp_ui_shaped_state(ui)->caret == 12);
    unsigned attempts = layout_sets;
    draw(NULL);
    key(PP_KEY_LEFT, 0);
    key(PP_KEY_BACKSPACE, 0);
    draw(NULL);
    fprintf(stderr, "failed display attempts: saved=%u idle=%u\n", attempts, layout_sets);
    CHECK(layout_sets == attempts && error_drawn);
    CHECK(!strcmp(draft, "A\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb" "B"));
    CHECK(pp_ui_shaped_state(ui)->caret == 12);
    pp_assets_release_raster(assets, block);
    pp_assets_release_raster(assets, block - held);
    draw(NULL);
    CHECK(!error_drawn && pp_ui_shaped_view(ui)->generation == generation + 1);
    CHECK(layout_sets == attempts + 1);
    key(PP_KEY_BACKSPACE, 0);
    draw(NULL);
    CHECK(!strcmp(draft, "AB"));
    /* A failed join keeps its raw UTF-8 position until matching geometry exists.
     * Never clamp it with the retained, unrelated two-component layout. */
    fixture("A\xf0\x9f\x91\xa9\xf0\x9f\x92\xbb" "B");
    click_byte(5);
    held = pp_assets_external_raster_bytes(assets);
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    CHECK(pp_assets_reserve_raster(assets, block - held, &error));
    payload("\xe2\x80\x8d");
    draw(NULL);
    CHECK(error_drawn && pp_ui_shaped_state(ui)->caret == 8);
    key(PP_KEY_LEFT, PP_MOD_SHIFT);
    key(PP_KEY_DELETE, 0);
    draw(NULL);
    CHECK(pp_ui_shaped_state(ui)->caret == 8 && pp_ui_shaped_state(ui)->anchor == 8);
    CHECK(!strcmp(draft, "A\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb" "B"));
    pp_assets_release_raster(assets, block);
    pp_assets_release_raster(assets, block - held);
    draw(NULL);
    CHECK(!error_drawn && pp_ui_shaped_state(ui)->caret == 12);
    key(PP_KEY_BACKSPACE, 0);
    draw(NULL);
    CHECK(!strcmp(draft, "AB"));
    /* Leave space for a short preceding row but reject the following row.
     * Failed candidates must release their partial reservations. */
    fixture("AB");
    click_byte(1);
    held = pp_assets_external_raster_bytes(assets);
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    CHECK(pp_assets_reserve_raster(assets, block - 2 * held, &error));
    size_t occupied = pp_assets_raster_bytes(assets);
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .composing = true,
                            .text_utf8 = "a\nWWWWWWWWWWWWWWWWWWWW"});
    draw(NULL);
    CHECK(error_drawn && !strcmp(draft, "AB"));
    CHECK(pp_assets_raster_bytes(assets) == occupied);
    attempts = layout_sets;
    draw(NULL);
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .composing = true,
                            .text_utf8 = "a\nWWWWWWWWWWWWWWWWWWWW", .composition_start = 1});
    draw(NULL);
    CHECK(layout_sets == attempts && error_drawn);
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .composing = true,
                            .text_utf8 = "b\nWWWWWWWWWWWWWWWWWWWW"});
    draw(NULL);
    CHECK(layout_sets == attempts + 1 && error_drawn);
    CHECK(pp_assets_raster_bytes(assets) == occupied);
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .text_utf8 = ""});
    draw(NULL);
    CHECK(!error_drawn && !pp_ui_shaped_state(ui)->composing && !strcmp(draft, "AB"));
    CHECK(pp_ui_shaped_state(ui)->caret == 1 && pp_ui_shaped_state(ui)->anchor == 1);
    pp_assets_release_raster(assets, block);
    pp_assets_release_raster(assets, block - 2 * held);
    char oversized[1301];
    memset(oversized, 'W', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    key(PP_KEY_A, PP_MOD_CONTROL);
    payload(oversized);
    draw(NULL);
    CHECK(error_drawn && !strcmp(draft, oversized));
    attempts = layout_sets;
    draw(NULL);
    key(PP_KEY_A, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(error_drawn && layout_sets == attempts);
    payload("recovered");
    draw(NULL);
    CHECK(!error_drawn && !strcmp(draft, "recovered"));
    key(PP_KEY_Z, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(error_drawn && !strcmp(draft, oversized));
    attempts = layout_sets;
    draw(NULL);
    CHECK(error_drawn && layout_sets == attempts);
    key(PP_KEY_Z, PP_MOD_CONTROL);
    draw(NULL);
    CHECK(!error_drawn && !strcmp(draft, "AB"));
    /* A new immutable layout configuration invalidates the failed-display
     * signature even when the full preedit bytes are unchanged. */
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .composing = true,
                            .text_utf8 = oversized});
    draw(NULL);
    CHECK(error_drawn && !strcmp(draft, "AB"));
    attempts = layout_sets;
    unsigned creates = layout_creates;
    forced_density = 1.5f;
    draw(NULL);
    CHECK(error_drawn && pp_ui_shaped_view(ui)->density == 1.5);
    CHECK(layout_creates == creates + 1 && layout_sets == attempts + 2);
    attempts = layout_sets;
    draw(NULL);
    CHECK(error_drawn && layout_sets == attempts);
    event((PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .text_utf8 = ""});
    draw(NULL);
    CHECK(!error_drawn && !strcmp(draft, "AB"));
    forced_density = 0;
    CHECK(pp_ui_shaped_reset(ui));
    CHECK(pp_assets_external_raster_bytes(assets) == 0);
  } else if (!strcmp(which, "context")) {
    strcpy(draft, "owned");
    draw(NULL);
    PPUIBackend *other = NULL;
    CHECK(pp_ui_backend_create(&other, &options, &error));
    CHECK(SDL_GL_MakeCurrent(other->window, other->gl_context));
    CHECK(glIsTexture(other->font_texture));
    expected_context = ui->gl_context;
    deny_context = true;
    pp_ui_focus(ui, "item-text");
    uint64_t saved_focus = ui->focus_id;
    CHECK(!pp_ui_backend_reset(ui, &error));
    CHECK(ui->focus_id == saved_focus && strstr(error.message, "context"));
    CHECK(foreign_deletes == 0);
    CHECK(ui->shaped && glIsTexture(other->font_texture));
    deny_context = false;
    pp_ui_shaped_reset(ui);
    CHECK(!ui->shaped);
    expected_context = NULL;
    CHECK(SDL_GL_MakeCurrent(other->window, other->gl_context));
    CHECK(glIsTexture(other->font_texture));
    draw(NULL);
    CHECK(pp_assets_external_raster_bytes(assets) > 0);
    CHECK(SDL_GL_MakeCurrent(other->window, other->gl_context));
    expected_context = ui->gl_context;
    deny_context = true;
    pp_ui_shaped_destroy(ui);
    CHECK(!ui->shaped && foreign_deletes == 0);
    CHECK(pp_assets_external_raster_bytes(assets) == 0 &&
          glIsTexture(other->font_texture));
    expected_context = NULL;
    deny_context = false;
    pp_ui_backend_destroy(other);
  } else {
    strcpy(draft, "density retained");
    draw(NULL);
    pp_ui_focus(ui, "item-text");
    key(PP_KEY_END, 0);
    draw(NULL);
    size_t caret = pp_ui_shaped_state(ui)->caret;
    const PPUITextView *old = pp_ui_shaped_view(ui);
    size_t held = pp_assets_external_raster_bytes(assets);
    CHECK(pp_assets_reserve_raster(assets, block, &error));
    CHECK(pp_assets_reserve_raster(assets, block - held, &error));
    forced_density = 1.5f;
    draw(NULL);
    CHECK(pp_ui_shaped_view(ui) == old && error_drawn);
    unsigned attempts = layout_creates;
    draw(NULL);
    key(PP_KEY_LEFT, 0);
    draw(NULL);
    draw(NULL);
    CHECK(layout_creates == attempts && error_drawn &&
          pp_ui_shaped_state(ui)->caret < caret);
    pp_assets_release_raster(assets, block);
    pp_assets_release_raster(assets, block - held);
    draw(NULL);
    CHECK(pp_ui_shaped_view(ui)->density == 1.5 && !error_drawn);
    CHECK(layout_creates == attempts + 1);
    forced_density = 0;
  }
  pp_ui_backend_destroy(ui);
  ui = NULL;
  CHECK(pp_assets_external_raster_bytes(assets) == 0);
  pp_assets_destroy(assets);
  printf("PASS failure recovery: %s\n", which);
}
#ifdef _WIN32
static void native_cleanup(void) {
  INPUT release[8] = {0};
  const WORD codes[] = {VK_CONTROL, VK_SHIFT,  VK_HOME, VK_RIGHT,
                        VK_BACK,    VK_DELETE, VK_LWIN, VK_ESCAPE};
  for (int i = 0; i < 8; i++) {
    release[i].type = INPUT_KEYBOARD;
    release[i].ki.wVk = codes[i];
    release[i].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  SendInput(8, release, sizeof(INPUT));
  pp_ui_backend_destroy(ui);
  ui = NULL;
}
static void native_pump(void) {
  Uint64 until = SDL_GetTicks() + 120;
  do {
    PPPlatformEvent e;
    while (pp_ui_poll(&e))
      pp_ui_backend_event(ui, &e);
    draw(NULL);
    SDL_Delay(5);
  } while (SDL_GetTicks() < until);
}
static void native_key(WORD code, bool shift, bool control) {
  INPUT input[6] = {0};
  UINT n = 0;
  if (control) {
    input[n].type = INPUT_KEYBOARD;
    input[n++].ki.wVk = VK_CONTROL;
  }
  if (shift) {
    input[n].type = INPUT_KEYBOARD;
    input[n++].ki.wVk = VK_SHIFT;
  }
  DWORD flags = (code == VK_HOME || code == VK_RIGHT || code == VK_DELETE)
                    ? KEYEVENTF_EXTENDEDKEY
                    : 0;
  input[n].type = INPUT_KEYBOARD;
  input[n].ki.dwFlags = flags;
  input[n].ki.wScan = (WORD)MapVirtualKeyW(code, MAPVK_VK_TO_VSC);
  input[n++].ki.wVk = code;
  input[n].type = INPUT_KEYBOARD;
  input[n].ki.wVk = code;
  input[n].ki.wScan = (WORD)MapVirtualKeyW(code, MAPVK_VK_TO_VSC);
  input[n++].ki.dwFlags = flags | KEYEVENTF_KEYUP;
  if (shift) {
    input[n].type = INPUT_KEYBOARD;
    input[n].ki.wVk = VK_SHIFT;
    input[n++].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  if (control) {
    input[n].type = INPUT_KEYBOARD;
    input[n].ki.wVk = VK_CONTROL;
    input[n++].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  CHECK(SendInput(n, input, sizeof(INPUT)) == n);
  native_pump();
}
static int native_input(void) {
  atexit(native_cleanup);
  PPUIWindowOptions options = {
      "PointyPointer native emoji input", {120, 120, 720, 300}, 1, true, true};
  CHECK(pp_ui_backend_create(&ui, &options, &error));
  draft[0] = 0;
  draw(NULL);
  pp_ui_focus(ui, "item-text");
  pp_ui_show(ui, true);
  SDL_RaiseWindow(ui->window);
  HWND window =
      SDL_GetPointerProperty(SDL_GetWindowProperties(ui->window),
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  CHECK(window && SetForegroundWindow(window));
  native_pump();
  CHECK(GetForegroundWindow() == window);
  /* Actual OS Unicode keyboard input, including UTF-16 surrogate pairs. */
  const WORD utf16[] = {'A', 0xd83d, 0xdc69, 0xd83d, 0xdcbb, 'B'};
  INPUT input[12] = {0};
  for (int i = 0; i < 6; i++) {
    input[2 * i].type = INPUT_KEYBOARD;
    input[2 * i].ki.wScan = utf16[i];
    input[2 * i].ki.dwFlags = KEYEVENTF_UNICODE;
    input[2 * i + 1] = input[2 * i];
    input[2 * i + 1].ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  CHECK(SendInput(12, input, sizeof(INPUT)) == 12);
  native_pump();
  CHECK(!strcmp(draft, "A\xf0\x9f\x91\xa9\xf0\x9f\x92\xbb" "B"));
  native_key(VK_HOME, false, false);
  native_key(VK_RIGHT, false, false);
  native_key(VK_RIGHT, false, false);
  CHECK(pp_ui_shaped_state(ui)->caret == 5);
  INPUT joiner[2] = {0};
  joiner[0].type = INPUT_KEYBOARD;
  joiner[0].ki.wScan = 0x200d;
  joiner[0].ki.dwFlags = KEYEVENTF_UNICODE;
  joiner[1] = joiner[0];
  joiner[1].ki.dwFlags |= KEYEVENTF_KEYUP;
  CHECK(SendInput(2, joiner, sizeof(INPUT)) == 2);
  native_pump();
  const char *expected = "A\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
                         "B";
  CHECK(!strcmp(draft, expected));
  CHECK(pp_ui_shaped_view(ui)->caret_count == 4);
  CHECK(pp_ui_shaped_state(ui)->caret == 12);
  native_key(VK_BACK, false, false);
  CHECK(!strcmp(draft, "AB"));
  native_key('Z', false, true);
  CHECK(!strcmp(draft, expected));
  native_key(VK_HOME, false, false);
  native_key(VK_RIGHT, false, false);
  CHECK(pp_ui_shaped_state(ui)->caret == 1);
  native_key(VK_RIGHT, true, false);
  CHECK(pp_ui_shaped_state(ui)->caret == 12 &&
        pp_ui_shaped_state(ui)->anchor == 1);
  native_key(VK_BACK, false, false);
  CHECK(!strcmp(draft, "AB"));
  native_key('Z', false, true);
  CHECK(!strcmp(draft, expected));
  draw(PP_EDITOR_OUTPUT "/shaped-editor-native-input.bmp");
  puts("PASS OS SendInput UTF16->SDL->actual field: inserted joiner, A/B survive Backspace/undo, ZWJ exact bytes, 4 cluster "
       "stops, physical arrows/selection/delete/undo. No picker selection or "
       "native IME claimed.");
  return 0;
}
#endif
int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--joined")) {
    joined_cluster_edits();
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--pages")) {
    page_navigation();
    return 0;
  }
  if (argc == 3 && !strcmp(argv[1], "--failure")) {
    failure_case(argv[2]);
    return 0;
  }
#ifdef _WIN32
  if (argc == 2 && !strcmp(argv[1], "--native-input"))
    return native_input();
#endif
  if (argc == 2 && !strcmp(argv[1], "--native-observe"))
    return native_observe();
  field_pixels(1);
  field_pixels(1.5f);
  cluster_edits();
  joined_cluster_edits();
  page_navigation();
  overhang_placement();
  layout_contracts();
  failure_case("initial");
  failure_case("context");
  failure_case("density");
  failure_case("display");
  puts("shaped_editor: field pixels, clusters, clipboard, atomic undo/redo, "
       "blank rows, navigation, preedit and reset passed");
  SDL_free(clipboard_text);
  return 0;
}
