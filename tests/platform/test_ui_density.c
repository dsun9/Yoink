/* Only SDL's driver density query is controlled. Settings, Nuklear, font
 * baking, GL textures and preview rendering are the real production code.
 * Every window remains hidden; no OS input is injected. */
#include "../../src/ui/settings_internal.h"
#include "../../src/ui/shaped_edit.h"
#include "../../src/app/content_sources.h"
#include "../test.h"
#include <string.h>
#include <math.h>
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
  for(int i=0;i<1000&&pp_content_requested(settings->editor.retained.consumer);i++){SDL_Delay(1);draw();}
  CHECK(!pp_content_requested(settings->editor.retained.consumer));
  double source_clock=settings->editor.animation_start;
  PPContentSources *sources=pp_app_content_sources(app,&error);
  PPRasterStatus worker_before=pp_raster_worker_status(pp_content_sources_worker(sources));
  PPRendererMetrics render_before=pp_renderer_metrics(settings->editor.renderer);
  SDL_Delay(20);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Preview identity"},&error));draw();
  CHECK(settings->editor.animation_start==source_clock);
  CHECK(pp_raster_worker_status(pp_content_sources_worker(sources)).source_builds==worker_before.source_builds);
  PPRendererMetrics render_after=pp_renderer_metrics(settings->editor.renderer);
  CHECK(render_after.source_uploads==render_before.source_uploads&&render_after.transform_draws==render_before.transform_draws);
  PPItem appearance=*pp_project_item(pp_app_project(app),settings->editor.id);appearance.opacity=.63;
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_EDIT_ITEM,.item_id=appearance.id,.item=&appearance},&error));draw();
  CHECK(settings->editor.animation_start==source_clock);
  CHECK(pp_raster_worker_status(pp_content_sources_worker(sources)).source_builds==worker_before.source_builds);
  CHECK(pp_renderer_metrics(settings->editor.renderer).source_uploads==render_before.source_uploads);
  pp_ui_focus(ui, "item-text");
  key(PP_KEY_END, 0);
  pp_settings_event(
      settings, &(PPPlatformEvent){.kind = PP_EVENT_TEXT,
                                   .window_id = pp_settings_window_id(settings),
                                   .codepoint = 'X'});
  draw();
  CHECK(pp_ui_shaped_state(ui)->undo_count == 1);
  CHECK(settings->editor.animation_start>source_clock&&pp_ui_focused(ui,"item-text"));
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
  PPRect toolbar_save;
  CHECK(pp_ui_control_rect(ui, "save", &toolbar_save));
  CHECK(toolbar_save.w == 44 && toolbar_save.h == 36);
  CHECK(initial_preview_width > 0 && ui->body_font->info.height == 16);
  const float densities[] = {1.5f, 2, 1};
  const float font_pixels[] = {24, 32, 16};
  const float preview_factors[] = {1.5f, 2, 1};
  for (size_t i = 0; i < 3; i++) {
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
    CHECK(fabs(settings->editor.preview_width -
          initial_preview_width * preview_factors[i]) <= 1);
    PPRect save;
    CHECK(pp_ui_control_rect(ui, "save", &save));
    CHECK(save.x == toolbar_save.x && save.y == toolbar_save.y &&
          save.w == 44 && save.h == 36);
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
  CHECK(!settings->editor.retained.consumer&&!settings->editor.preview_texture);
  CHECK(pp_app_ui(app)->geometry.w == 1040 &&
        pp_app_ui(app)->geometry.h == 1800);
  pp_settings_revert(settings);
  PPItemSnapshot *independent=settings->editor.preview;pp_item_snapshot_retain(independent);
  PPContentConsumer *consumer=NULL;CHECK(pp_content_consumer_create(sources,&consumer,&error));
  PPContentFrame retained={0};double next;PPVec2 layout=pp_item_snapshot_size(independent);
  CHECK(pp_content_frame(consumer,independent,fmax(layout.x,layout.y),1,0,&retained,&next,&error));CHECK(retained.frame);pp_content_accept(consumer);
  uint64_t identity=retained.content_id;int source_width=retained.frame->width,source_height=retained.frame->height;
  size_t bytes=retained.frame->stride*retained.frame->height;uint8_t *pixels=malloc(bytes);CHECK(pixels);memcpy(pixels,retained.frame->pixels,bytes);
  uint64_t generation=pp_app_generation(app);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_NEW},&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,.choice=PP_UNSAVED_DISCARD},&error));
  pp_settings_draw(settings);CHECK(!settings->failed&&pp_app_generation(app)!=generation);
  CHECK(settings->generation==pp_app_generation(app)&&settings->editor.generation==pp_app_generation(app));
  CHECK(!settings->editor.retained.consumer&&!settings->editor.preview_texture);
  CHECK(pp_content_frame(consumer,independent,fmax(layout.x,layout.y),1,0,&retained,&next,&error));
  CHECK(retained.content_id==identity&&retained.frame->width==source_width&&retained.frame->height==source_height&&!memcmp(pixels,retained.frame->pixels,bytes));
  free(pixels);pp_content_consumer_destroy(consumer);pp_item_snapshot_release(independent);
  puts("PASS hidden Settings project epoch cancels obsolete preview work and keeps an independent source consumer's owned pixels");
  puts("PASS Settings artwork clock changes only on content commit, rename/opacity reuse sources, closure releases its consumer; hidden density 1->2->1: real font/preview rasters "
       "refresh at 1->1.5->2->1, old texture released; toolbar logical targets, drafts, selection, focus, window and "
       "geometry preserved");
  /* Join private source work before CRT exit callbacks acquire the onexit
   * lock; HarfBuzz may register its own finalizer during that work. */
  cleanup();
  return 0;
}
