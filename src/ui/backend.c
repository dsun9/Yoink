/* OpenGL conversion, clipboard and input patterns adapted from BongoCat
 * src/ui/backend/{ui_backend.c,ui_input.c}, revision recorded in third-party
 * notices. BongoCat contributors, AGPL-3.0-or-later. Each instance owns its
 * context, fonts, GL resources and focus registry; no global context lookup. */
#define NK_IMPLEMENTATION
#include "backend.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PPUIVertex {
  float position[2], uv[2];
  nk_byte color[4];
} PPUIVertex;
static bool fail(PPError *error, const char *message) {
  if (error) {
    error->code = PP_ERROR_RENDER;
    snprintf(error->message, sizeof(error->message), "%s", message);
  }
  return false;
}
static uint64_t control_id(const char *name) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    value = (value ^ *p) * UINT64_C(1099511628211);
  return value;
}
static void clipboard_copy(nk_handle user, const char *text, int length) {
  (void)user;
  if (length <= 0)
    return;
  char *copy = malloc((size_t)length + 1);
  if (!copy)
    return;
  memcpy(copy, text, (size_t)length);
  copy[length] = 0;
  SDL_SetClipboardText(copy);
  free(copy);
}
static void clipboard_paste(nk_handle user, struct nk_text_edit *edit) {
  (void)user;
  char *text = SDL_GetClipboardText();
  if (text) {
    nk_textedit_paste(edit, text, (int)strlen(text));
    SDL_free(text);
  }
}
static bool create_fonts(PPUIBackend *ui, PPError *error) {
  nk_font_atlas_init_default(&ui->atlas);
  nk_font_atlas_begin(&ui->atlas);
  PPBlob body = {0}, heading = {0};
  bool body_found = pp_platform_find_font("Segoe UI", &body, NULL);
  bool heading_found =
      pp_platform_find_font("Segoe UI Semibold", &heading, NULL);
  struct nk_font_config config = nk_font_config(0);
  config.oversample_h = config.oversample_v = 2;
  static const nk_rune ranges[] = {0x20, 0xff, 0x370, 0x52f, 0x25a1, 0x25a1, 0};
  config.range = ranges;
  config.fallback_glyph = 0x25a1;
  if (body_found) {
    ui->body_font = nk_font_atlas_add_from_memory(
        &ui->atlas, body.data, body.size, pp_ui_theme.body_size * ui->scale,
        &config);
    ui->utility_font = nk_font_atlas_add_from_memory(
        &ui->atlas, body.data, body.size, pp_ui_theme.utility_size * ui->scale,
        &config);
  } else {
    ui->body_font = nk_font_atlas_add_default(
        &ui->atlas, pp_ui_theme.body_size * ui->scale, NULL);
    ui->utility_font = nk_font_atlas_add_default(
        &ui->atlas, pp_ui_theme.utility_size * ui->scale, NULL);
    snprintf(ui->font_diagnostic, sizeof(ui->font_diagnostic),
             "Control font: ProggyClean (Segoe UI unavailable). Item preview "
             "uses its own fonts.");
  }
  if (heading_found)
    ui->heading_font = nk_font_atlas_add_from_memory(
        &ui->atlas, heading.data, heading.size,
        pp_ui_theme.heading_size * ui->scale, &config);
  else if (body_found)
    ui->heading_font = nk_font_atlas_add_from_memory(
        &ui->atlas, body.data, body.size, pp_ui_theme.heading_size * ui->scale,
        &config);
  else
    ui->heading_font = nk_font_atlas_add_default(
        &ui->atlas, pp_ui_theme.heading_size * ui->scale, NULL);
  int width = 0, height = 0;
  const void *pixels =
      nk_font_atlas_bake(&ui->atlas, &width, &height, NK_FONT_ATLAS_RGBA32);
  pp_blob_destroy(&body);
  pp_blob_destroy(&heading);
  if (!pixels || !ui->body_font || !ui->heading_font || !ui->utility_font)
    return fail(error, "Cannot build settings font atlas");
  glGenTextures(1, &ui->font_texture);
  glBindTexture(GL_TEXTURE_2D, ui->font_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  nk_font_atlas_end(&ui->atlas, nk_handle_id((int)ui->font_texture),
                    &ui->null_texture);
  ui->body_font->handle.height /= ui->scale;
  ui->heading_font->handle.height /= ui->scale;
  ui->utility_font->handle.height /= ui->scale;
  nk_style_set_font(&ui->context, &ui->body_font->handle);
  return glGetError() == GL_NO_ERROR ||
         fail(error, "Cannot upload settings fonts");
}
bool pp_ui_backend_create(PPUIBackend **out, const PPUIWindowOptions *options,
                          PPError *error) {
  if (out)
    *out = NULL;
  if (!out || !options)
    return fail(error, "Missing UI window options");
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    return fail(error, SDL_GetError());
  PPUIBackend *ui = calloc(1, sizeof(*ui));
  if (!ui) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return fail(error, "Cannot allocate settings backend");
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN |
                          SDL_WINDOW_HIGH_PIXEL_DENSITY |
                          SDL_WINDOW_ALWAYS_ON_TOP;
  if (options->resizable)
    flags |= SDL_WINDOW_RESIZABLE;
  if (options->compact) flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY;
  ui->window = SDL_CreateWindow(options->title, (int)options->geometry.w,
                                (int)options->geometry.h, flags);
  if (ui->window)
    ui->gl_context = SDL_GL_CreateContext(ui->window);
  if (!ui->gl_context || !SDL_GL_MakeCurrent(ui->window, ui->gl_context) ||
      !pp_gl_load(&ui->gl, error))
    goto failed;
  ui->scale = options->scale > 0 ? options->scale
                                 : SDL_GetWindowDisplayScale(ui->window);
  if (ui->scale <= 0)
    ui->scale = 1;
  SDL_SetWindowSize(ui->window, (int)(options->geometry.w * ui->scale),
                    (int)(options->geometry.h * ui->scale));
  if (!options->compact)
    SDL_SetWindowMinimumSize(ui->window, (int)(640 * ui->scale),
                             (int)(480 * ui->scale));
  SDL_SetWindowPosition(ui->window, (int)options->geometry.x,
                        (int)options->geometry.y);
  SDL_GL_SetSwapInterval(1);
  const char *vertex =
      "#version 150\n in vec2 Position; in vec2 TexCoord; in vec4 Color; "
      "uniform mat4 Projection; out vec2 UV; out vec4 Tint; void "
      "main(){UV=TexCoord;Tint=Color;gl_Position=Projection*vec4(Position,0,1);"
      "}";
  const char *fragment =
      "#version 150\n in vec2 UV; in vec4 Tint; uniform sampler2D Texture; out "
      "vec4 color; void main(){color=Tint*texture(Texture,UV);}";
  ui->program = pp_gl_program(&ui->gl, vertex, fragment, error);
  if (!ui->program)
    goto failed;
  ui->projection_location =
      ui->gl.GetUniformLocation(ui->program, "Projection");
  ui->gl.GenVertexArrays(1, &ui->vao);
  ui->gl.BindVertexArray(ui->vao);
  ui->gl.GenBuffers(1, &ui->vbo);
  ui->gl.GenBuffers(1, &ui->ebo);
  ui->gl.BindBuffer(GL_ARRAY_BUFFER, ui->vbo);
  ui->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->ebo);
  const char *attributes[] = {"Position", "TexCoord", "Color"};
  const size_t offsets[] = {offsetof(PPUIVertex, position),
                            offsetof(PPUIVertex, uv),
                            offsetof(PPUIVertex, color)};
  for (int i = 0; i < 3; i++) {
    GLint at = ui->gl.GetAttribLocation(ui->program, attributes[i]);
    if (at < 0) {
      fail(error, "Settings shader attribute is unavailable");
      goto failed;
    }
    ui->gl.EnableVertexAttribArray((GLuint)at);
    ui->gl.VertexAttribPointer(
        (GLuint)at, i == 2 ? 4 : 2, i == 2 ? GL_UNSIGNED_BYTE : GL_FLOAT,
        i == 2 ? GL_TRUE : GL_FALSE, sizeof(PPUIVertex), (void *)offsets[i]);
  }
  if (!nk_init_default(&ui->context, NULL)) {
    fail(error, "Cannot allocate Nuklear context");
    goto failed;
  }
  nk_buffer_init_default(&ui->commands);
  nk_buffer_init_default(&ui->vertices);
  nk_buffer_init_default(&ui->elements);
  if (!create_fonts(ui, error))
    goto failed;
  ui->context.clip.copy = clipboard_copy;
  ui->context.clip.paste = clipboard_paste;
  ui->context.clip.userdata = nk_handle_ptr(ui);
  pp_ui_apply_theme(ui);
  ui->arrow_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
  ui->hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
  ui->text_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
  SDL_StartTextInput(ui->window);
  *out = ui;
  return true;
failed:
  if (error && !error->code)
    fail(error, SDL_GetError());
  pp_ui_backend_destroy(ui);
  return false;
}
void pp_ui_backend_destroy(PPUIBackend *ui) {
  if (!ui)
    return;
  if (ui->gl_context && SDL_GL_MakeCurrent(ui->window, ui->gl_context)) {
    if (ui->font_texture)
      glDeleteTextures(1, &ui->font_texture);
    if (ui->vbo)
      ui->gl.DeleteBuffers(1, &ui->vbo);
    if (ui->ebo)
      ui->gl.DeleteBuffers(1, &ui->ebo);
    if (ui->vao)
      ui->gl.DeleteVertexArrays(1, &ui->vao);
    if (ui->program)
      ui->gl.DeleteProgram(ui->program);
  }
  nk_font_atlas_clear(&ui->atlas);
  nk_buffer_free(&ui->commands);
  nk_buffer_free(&ui->vertices);
  nk_buffer_free(&ui->elements);
  nk_free(&ui->context);
  SDL_DestroyCursor(ui->arrow_cursor);
  SDL_DestroyCursor(ui->hand_cursor);
  SDL_DestroyCursor(ui->text_cursor);
  if (ui->gl_context)
    SDL_GL_DestroyContext(ui->gl_context);
  if (ui->window)
    SDL_DestroyWindow(ui->window);
  free(ui->controls);
  free(ui);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
uint32_t pp_ui_backend_window_id(const PPUIBackend *ui) {
  return ui ? SDL_GetWindowID(ui->window) : 0;
}
void pp_ui_show(PPUIBackend *ui, bool show) {
  if (ui->visible == show)
    return;
  ui->visible = show;
  if (show)
    SDL_ShowWindow(ui->window);
  else
    SDL_HideWindow(ui->window);
}
static void key(PPUIBackend *ui, PPKey code, bool down, bool control) {
  struct nk_context *ctx = &ui->context;
  switch (code) {
  case PP_KEY_SHIFT:
    nk_input_key(ctx, NK_KEY_SHIFT, down);
    break;
  case PP_KEY_ALT:
    nk_input_key(ctx, NK_KEY_ALT, down);
    break;
  case PP_KEY_DELETE:
    nk_input_key(ctx, NK_KEY_DEL, down);
    break;
  case PP_KEY_ENTER:
    nk_input_key(ctx, NK_KEY_ENTER, down && !ui->composing);
    break;
  case PP_KEY_BACKSPACE:
    nk_input_key(ctx, NK_KEY_BACKSPACE, down);
    break;
  case PP_KEY_HOME:
    nk_input_key(ctx, NK_KEY_TEXT_START, down);
    break;
  case PP_KEY_END:
    nk_input_key(ctx, NK_KEY_TEXT_END, down);
    break;
  case PP_KEY_LEFT:
    nk_input_key(ctx, control ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down);
    break;
  case PP_KEY_RIGHT:
    nk_input_key(ctx, control ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down);
    break;
  case PP_KEY_UP:
    nk_input_key(ctx, NK_KEY_UP, down);
    break;
  case PP_KEY_DOWN:
    nk_input_key(ctx, NK_KEY_DOWN, down);
    break;
  case PP_KEY_A:
    nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down && control);
    break;
  case PP_KEY_C:
    nk_input_key(ctx, NK_KEY_COPY, down && control);
    break;
  case PP_KEY_V:
    nk_input_key(ctx, NK_KEY_PASTE, down && control);
    break;
  case PP_KEY_X:
    nk_input_key(ctx, NK_KEY_CUT, down && control);
    break;
  case PP_KEY_Z:
    nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && control);
    break;
  default:
    break;
  }
}
void pp_ui_backend_event(PPUIBackend *ui, const PPPlatformEvent *event) {
  if (!ui || !event || event->window_id != pp_ui_backend_window_id(ui))
    return;
  if (!ui->input_started) {
    nk_input_begin(&ui->context);
    ui->input_started = true;
  }
  int x = (int)(event->window_point.x / ui->scale),
      y = (int)(event->window_point.y / ui->scale);
  switch (event->kind) {
  case PP_EVENT_POINTER_MOVE:
    nk_input_motion(&ui->context, x, y);
    break;
  case PP_EVENT_BUTTON_DOWN:
  case PP_EVENT_BUTTON_UP:
    nk_input_button(&ui->context,
                    event->button == PP_BUTTON_LEFT ? NK_BUTTON_LEFT
                                                    : NK_BUTTON_RIGHT,
                    x, y, event->kind == PP_EVENT_BUTTON_DOWN);
    if (event->kind == PP_EVENT_BUTTON_DOWN)
      ui->keyboard_focus = false;
    break;
  case PP_EVENT_WHEEL:
    nk_input_scroll(&ui->context, nk_vec2(0, (float)event->wheel_delta));
    break;
  case PP_EVENT_TEXT:
    nk_input_unicode(&ui->context, event->codepoint);
    ui->composing = false;
    break;
  case PP_EVENT_COMPOSITION:
    ui->composing = event->composing;
    break;
  case PP_EVENT_KEY_DOWN:
  case PP_EVENT_KEY_UP: {
    bool down = event->kind == PP_EVENT_KEY_DOWN;
    ui->modifiers = event->modifiers;
    key(ui, event->key, down, (event->modifiers & PP_MOD_CONTROL) != 0);
    if (down && !ui->composing) {
      if (event->key == PP_KEY_TAB)
        ui->tab_direction = event->modifiers & PP_MOD_SHIFT ? -1 : 1;
      if (!event->repeat &&
          (event->key == PP_KEY_ENTER || event->key == PP_KEY_SPACE))
        ui->activate = true;
      if (event->key == PP_KEY_ESCAPE)
        ui->escape = true;
      ui->navigation = event->key;
    }
    break;
  }
  case PP_EVENT_FOCUS_LOST:
  case PP_EVENT_CANCEL:
    memset(&ui->context.input, 0, sizeof(ui->context.input));
    ui->composing = false;
    ui->activate = false;
    ui->modifiers = 0;
    break;
  default:
    break;
  }
}
bool pp_ui_begin(PPUIBackend *ui, PPError *error) {
  if (!SDL_GL_MakeCurrent(ui->window, ui->gl_context))
    return fail(error, SDL_GetError());
  if (!ui->input_started)
    nk_input_begin(&ui->context);
  nk_input_end(&ui->context);
  ui->input_started = false;
  int width, height;
  SDL_GetWindowSize(ui->window, &width, &height);
  ui->width = width / ui->scale;
  ui->height = height / ui->scale;
  if (ui->tab_direction && ui->control_count) {
    int current = ui->tab_direction > 0 ? -1 : 0;
    for (size_t i = 0; i < ui->control_count; i++)
      if (ui->controls[i].id == ui->focus_id)
        current = (int)i;
    for (size_t n = 0; n < ui->control_count; n++) {
      current = (current + ui->tab_direction + (int)ui->control_count) %
                (int)ui->control_count;
      if (ui->controls[current].enabled) {
        ui->focus_id = ui->controls[current].id;
        break;
      }
    }
    ui->keyboard_focus = true;
  }
  ui->tab_direction = 0;
  ui->control_count = 0;
  ui->pointer_target = ui->text_target = false;
  return true;
}
bool pp_ui_end(PPUIBackend *ui, PPError *error) {
  static const struct nk_draw_vertex_layout_element layout[] = {
      {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(PPUIVertex, position)},
      {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(PPUIVertex, uv)},
      {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(PPUIVertex, color)},
      {NK_VERTEX_LAYOUT_END}};
  struct nk_convert_config config = {0};
  config.vertex_layout = layout;
  config.vertex_size = sizeof(PPUIVertex);
  config.vertex_alignment = NK_ALIGNOF(PPUIVertex);
  config.tex_null = ui->null_texture;
  config.global_alpha = 1;
  config.circle_segment_count = config.curve_segment_count =
      config.arc_segment_count = 24;
  config.shape_AA = config.line_AA = NK_ANTI_ALIASING_ON;
  nk_flags result = nk_convert(&ui->context, &ui->commands, &ui->vertices,
                               &ui->elements, &config);
  if (result != NK_CONVERT_SUCCESS)
    return fail(error, "Settings drawing exceeded the command buffer");
  int width, height;
  SDL_GetWindowSizeInPixels(ui->window, &width, &height);
  float projection[4][4] = {{2 / ui->width, 0, 0, 0},
                            {0, -2 / ui->height, 0, 0},
                            {0, 0, -1, 0},
                            {-1, 1, 0, 1}};
  glViewport(0, 0, width, height);
  glClearColor(pp_ui_theme.canvas.r / 255.f, pp_ui_theme.canvas.g / 255.f,
               pp_ui_theme.canvas.b / 255.f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  ui->gl.UseProgram(ui->program);
  ui->gl.Uniform1i(ui->gl.GetUniformLocation(ui->program, "Texture"), 0);
  ui->gl.UniformMatrix4fv(ui->projection_location, 1, GL_FALSE,
                          &projection[0][0]);
  ui->gl.ActiveTexture(GL_TEXTURE0);
  ui->gl.BindVertexArray(ui->vao);
  ui->gl.BindBuffer(GL_ARRAY_BUFFER, ui->vbo);
  ui->gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ui->vertices.allocated,
                    nk_buffer_memory(&ui->vertices), GL_STREAM_DRAW);
  ui->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->ebo);
  ui->gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ui->elements.allocated,
                    nk_buffer_memory(&ui->elements), GL_STREAM_DRAW);
  const struct nk_draw_command *command;
  size_t offset = 0;
  float sx = width / ui->width, sy = height / ui->height;
  nk_draw_foreach(command, &ui->context, &ui->commands) {
    if (!command->elem_count)
      continue;
    glBindTexture(GL_TEXTURE_2D, (GLuint)command->texture.id);
    glScissor(
        (GLint)(command->clip_rect.x * sx),
        (GLint)((ui->height - command->clip_rect.y - command->clip_rect.h) *
                sy),
        (GLsizei)(command->clip_rect.w * sx),
        (GLsizei)(command->clip_rect.h * sy));
    glDrawElements(GL_TRIANGLES, (GLsizei)command->elem_count,
                   GL_UNSIGNED_SHORT, (void *)offset);
    offset += command->elem_count * sizeof(nk_draw_index);
  }
  nk_clear(&ui->context);
  nk_buffer_clear(&ui->commands);
  nk_buffer_clear(&ui->vertices);
  nk_buffer_clear(&ui->elements);
  glDisable(GL_SCISSOR_TEST);
  ui->activate = ui->escape = false;
  ui->navigation = PP_KEY_NONE;
  if (ui->visible && SDL_GetMouseFocus()==ui->window)
    SDL_SetCursor(ui->text_target      ? ui->text_cursor
                : ui->pointer_target ? ui->hand_cursor
                                     : ui->arrow_cursor);
  if (glGetError() != GL_NO_ERROR)
    return fail(error, "OpenGL could not draw the settings window");
  if (ui->failed)
    return fail(error, "Cannot retain the keyboard control registry");
  if (ui->capture_path) {
    bool captured = pp_ui_capture(ui, ui->capture_path, error);
    ui->capture_path = NULL;
    if (!captured)
      return false;
  }
  return SDL_GL_SwapWindow(ui->window) || fail(error, SDL_GetError());
}
void pp_ui_modal(PPUIBackend *ui, bool modal) {
  if (modal != ui->last_modal) {
    if (modal) {
      ui->restore_focus = ui->focus_id;
      ui->focus_id = control_id("cancel-transition");
    } else
      ui->focus_id = ui->restore_focus;
    ui->keyboard_focus = true;
    ui->control_count = 0;
    ui->activate = ui->escape = false;
  }
  ui->modal = ui->last_modal = modal;
}
static bool control(PPUIBackend *ui, const char *id, const char *label,
                    bool enabled, bool edit, struct nk_rect *bounds) {
  *bounds = nk_widget_bounds(&ui->context);
  uint64_t hash = control_id(id);
  if (ui->control_count == ui->control_capacity) {
    size_t capacity = ui->control_capacity ? ui->control_capacity * 2 : 64;
    PPUIControl *controls = realloc(ui->controls, capacity * sizeof(*controls));
    if (!controls)
      ui->failed = true;
    else {
      ui->controls = controls;
      ui->control_capacity = capacity;
    }
  }
  if (ui->control_count < ui->control_capacity) {
    PPUIControl *record = &ui->controls[ui->control_count++];
    *record = (PPUIControl){
        .id = hash, .bounds = *bounds, .enabled = enabled, .edit = edit};
    snprintf(record->label, sizeof(record->label), "%s", label);
  }
  if (enabled && nk_input_is_mouse_hovering_rect(&ui->context.input, *bounds)) {
    ui->pointer_target = !edit;
    ui->text_target = edit;
    if (nk_input_is_mouse_pressed(&ui->context.input, NK_BUTTON_LEFT) ||
        nk_input_is_mouse_released(&ui->context.input, NK_BUTTON_LEFT))
      ui->focus_id = hash;
  }
  bool focused = enabled && ui->focus_id == hash;
  return focused;
}
void pp_ui_reveal_focus(PPUIBackend *ui, const char *group,
                        struct nk_rect viewport, size_t first_control) {
  if (!ui->keyboard_focus || ui->popup_id)
    return;
  for (size_t i = first_control; i < ui->control_count; i++)
    if (ui->controls[i].id == ui->focus_id && ui->controls[i].enabled) {
      struct nk_rect bounds = ui->controls[i].bounds;
      nk_uint x, y;
      nk_group_get_scroll(&ui->context, group, &x, &y);
      if (bounds.y < viewport.y + 24)
        nk_group_set_scroll(&ui->context, group, x,
                            (nk_uint)fmax(0, y + bounds.y - viewport.y - 24));
      else if (bounds.y + bounds.h > viewport.y + viewport.h - 32)
        nk_group_set_scroll(
            &ui->context, group, x,
            y + (nk_uint)(bounds.y + bounds.h - viewport.y - viewport.h + 32));
      break;
    }
}
void pp_ui_focus(PPUIBackend *ui, const char *id) {
  ui->focus_id = control_id(id);
  ui->keyboard_focus = true;
}
static void focus_outline(PPUIBackend *ui, struct nk_rect bounds,
                          bool focused) {
  if (focused && ui->keyboard_focus)
    nk_stroke_rect(nk_window_get_canvas(&ui->context), bounds,
                   pp_ui_theme.control_radius, 2, pp_ui_theme.accent);
}
bool pp_ui_button(PPUIBackend *ui, const char *id, const char *label,
                  bool enabled) {
  enabled = enabled && (!ui->popup_id || ui->in_popup);
  struct nk_rect bounds;
  bool focused = control(ui, id, label, enabled, false, &bounds);
  if (!enabled)
    nk_widget_disable_begin(&ui->context);
  bool clicked = nk_button_label(&ui->context, label) != 0;
  if (!enabled)
    nk_widget_disable_end(&ui->context);
  focus_outline(ui, bounds, focused);
  bool activated = enabled && (clicked || (focused && ui->activate));
  if (activated)
    ui->activate = false;
  return activated;
}
bool pp_ui_select(PPUIBackend *ui, const char *id, const char *label,
                  bool selected, bool enabled) {
  struct nk_style_button saved = ui->context.style.button;
  if (selected) {
    ui->context.style.button.normal = nk_style_item_color(pp_ui_theme.accent);
    ui->context.style.button.text_normal = pp_ui_theme.surface;
  }
  bool clicked = pp_ui_button(ui, id, label, enabled);
  ui->context.style.button = saved;
  return clicked;
}
bool pp_ui_edit(PPUIBackend *ui, const char *id, char *value, int capacity,
                bool multiline, bool enabled) {
  enabled = enabled && (!ui->popup_id || ui->in_popup);
  struct nk_rect bounds;
  bool focused = control(ui, id, id, enabled, true, &bounds);
  nk_flags flags = (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE |
                   NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER;
  if (multiline)
    flags = NK_EDIT_BOX;
  if (focused && ui->keyboard_focus)
    nk_edit_focus(&ui->context, NK_EDIT_ALWAYS_INSERT_MODE);
  else if (ui->keyboard_focus && ui->context.current->edit.active &&
           ui->context.current->edit.name == ui->context.current->edit.seq)
    nk_edit_unfocus(&ui->context);
  if (!enabled)
    nk_widget_disable_begin(&ui->context);
  int length = (int)strlen(value);
  /* Nuklear reports active/commit states, not changed. The editor consumes a
   * hash of the bytes so equal edits remain clean at the command boundary. */
  uint64_t before = control_id(value);
  nk_edit_string(&ui->context, flags, value, &length, capacity - 1,
                 nk_filter_default);
  value[length] = 0;
  if (!enabled)
    nk_widget_disable_end(&ui->context);
  focus_outline(ui, bounds, focused);
  if (focused) {
    SDL_Rect area = {(int)(bounds.x * ui->scale), (int)(bounds.y * ui->scale),
                     (int)(bounds.w * ui->scale), (int)(bounds.h * ui->scale)};
    SDL_SetTextInputArea(ui->window, &area, 0);
  }
  return enabled && !ui->composing && before != control_id(value);
}
bool pp_ui_unicode_notice(const PPUIBackend *ui, const char *text, char *out,
                          size_t capacity) {
  char points[96] = {0};
  size_t length = 0, missing = 0;
  for (const char *p = text; p && *p;) {
    nk_rune rune;
    int used = nk_utf_decode(p, &rune, (int)strlen(p));
    if (used <= 0)
      break;
    p += used;
    if (rune == '\n' || rune == '\r' || rune == '\t')
      continue;
    const struct nk_font_glyph *glyph = nk_font_find_glyph(ui->body_font, rune);
    if (glyph && glyph->codepoint == rune)
      continue;
    missing++;
    if (missing <= 8)
      length += (size_t)snprintf(points + length, sizeof(points) - length,
                                 " U+%04X", (unsigned)rune);
  }
  if (capacity)
    out[0] = 0;
  if (!missing)
    return false;
  snprintf(out, capacity, "Editor placeholders:%s%s. Stored text is unchanged.",
           points, missing > 8 ? " ..." : "");
  return true;
}
bool pp_ui_choice(PPUIBackend *ui, const char *id, const char *const *options,
                  int count, int *selected, bool enabled) {
  if (count <= 0)
    return false;
  struct nk_context *ctx = &ui->context;
  struct nk_rect trigger = nk_widget_bounds(ctx);
  uint64_t hash = control_id(id);
  char label[180];
  snprintf(label, sizeof(label), "%s  v",
           options[*selected >= 0 && *selected < count ? *selected : 0]);
  if (pp_ui_button(ui, id, label, enabled)) {
    ui->popup_id = hash;
    ui->popup_index = *selected;
    ui->activate = false;
  }
  if (ui->popup_id != hash)
    return false;
  /* Nuklear positions a popup relative to the current panel's clipped
   * content origin, including nested/scrolled groups. */
  struct nk_rect parent = nk_window_get_content_region(ctx);
  float height = fminf(220, count * 38.f + 24);
  float top = trigger.y + trigger.h;
  if (top + height > ui->height - 12)
    top = trigger.y - height;
  struct nk_rect bounds =
      nk_rect(trigger.x - parent.x, top - parent.y, trigger.w, height);
  bool changed = false;
  if (nk_popup_begin(ctx, NK_POPUP_STATIC, id, NK_WINDOW_BORDER, bounds)) {
    ui->in_popup = true;
    struct nk_rect popup_viewport = nk_window_get_content_region(ctx);
    struct nk_rect focused_option = {0};
    if (ui->navigation == PP_KEY_UP)
      ui->popup_index = (ui->popup_index + count - 1) % count;
    if (ui->navigation == PP_KEY_DOWN)
      ui->popup_index = (ui->popup_index + 1) % count;
    if (ui->navigation == PP_KEY_HOME)
      ui->popup_index = 0;
    if (ui->navigation == PP_KEY_END)
      ui->popup_index = count - 1;
    if (ui->escape) {
      nk_popup_close(ctx);
      ui->popup_id = 0;
      ui->focus_id = hash;
      ui->escape = false;
    }
    for (int i = 0; i < count; i++) {
      char option_id[100];
      snprintf(option_id, sizeof(option_id), "%s-option-%d", id, i);
      nk_layout_row_dynamic(ctx, 32, 1);
      if (ui->keyboard_focus && i == ui->popup_index)
        ui->focus_id = control_id(option_id);
      if (i == ui->popup_index) focused_option = nk_widget_bounds(ctx);
      if (pp_ui_select(ui, option_id, options[i], i == *selected, true)) {
        *selected = i;
        changed = true;
        nk_popup_close(ctx);
        ui->popup_id = 0;
        ui->focus_id = hash;
      }
    }
    if (ui->keyboard_focus && ui->popup_id) {
      nk_uint x,y;
      nk_window_get_scroll(ctx,&x,&y);
      if (focused_option.y<popup_viewport.y)
        nk_window_set_scroll(ctx,x,(nk_uint)fmax(0,y+focused_option.y-popup_viewport.y));
      else if (focused_option.y+focused_option.h>popup_viewport.y+popup_viewport.h)
        nk_window_set_scroll(ctx,x,y+(nk_uint)(focused_option.y+focused_option.h-popup_viewport.y-popup_viewport.h));
    }
    ui->in_popup = false;
    nk_popup_end(ctx);
  } else {
    ui->popup_id = 0;
    ui->focus_id = hash;
  }
  return changed;
}
void pp_ui_heading(PPUIBackend *ui, const char *text) {
  nk_style_push_font(&ui->context, &ui->heading_font->handle);
  nk_layout_row_dynamic(&ui->context, 28, 1);
  nk_label(&ui->context, text, NK_TEXT_LEFT);
  nk_style_pop_font(&ui->context);
}
void pp_ui_error(PPUIBackend *ui, const char *text) {
  nk_layout_row_dynamic(&ui->context, 38, 1);
  nk_label_wrap(&ui->context, text ? text : "");
}
bool pp_ui_control_rect(const PPUIBackend *ui, const char *id, PPRect *bounds) {
  for (size_t i = 0; i < ui->control_count; i++)
    if (ui->controls[i].id == control_id(id)) {
      struct nk_rect r = ui->controls[i].bounds;
      *bounds = (PPRect){r.x, r.y, r.w, r.h};
      return true;
    }
  return false;
}
bool pp_ui_focused(const PPUIBackend *ui, const char *id) {
  return ui->focus_id == control_id(id);
}
bool pp_ui_upload_frame(PPUIBackend *ui, GLuint *texture, const PPFrame *frame,
                        PPError *error) {
  if (!SDL_GL_MakeCurrent(ui->window, ui->gl_context))
    return fail(error, SDL_GetError());
  size_t size = (size_t)frame->width * frame->height * 4;
  uint8_t *straight = malloc(size);
  if (!straight)
    return fail(error, "Cannot allocate preview upload");
  for (int y = 0; y < frame->height; y++)
    for (int x = 0; x < frame->width; x++) {
      const uint8_t *source = frame->pixels + y * frame->stride + x * 4;
      uint8_t *target = straight + ((size_t)y * frame->width + x) * 4;
      for (int c = 0; c < 3; c++)
        target[c] =
            source[3] ? (uint8_t)fmin(255, source[c] * 255.0 / source[3]) : 0;
      target[3] = source[3];
    }
  if (!*texture)
    glGenTextures(1, texture);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, straight);
  free(straight);
  return glGetError() == GL_NO_ERROR ||
         fail(error, "Cannot upload item preview");
}
bool pp_ui_capture(PPUIBackend *ui, const char *path, PPError *error) {
  if (!SDL_GL_MakeCurrent(ui->window, ui->gl_context))
    return fail(error, SDL_GetError());
  int width, height;
  SDL_GetWindowSizeInPixels(ui->window, &width, &height);
  SDL_Surface *surface =
      SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  uint8_t *pixels = malloc((size_t)width * height * 4);
  if (!surface || !pixels) {
    SDL_DestroySurface(surface);
    free(pixels);
    return fail(error, "Cannot allocate screenshot");
  }
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  for (int y = 0; y < height; y++)
    memcpy((uint8_t *)surface->pixels + y * surface->pitch,
           pixels + (size_t)(height - 1 - y) * width * 4, (size_t)width * 4);
  bool ok = SDL_SaveBMP(surface, path);
  SDL_DestroySurface(surface);
  free(pixels);
  return ok || fail(error, SDL_GetError());
}
