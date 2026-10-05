#include "../../src/app/scheduler.h"
#include "../test.h"
#include "pointy/interaction.h"
#include "pointy/assets.h"
#include <SDL3/SDL.h>
static void test_scheduler_deadlines(void) {
  PPSchedule s = {HUGE_VAL,HUGE_VAL,HUGE_VAL,HUGE_VAL,HUGE_VAL,HUGE_VAL};
  CHECK(isinf(pp_next_wake(&s, 0))); /* Static scene has no redraw deadline. */
  s.pointer = 1.0 / 60;
  CHECK(pp_next_wake(&s, 0) == s.pointer); /* Settings may stay closed. */
  s.easing = pp_schedule_tick(0, .120);
  CHECK(s.easing == 1.0 / 60);
  CHECK(pp_schedule_tick(.115, .120) == .120);
  CHECK(pp_ease_value(0, 10, .120, .120) == 10);
  CHECK(isinf(pp_schedule_tick(.120, .120)));
  s.easing = HUGE_VAL;
  s.gif = .007;
  CHECK(pp_next_wake(&s, 0) == .007); /* No 60 Hz quantization of GIF frames. */
  PPAssets *assets=NULL; PPError error={0}; PPAssetId id; size_t size;
  CHECK(pp_assets_create(&assets,&error));
  void *bytes=SDL_LoadFile(PP_MEDIA_FIXTURES "/finite.gif",&size); CHECK(bytes);
  CHECK(pp_asset_import(assets,(PPBytes){bytes,size},PP_ASSET_GIF,&id,&error)); SDL_free(bytes);
  PPAsset *asset=pp_asset_retain(assets,id); CHECK(asset);
  const PPFrame *frame=NULL;
  CHECK(pp_asset_frame(asset,.201,0,0,&frame,&s.gif,&error));
  CHECK(isinf(s.gif)); /* Actual stopped finite-loop media has no frame wake. */
  pp_asset_release(asset);pp_assets_destroy(assets);
  CHECK(pp_next_wake(&s, 0) == s.pointer);
  s.command = 0;
  CHECK(pp_next_wake(&s, 1) == 1);
  puts("PASS scheduler static, .120 easing endpoint, finite GIF, closed-settings pointer, exact GIF and queued commands");
}
int main(void) { test_scheduler_deadlines(); return 0; }
