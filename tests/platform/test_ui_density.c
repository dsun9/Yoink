/* Only SDL's driver density query is controlled. Settings, Nuklear, font
 * baking, GL textures and preview rendering are the real production code.
 * Every window remains hidden; no OS input is injected. */
#include "../../src/ui/settings_internal.h"
#include "../../src/ui/shaped_edit.h"
#include "../test.h"
#include <string.h>
static void cleanup(void);
#undef CHECK
#define CHECK(expr) do {if(!(expr)){fprintf(stderr,"%s:%d: CHECK(%s) failed\n",__FILE__,__LINE__,#expr);cleanup();exit(1);}}while(0)

static float density = 1;
float SDLCALL pp_test_window_density(SDL_Window *window) {
  CHECK(window != NULL);
  return density;
}
static PPApp *app;
static PPSettings *settings;
static void cleanup(void) {
  PPSettings *owned_settings=settings;settings=NULL;
  PPApp *owned_app=app;app=NULL;
  pp_settings_destroy(owned_settings);pp_app_destroy(owned_app);
}
static void draw(void) {
  pp_settings_draw(settings);
  CHECK(!settings->failed);
  CHECK(!(SDL_GetWindowFlags(settings->backend->window) &
          SDL_WINDOW_INPUT_FOCUS));
  CHECK(SDL_GetWindowFlags(settings->backend->window) & SDL_WINDOW_HIDDEN);
}
static void key(PPKey value, unsigned modifiers) {
  PPPlatformEvent e = {.kind = PP_EVENT_KEY_DOWN,
                       .window_id = pp_settings_window_id(settings),
                       .key = value,
                       .modifiers = modifiers};
  pp_settings_event(settings, &e);
  e.kind = PP_EVENT_KEY_UP;
  pp_settings_event(settings, &e);
}
int main(void) {
  PPError error = {0};
  atexit(cleanup);
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_SELECT_ANCHOR,
                                    .anchor = PP_ANCHOR_BOTTOM_RIGHT},
                       &error));
  CHECK(pp_settings_create_scaled(&settings, app, 1, &error));
  settings->render_hidden = true;
  PPUIBackend *ui = settings->backend;
  SDL_Window *window = ui->window;
  CHECK(SDL_SetWindowSize(window, 1040, 1800));
  draw();
  CHECK(pp_renderer_metrics(settings->editor.renderer).readbacks==0);
  pp_ui_focus(ui, "item-text");
  key(PP_KEY_END, 0);
  pp_settings_event(
      settings, &(PPPlatformEvent){.kind = PP_EVENT_TEXT,
                                   .window_id = pp_settings_window_id(settings),
                                   .codepoint = 'X'});
  draw();
  CHECK(pp_ui_shaped_state(ui)->undo_count == 1);
  size_t edit_caret = pp_ui_shaped_state(ui)->caret;
  pp_ui_focus(ui, "item-size");
  draw();
  key(PP_KEY_A, PP_MOD_CONTROL);
  draw();
  pp_settings_event(
      settings, &(PPPlatformEvent){.kind = PP_EVENT_TEXT,
                                   .window_id = pp_settings_window_id(settings),
                                   .codepoint = '0'});
  draw();
  CHECK(!strcmp(settings->editor.size, "0") &&
        settings->editor.appearance_error[0]);
  CHECK(pp_ui_focused(ui, "item-size"));
  uint64_t selected = pp_app_ui(app)->selected_item, focus = ui->focus_id;
  PPItemSnapshot *preview = settings->editor.preview;
  char *draft_text = settings->editor.text;
  int x, y, width, height;
  CHECK(SDL_GetWindowPosition(window, &x, &y));
  CHECK(SDL_GetWindowSize(window, &width, &height));
  int initial_preview_width = settings->editor.preview_width;
  CHECK(initial_preview_width > 0 && ui->body_font->info.height == 16);
  const float densities[] = {2, 1};
  const float font_pixels[] = {32, 16};
  const int preview_factors[] = {2, 1};
  for (size_t i = 0; i < 2; i++) {
    GLuint old_texture = ui->font_texture;
    density = densities[i];
    pp_settings_event(
        settings,
        &(PPPlatformEvent){.kind = PP_EVENT_WINDOW_RESIZE,
                           .window_id = pp_settings_window_id(settings)});
    draw();
    /* A deleted atlas name may already be reused by a field image. Verify
     * that it no longer holds an atlas: any reused dimensions must match a
     * current field row, whose pixels are covered in shaped_editor. */
    if (glIsTexture(old_texture)) {
      GLint reused_width = 0, reused_height = 0;
      glBindTexture(GL_TEXTURE_2D, old_texture);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
                               &reused_width);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT,
                               &reused_height);
      const PPUITextView *view = pp_ui_shaped_view(ui);
      bool row_texture = false;
      for (size_t row = 0; row < view->row_count; row++)
        if (view->rows[row].frame.width == reused_width &&
            view->rows[row].frame.height == reused_height)
          row_texture = true;
      const PPUIContentPreview *retained=&settings->editor.retained;
      bool preview_texture=(reused_width==retained->width&&reused_height==retained->height)||
        (reused_width==retained->source_width&&reused_height==retained->source_height);
      CHECK(row_texture||preview_texture);
    }
    GLint atlas_width = 0, atlas_height = 0;
    glBindTexture(GL_TEXTURE_2D, ui->font_texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &atlas_width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT,
                             &atlas_height);
    /* Nuklear releases bake dimensions in atlas_end; GL owns the image. */
    CHECK(atlas_width > 0 && atlas_height > 0 && glGetError() == GL_NO_ERROR);
    CHECK(ui->scale == densities[i] && ui->window_scale == 1);
    CHECK(pp_ui_shaped_view(ui)->density == densities[i]);
    CHECK(pp_ui_shaped_state(ui)->caret == edit_caret &&
          pp_ui_shaped_state(ui)->undo_count == 1);
    CHECK(ui->body_font->info.height == font_pixels[i]);
    CHECK(ui->body_font->handle.height == 16);
    CHECK(ui->context.style.font == &ui->body_font->handle);
    /* The field may allocate an image after the atlas swap and legitimately
     * reuse the deleted GL name; only the new atlas identity is stable. */
    CHECK(ui->font_texture != old_texture);
    CHECK(glIsTexture(ui->font_texture));
    CHECK(settings->editor.preview_width ==
          initial_preview_width * preview_factors[i]);
    CHECK(settings->backend == ui && ui->window == window);
    CHECK(!strcmp(settings->editor.size, "0") &&
          settings->editor.appearance_error[0]);
    CHECK(settings->editor.text == draft_text &&
          settings->editor.preview == preview);
    CHECK(pp_app_ui(app)->selected_item == selected &&
          pp_app_ui(app)->anchor == PP_ANCHOR_BOTTOM_RIGHT);
    CHECK(ui->focus_id == focus && pp_ui_focused(ui, "item-size"));
    int actual_x, actual_y, actual_width, actual_height;
    CHECK(SDL_GetWindowPosition(window, &actual_x, &actual_y));
    CHECK(SDL_GetWindowSize(window, &actual_width, &actual_height));
    CHECK(actual_x == x && actual_y == y && actual_width == width &&
          actual_height == height);
    CHECK(ui->width == 1040 && ui->height == 1800);
    GLuint stable_texture = ui->font_texture;
    draw();
    CHECK(ui->font_texture == stable_texture); /* No needless rebuild. */
  }
  pp_settings_close(settings);
  CHECK(pp_app_ui(app)->geometry.w == 1040 &&
        pp_app_ui(app)->geometry.h == 1800);
  puts("PASS hidden Settings density 1->2->1: real font/preview rasters "
       "refresh, old texture released; drafts, selection, focus, window and "
       "geometry preserved");
  /* Join private source work before CRT exit callbacks acquire the onexit
   * lock; HarfBuzz may register its own finalizer during that work. */
  cleanup();
  return 0;
}
