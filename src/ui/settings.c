#include "settings_internal.h"
#include "shaped_edit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *anchor_names[PP_ANCHOR_COUNT] = {
    "Top left",     "Top center",  "Top right",     "Center left",
    "Center right", "Bottom left", "Bottom center", "Bottom right"};
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
  nk_layout_row_dynamic(ctx, 22, 1);
  nk_label(ctx, "Shared on every display", NK_TEXT_LEFT);
  const int positions[] = {0, 1, 2, 3, -1, 4, 5, 6, 7};
  for (int row = 0; row < 3; row++) {
    nk_layout_row_dynamic(ctx, 40, 3);
    for (int col = 0; col < 3; col++) {
      int anchor = positions[row * 3 + col];
      if (anchor < 0) {
        struct nk_rect rect;
        nk_widget(&rect, ctx);
        nk_stroke_rect(nk_window_get_canvas(ctx), rect, 4, 2,
                       pp_ui_theme.muted);
        struct nk_user_font *font = &ui->utility_font->handle;
        float label_width =
            font->width(font->userdata, font->height, "Display", 7);
        struct nk_rect label = nk_rect(rect.x + (rect.w - label_width) / 2,
                                       rect.y + (rect.h - font->height) / 2,
                                       label_width, font->height);
        nk_draw_text(nk_window_get_canvas(ctx), label, "Display", 7, font,
                     pp_ui_theme.canvas, pp_ui_theme.muted);
      } else {
        char id[32];
        snprintf(id, sizeof(id), "anchor-%d", anchor);
        const char *short_names[] = {"Top L", "Top",      "Top R",  "Left",
                                     "Right", "Bottom L", "Bottom", "Bottom R"};
        if (pp_ui_select(ui, id, short_names[anchor], selected == anchor,
                         enabled))
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
  nk_layout_row_dynamic(ctx, 32, 1);
  if (pp_ui_button(ui, "anchor-enabled",
                   list->enabled ? "Enabled  /  click to disable"
                                 : "Disabled  /  click to enable",
                   enabled))
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SET_ENABLED,
                                              .anchor = selected,
                                              .enabled = !list->enabled});
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
  if (nk_group_begin(ctx, "anchor-items", NK_WINDOW_BORDER)) {
    if (!list->count) {
      nk_layout_row_dynamic(ctx, 48, 1);
      nk_label_wrap(ctx, "No items. Add text or import an image below.");
    }
    for (size_t i = 0; i < list->count; i++) {
      const PPItem *item =
          pp_project_item(pp_app_project(settings->app), list->item_ids[i]);
      char label[128], id[48];
      snprintf(label, sizeof(label), "%zu. %s%s", i + 1,
               item->kind == PP_ITEM_TEXT ? "Text / emoji" : "Image",
               item->id == list->default_id ? "  [Default]" : "");
      snprintf(id, sizeof(id), "item-%llu", (unsigned long long)item->id);
      nk_layout_row_dynamic(ctx, 32, 1);
      if (pp_ui_select(ui, id, label, item->id == state->selected_item,
                       enabled))
        pp_settings_command(
            settings,
            (PPCommand){.kind = PP_COMMAND_SELECT_ITEM, .item_id = item->id});
    }
    nk_group_end(ctx);
  }
  pp_ui_reveal_focus(ui, "anchor-items", list_viewport, first_control);
  size_t selected_index = SIZE_MAX;
  for (size_t i = 0; i < list->count; i++)
    if (list->item_ids[i] == state->selected_item)
      selected_index = i;
  nk_layout_row_dynamic(ctx, 32, 2);
  if (pp_ui_button(ui, "add-text", "Add text", enabled)) {
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_ADD_TEXT,
                                              .anchor = state->anchor});
    return; /* Project accessors above are borrowed through mutation only. */
  }
  if (pp_ui_button(ui, "add-image", "Import image", enabled))
    pp_settings_dialog(settings, PP_DIALOG_IMPORT_IMAGE, true);
  nk_layout_row_dynamic(ctx, 32, 2);
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
  nk_layout_row_dynamic(ctx, 32, 1);
  if (pp_ui_button(ui, "make-default", "Make default for this position",
                   enabled && selected_index < list->count &&
                       state->selected_item != list->default_id))
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SET_DEFAULT,
                                              .anchor = state->anchor,
                                              .item_id = state->selected_item});
  nk_layout_row_dynamic(ctx, 32, 1);
  if (pp_ui_button(ui, "remove-item", "Remove selected item",
                   enabled && selected_index < list->count))
    pp_project_request_action(settings, PP_PROJECT_ACTION_REMOVE);
  nk_layout_row_dynamic(ctx, 40, 1);
  nk_label_wrap(ctx, "Selecting an item leaves the default unchanged. Make "
                     "default chooses what future pulls create.");
}
static void interaction_field(PPSettings *settings, const char *label,
                              const char *id, char *text, int capacity,
                              bool *changed) {
  nk_layout_row_dynamic(&settings->backend->context, 20, 1);
  nk_label(&settings->backend->context, label, NK_TEXT_LEFT);
  nk_layout_row_dynamic(&settings->backend->context, 32, 1);
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
  nk_layout_row_dynamic(&ui->context, 32, 2);
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
    pp_item_editor_release_preview(settings);
    settings->generation = pp_app_generation(settings->app);
    pp_settings_revert(settings);
  }
  if (!pp_app_ui(settings->app)->settings_open) {
    pp_ui_shaped_suspend(settings->backend);
    pp_item_editor_release_preview(settings);
    pp_ui_show(settings->backend, false);
    return;
  }
  PPUIBackend *ui = settings->backend;
  PPError error = {0};
  float previous_scale = ui->scale;
  if (!pp_ui_begin(ui, &error))
    goto failed;
  settings->rendering = true;
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
    float body_height = fmaxf(100, ui->height - 294 - (page_errors ? 38 : 0));
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
      nk_layout_row_dynamic(ctx, 32, 2);
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
    nk_layout_row_dynamic(ctx, 32, 3);
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
  pp_ui_backend_destroy(settings->backend);
  free(settings);
}
