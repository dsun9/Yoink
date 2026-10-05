#include "../../src/ui/backend.h"
#include "../test.h"
#include "pointy/ui.h"
#include <SDL3/SDL.h>
#include <string.h>
static bool next_event(PPPlatformEvent *event) {
  /* SDL's poll sentinel ends a pump snapshot even if a new event was pushed
   * after its last item. The application resumes at the next loop iteration. */
  for (int i = 0; i < 4; i++)
    if (pp_ui_poll(event))
      return true;
  return false;
}
static float test_width(nk_handle handle, float height, const char *text,
                        int length) {
  (void)handle;
  (void)height;
  (void)text;
  return length * 8.f;
}
static bool draw_click_button(PPUIBackend *ui) {
  bool clicked = false;
  if (nk_begin(&ui->context, "click-test", nk_rect(0, 0, 300, 200),
               NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_dynamic(&ui->context, 32, 1);
    clicked = pp_ui_button(ui, "target", "Import font", true);
  }
  nk_end(&ui->context);
  nk_clear(&ui->context);
  return clicked;
}
static void test_coalesced_click(void) {
  struct nk_user_font font = {.height = 16, .width = test_width};
  PPUIBackend ui = {.scale = 2, .window_scale = 1};
  CHECK(nk_init_default(&ui.context, &font));
  nk_input_begin(&ui.context);
  nk_input_end(&ui.context);
  CHECK(!draw_click_button(&ui));
  PPRect target;
  CHECK(pp_ui_control_rect(&ui, "target", &target));
  PPPlatformEvent event = {
      .kind = PP_EVENT_POINTER_MOVE,
      .window_point = {target.x + target.w / 2, target.y + target.h / 2},
      .button = PP_BUTTON_LEFT};
  pp_ui_backend_event(&ui, &event);
  CHECK(ui.context.input.mouse.pos.x == (int)event.window_point.x);
  event.kind = PP_EVENT_BUTTON_DOWN;
  pp_ui_backend_event(&ui, &event);
  event.kind = PP_EVENT_BUTTON_UP;
  pp_ui_backend_event(&ui, &event);
  nk_input_end(&ui.context);
  ui.input_started = false;
  CHECK(draw_click_button(&ui));
  nk_free(&ui.context);
  free(ui.controls);
}
static void test_clipped_control_focus(void) {
  struct nk_user_font font = {.height = 16, .width = test_width};
  PPUIBackend ui = {.scale = 1, .window_scale = 1};
  CHECK(nk_init_default(&ui.context, &font));
  nk_input_begin(&ui.context);
  nk_input_end(&ui.context);
  CHECK(!draw_click_button(&ui));
  PPRect target;
  CHECK(pp_ui_control_rect(&ui, "target", &target));
  PPPlatformEvent event = {.kind = PP_EVENT_POINTER_MOVE,
                           .window_point = {target.x + target.w / 2,
                                            target.y + target.h / 2},
                           .button = PP_BUTTON_LEFT};
  pp_ui_backend_event(&ui, &event);
  event.kind = PP_EVENT_BUTTON_DOWN;
  pp_ui_backend_event(&ui, &event);
  event.kind = PP_EVENT_BUTTON_UP;
  pp_ui_backend_event(&ui, &event);
  nk_input_end(&ui.context);
  ui.input_started = false;
  pp_ui_focus(&ui, "new");
  CHECK(nk_begin(&ui.context, "click-test", nk_rect(0, 0, 300, 200),
                 NK_WINDOW_NO_SCROLLBAR));
  nk_layout_row_dynamic(&ui.context, 32, 1);
  /* A scrolled group can lay out a control behind the project toolbar. Its
   * full bounds still contain the click, but its panel clips that area out. */
  ui.context.current->layout->clip = nk_rect(0, 100, 300, 100);
  CHECK(!pp_ui_button(&ui, "target", "Clipped anchor", true));
  CHECK(pp_ui_focused(&ui, "new"));
  pp_ui_modal(&ui, true);
  pp_ui_modal(&ui, false);
  CHECK(pp_ui_focused(&ui, "new"));
  nk_end(&ui.context);
  nk_free(&ui.context);
  free(ui.controls);
}

int main(void) {
  CHECK(SDL_InitSubSystem(SDL_INIT_EVENTS));
  test_coalesced_click();
  test_clipped_control_focus();
  SDL_Event source = {0};
  source.type = SDL_EVENT_TEXT_INPUT;
  source.text.windowID = 42;
  source.text.text = "A\xc3\xa9\xf0\x9f\x91\x89";
  CHECK(SDL_PushEvent(&source));
  PPPlatformEvent event;
  CHECK(pp_ui_poll(&event) && event.kind == PP_EVENT_TEXT &&
        event.window_id == 42 && event.codepoint == 'A');
  CHECK(pp_ui_poll(&event) && event.codepoint == 0xe9);
  CHECK(pp_ui_poll(&event) && event.codepoint == 0x1f449);
  source = (SDL_Event){0};
  source.type = SDL_EVENT_TEXT_EDITING;
  source.edit.windowID = 42;
  source.edit.text = "composition";
  CHECK(SDL_PushEvent(&source));
  CHECK(next_event(&event) && event.kind == PP_EVENT_COMPOSITION &&
        event.composing);
  source = (SDL_Event){0};
  source.type = SDL_EVENT_KEY_DOWN;
  source.key.windowID = 42;
  source.key.key = SDLK_TAB;
  source.key.down = true;
  source.key.mod = SDL_KMOD_SHIFT;
  CHECK(SDL_PushEvent(&source));
  CHECK(next_event(&event) && event.key == PP_KEY_TAB &&
        event.modifiers == PP_MOD_SHIFT && event.kind == PP_EVENT_KEY_DOWN);
  PPUIBackend ui = {.scale = 1, .window_scale = 1};
  CHECK(nk_init_default(&ui.context, NULL));
  pp_ui_backend_event(
      &ui, &(PPPlatformEvent){.kind = PP_EVENT_COMPOSITION, .composing = true});
  pp_ui_backend_event(
      &ui, &(PPPlatformEvent){.kind = PP_EVENT_KEY_DOWN, .key = PP_KEY_ENTER});
  CHECK(ui.composing && !ui.activate);
  pp_ui_backend_event(
      &ui, &(PPPlatformEvent){.kind = PP_EVENT_TEXT, .codepoint = 0x1f449});
  CHECK(!ui.composing && ui.context.input.keyboard.text_len == 4);
  pp_ui_backend_event(&ui, &(PPPlatformEvent){.kind = PP_EVENT_KEY_DOWN,
                                              .key = PP_KEY_TAB,
                                              .modifiers = PP_MOD_SHIFT});
  CHECK(ui.tab_direction == -1);
  nk_free(&ui.context);
  SDL_QuitSubSystem(SDL_INIT_EVENTS);
  puts("ui_events: Unicode scalars, window routing, modifiers and IME shortcut "
       "guard passed");
  return 0;
}
