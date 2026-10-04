#include "backend.h"
#include <string.h>

static PPKey normalize_key(SDL_Keycode key) {
  switch (key) {
  case SDLK_TAB:
    return PP_KEY_TAB;
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return PP_KEY_ENTER;
  case SDLK_ESCAPE:
    return PP_KEY_ESCAPE;
  case SDLK_SPACE:
    return PP_KEY_SPACE;
  case SDLK_LEFT:
    return PP_KEY_LEFT;
  case SDLK_RIGHT:
    return PP_KEY_RIGHT;
  case SDLK_UP:
    return PP_KEY_UP;
  case SDLK_DOWN:
    return PP_KEY_DOWN;
  case SDLK_HOME:
    return PP_KEY_HOME;
  case SDLK_END:
    return PP_KEY_END;
  case SDLK_PAGEUP:
    return PP_KEY_PAGE_UP;
  case SDLK_PAGEDOWN:
    return PP_KEY_PAGE_DOWN;
  case SDLK_BACKSPACE:
    return PP_KEY_BACKSPACE;
  case SDLK_DELETE:
    return PP_KEY_DELETE;
  case SDLK_A:
    return PP_KEY_A;
  case SDLK_C:
    return PP_KEY_C;
  case SDLK_V:
    return PP_KEY_V;
  case SDLK_X:
    return PP_KEY_X;
  case SDLK_Z:
    return PP_KEY_Z;
  case SDLK_S:
    return PP_KEY_S;
  case SDLK_N:
    return PP_KEY_N;
  case SDLK_O:
    return PP_KEY_O;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    return PP_KEY_SHIFT;
  case SDLK_LCTRL:
  case SDLK_RCTRL:
    return PP_KEY_CONTROL;
  case SDLK_LALT:
  case SDLK_RALT:
    return PP_KEY_ALT;
  default:
    return PP_KEY_NONE;
  }
}
bool pp_ui_poll(PPPlatformEvent *out) {
  static char *text;
  static size_t offset;
  static uint32_t text_window;
  if (!out)
    return false;
  *out = (PPPlatformEvent){0};
  if (text && text[offset]) {
    nk_rune rune;
    int used = nk_utf_decode(text + offset, &rune, (int)strlen(text + offset));
    if (used > 0) {
      offset += (size_t)used;
      out->kind = PP_EVENT_TEXT;
      out->window_id = text_window;
      out->codepoint = rune;
      return true;
    }
  }
  SDL_free(text);
  text = NULL;
  offset = 0;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    out->timestamp = event.common.timestamp / 1e9;
    switch (event.type) {
    case SDL_EVENT_QUIT:
      out->kind = PP_EVENT_QUIT;
      return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      out->kind = PP_EVENT_WINDOW_CLOSE;
      out->window_id = event.window.windowID;
      return true;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      out->kind = PP_EVENT_WINDOW_RESIZE;
      out->window_id = event.window.windowID;
      return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      out->kind = PP_EVENT_FOCUS_LOST;
      out->window_id = event.window.windowID;
      return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      out->kind = event.key.down ? PP_EVENT_KEY_DOWN : PP_EVENT_KEY_UP;
      out->window_id = event.key.windowID;
      out->key = normalize_key(event.key.key);
      out->repeat = event.key.repeat;
      if (event.key.mod & SDL_KMOD_SHIFT)
        out->modifiers |= PP_MOD_SHIFT;
      if (event.key.mod & SDL_KMOD_CTRL)
        out->modifiers |= PP_MOD_CONTROL;
      if (event.key.mod & SDL_KMOD_ALT)
        out->modifiers |= PP_MOD_ALT;
      return true;
    case SDL_EVENT_TEXT_EDITING:
      out->kind = PP_EVENT_COMPOSITION;
      out->window_id = event.edit.windowID;
      out->composing = event.edit.text && *event.edit.text;
      return true;
    case SDL_EVENT_TEXT_INPUT:
      text = SDL_strdup(event.text.text);
      text_window = event.text.windowID;
      if (!text) {
        out->kind = PP_EVENT_CANCEL;
        out->window_id = text_window;
        return true;
      }
      return pp_ui_poll(out);
    case SDL_EVENT_MOUSE_MOTION:
      out->kind = PP_EVENT_POINTER_MOVE;
      out->window_id = event.motion.windowID;
      out->window_point = (PPVec2){event.motion.x, event.motion.y};
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      out->kind = event.button.down ? PP_EVENT_BUTTON_DOWN : PP_EVENT_BUTTON_UP;
      out->window_id = event.button.windowID;
      out->window_point = (PPVec2){event.button.x, event.button.y};
      out->button = event.button.button == SDL_BUTTON_LEFT ? PP_BUTTON_LEFT
                    : event.button.button == SDL_BUTTON_RIGHT
                        ? PP_BUTTON_RIGHT
                        : PP_BUTTON_MIDDLE;
      return true;
    case SDL_EVENT_MOUSE_WHEEL:
      out->kind = PP_EVENT_WHEEL;
      out->window_id = event.wheel.windowID;
      out->wheel_delta = event.wheel.y;
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        out->wheel_delta *= -1;
      return true;
    default:
      break;
    }
  }
  return false;
}
