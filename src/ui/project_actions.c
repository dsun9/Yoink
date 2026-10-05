#include "settings_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool pp_settings_command(PPSettings *settings, PPCommand command) {
  /* These commands replace the current editor binding. Keep its pending
   * input on the original item until every field has been validated. Modal
   * removal runs after field traversal and therefore needs no deferral. */
  if (settings->drawing_fields &&
      (command.kind == PP_COMMAND_SELECT_ANCHOR ||
       command.kind == PP_COMMAND_SELECT_ITEM ||
       command.kind == PP_COMMAND_ADD_TEXT ||
       command.kind == PP_COMMAND_REMOVE_ITEM)) {
    if (settings->pending_action == PP_PROJECT_ACTION_NONE) {
      settings->pending_action = PP_PROJECT_ACTION_COMMAND;
      settings->pending_command = command;
    }
    return true;
  }
  PPError error = {0};
  bool ok = pp_app_command(settings->app, &command, &error);
  if (!ok)
    snprintf(settings->status, sizeof(settings->status), "%s", error.message);
  else if ((command.kind == PP_COMMAND_SAVE &&
            !pp_app_ui(settings->app)->needs_save_path) ||
           (command.kind == PP_COMMAND_SAVE_AS && command.path))
    snprintf(settings->status, sizeof(settings->status), "Project saved.");
  return ok;
}
static void selected_file(void *userdata, const char *path,
                          const PPError *error) {
  PPSettings *settings = userdata;
  settings->dialog_pending = false;
  if (error) {
    snprintf(settings->status, sizeof(settings->status), "%s", error->message);
    if (settings->save_for_transition)
      pp_settings_command(settings,
                          (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                      .choice = PP_UNSAVED_CANCEL});
    settings->save_for_transition = false;
    return;
  }
  if (settings->dialog_generation != pp_app_generation(settings->app)) {
    snprintf(settings->status, sizeof(settings->status),
             "The project changed while the file chooser was open. Choose the "
             "file again.");
    return;
  }
  if (settings->save_for_transition) {
    pp_settings_command(
        settings,
        (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                    .choice = path ? PP_UNSAVED_SAVE : PP_UNSAVED_CANCEL,
                    .path = path});
    settings->save_for_transition = false;
    return;
  }
  PPCommandKind kind =
      settings->dialog_kind == PP_DIALOG_OPEN_PROJECT   ? PP_COMMAND_OPEN
      : settings->dialog_kind == PP_DIALOG_SAVE_PROJECT ? PP_COMMAND_SAVE_AS
      : settings->dialog_kind == PP_DIALOG_IMPORT_IMAGE
          ? PP_COMMAND_IMPORT_IMAGE
          : PP_COMMAND_IMPORT_FONT;
  if (pp_settings_command(settings,
                          (PPCommand){.kind = kind,
                                      .path = path,
                                      .anchor = settings->dialog_anchor,
                                      .item_id = settings->dialog_item}) &&
      path) {
    if (kind == PP_COMMAND_OPEN &&
        !pp_app_ui(settings->app)->transition_pending)
      snprintf(settings->status, sizeof(settings->status),
               "Project opened. Temporary sprites cleared.");
    if (kind == PP_COMMAND_IMPORT_IMAGE || kind == PP_COMMAND_IMPORT_FONT)
      snprintf(settings->status, sizeof(settings->status),
               "Imported into this project. Save to keep these changes.");
  }
}
static void dispatch_dialog(PPSettings *settings) {
  PPDialogKind kind = settings->dialog_kind;
  settings->dialog_pending = true;
  pp_platform_dialog_parent(pp_settings_window_id(settings));
  const char *path = pp_app_path(settings->app);
  pp_platform_file_dialog(kind,
                          *path                            ? path
                          : kind == PP_DIALOG_SAVE_PROJECT ? "Untitled.pointy"
                                                           : NULL,
                          selected_file, settings);
}
void pp_settings_dialog(PPSettings *settings, PPDialogKind kind,
                        bool add_image) {
  if (settings->dialog_pending ||
      (settings->drawing_fields && settings->pending_action != PP_PROJECT_ACTION_NONE))
    return;
  settings->dialog_kind = kind;
  settings->dialog_anchor = pp_app_ui(settings->app)->anchor;
  settings->dialog_item = add_image ? 0 : pp_app_ui(settings->app)->selected_item;
  settings->dialog_generation = pp_app_generation(settings->app);
  /* Capture the intended target, but leave fields enabled until the entire
   * input batch has traversed the shared validation boundary. */
  if (settings->drawing_fields)
    settings->pending_action = PP_PROJECT_ACTION_DIALOG;
  else
    dispatch_dialog(settings);
}
void pp_project_request_action(PPSettings *settings, PPProjectAction action) {
  /* A frame accepts one project action, just as an open modal or chooser does.
   * Keep the first request while the current editor input is still pending. */
  if (settings->pending_action == PP_PROJECT_ACTION_NONE)
    settings->pending_action = action;
}
void pp_project_apply_action(PPSettings *settings) {
  PPProjectAction action = settings->pending_action;
  settings->pending_action = PP_PROJECT_ACTION_NONE;
  if (action == PP_PROJECT_ACTION_NONE || settings->dialog_pending)
    return;
  /* Closing retains invalid drafts and a pending confirmation for reopening. */
  if (action == PP_PROJECT_ACTION_CLOSE) {
    pp_settings_close(settings);
    return;
  }
  if (pp_app_ui(settings->app)->transition_pending ||
      settings->remove_confirmation)
    return;
  if (pp_settings_invalid(settings)) {
    snprintf(settings->status, sizeof(settings->status),
             "Correct the highlighted fields or choose Revert fields before "
             "continuing.");
    return;
  }
  switch (action) {
  case PP_PROJECT_ACTION_DIALOG:
    dispatch_dialog(settings);
    break;
  case PP_PROJECT_ACTION_NEW:
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_NEW});
    break;
  case PP_PROJECT_ACTION_OPEN:
    pp_settings_dialog(settings, PP_DIALOG_OPEN_PROJECT, false);
    break;
  case PP_PROJECT_ACTION_SAVE:
    pp_settings_command(settings, (PPCommand){.kind = PP_COMMAND_SAVE});
    if (pp_app_ui(settings->app)->needs_save_path)
      pp_settings_dialog(settings, PP_DIALOG_SAVE_PROJECT, false);
    break;
  case PP_PROJECT_ACTION_SAVE_AS:
    pp_settings_dialog(settings, PP_DIALOG_SAVE_PROJECT, false);
    break;
  case PP_PROJECT_ACTION_COMMAND:
    pp_settings_command(settings, settings->pending_command);
    break;
  case PP_PROJECT_ACTION_REMOVE:
    settings->remove_confirmation = true;
    break;
  default:
    break;
  }
}
void pp_project_actions(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  struct nk_context *ctx = &ui->context;
  bool enabled = !settings->dialog_pending &&
                 !pp_app_ui(settings->app)->transition_pending &&
                 !settings->remove_confirmation;
  nk_layout_row_dynamic(ctx, 24, 1);
  nk_label(ctx,
           pp_app_dirty(settings->app) ? "Project name  /  Unsaved changes"
           : *pp_app_path(settings->app)
               ? "Project name  /  Saved"
               : "Project name  /  Not saved to a file",
           NK_TEXT_LEFT);
  nk_layout_row_dynamic(ctx, 32, 1);
  if (pp_ui_edit(ui, "project-name", settings->project_name,
                 sizeof(settings->project_name), false, enabled)) {
    PPError error;
    if (!pp_app_command(settings->app,
                        &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                                     .name = settings->project_name},
                        &error))
      snprintf(settings->name_error, sizeof(settings->name_error), "%s",
               error.message);
    else
      settings->name_error[0] = 0;
  }
  bool actions_enabled = enabled && !pp_settings_invalid(settings);
  nk_layout_row_dynamic(ctx, 32, 4);
  if (pp_ui_button(ui, "new", "New", actions_enabled))
    pp_project_request_action(settings, PP_PROJECT_ACTION_NEW);
  if (pp_ui_button(ui, "open", "Open", actions_enabled))
    pp_project_request_action(settings, PP_PROJECT_ACTION_OPEN);
  if (pp_ui_button(ui, "save", "Save", actions_enabled))
    pp_project_request_action(settings, PP_PROJECT_ACTION_SAVE);
  if (pp_ui_button(ui, "save-as", "Save As", actions_enabled))
    pp_project_request_action(settings, PP_PROJECT_ACTION_SAVE_AS);
  char unicode_notice[256];
  pp_ui_unicode_notice(ui, settings->project_name, unicode_notice,
                       sizeof(unicode_notice));
  nk_layout_row_dynamic(ctx, 38, 1);
  nk_label_wrap(
      ctx,
      settings->name_error[0] ? settings->name_error
      : unicode_notice[0]
          ? unicode_notice
          : "Ctrl+N New   Ctrl+O Open   Ctrl+S Save   Ctrl+Shift+S Save As");
}
void pp_project_confirmation(PPSettings *settings) {
  PPUIBackend *ui = settings->backend;
  struct nk_context *ctx = &ui->context;
  bool transition = pp_app_ui(settings->app)->transition_pending;
  bool modal = transition || settings->remove_confirmation;
  pp_ui_modal(ui, modal);
  if (!modal)
    return;
  /* A separate Nuklear window receives all input; settings draws with
   * NK_WINDOW_NO_INPUT while this modal is present. Registry is modal-only. */
  ui->control_count = 0;
  float width = fminf(500, ui->width - 32);
  struct nk_rect bounds = nk_rect(
      (ui->width - width) / 2, fmaxf(16, (ui->height - 220) / 2), width, 220);
  if (nk_begin(ctx, "Confirm changes", bounds,
               NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_dynamic(ctx, 64, 1);
    nk_label_wrap(
        ctx,
        transition
            ? "Save changes before continuing? New, Open and Quit clear "
              "temporary sprites after they succeed."
            : "Remove this item from the selected anchor? Existing placed "
              "sprites keep their appearance. Save to keep the changed list.");
    nk_layout_row_dynamic(ctx, 32, transition ? 3 : 2);
    if (pp_ui_button(ui, "cancel-transition", "Cancel",
                     !settings->dialog_pending) ||
        ui->escape) {
      if (transition)
        pp_settings_command(settings,
                            (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                        .choice = PP_UNSAVED_CANCEL});
      settings->remove_confirmation = false;
    }
    if (transition) {
      if (pp_ui_button(ui, "discard-transition", "Discard",
                       !settings->dialog_pending))
        pp_settings_command(settings,
                            (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                        .choice = PP_UNSAVED_DISCARD});
      if (pp_ui_button(ui, "save-transition", "Save",
                       !settings->dialog_pending)) {
        pp_settings_command(settings,
                            (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                        .choice = PP_UNSAVED_SAVE});
        if (pp_app_ui(settings->app)->needs_save_path) {
          settings->save_for_transition = true;
          pp_settings_dialog(settings, PP_DIALOG_SAVE_PROJECT, false);
        }
      }
    } else if (pp_ui_button(ui, "remove-confirm", "Remove item", true)) {
      pp_settings_command(
          settings,
          (PPCommand){.kind = PP_COMMAND_REMOVE_ITEM,
                      .anchor = pp_app_ui(settings->app)->anchor,
                      .item_id = pp_app_ui(settings->app)->selected_item});
      settings->remove_confirmation = false;
    }
    nk_layout_row_dynamic(ctx, 48, 1);
    nk_label_wrap(ctx, settings->dialog_pending
                           ? "Choose a project file to continue."
                           : settings->status);
  }
  nk_end(ctx);
}
