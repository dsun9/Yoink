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
  memset(editor->font_warnings, 0, sizeof(editor->font_warnings));
  if (editor->draft.kind != PP_ITEM_TEXT)
    return;
  const PPTextDiagnostics *prepared=pp_item_snapshot_text_diagnostics(editor->preview);
  if(!prepared)return;
  PPTextDiagnostics diagnostics=*prepared;
  PPAsset *fallback = pp_asset_retain(pp_app_assets(settings->app),
                                      editor->draft.text_style.fallback_asset);
  PPFontInfo info = {0};
  if (fallback)
    pp_font_inspect(pp_asset_source(fallback),
                    editor->draft.text_style.fallback_face, &info, NULL);
  snprintf(editor->diagnostics, sizeof(editor->diagnostics),
           "Requested: %.100s. Fallback: %.100s. Fallback runs: %zu. "
           "Missing glyphs: %zu. Bitmap glyphs: %zu; scalable glyphs: %zu.%s",
           editor->draft.text_style.requested_family,
           fallback ? info.family : "none", diagnostics.fallback_runs,
           diagnostics.missing_glyphs, diagnostics.artwork.bitmap_glyphs,
           diagnostics.artwork.scalable_glyphs,
           diagnostics.default_emoji_fallback
               ? " Bundled Noto supplies emoji because Segoe UI Emoji is unavailable."
               : "");
  if (diagnostics.missing_requested_families)
    snprintf(editor->font_warnings[0], sizeof(editor->font_warnings[0]),
             "Requested font is unavailable. Import that font or choose a matching collection face.");
  if (diagnostics.fallback_runs &&
      (diagnostics.missing_requested_families || diagnostics.missing_glyphs))
    snprintf(editor->font_warnings[1], sizeof(editor->font_warnings[1]),
             "Fallback renders %zu text runs. Import a font with these characters if the appearance is unexpected.",
             diagnostics.fallback_runs);
  if (diagnostics.missing_glyphs)
    snprintf(editor->font_warnings[2], sizeof(editor->font_warnings[2]),
             "%zu missing glyphs. Import a font with these characters or change the text.",
             diagnostics.missing_glyphs);
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
  bool selection_changed=editor->id!=id||editor->generation!=pp_app_generation(settings->app);
  PPAssetId previous=pp_item_snapshot_artwork_id(editor->preview),current=pp_item_snapshot_artwork_id(snapshot);
  bool content_changed=selection_changed||memcmp(&previous,&current,sizeof(current));
  if(content_changed&&!pp_ui_shaped_reset(settings->backend)) {
    pp_item_snapshot_release(snapshot);
    snprintf(editor->content_error,sizeof(editor->content_error),"Cannot bind editor context; retry selection.");
    return;
  }
  if(selection_changed)pp_item_editor_release_preview(settings);
  pp_item_snapshot_release(editor->preview);
  editor->preview = snapshot;
  editor->id = id;
  editor->generation = pp_app_generation(settings->app);
  editor->revision = pp_project_revision(pp_app_project(settings->app));
  editor->content_error[0] = editor->appearance_error[0] =
      editor->font_error[0] = 0;
  if(content_changed)editor->animation_start = SDL_GetTicks() / 1000.0;
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
  snprintf(editor->label, sizeof(editor->label), "%s", editor->draft.label ? editor->draft.label : "");
  editor->draft.label = editor->label;
  editor->draft.text_style.requested_family = editor->family;
  text_diagnostics(settings);
}
void pp_item_editor_validate(PPSettings *settings) {
  PPItemEditor *editor = &settings->editor;
  if (!editor->changed || settings->backend->composing)
    return;
  editor->changed = false;
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
  item.label = editor->label;
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
  PPAssetId previous=pp_item_snapshot_artwork_id(editor->preview);
  pp_item_snapshot_release(editor->preview);
  editor->preview =
      pp_project_snapshot(pp_app_project(settings->app), editor->id);
  PPAssetId current=pp_item_snapshot_artwork_id(editor->preview);
  if(memcmp(&previous,&current,sizeof(current))) {
    editor->animation_start=SDL_GetTicks()/1000.0;
  }
  editor->revision = pp_project_revision(pp_app_project(settings->app));
  editor->preview_dirty = true;
  text_diagnostics(settings);
}
static const char *appearance_error(const PPItemEditor *editor, const char *id) {
  const char *prefix = !strcmp(id, "item-size") ? "Size:"
                     : !strcmp(id, "text-color") ? "Text color:"
                     : !strcmp(id, "item-angle") ? "Angle:"
                     : !strcmp(id, "item-opacity") ? "Opacity:" : "";
  return *prefix && !strncmp(editor->appearance_error, prefix, strlen(prefix))
             ? editor->appearance_error : "";
}
static void field(PPSettings *settings, const char *label, const char *id,
                  char *value, int capacity, bool *changed) {
  PPUIBackend *ui = settings->backend;
  nk_layout_row_dynamic(&ui->context, 20, 1);
  nk_label(&ui->context, label, NK_TEXT_LEFT);
  nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 1);
  *changed =
      pp_ui_edit(ui, id, value, capacity, false, !settings->dialog_pending) ||
      *changed;
  pp_ui_error(ui, appearance_error(&settings->editor, id));
}
static void paired_fields(PPSettings *settings, const char *first_label,
                          const char *first_id, char *first, int first_capacity,
                          const char *second_label, const char *second_id,
                          char *second, int second_capacity, bool *changed) {
  PPUIBackend *ui = settings->backend;
  if (ui->width < 860 || nk_window_get_content_region(&ui->context).w < 460) {
    field(settings, first_label, first_id, first, first_capacity, changed);
    field(settings, second_label, second_id, second, second_capacity, changed);
    return;
  }
  nk_layout_row_dynamic(&ui->context, 20, 2);
  nk_label(&ui->context, first_label, NK_TEXT_LEFT);
  nk_label(&ui->context, second_label, NK_TEXT_LEFT);
  nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 2);
  *changed = pp_ui_edit(ui, first_id, first, first_capacity, false,
                        !settings->dialog_pending) || *changed;
  *changed = pp_ui_edit(ui, second_id, second, second_capacity, false,
                        !settings->dialog_pending) || *changed;
  pp_ui_error(ui, appearance_error(&settings->editor, first_id));
  pp_ui_error(ui, appearance_error(&settings->editor, second_id));
}
static void preview(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  PPItemEditor *editor = &settings->editor;
  nk_layout_row_dynamic(&ui->context, 108, 1);
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
  if (editor->preview) {
    double next;
    PPError error = {0};
    PPVec2 size = pp_item_snapshot_size(editor->preview);
    double longest = fmax(size.x, size.y);
    double fit = fmin((rect.h - 20) / size.y, (rect.w - 28) / size.x);
    fit = fmin(1, fit);
    if(!pp_ui_preview_update(ui,settings->app,&editor->retained,editor->preview,
        longest*fit,editor->draft.angle_deg,editor->draft.opacity,ui->scale,
        fmax(0,now-editor->animation_start),&next,&error))
      goto preview_failed;
    editor->renderer=editor->retained.renderer;
    editor->preview_texture=editor->retained.texture;
    editor->preview_width=editor->retained.width;editor->preview_height=editor->retained.height;
    editor->preview_dirty = false;
    editor->next_frame = editor->animation_start + next;
    goto preview_ready;
  preview_failed:
    snprintf(settings->status, sizeof(settings->status), "Preview: %s",
             error.message);
    editor->next_frame = INFINITY;
    editor->preview_dirty = false;
    SDL_GL_MakeCurrent(ui->window, ui->gl_context);
    editor->renderer=editor->retained.renderer;editor->preview_texture=editor->retained.texture;
    editor->preview_width=editor->retained.width;editor->preview_height=editor->retained.height;
  }
preview_ready:
  if (editor->preview_texture) {
    float w = editor->preview_width / editor->retained.density,
          h = editor->preview_height / editor->retained.density;
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
static int compare_families(const void *a, const void *b) {
  return SDL_strcasecmp(*(const char *const *)a, *(const char *const *)b);
}
static void font_choice(PPSettings *settings, bool *changed) {
  PPUIBackend *ui = settings->backend;
  PPItemEditor *editor = &settings->editor;
  if (!settings->font_options) {
    pp_platform_font_families(&settings->font_families, NULL);
    int count = 1;
    for (size_t offset = 0; offset < settings->font_families.size;)
      { count++; offset += strlen((const char *)settings->font_families.data + offset) + 1; }
    settings->font_options = calloc((size_t)count, sizeof(*settings->font_options));
    if (!settings->font_options) { ui->failed = true; return; }
    settings->font_count = count;
    int index = 1;
    for (size_t offset = 0; offset < settings->font_families.size;) {
      const char *name = (const char *)settings->font_families.data + offset;
      settings->font_options[index++] = name;
      offset += strlen(name) + 1;
    }
    qsort(settings->font_options + 1, (size_t)count - 1, sizeof(*settings->font_options), compare_families);
  }
  settings->font_options[0] = editor->family;
  int selected = 0;
  for (int i = 1; i < settings->font_count; i++)
    if (!SDL_strcasecmp(editor->family, settings->font_options[i])) { selected = i; break; }
  if (!pp_ui_choice(ui, "font-family-choice", settings->font_options, settings->font_count,
                    &selected, !settings->dialog_pending) || selected == 0)
    return;
  const char *family = settings->font_options[selected];
  PPBlob bytes = {0}; PPError error = {0}; PPFontInfo info = {0};
  bool ok = pp_platform_find_font(family, &bytes, &error) &&
            pp_font_inspect((PPBytes){bytes.data, bytes.size}, 0, &info, &error);
  int face = -1;
  size_t faces = info.face_count;
  for (size_t i = 0; ok && i < faces; i++) {
    ok = pp_font_inspect((PPBytes){bytes.data, bytes.size}, (int)i, &info, &error);
    char styled[256];
    snprintf(styled, sizeof(styled), "%s %s", info.family, info.style);
    if (ok && (!SDL_strcasecmp(info.family, family) || !SDL_strcasecmp(styled, family)) && face < 0) face = (int)i;
    if (ok && !info.embeddable) {
      ok = false;
      snprintf(error.message, sizeof(error.message), "This font does not permit portable editable embedding.");
    }
  }
  PPAssetId asset;
  char chosen_family[sizeof(editor->family)] = {0};
  if (ok && face >= 0) {
    ok = pp_font_inspect((PPBytes){bytes.data, bytes.size}, face, &info, &error);
    if (ok) snprintf(chosen_family, sizeof(chosen_family), "%s", info.family);
  }
  if (ok && face >= 0)
    ok = pp_asset_import(pp_app_assets(settings->app), (PPBytes){bytes.data, bytes.size}, PP_ASSET_FONT, &asset, &error);
  else ok = false;
  pp_blob_destroy(&bytes);
  if (!ok) {
    snprintf(editor->font_error, sizeof(editor->font_error), "Font: %.490s",
             error.message[0] ? error.message : "Selected family is unavailable.");
    return;
  }
  editor->draft.text_style.primary_asset = asset;
  editor->draft.text_style.primary_face = face;
  snprintf(editor->family, sizeof(editor->family), "%s", chosen_family);
  snprintf(editor->face, sizeof(editor->face), "%d", face);
  *changed = true;
}
void pp_item_editor(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  PPItemEditor *editor = &settings->editor;
  /* An outside click may cancel composition or dismiss a popup while drawing
   * earlier controls. That same click must not also change this disclosure. */
  bool disclosure_enabled = !settings->dialog_pending && !ui->composing &&
                            !ui->popup_id && !ui->modal;
  pp_item_editor_sync(settings);
  pp_ui_heading(ui, "Selected item");
  if (!editor->preview) {
    nk_layout_row_dynamic(&ui->context, 60, 1);
    nk_label_wrap(&ui->context, "This anchor has no items. Add text or import "
                                "an image to create its first default.");
    return;
  }
  preview(settings);
  if (ui->font_diagnostic[0]) {
    nk_layout_row_dynamic(&ui->context, 48, 1);
    nk_label_wrap(&ui->context, ui->font_diagnostic);
  }
  bool changed = false;
  pp_ui_heading(ui, "Content");
  field(settings, "Label", "item-label", editor->label, sizeof(editor->label), &changed);
  pp_ui_secondary(ui, "Used in the item list and drawer. Leave empty to use the content.");
  if (editor->draft.kind == PP_ITEM_TEXT && editor->text) {
    ui->text_assets=pp_app_assets(settings->app);
    nk_layout_row_dynamic(&ui->context, 20, 1);
    nk_label(&ui->context, "Text / emoji (Unicode, multiline)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(&ui->context, 78, 1);
    changed = pp_ui_edit(ui, "item-text", editor->text, editor->text_capacity,
                         true, !settings->dialog_pending) || changed;
    pp_ui_error(ui, editor->content_error);
    for (size_t i = 0; i < 4; i++)
      pp_ui_error(ui, editor->font_warnings[i]);
    nk_layout_row_dynamic(&ui->context, 20, 1);
    nk_label(&ui->context, "Font", NK_TEXT_LEFT);
    float font_widths[] = {
        fmaxf(1, nk_widget_width(&ui->context) - 112 - ui->context.style.window.spacing.x),
        112};
    nk_layout_row(&ui->context, NK_STATIC, pp_ui_theme.row_height, 2, font_widths);
    font_choice(settings, &changed);
    struct nk_rect import_bounds = nk_widget_bounds(&ui->context);
    if (pp_ui_button(ui, "import-font", "Import font",
                     pp_settings_action_enabled(settings)))
      pp_settings_dialog(settings, PP_DIALOG_IMPORT_FONT, false);
    pp_ui_tooltip(ui, import_bounds, "import-font",
                  "Import a permitted TTF, OTF, TTC or OTC font");
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
      nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 1);
      if (pp_ui_choice(ui, "font-face-choice", options,
                       (int)font_info.face_count, &selected,
                       !settings->dialog_pending)) {
        snprintf(editor->face, sizeof(editor->face), "%d", selected);
        changed = true;
      }
    }
    pp_asset_release(font_asset);
    pp_ui_error(ui, editor->font_error);
    nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 1);
    if (pp_ui_button(ui, "font-details", editor->font_details_open
                         ? "Hide font details" : "Font details",
                     disclosure_enabled))
      settings->font_details_pending = true;
    if (editor->font_details_open) {
      field(settings, "Collection index (0 = first face)", "font-face",
            editor->face, sizeof(editor->face), &changed);
      if (editor->diagnostics[0]) {
        nk_layout_row_dynamic(&ui->context, 52, 1);
        nk_label_wrap(&ui->context, editor->diagnostics);
      }
    }
  } else {
    nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 1);
    if (pp_ui_button(ui, "replace-image", "Replace image (PNG / GIF / SVG)",
                     pp_settings_action_enabled(settings)))
      pp_settings_dialog(settings, PP_DIALOG_IMPORT_IMAGE, false);
  }
  pp_ui_heading(ui, "Appearance");
  if (editor->draft.kind == PP_ITEM_TEXT)
    paired_fields(settings, "Font size (logical pixels)", "item-size",
                  editor->size, sizeof(editor->size), "Color (#RRGGBB)",
                  "text-color", editor->color, sizeof(editor->color), &changed);
  else
    field(settings, "Longest side (logical pixels)", "item-size", editor->size,
          sizeof(editor->size), &changed);
  paired_fields(settings, "Angle (degrees)", "item-angle", editor->angle,
                sizeof(editor->angle), "Opacity (%)", "item-opacity",
                editor->opacity, sizeof(editor->opacity), &changed);
  editor->changed = editor->changed || changed;
  pp_ui_secondary(ui, "Valid edits apply to future pulls.");
}
void pp_item_editor_destroy(PPSettings *settings) {
  pp_ui_shaped_destroy(settings->backend);
  PPItemEditor *editor = &settings->editor;
  pp_item_snapshot_release(editor->preview);
  free(editor->text);
  pp_item_editor_release_preview(settings);
  memset(editor, 0, sizeof(*editor));
}
void pp_item_editor_release_preview(PPSettings *settings) {
  PPItemEditor *editor=&settings->editor;pp_ui_preview_release(&editor->retained);
  editor->renderer=NULL;editor->preview_texture=0;editor->preview_width=editor->preview_height=0;editor->preview_dirty=true;
}
