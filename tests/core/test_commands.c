#include "../test.h"
#include "pointy/app.h"
#include "pointy/platform.h"
#include "pointy/project_store.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <windows.h>

static PPError error;
static void run(PPApp *app, PPCommand command) {
  if (!pp_app_command(app, &command, &error)) {
    fprintf(stderr, "Command failed: %s\n", error.message);
    CHECK(false);
  }
}
static void test_project_commands(void) {
  PPApp *app = NULL;
  PPAppOptions options = {"", false};
  CHECK(pp_app_create(&app, &options, &error));
  CHECK(!pp_app_dirty(app));
  CHECK(!pp_app_ui(app)->settings_open);
  run(app, (PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                       .name = "Untitled project"});
  CHECK(!pp_app_dirty(app));
  CHECK(!pp_app_command(
      app, &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT, .name = "\xc0\xaf"},
      &error));
  CHECK(!pp_app_dirty(app) && !strcmp(pp_app_name(app), "Untitled project"));
  const char *bad_names[] = {"", " \xc2\xa0", "bad\nname", "\xed\xa0\x80",
                             "\xf4\x90\x80\x80"};
  for (size_t i = 0; i < sizeof(bad_names) / sizeof(*bad_names); i++)
    CHECK(!pp_app_command(
        app,
        &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT, .name = bad_names[i]},
        &error));
  char too_long[PP_PROJECT_NAME_MAX + 2];
  memset(too_long, 'a', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = 0;
  CHECK(!pp_app_command(
      app, &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT, .name = too_long},
      &error));
  CHECK(!pp_app_dirty(app));
  PPScene *scene = pp_app_scene(app);
  uint64_t first_scene, next_scene;
  CHECK(pp_scene_spawn(scene, pp_app_project(app), PP_ANCHOR_TOP_LEFT,
                       (PPVec2){100, 100}, 0, &first_scene, &error));
  uint64_t first_item =
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->default_id;
  PPItem item = *pp_project_item(pp_app_project(app), first_item);
  run(app, (PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                       .item_id = first_item,
                       .item = &item});
  run(app, (PPCommand){.kind = PP_COMMAND_SET_ENABLED,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .enabled = true});
  run(app, (PPCommand){.kind = PP_COMMAND_SET_DEFAULT,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .item_id = first_item});
  run(app, (PPCommand){.kind = PP_COMMAND_REORDER_ITEM,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .item_id = first_item,
                       .index = 0});
  run(app,
      (PPCommand){.kind = PP_COMMAND_SET_INTERACTION,
                  .interaction = *pp_project_settings(pp_app_project(app))});
  CHECK(!pp_app_dirty(app));
  item.id = 0;
  run(app, (PPCommand){.kind = PP_COMMAND_ADD_ITEM,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .item = &item});
  CHECK(pp_app_dirty(app));
  uint64_t second_item = pp_app_ui(app)->selected_item;
  CHECK(second_item != first_item);
  CHECK(
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->default_id ==
      first_item);
  run(app, (PPCommand){.kind = PP_COMMAND_SELECT_ITEM, .item_id = first_item});
  CHECK(
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->default_id ==
      first_item);
  run(app, (PPCommand){.kind = PP_COMMAND_SET_DEFAULT,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .item_id = second_item});
  CHECK(
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->default_id ==
      second_item);
  run(app, (PPCommand){.kind = PP_COMMAND_SAVE_AS, .path = NULL});
  CHECK(pp_app_dirty(app) && !pp_app_path(app)[0]);
  CHECK(!pp_app_command(
      app,
      &(PPCommand){.kind = PP_COMMAND_SAVE_AS,
                   .path = "Z:/missing-pointy-directory/fail.pointy"},
      &error));
  CHECK(pp_app_dirty(app) && !pp_app_path(app)[0] &&
        pp_scene_count(scene) == 1);
  run(app, (PPCommand){.kind = PP_COMMAND_NEW});
  CHECK(pp_app_ui(app)->transition_pending);
  CHECK(!pp_app_command(app,
                        &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                                     .name = "Blocked while deciding"},
                        &error));
  run(app, (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                       .choice = PP_UNSAVED_CANCEL});
  CHECK(pp_app_dirty(app) && pp_scene_count(scene) == 1);
  run(app, (PPCommand){.kind = PP_COMMAND_NEW});
  run(app, (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                       .choice = PP_UNSAVED_SAVE});
  CHECK(pp_app_ui(app)->transition_pending && pp_app_ui(app)->needs_save_path &&
        pp_app_dirty(app));
  run(app, (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                       .choice = PP_UNSAVED_CANCEL});
  CHECK(!pp_app_ui(app)->transition_pending &&
        !pp_app_ui(app)->needs_save_path && pp_scene_count(scene) == 1);
  run(app, (PPCommand){.kind = PP_COMMAND_NEW});
  CHECK(!pp_app_command(
      app,
      &(PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                   .choice = PP_UNSAVED_SAVE,
                   .path = "Z:/missing-pointy-directory/fail.pointy"},
      &error));
  CHECK(!pp_app_ui(app)->transition_pending && pp_app_dirty(app) &&
        pp_scene_count(scene) == 1);
  run(app, (PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                       .name = "Pr\xc3\xa9sentation \xf0\x9f\x91\x89"});
  run(app,
      (PPCommand){.kind = PP_COMMAND_SAVE_AS, .path = "commands-test.pointy"});
  CHECK(!pp_app_dirty(app));
  CHECK(!strcmp(pp_app_name(app), "Pr\xc3\xa9sentation \xf0\x9f\x91\x89"));
  run(app,
      (PPCommand){.kind = PP_COMMAND_RENAME_PROJECT, .name = pp_app_name(app)});
  CHECK(!pp_app_dirty(app));
  PPItem invalid = *pp_project_item(pp_app_project(app), first_item);
  invalid.opacity = 2;
  CHECK(!pp_app_command(app,
                        &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                     .item_id = first_item,
                                     .item = &invalid},
                        &error));
  CHECK(!pp_app_dirty(app));
  uint64_t generation = pp_app_generation(app);
  const PPProject *before = pp_app_project(app);
  CHECK(!pp_app_command(
      app, &(PPCommand){.kind = PP_COMMAND_OPEN, .path = "missing.pointy"},
      &error));
  CHECK(pp_app_project(app) == before && pp_scene_count(scene) == 1 &&
        pp_app_generation(app) == generation);
  run(app,
      (PPCommand){.kind = PP_COMMAND_OPEN, .path = "commands-test.pointy"});
  CHECK(!strcmp(pp_app_name(app), "Pr\xc3\xa9sentation \xf0\x9f\x91\x89"));
  CHECK(pp_scene_count(scene) == 0 && pp_app_generation(app) == generation + 1);
  CHECK(pp_app_scene(app) == scene);
  CHECK(pp_scene_spawn(scene, pp_app_project(app), PP_ANCHOR_TOP_LEFT,
                       (PPVec2){0, 0}, 0, &next_scene, &error));
  CHECK(next_scene > first_scene);
  run(app, (PPCommand){.kind = PP_COMMAND_SET_ENABLED,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .enabled = false});
  run(app, (PPCommand){.kind = PP_COMMAND_SHOW_SETTINGS});
  run(app, (PPCommand){.kind = PP_COMMAND_CLOSE_SETTINGS});
  CHECK(pp_app_dirty(app) && !pp_app_ui(app)->settings_open &&
        !pp_app_ui(app)->quit_requested);
  run(app, (PPCommand){.kind = PP_COMMAND_QUIT});
  CHECK(pp_app_ui(app)->transition_pending && !pp_app_ui(app)->quit_requested);
  run(app, (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                       .choice = PP_UNSAVED_CANCEL});
  run(app, (PPCommand){.kind = PP_COMMAND_NEW});
  run(app, (PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                       .choice = PP_UNSAVED_DISCARD});
  CHECK(!pp_app_dirty(app) && !pp_app_path(app)[0] && !pp_scene_count(scene));
  run(app, (PPCommand){.kind = PP_COMMAND_QUIT});
  CHECK(pp_app_ui(app)->quit_requested && !pp_app_ui(app)->transition_pending);
  pp_app_destroy(app);
  remove("commands-test.pointy");
}
static void test_preferences_and_imports(void) {
  char directory[260], path[300], preferences[300];
  snprintf(directory, sizeof(directory), "commands-private-%lu-%llu",
           GetCurrentProcessId(), GetTickCount64());
  CHECK(CreateDirectoryA(directory, NULL));
  snprintf(path, sizeof(path), "%s/project.pointy", directory);
  snprintf(preferences, sizeof(preferences), "%s/preferences.json", directory);
  PPAppOptions options = {directory, true};
  PPApp *app = NULL;
  CHECK(pp_app_create(&app, &options, &error));
  run(app,
      (PPCommand){.kind = PP_COMMAND_RENAME_PROJECT, .name = "Portable name"});
  run(app, (PPCommand){.kind = PP_COMMAND_SAVE_AS, .path = path});
  pp_app_destroy(app);
  CHECK(pp_app_create(&app, &options, &error));
  CHECK(!strcmp(pp_app_name(app), "Portable name") && !pp_app_dirty(app));
  uint64_t id = pp_app_ui(app)->selected_item;
  run(app, (PPCommand){.kind = PP_COMMAND_IMPORT_FONT,
                       .item_id = id,
                       .path = PP_FONT_FIXTURES "/PointyFixture.ttc",
                       .font_face = 1});
  CHECK(pp_project_item(pp_app_project(app), id)->text_style.primary_face == 1);
  PPItem item = *pp_project_item(pp_app_project(app), id);
  item.text = "A";
  run(app,
      (PPCommand){.kind = PP_COMMAND_EDIT_ITEM, .item_id = id, .item = &item});
  run(app, (PPCommand){.kind = PP_COMMAND_SAVE});
  CHECK(!pp_app_dirty(app));
  CHECK(!pp_app_command(
      app,
      &(PPCommand){.kind = PP_COMMAND_IMPORT_FONT,
                   .item_id = id,
                   .path = PP_FONT_FIXTURES "/PointyFixture.ttc",
                   .font_face = 65536},
      &error));
  CHECK(!pp_app_dirty(app) &&
        pp_project_item(pp_app_project(app), id)->text_style.primary_face == 1);
  run(app, (PPCommand){.kind = PP_COMMAND_IMPORT_IMAGE,
                       .anchor = PP_ANCHOR_TOP_LEFT,
                       .path = PP_MEDIA_FIXTURES "/alpha.png"});
  CHECK(pp_app_dirty(app));
  run(app, (PPCommand){.kind = PP_COMMAND_SAVE});
  pp_app_destroy(app);
  CHECK(pp_app_create(&app, &options, &error));
  CHECK(pp_project_item(pp_app_project(app), id)->text_style.primary_face == 1);
  CHECK(pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->count == 2);
  pp_app_destroy(app);
  CHECK(pp_platform_remove_file(path, &error));
  CHECK(pp_app_create(&app, &options, &error));
  CHECK(!pp_app_dirty(app) && !*pp_app_path(app));
  CHECK(strstr(pp_app_diagnostic(app), "Last project could not open"));
  run(app, (PPCommand){.kind = PP_COMMAND_SET_GEOMETRY,
                       .geometry = {10, 20, 800, 600}});
  CHECK(!pp_platform_open_file(path, "rb", NULL));
  pp_app_destroy(app);
  char data[4096];
  FILE *f = pp_platform_open_file(preferences, "rb", &error);
  CHECK(f);
  size_t size = fread(data, 1, sizeof(data) - 1, f);
  data[size] = 0;
  fclose(f);
  CHECK(strstr(data,
               "project.pointy")); /* Missing remembered path is retained. */
  CHECK(pp_platform_remove_file(preferences, &error));
  CHECK(RemoveDirectoryA(directory));
}
int main(void) {
  test_project_commands();
  test_preferences_and_imports();
  puts("commands: transaction, dirty, selection, scene lifetime and unsaved "
       "transitions passed");
  return 0;
}
