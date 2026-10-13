#include "settings_internal.h"
#include "shaped_edit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *anchor_names[PP_ANCHOR_COUNT] = {
    "Top left",     "Top center",  "Top right",     "Center left",
    "Center right", "Bottom left", "Bottom center", "Bottom right"};
static void release_row_thumbnails(PPSettings *settings, bool all) {
  for (size_t i = 0; i < 8; i++) {
    if (all || !settings->row_thumbnail_used[i]) {
      pp_ui_preview_release(&settings->row_thumbnails[i]);
      settings->row_thumbnail_ids[i] = 0;
    }
  }
}
static void row_label(PPUIBackend *ui, const PPItem *item, char *label, size_t capacity) {
  if (item->kind == PP_ITEM_IMAGE) {
    snprintf(label, capacity, "Image %llu", (unsigned long long)item->id);
    return;
  }
  size_t length = 0;
  label[0] = 0;
  for (const char *p = item->text; p && *p && length + 12 < capacity;) {
    nk_rune rune;
    int used = nk_utf_decode(p, &rune, (int)strlen(p));
    if (used <= 0) break;
    const struct nk_font_glyph *glyph = nk_font_find_glyph(ui->body_font, rune);
    if (rune <= ' ') {
      if (length && label[length - 1] != ' ') label[length++] = ' ';
    } else if (glyph && glyph->codepoint == rune) {
      memcpy(label + length, p, (size_t)used);
      length += (size_t)used;
    } else {
      length += (size_t)snprintf(label + length, capacity - length, "U+%04X", (unsigned)rune);
    }
    p += used;
  }
  while (length && label[length - 1] == ' ') length--;
  label[length] = 0;
  if (!length) snprintf(label, capacity, "Text %llu", (unsigned long long)item->id);
}
static void row_thumbnail(PPSettings *settings, const PPItem *item, struct nk_rect bounds) {
  PPUIBackend *ui = settings->backend;
  struct nk_rect clip = ui->context.current->layout->clip;
  if (bounds.y + bounds.h <= clip.y || bounds.y >= clip.y + clip.h ||
      bounds.x + bounds.w <= clip.x || bounds.x >= clip.x + clip.w)
    return;
  size_t slot = 8;
  for (size_t i = 0; i < 8; i++)
    if (settings->row_thumbnail_ids[i] == item->id) { slot = i; break; }
  if (slot == 8) {
    for (size_t i = 0; i < 8; i++)
      if (!settings->row_thumbnail_ids[i]) { slot = i; break; }
  }
  if (slot == 8) {
    for (size_t i = 0; i < 8; i++)
      if (!settings->row_thumbnail_used[i]) { slot = i; break; }
  }
  if (slot == 8) return;
  /* A slot referenced by this frame is never recycled. Unused consumers are
   * released only after the queued Nuklear frame finishes. */
  settings->row_thumbnail_used[slot] = true;
  settings->row_thumbnail_ids[slot] = item->id;
  PPUIContentPreview *preview = &settings->row_thumbnails[slot];
  PPItemSnapshot *snapshot = pp_project_snapshot(pp_app_project(settings->app), item->id);
  PPError error = {0}; double next;
  if (!pp_ui_preview_update(ui, settings->app, preview, snapshot, 32, 0, 1,
                            ui->scale, 0, &next, &error))
    snprintf(settings->status, sizeof(settings->status), "Thumbnail: %.490s", error.message);
  pp_item_snapshot_release(snapshot);
  if (preview->texture && preview->width && preview->height) {
    float factor = fminf(bounds.w / preview->width, bounds.h / preview->height);
    float width = preview->width * factor, height = preview->height * factor;
    nk_draw_image(nk_window_get_canvas(&ui->context),
        nk_rect(bounds.x + (bounds.w - width) / 2, bounds.y + (bounds.h - height) / 2, width, height),
        &(struct nk_image){.handle = nk_handle_id((int)preview->texture)}, nk_rgba(255, 255, 255, 255));
  }
}
static void row_content(PPSettings *settings, const PPItem *item, const char *label,
                        struct nk_rect bounds, bool selected) {
  PPUIBackend *ui = settings->backend;
  row_thumbnail(settings, item, nk_rect(bounds.x + 5, bounds.y + 6, 32, 32));
  struct nk_user_font *font = &ui->utility_font->handle;
  struct nk_rect text = nk_rect(bounds.x + 44, bounds.y + (bounds.h - font->height) / 2,
                                fmaxf(0, bounds.w - 50), font->height);
  char display[128];
  snprintf(display, sizeof(display), "%s", label);
  int length = (int)strlen(label);
  bool truncated = font->width(font->userdata, font->height, label, length) > text.w;
  float suffix_width = truncated ? font->width(font->userdata, font->height, "...", 3) : 0;
  while (length && font->width(font->userdata, font->height, label, length) + suffix_width > text.w) {
    length--;
    while (length && ((unsigned char)label[length] & 0xc0) == 0x80) length--;
  }
  if (truncated && text.w >= suffix_width) {
    memcpy(display + length, "...", 4);
    length += 3;
  }
  nk_draw_text(nk_window_get_canvas(&ui->context), text, display, length, font,
               selected ? pp_ui_theme.selection : pp_ui_theme.field,
               selected ? pp_ui_theme.accent_hover : pp_ui_theme.ink);
}
bool pp_settings_invalid(const PPSettings *settings) {
  return settings->backend->composing || settings->name_error[0] ||
         settings->interaction_error[0] || settings->editor.content_error[0] ||
         settings->editor.appearance_error[0] || settings->editor.font_error[0];
}
void pp_settings_revert(PPSettings *settings) {
  settings->name_error[0] = settings->interaction_error[0] = 0;
  settings->interaction_changed = settings->editor.changed = false;
  settings->editor.id = 0;
  snprintf(settings->project_name, sizeof(settings->project_name), "%s",
           pp_app_name(settings->app));
  const PPProjectSettings *s =
      pp_project_settings(pp_app_project(settings->app));
  snprintf(settings->activation, sizeof(settings->activation), "%.6g",
           s->activation_dip);
  snprintf(settings->rotation, sizeof(settings->rotation), "%.6g",
           s->rotation_step_deg);
  snprintf(settings->resize, sizeof(settings->resize), "%.6g",
           (s->resize_factor - 1) * 100);
  snprintf(settings->easing, sizeof(settings->easing), "%.6g",
           s->easing_seconds * 1000);
  pp_item_editor_sync(settings);
}
bool pp_settings_create(PPSettings **out, PPApp *app, PPError *error) {
  return pp_settings_create_scaled(out, app, 0, error);
}
bool pp_settings_create_scaled(PPSettings **out, PPApp *app, float scale,
                               PPError *error) {
  if (out)
    *out = NULL;
  if (!out || !app) {
    if (error) {
      error->code = PP_ERROR_ARGUMENT;
      snprintf(error->message, sizeof(error->message), "Settings requires an app and output pointer");
    }
    return false;
  }
  PPSettings *settings = calloc(1, sizeof(*settings));
  if (!settings) {
    if (error) {
      error->code = PP_ERROR_MEMORY;
      snprintf(error->message, sizeof(error->message), "Cannot allocate Settings");
    }
    return false;
  }
  settings->app = app;
  PPUIWindowOptions options = {"Yoink Settings",
                               pp_app_ui(app)->geometry, scale, true};
  if (!pp_ui_backend_create(&settings->backend, &options, error)) {
    free(settings);
    return false;
  }
  settings->backend->content_sources=pp_app_content_sources(app,error);
  settings->backend->text_assets=pp_app_assets(app);
  if(!settings->backend->content_sources){pp_ui_backend_destroy(settings->backend);free(settings);return false;}
  settings->generation = pp_app_generation(app);
  pp_settings_revert(settings);
  pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SHOW_SETTINGS});
  pp_platform_settings_window(pp_settings_window_id(settings));
  snprintf(settings->status, sizeof(settings->status), "%s",
           pp_app_diagnostic(app));
  *out = settings;
  return true;
}
uint32_t pp_settings_window_id(const PPSettings *settings) {
  return settings ? pp_ui_backend_window_id(settings->backend) : 0;
}
static void geometry(PPSettings *settings) {
  int x, y, width, height;
  SDL_GetWindowPosition(settings->backend->window, &x, &y);
  SDL_GetWindowSize(settings->backend->window, &width, &height);
  PPCommand command = {.kind = PP_COMMAND_SET_GEOMETRY,
                       .geometry = {x, y, width / settings->backend->window_scale,
                                    height / settings->backend->window_scale}};
  pp_settings_command(settings, command);
}
void pp_settings_close(PPSettings *settings) {
  geometry(settings);
  pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_CLOSE_SETTINGS});
  pp_ui_shaped_suspend(settings->backend);
  if (!settings->rendering)
    pp_item_editor_release_preview(settings);
  if (!settings->rendering)
    release_row_thumbnails(settings, true);
}
void pp_settings_event(PPSettings *settings, const PPPlatformEvent *event) {
  if (!settings || !event ||
      event->window_id != pp_settings_window_id(settings))
    return;
  pp_ui_backend_event(settings->backend, event);
  if (event->kind == PP_EVENT_WINDOW_CLOSE) {
    if (settings->dialog_pending)
      return;
    pp_project_request_action(settings, PP_PROJECT_ACTION_CLOSE);
    return;
  }
  if (event->kind != PP_EVENT_KEY_DOWN || event->repeat ||
      settings->backend->composing || settings->dialog_pending ||
      pp_app_ui(settings->app)->transition_pending ||
      settings->remove_confirmation)
    return;
  if ((event->modifiers & PP_MOD_CONTROL) &&
      (event->key == PP_KEY_S || event->key == PP_KEY_N ||
       event->key == PP_KEY_O)) {
    if (event->key == PP_KEY_S) {
      pp_project_request_action(settings, event->modifiers & PP_MOD_SHIFT
                                              ? PP_PROJECT_ACTION_SAVE_AS
                                              : PP_PROJECT_ACTION_SAVE);
    } else if (event->key == PP_KEY_N)
      pp_project_request_action(settings, PP_PROJECT_ACTION_NEW);
    else
      pp_project_request_action(settings, PP_PROJECT_ACTION_OPEN);
  }
}
static void anchor_selector(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  struct nk_context *ctx = &ui->context;
  PPAnchor selected = pp_app_ui(settings->app)->anchor;
  bool enabled = !settings->dialog_pending && !pp_settings_invalid(settings);
  pp_ui_heading(ui, "Pull from this position");
  pp_ui_secondary(ui, "Shared on every display");
  const int positions[] = {0, 1, 2, 3, -1, 4, 5, 6, 7};
  for (int row = 0; row < 3; row++) {
    nk_layout_row_dynamic(ctx, 40, 3);
    for (int col = 0; col < 3; col++) {
      int anchor = positions[row * 3 + col];
      if (anchor < 0) {
        struct nk_rect rect;
        nk_widget(&rect, ctx);
        pp_ui_draw_icon(nk_window_get_canvas(ctx), rect, PP_UI_ICON_MONITOR,
                         pp_ui_theme.muted);
      } else {
        char id[32];
        snprintf(id, sizeof(id), "anchor-%d", anchor);
        if (pp_ui_icon_button(ui, id, (PPUIIcon)(PP_UI_ICON_NORTHWEST + anchor),
                              anchor_names[anchor], anchor_names[anchor],
                              enabled, selected == anchor))
          pp_settings_command(settings,
                              (PPCommand){.kind = PP_COMMAND_SELECT_ANCHOR,
                                          .anchor = (PPAnchor)anchor});
      }
    }
  }
  selected = pp_app_ui(settings->app)->anchor;
  const PPAnchorList *list =
      pp_project_anchor(pp_app_project(settings->app), selected);
  nk_layout_row_dynamic(ctx, 28, 1);
  nk_label(ctx, anchor_names[selected], NK_TEXT_LEFT);
  nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 1);
  bool list_enabled = list->enabled;
  if (pp_ui_switch(ui, "anchor-enabled", "Enabled", &list_enabled, enabled))
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SET_ENABLED,
                                              .anchor = selected,
                                              .enabled = list_enabled});
}
static void ordered_items(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  struct nk_context *ctx = &ui->context;
  const PPAppUIState *state = pp_app_ui(settings->app);
  const PPAnchorList *list =
      pp_project_anchor(pp_app_project(settings->app), state->anchor);
  bool enabled = !settings->dialog_pending && !pp_settings_invalid(settings);
  if (enabled && !ui->popup_id && ui->navigation) {
    for (size_t i = 0; i < list->count; i++) {
      char id[48];
      snprintf(id, sizeof(id), "item-%llu",
               (unsigned long long)list->item_ids[i]);
      if (!pp_ui_focused(ui, id))
        continue;
      size_t next = i;
      if (ui->navigation == PP_KEY_UP && i)
        next = i - 1;
      if (ui->navigation == PP_KEY_DOWN && i + 1 < list->count)
        next = i + 1;
      if (ui->navigation == PP_KEY_HOME)
        next = 0;
      if (ui->navigation == PP_KEY_END)
        next = list->count - 1;
      if (next != i) {
        pp_settings_command(settings,
                            (PPCommand){.kind = PP_COMMAND_SELECT_ITEM,
                                        .item_id = list->item_ids[next]});
        snprintf(id, sizeof(id), "item-%llu",
                 (unsigned long long)list->item_ids[next]);
        pp_ui_focus(ui, id);
        ui->navigation = PP_KEY_NONE;
      }
      break;
    }
  }
  pp_ui_heading(ui, "Ordered items");
  nk_layout_row_dynamic(ctx, 180, 1);
  struct nk_rect list_viewport = nk_widget_bounds(ctx);
  size_t first_control = ui->control_count;
  struct nk_rect parent_clip = ctx->current->layout->clip;
  bool offscreen = list_viewport.y >= parent_clip.y + parent_clip.h ||
                   list_viewport.y + list_viewport.h <= parent_clip.y;
  nk_flags group_flags = NK_WINDOW_BORDER |
                         (ctx->current->layout->flags & (NK_WINDOW_ROM | NK_WINDOW_NO_INPUT));
  /* Nuklear skips fully clipped groups, which would remove every row from the
   * keyboard registry. MOVABLE bypasses that early return, and ROM prevents
   * movement during begin. Restore the intended flags before drawing widgets;
   * the group retains its actual clipped viewport and pointer eligibility. */
  if (nk_group_begin(ctx, "anchor-items", group_flags |
                            (offscreen ? NK_WINDOW_MOVABLE | NK_WINDOW_ROM : 0))) {
    ctx->current->layout->flags = group_flags;
    if (!list->count) {
      nk_layout_row_dynamic(ctx, 48, 1);
      nk_label_wrap(ctx, "No items. Add text or import an image below.");
    }
    for (size_t i = 0; i < list->count; i++) {
      const PPItem *item =
          pp_project_item(pp_app_project(settings->app), list->item_ids[i]);
      char label[128], id[48], default_id[48], delete_id[48];
      row_label(ui, item, label, sizeof(label));
      snprintf(id, sizeof(id), "item-%llu", (unsigned long long)item->id);
      snprintf(default_id, sizeof(default_id), "item-default-%llu", (unsigned long long)item->id);
      snprintf(delete_id, sizeof(delete_id), "item-delete-%llu", (unsigned long long)item->id);
      float width = nk_window_get_content_region(ctx).w;
      float widths[] = {fmaxf(40, width - 56 - 2 * ctx->style.window.spacing.x), 28, 28};
      nk_layout_row(ctx, NK_STATIC, 44, 3, widths);
      struct nk_rect row = nk_widget_bounds(ctx);
      struct nk_rect content = row;
      row.w = width;
      bool actions = (nk_input_is_mouse_hovering_rect(&ctx->input, row) &&
                      nk_input_is_mouse_hovering_rect(&ctx->input, ctx->current->layout->clip)) ||
                     (ui->keyboard_focus && (pp_ui_focused(ui, id) ||
                      pp_ui_focused(ui, default_id) || pp_ui_focused(ui, delete_id)));
      if (pp_ui_select(ui, id, "", item->id == state->selected_item,
                       enabled))
        pp_settings_command(
            settings,
            (PPCommand){.kind = PP_COMMAND_SELECT_ITEM, .item_id = item->id});
      if (ui->control_count)
        snprintf(ui->controls[ui->control_count - 1].label, sizeof(ui->controls[0].label), "%s", label);
      row_content(settings, item, label, content, item->id == state->selected_item);
      if (content.y >= ctx->current->layout->clip.y &&
          content.y + content.h <= ctx->current->layout->clip.y + ctx->current->layout->clip.h)
        pp_ui_tooltip(ui, content, id, label);
      if (actions) {
        if (pp_ui_icon_button(ui, default_id, item->id == list->default_id ? PP_UI_ICON_STAR : PP_UI_ICON_STAR_OUTLINE,
                              "Default", "Default for future pulls", enabled, false))
          pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SET_DEFAULT,
                                .anchor = state->anchor, .item_id = item->id});
        if (pp_ui_icon_button(ui, delete_id, PP_UI_ICON_DELETE, "Delete", "Delete this item", enabled, false))
          pp_project_request_remove(settings, state->anchor, item->id);
        /* Focus reveal settles on the following frame. Do not anchor action
         * help to an offscreen row while either scroll owner is moving it. */
        if (ui->keyboard_focus && (pp_ui_focused(ui, default_id) || pp_ui_focused(ui, delete_id)) &&
            (row.y < ctx->current->layout->clip.y ||
             row.y + row.h > ctx->current->layout->clip.y + ctx->current->layout->clip.h)) {
          ui->tooltip_text[0] = 0;
          ui->tooltip_focus = false;
        }
      } else {
        struct nk_rect bounds;
        nk_widget(&bounds, ctx);
        if (item->id == list->default_id)
          pp_ui_draw_icon(nk_window_get_canvas(ctx), bounds, PP_UI_ICON_STAR, pp_ui_theme.accent);
        nk_widget(&bounds, ctx);
      }
    }
    nk_group_end(ctx);
  }
  pp_ui_reveal_focus(ui, "anchor-items", list_viewport, first_control);
  size_t selected_index = SIZE_MAX;
  for (size_t i = 0; i < list->count; i++)
    if (list->item_ids[i] == state->selected_item)
      selected_index = i;
  nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 2);
  if (pp_ui_button(ui, "add-text", "Add text", enabled)) {
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_ADD_TEXT,
                                              .anchor = state->anchor});
    return; /* Project accessors above are borrowed through mutation only. */
  }
  if (pp_ui_button(ui, "add-image", "Add image", enabled))
    pp_settings_dialog(settings, PP_DIALOG_IMPORT_IMAGE, true);
  nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 2);
  if (pp_ui_button(ui, "move-up", "Move up",
                   enabled && selected_index > 0 &&
                       selected_index < list->count)) {
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_REORDER_ITEM,
                                              .anchor = state->anchor,
                                              .item_id = state->selected_item,
                                              .index = selected_index - 1});
    return;
  }
  if (pp_ui_button(ui, "move-down", "Move down",
                   enabled && selected_index < list->count &&
                       selected_index + 1 < list->count)) {
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_REORDER_ITEM,
                                              .anchor = state->anchor,
                                              .item_id = state->selected_item,
                                              .index = selected_index + 1});
    return;
  }
  nk_layout_row_dynamic(ctx, 40, 1);
  nk_label_wrap(ctx, "Selecting an item leaves the default unchanged. Make "
                     "default chooses what future pulls create.");
}
static void interaction_field(PPSettings *settings, const char *label,
                              const char *id, char *text, int capacity,
                              bool *changed) {
  nk_layout_row_dynamic(&settings->backend->context, 20, 1);
  nk_label(&settings->backend->context, label, NK_TEXT_LEFT);
  nk_layout_row_dynamic(&settings->backend->context, pp_ui_theme.row_height, 1);
  *changed = pp_ui_edit(settings->backend, id, text, capacity, false,
                        !settings->dialog_pending) ||
             *changed;
}
static void interaction(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  pp_ui_heading(ui, "Interaction on every display");
  bool changed = false;
  interaction_field(settings, "Activation region (logical pixels)",
                    "activation", settings->activation,
                    sizeof(settings->activation), &changed);
  interaction_field(settings, "Rotation per wheel notch (degrees)", "rotation",
                    settings->rotation, sizeof(settings->rotation), &changed);
  interaction_field(settings, "Resize per wheel notch (%)", "resize",
                    settings->resize, sizeof(settings->resize), &changed);
  interaction_field(settings, "Easing duration (milliseconds)", "easing",
                    settings->easing, sizeof(settings->easing), &changed);
  settings->interaction_changed = settings->interaction_changed || changed;
  pp_ui_error(ui, settings->interaction_error);
}
static void validate_interaction(PPSettings *settings) {
  if (settings->interaction_changed && !settings->backend->composing) {
    settings->interaction_changed = false;
    PPProjectSettings values;
    if (!pp_ui_number(settings->activation, 1, 4096, &values.activation_dip))
      snprintf(settings->interaction_error, sizeof(settings->interaction_error),
               "Activation: enter 1–4096 logical pixels.");
    else if (!pp_ui_number(settings->rotation, .001, 360,
                           &values.rotation_step_deg))
      snprintf(settings->interaction_error, sizeof(settings->interaction_error),
               "Rotation: enter more than 0 and at most 360 degrees.");
    else if (!pp_ui_number(settings->resize, .001, 100, &values.resize_factor))
      snprintf(settings->interaction_error, sizeof(settings->interaction_error),
               "Resize: enter more than 0 and at most 100 percent.");
    else if (!pp_ui_number(settings->easing, 0, 10000, &values.easing_seconds))
      snprintf(settings->interaction_error, sizeof(settings->interaction_error),
               "Easing: enter 0–10000 milliseconds.");
    else {
      values.resize_factor = 1 + values.resize_factor / 100;
      values.easing_seconds /= 1000;
      settings->interaction_error[0] = 0;
      pp_settings_command(settings,
                          (PPCommand){.kind = PP_COMMAND_SET_INTERACTION,
                                      .interaction = values});
    }
  }
}
static void navigation(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  bool enabled = !settings->dialog_pending && !ui->composing;
  nk_layout_row_dynamic(&ui->context, pp_ui_theme.row_height, 2);
  bool item_errors = settings->editor.content_error[0] ||
                     settings->editor.appearance_error[0] ||
                     settings->editor.font_error[0];
  if (pp_ui_select(ui, "tab-items", item_errors ? "Items !" : "Items",
                   settings->page == PP_SETTINGS_ITEMS,
                   enabled)) {
    settings->requested_page = PP_SETTINGS_ITEMS;
    settings->page_pending = true;
  }
  if (pp_ui_select(ui, "tab-interaction", settings->interaction_error[0]
                                             ? "Interaction !" : "Interaction",
                   settings->page == PP_SETTINGS_INTERACTION, enabled)) {
    settings->requested_page = PP_SETTINGS_INTERACTION;
    settings->page_pending = true;
  }
}
bool pp_settings_can_transition(PPSettings *settings) {
  if (!settings) return true;
  if (settings->dialog_pending || settings->failed || pp_settings_invalid(settings)) {
    snprintf(settings->status,sizeof(settings->status),"Correct or revert the fields, and close any file chooser, before continuing.");
    return false;
  }
  return true;
}
bool pp_settings_recover(PPSettings *settings, PPError *error) {
  if (!settings || !settings->failed)
    return true;
  for (;;) {
    /* Retain the entire editor and Nuklear input batch. A transient context or
     * allocation error can be retried without discarding pending draft text. */
    settings->failed = settings->backend->failed = false;
    pp_settings_command(settings, (PPCommand){.kind=PP_COMMAND_SHOW_SETTINGS});
    pp_settings_draw(settings);
    if (!settings->failed) {
      snprintf(settings->status, sizeof(settings->status),
               "Settings recovered. Earlier rendering failure: %.430s",
               settings->render_failure);
      return true;
    }
    bool can_save = !settings->dialog_pending &&
                    !settings->backend->input_started &&
                    settings->pending_action == PP_PROJECT_ACTION_NONE &&
                    !pp_settings_invalid(settings);
    char message[1600];
    snprintf(message, sizeof(message),
             "Settings could not render: %.500s\n\n"
             "Your project and pending drafts are still in memory. Retry Settings "
             "to try the retained editor again, or Cancel to keep running.\n\n"
             "Quit without saving discards unsaved project changes AND pending "
             "editor drafts. %s\n\n%.400s",
             settings->render_failure,
             can_save ? "Save project and quit saves the validated project first."
                      : "Saving is unavailable until pending input/invalid drafts "
                        "are resolved in Settings and any file chooser is closed.",
             settings->status);
    SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Retry Settings"},
        {0, 2, "Quit without saving"},
        {0, 3, "Save project and quit"}};
    SDL_MessageBoxData box = {.flags=SDL_MESSAGEBOX_ERROR,
                             .title="Yoink Settings recovery",
                             .message=message, .numbuttons=can_save ? 4 : 3,
                             .buttons=buttons};
    int choice=0;
    if (!SDL_ShowMessageBox(&box, &choice)) {
      if(error) *error=(PPError){PP_ERROR_PLATFORM,"Cannot show Settings recovery choices; project remains in memory"};
      return false;
    }
    if (choice==1)
      continue; /* One new draw only after an explicit Retry choice. */
    if (choice!=2 && !(choice==3 && can_save))
      return false;
    /* Replace a previous New/Open/Quit confirmation with the explicit native
     * shutdown choice. Cancelling that old transition never changes project data. */
    if (pp_app_ui(settings->app)->transition_pending)
      pp_settings_command(settings,(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,
                                               .choice=PP_UNSAVED_CANCEL});
    if (!pp_settings_command(settings,(PPCommand){.kind=PP_COMMAND_QUIT}))
      return false;
    if (pp_app_ui(settings->app)->transition_pending &&
        !pp_settings_command(settings,(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,
            .choice=choice==3 ? PP_UNSAVED_SAVE : PP_UNSAVED_DISCARD}))
      return false;
    if (pp_app_ui(settings->app)->needs_save_path) {
      settings->save_for_transition=true;
      pp_settings_dialog(settings,PP_DIALOG_SAVE_PROJECT,false);
    }
    return false;
  }
}
void pp_settings_draw(PPSettings *settings) {
  if (!settings)
    return;
  pp_platform_poll_dialogs();
  if (settings->failed)
    return;
  if (settings->generation != pp_app_generation(settings->app)) {
    release_row_thumbnails(settings, true);
    pp_item_editor_release_preview(settings);
    settings->generation = pp_app_generation(settings->app);
    pp_settings_revert(settings);
  }
  if (!pp_app_ui(settings->app)->settings_open) {
    pp_ui_shaped_suspend(settings->backend);
    pp_item_editor_release_preview(settings);
    release_row_thumbnails(settings, true);
    pp_ui_show(settings->backend, false);
    return;
  }
  PPUIBackend *ui = settings->backend;
  PPError error = {0};
  float previous_scale = ui->scale;
  if (!pp_ui_begin(ui, &error))
    goto failed;
  settings->rendering = true;
  memset(settings->row_thumbnail_used, 0, sizeof(settings->row_thumbnail_used));
  if (ui->scale != previous_scale)
    settings->editor.preview_dirty = true;
  bool modal = pp_app_ui(settings->app)->transition_pending ||
               settings->remove_confirmation;
  pp_ui_modal(ui, modal);
  nk_flags flags = NK_WINDOW_NO_SCROLLBAR | (modal ? NK_WINDOW_NO_INPUT : 0);
  struct nk_context *ctx = &ui->context;
  settings->drawing_fields = true;
  /* Stable Nuklear window ID; the native title carries the public app name. */
  if (nk_begin(ctx, "PointyPointer", nk_rect(0, 0, ui->width, ui->height),
               flags)) {
    pp_project_actions(settings);
    navigation(settings);
    bool item_errors = settings->editor.content_error[0] ||
                       settings->editor.appearance_error[0] ||
                       settings->editor.font_error[0];
    bool page_errors = item_errors || settings->interaction_error[0];
    struct nk_panel *panel = nk_window_get_panel(ctx);
    float body_top = panel->at_y + panel->row.height;
    float body_height = fmaxf(100, ui->height - body_top - 104 -
                                     (page_errors ? 38 : 0));
    nk_layout_row_dynamic(ctx, body_height, 1);
    struct nk_rect content_viewport = nk_widget_bounds(ctx);
    size_t first_control = ui->control_count;
    const char *content_group = settings->page == PP_SETTINGS_ITEMS
                                    ? "items-content" : "interaction-content";
    if (nk_group_begin(ctx, content_group, 0)) {
      if (settings->page == PP_SETTINGS_INTERACTION) {
        pp_ui_shaped_suspend(ui);
        interaction(settings);
      } else if (ui->width >= 860) {
        float widths[] = {300, nk_window_get_content_region(ctx).w - 312};
        nk_layout_row(ctx, NK_STATIC, fmaxf(body_height, settings->items_height),
                      2, widths);
        float content_height = 0;
        if (nk_group_begin(ctx, "position-column", NK_WINDOW_NO_SCROLLBAR)) {
          float top = nk_window_get_panel(ctx)->at_y;
          anchor_selector(settings);
          ordered_items(settings);
          struct nk_panel *panel = nk_window_get_panel(ctx);
          content_height = panel->at_y + panel->row.height - top +
                           2 * ctx->style.window.group_padding.y;
          nk_group_end(ctx);
        }
        if (nk_group_begin(ctx, "editor-column", NK_WINDOW_NO_SCROLLBAR)) {
          float top = nk_window_get_panel(ctx)->at_y;
          pp_item_editor(settings);
          struct nk_panel *panel = nk_window_get_panel(ctx);
          content_height = fmaxf(content_height,
                                panel->at_y + panel->row.height - top +
                                    2 * ctx->style.window.group_padding.y);
          nk_group_end(ctx);
        }
        settings->items_height = content_height;
      } else {
        anchor_selector(settings);
        ordered_items(settings);
        pp_item_editor(settings);
      }
      nk_group_end(ctx);
    }
    pp_ui_reveal_focus(ui, content_group, content_viewport, first_control);
    nk_layout_row_dynamic(ctx, 42, 1);
    const char *status =
        pp_settings_invalid(settings)
            ? "Correct the highlighted fields or choose Revert fields. Pending "
              "invalid text has not been saved."
        : settings->dialog_pending
            ? "File chooser open. Choose a file or cancel to continue."
            : settings->status;
    nk_label_wrap(ctx, *status ? status
                               : "Eight shared positions. Temporary sprites "
                                 "are never saved in a project.");
    if (page_errors) {
      nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 2);
      if (pp_ui_button(ui, "show-item-errors", "Correct Items fields", item_errors &&
                       !settings->dialog_pending && !ui->composing)) {
        settings->requested_page = PP_SETTINGS_ITEMS;
        settings->page_pending = true;
      }
      if (pp_ui_button(ui, "show-interaction-errors", "Correct Interaction fields",
                       settings->interaction_error[0] &&
                       !settings->dialog_pending && !ui->composing)) {
        settings->requested_page = PP_SETTINGS_INTERACTION;
        settings->page_pending = true;
      }
    }
    nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 3);
    if (pp_ui_button(ui, "revert-fields", "Revert fields",
                     pp_settings_invalid(settings) &&
                         !settings->dialog_pending && !ui->composing))
      settings->revert_pending = true;
    if (pp_ui_button(ui, "close-settings", "Close settings",
                     !settings->dialog_pending))
      pp_project_request_action(settings, PP_PROJECT_ACTION_CLOSE);
    if (pp_ui_button(ui, "quit", "Quit",
                     !settings->dialog_pending &&
                         !pp_settings_invalid(settings)))
      pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_QUIT});
    if (settings->page_pending && !modal && !settings->dialog_pending &&
        !ui->composing && !ui->popup_id &&
        settings->page != settings->requested_page)
      nk_edit_unfocus(ctx);
  }
  nk_end(ctx);
  settings->drawing_fields = false;
  /* Validation is independent of which page is visible. Navigation and actions
   * run only after the outgoing page has consumed its complete input batch. */
  pp_item_editor_validate(settings);
  validate_interaction(settings);
  pp_project_apply_action(settings);
  pp_project_confirmation(settings);
  if (!pp_ui_end(ui, &error))
    goto failed;
  settings->rendering = false;
  release_row_thumbnails(settings, !pp_app_ui(settings->app)->settings_open);
  if (settings->revert_pending) {
    settings->revert_pending = false;
    /* Reset may delete the text field's textures. The queued frame has
     * finished using them, and the next draw publishes the recovered fields. */
    pp_settings_revert(settings);
    pp_settings_draw(settings);
    return;
  }
  if (settings->page_pending) {
    settings->page_pending = false;
    if (!modal && !settings->dialog_pending && !ui->composing && !ui->popup_id &&
        settings->page != settings->requested_page) {
      settings->page = settings->requested_page;
      pp_ui_focus(ui, settings->page == PP_SETTINGS_ITEMS ? "tab-items"
                                                         : "tab-interaction");
      /* Rebuild the visible control registry without replaying the input batch. */
      pp_settings_draw(settings);
      return;
    }
  }
  if (!settings->render_hidden)
    pp_ui_show(ui, pp_app_ui(settings->app)->settings_open);
  if(!pp_app_ui(settings->app)->settings_open)pp_item_editor_release_preview(settings);
  return;
failed:
  settings->rendering = false;
  release_row_thumbnails(settings, true);
  pp_item_editor_release_preview(settings);
  settings->failed = true;
  if (!settings->render_failure[0])
    snprintf(settings->render_failure,sizeof(settings->render_failure),"%s",error.message);
  snprintf(settings->status, sizeof(settings->status), "%s", error.message);
  pp_ui_show(ui, false);
  fprintf(stderr, "Settings closed after rendering failure: %s\n",
          error.message);
}
void pp_settings_destroy(PPSettings *settings) {
  if (!settings)
    return;
  pp_platform_cancel_file_dialogs(settings);
  pp_platform_settings_window(0);
  pp_item_editor_destroy(settings);
  release_row_thumbnails(settings, true);
  pp_ui_backend_destroy(settings->backend);
  free(settings);
}
