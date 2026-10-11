#include "settings_internal.h"
#include "shaped_edit.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool pp_ui_number(const char *text, double low, double high, double *out) {
  if (!text || !*text)
    return false;
  char *end;
  errno = 0;
  double value = strtod(text, &end);
  if (*end || errno || !isfinite(value) || value < low || value > high)
    return false;
  *out = value;
  return true;
}
static void text_diagnostics(PPSettings *settings) {
  PPItemEditor *editor = &settings->editor;
  editor->diagnostics[0] = 0;
  if (editor->draft.kind != PP_ITEM_TEXT)
    return;
  PPFrame frame = {0};
  PPTextDiagnostics diagnostics;
  PPError error;
  if (!pp_text_render(pp_app_assets(settings->app), editor->draft.text,
                      &editor->draft.text_style, .5, &frame, &diagnostics,
                      &error)) {
    snprintf(editor->diagnostics, sizeof(editor->diagnostics), "Preview: %s",
             error.message);
    return;
  }
  pp_frame_destroy(&frame);
  PPAsset *fallback = pp_asset_retain(pp_app_assets(settings->app),
                                      editor->draft.text_style.fallback_asset);
  PPFontInfo info = {0};
  if (fallback)
    pp_font_inspect(pp_asset_source(fallback),
                    editor->draft.text_style.fallback_face, &info, NULL);
  snprintf(editor->diagnostics, sizeof(editor->diagnostics),
           "Requested: %.100s. Fallback: %.100s. Used fallback runs: %zu. "
           "Missing glyphs: %zu.%s%s",
           editor->draft.text_style.requested_family,
           fallback ? info.family : "none", diagnostics.fallback_runs,
           diagnostics.missing_glyphs,
           diagnostics.missing_requested_families
               ? " Requested family is unavailable."
               : "",
           diagnostics.default_emoji_fallback
               ? " Bundled Noto is selected because Segoe UI Emoji was unavailable."
               : "");
  pp_asset_release(fallback);
}
void pp_item_editor_sync(PPSettings *settings) {
  PPItemEditor *editor = &settings->editor;
  uint64_t id = pp_app_ui(settings->app)->selected_item;
  PPItemSnapshot *snapshot =
      pp_project_snapshot(pp_app_project(settings->app), id);
  if (editor->id == id &&
      editor->generation == pp_app_generation(settings->app) &&
      snapshot == editor->preview) {
    pp_item_snapshot_release(snapshot);
    return;
  }
  if(!pp_ui_shaped_reset(settings->backend)) {
    pp_item_snapshot_release(snapshot);
    snprintf(editor->content_error,sizeof(editor->content_error),"Cannot bind editor context; retry selection.");
    return;
  }
  pp_item_snapshot_release(editor->preview);
  editor->preview = snapshot;
  editor->id = id;
  editor->generation = pp_app_generation(settings->app);
  editor->revision = pp_project_revision(pp_app_project(settings->app));
  editor->content_error[0] = editor->appearance_error[0] =
      editor->font_error[0] = 0;
  editor->animation_start = SDL_GetTicks() / 1000.0;
  editor->preview_dirty = true;
  if (!snapshot)
    return;
  editor->draft = *pp_item_snapshot_item(snapshot);
  size_t length = editor->draft.text ? strlen(editor->draft.text) : 0;
  size_t capacity = length + 4096;
  if (capacity < 16384)
    capacity = 16384;
  char *text = realloc(editor->text, capacity);
  if (!text) {
    snprintf(editor->content_error, sizeof(editor->content_error),
             "Cannot allocate text editor. Other project data is unchanged.");
    return;
  }
  editor->text = text;
  editor->text_capacity = (int)capacity;
  memcpy(editor->text, editor->draft.text ? editor->draft.text : "",
         length + 1);
  snprintf(editor->family, sizeof(editor->family), "%s",
           editor->draft.text_style.requested_family
               ? editor->draft.text_style.requested_family
               : "");
  snprintf(editor->size, sizeof(editor->size), "%.6g",
           editor->draft.kind == PP_ITEM_TEXT
               ? editor->draft.text_style.font_size_dip
               : editor->draft.image_longest_dip);
  snprintf(editor->angle, sizeof(editor->angle), "%.6g",
           editor->draft.angle_deg);
  snprintf(editor->opacity, sizeof(editor->opacity), "%.6g",
           editor->draft.opacity * 100);
  snprintf(editor->color, sizeof(editor->color), "#%02X%02X%02X",
           editor->draft.text_style.color[0], editor->draft.text_style.color[1],
           editor->draft.text_style.color[2]);
  snprintf(editor->face, sizeof(editor->face), "%d",
           editor->draft.text_style.primary_face);
  editor->draft.text = editor->text;
  editor->draft.text_style.requested_family = editor->family;
  text_diagnostics(settings);
}
static void commit(PPSettings *settings) {
  PPItemEditor *editor = &settings->editor;
  double size, angle, opacity, face;
  editor->appearance_error[0] = editor->content_error[0] =
      editor->font_error[0] = 0;
  if (!pp_ui_number(editor->size, 1, 4096, &size))
    snprintf(editor->appearance_error, sizeof(editor->appearance_error),
             "Size: enter a number from 1 to 4096 logical pixels.");
  else if (!pp_ui_number(editor->angle, -360000, 360000, &angle))
    snprintf(editor->appearance_error, sizeof(editor->appearance_error),
             "Angle: enter a number from -360000 to 360000 degrees.");
  else if (!pp_ui_number(editor->opacity, 0, 100, &opacity))
    snprintf(editor->appearance_error, sizeof(editor->appearance_error),
             "Opacity: enter a percentage from 0 to 100.");
  if (editor->appearance_error[0])
    return;
  PPItem item = editor->draft;
  item.angle_deg = angle;
  item.opacity = opacity / 100;
  if (item.kind == PP_ITEM_IMAGE)
    item.image_longest_dip = size;
  else {
    item.text = editor->text;
    item.text_style.font_size_dip = size;
    if (!pp_ui_number(editor->face, 0, 4095, &face) || floor(face) != face) {
      snprintf(editor->font_error, sizeof(editor->font_error),
               "Font face: enter a whole collection index starting at 0.");
      return;
    }
    PPAsset *font = pp_asset_retain(pp_app_assets(settings->app),
                                    item.text_style.primary_asset);
    PPFontInfo info;
    PPError error;
    if (font &&
        (!pp_font_inspect(pp_asset_source(font), (int)face, &info, &error) ||
         (size_t)face >= info.face_count)) {
      pp_asset_release(font);
      snprintf(editor->font_error, sizeof(editor->font_error),
               "Font face: this index is outside the imported collection.");
      return;
    }
    if (!font && face != 0) {
      snprintf(editor->font_error, sizeof(editor->font_error),
               "Font face: a missing primary font must use index 0.");
      return;
    }
    if (font)
      snprintf(editor->family, sizeof(editor->family), "%s", info.family);
    pp_asset_release(font);
    item.text_style.primary_face = (int)face;
    item.text_style.requested_family = editor->family;
    unsigned r, g, b;
    char extra;
    bool hex_digits = strlen(editor->color) == 7;
    for (size_t i = 1; hex_digits && i < 7; i++)
      hex_digits = isxdigit((unsigned char)editor->color[i]) != 0;
    if (!hex_digits || editor->color[0] != '#' ||
        sscanf(editor->color + 1, "%2x%2x%2x%c", &r, &g, &b, &extra) != 3) {
      snprintf(editor->appearance_error, sizeof(editor->appearance_error),
               "Text color: enter six hexadecimal digits, such as #203547.");
      return;
    }
    item.text_style.color[0] = (uint8_t)r;
    item.text_style.color[1] = (uint8_t)g;
    item.text_style.color[2] = (uint8_t)b;
  }
  PPError error;
  if (!pp_app_command(settings->app,
                      &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                   .item_id = editor->id,
                                   .item = &item},
                      &error)) {
    snprintf(editor->content_error, sizeof(editor->content_error),
             "Content: %s", error.message);
    return;
  }
  editor->draft = item;
  pp_item_snapshot_release(editor->preview);
  editor->preview =
      pp_project_snapshot(pp_app_project(settings->app), editor->id);
  editor->revision = pp_project_revision(pp_app_project(settings->app));
  editor->preview_dirty = true;
  text_diagnostics(settings);
}
static void field(PPSettings *settings, const char *label, const char *id,
                  char *value, int capacity, bool *changed) {
  PPUIBackend *ui = settings->backend;
  nk_layout_row_dynamic(&ui->context, 20, 1);
  nk_label(&ui->context, label, NK_TEXT_LEFT);
  nk_layout_row_dynamic(&ui->context, 32, 1);
  *changed =
      pp_ui_edit(ui, id, value, capacity, false, !settings->dialog_pending) ||
      *changed;
}
static void preview(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  PPItemEditor *editor = &settings->editor;
  nk_layout_row_dynamic(&ui->context, 180, 1);
  struct nk_rect rect;
  nk_widget(&rect, &ui->context);
  struct nk_command_buffer *canvas = nk_window_get_canvas(&ui->context);
  for (int y = 0; y < (int)rect.h; y += 18)
    for (int x = 0; x < (int)rect.w; x += 18)
      nk_fill_rect(canvas,
                   nk_rect(rect.x + x, rect.y + y, fminf(18, rect.w - x),
                           fminf(18, rect.h - y)),
                   0,
                   ((x / 18 + y / 18) % 2) ? pp_ui_theme.canvas
                                           : pp_ui_theme.surface);
  double now = SDL_GetTicks() / 1000.0;
  if (editor->preview && (editor->preview_dirty || now >= editor->next_frame)) {
    const PPFrame *source;
    double next;
    PPError error = {0};
    PPVec2 size = pp_item_snapshot_size(editor->preview);
    double longest = fmax(size.x, size.y);
    double fit = fmin(150.0 / longest, (rect.w - 28) / longest);
    fit = fmin(1, fit);
    if (!editor->renderer && !pp_renderer_create(&editor->renderer, &error))
      goto preview_failed;
    if (!pp_item_snapshot_frame(editor->preview, now - editor->animation_start,
                                fit * ui->scale, &source, &next, &error))
      goto preview_failed;
    PPFrame transformed = {0};
    if (!pp_renderer_transform(editor->renderer, source, longest * fit,
                               editor->draft.angle_deg, editor->draft.opacity,
                               ui->scale, &transformed, &error))
      goto preview_failed;
    bool uploaded =
        pp_ui_upload_frame(ui, &editor->preview_texture, &transformed, &error);
    editor->preview_width = transformed.width;
    editor->preview_height = transformed.height;
    pp_frame_destroy(&transformed);
    if (!uploaded)
      goto preview_failed;
    editor->preview_dirty = false;
    editor->next_frame = editor->animation_start + next;
    goto preview_ready;
  preview_failed:
    snprintf(settings->status, sizeof(settings->status), "Preview: %s",
             error.message);
    editor->next_frame = INFINITY;
    editor->preview_dirty = false;
    SDL_GL_MakeCurrent(ui->window, ui->gl_context);
  }
preview_ready:
  if (editor->preview_texture) {
    float w = editor->preview_width / ui->scale,
          h = editor->preview_height / ui->scale;
    float fit = fminf(1, fminf((rect.w - 12) / w, (rect.h - 12) / h));
    w *= fit;
    h *= fit;
    struct nk_image image = nk_image_id((int)editor->preview_texture);
    nk_draw_image(
        canvas,
        nk_rect(rect.x + (rect.w - w) / 2, rect.y + (rect.h - h) / 2, w, h),
        &image, pp_ui_theme.surface);
  }
}
void pp_item_editor(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  PPItemEditor *editor = &settings->editor;
  pp_item_editor_sync(settings);
  pp_ui_heading(ui, "Item appearance");
  if (!editor->preview) {
    nk_layout_row_dynamic(&ui->context, 60, 1);
    nk_label_wrap(&ui->context, "This anchor has no items. Add text or import "
                                "an image to create its first default.");
    return;
  }
  preview(settings);
  if (editor->diagnostics[0]) {
    nk_layout_row_dynamic(&ui->context, 64, 1);
    nk_label_wrap(&ui->context, editor->diagnostics);
  }
  if (ui->font_diagnostic[0]) {
    nk_layout_row_dynamic(&ui->context, 48, 1);
    nk_label_wrap(&ui->context, ui->font_diagnostic);
  }
  bool changed = false;
  if (editor->draft.kind == PP_ITEM_TEXT && editor->text) {
    ui->text_assets=pp_app_assets(settings->app);
    nk_layout_row_dynamic(&ui->context, 20, 1);
    nk_label(&ui->context, "Text / emoji (Unicode, multiline)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(&ui->context, 78, 1);
    changed = pp_ui_edit(ui, "item-text", editor->text, editor->text_capacity,
                         true, !settings->dialog_pending);
    pp_ui_error(ui, editor->content_error);
    nk_layout_row_dynamic(&ui->context, 24, 1);
    char font[300];
    snprintf(font, sizeof(font), "Font: %s", editor->family);
    nk_label(&ui->context, font, NK_TEXT_LEFT);
    nk_layout_row_dynamic(&ui->context, 32, 1);
    if (pp_ui_button(ui, "import-font", "Import font (TTF / OTF / TTC / OTC)",
                     !settings->dialog_pending &&
                         !pp_settings_invalid(settings)))
      pp_settings_dialog(settings, PP_DIALOG_IMPORT_FONT, false);
    field(settings, "Collection face (0 = first face)", "font-face",
          editor->face, sizeof(editor->face), &changed);
    PPAsset *font_asset = pp_asset_retain(
        pp_app_assets(settings->app), editor->draft.text_style.primary_asset);
    PPFontInfo font_info;
    if (font_asset &&
        pp_font_inspect(pp_asset_source(font_asset), 0, &font_info, NULL) &&
        font_info.face_count > 1 && font_info.face_count <= 64) {
      char names[64][160];
      const char *options[64];
      for (size_t i = 0; i < font_info.face_count; i++) {
        PPFontInfo face_info;
        if (pp_font_inspect(pp_asset_source(font_asset), (int)i, &face_info,
                            NULL))
          snprintf(names[i], sizeof(names[i]), "%zu: %.100s", i,
                   face_info.family);
        else
          snprintf(names[i], sizeof(names[i]), "%zu: unavailable", i);
        options[i] = names[i];
      }
      int selected = editor->draft.text_style.primary_face;
      nk_layout_row_dynamic(&ui->context, 32, 1);
      if (pp_ui_choice(ui, "font-face-choice", options,
                       (int)font_info.face_count, &selected,
                       !settings->dialog_pending)) {
        snprintf(editor->face, sizeof(editor->face), "%d", selected);
        changed = true;
      }
    }
    pp_asset_release(font_asset);
    pp_ui_error(ui, editor->font_error);
    field(settings, "Text color (#RRGGBB; color emoji keep their colors)",
          "text-color", editor->color, sizeof(editor->color), &changed);
  } else {
    nk_layout_row_dynamic(&ui->context, 32, 1);
    if (pp_ui_button(ui, "replace-image", "Replace image (PNG / GIF / SVG)",
                     !settings->dialog_pending &&
                         !pp_settings_invalid(settings)))
      pp_settings_dialog(settings, PP_DIALOG_IMPORT_IMAGE, false);
  }
  field(settings,
        editor->draft.kind == PP_ITEM_TEXT ? "Font size (logical pixels)"
                                           : "Longest side (logical pixels)",
        "item-size", editor->size, sizeof(editor->size), &changed);
  field(settings, "Angle (degrees)", "item-angle", editor->angle,
        sizeof(editor->angle), &changed);
  field(settings, "Opacity (%)", "item-opacity", editor->opacity,
        sizeof(editor->opacity), &changed);
  if (changed && !ui->composing)
    commit(settings);
  pp_ui_error(ui, editor->appearance_error);
  nk_layout_row_dynamic(&ui->context, 24, 1);
  nk_label(&ui->context, "Valid edits apply immediately to future pulls.",
           NK_TEXT_LEFT);
}
void pp_item_editor_destroy(PPSettings *settings) {
  pp_ui_shaped_destroy(settings->backend);
  PPItemEditor *editor = &settings->editor;
  pp_item_snapshot_release(editor->preview);
  free(editor->text);
  if (editor->preview_texture &&
      SDL_GL_MakeCurrent(settings->backend->window,
                         settings->backend->gl_context))
    glDeleteTextures(1, &editor->preview_texture);
  pp_renderer_destroy(editor->renderer);
  memset(editor, 0, sizeof(*editor));
}
