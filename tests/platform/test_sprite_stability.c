#include "../../src/app/presenter_internal.h"
#include "../../src/render/gl_api.h"
#include "../test.h"
#include <windows.h>
#include <math.h>
#include <string.h>

#define API(expr) do { bool ok = (expr); if (!ok) fprintf(stderr, "%s\n", error.message); CHECK(ok); } while (0)
static PPError error;
static PPAssets *assets;
static PPProject *project;
static PPAnchorPresenter *presenter;
static PPRenderer *renderer;
static PPSurface *surface;
static PPRetainedPresentation retained;

static HWND native(void) {
  SDL_Window *window = SDL_GetWindowFromID(pp_surface_window_id(surface));
  CHECK(window);
  return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

static void cleanup(void) {
  if (presenter) pp_retained_release(presenter, &retained);
  pp_surface_destroy(surface);
  pp_anchor_presenter_destroy(presenter);
  pp_renderer_destroy(renderer);
  pp_project_destroy(project);
  pp_assets_destroy(assets);
}

static void cursor(void) {
  API(pp_surface_create_gl(&surface, &error));
  API(pp_surface_place_gl(surface, (PPRect){300, 300, 64, 64}, true, &error));
  POINT previous_point;
  CHECK(GetCursorPos(&previous_point));
  CHECK(SetCursorPos(320, 320));
  SDL_PumpEvents();
  SendMessageW(native(), WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
  SDL_Cursor *hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
  CHECK(hand);
  SDL_Cursor *previous = SDL_GetCursor();
  CHECK(SDL_SetCursor(hand));
  CHECK(SDL_GetMouseFocus() == SDL_GetWindowFromID(pp_surface_window_id(surface)));
  SendMessageW(native(), WM_SETCURSOR, (WPARAM)native(), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  bool arrow = GetCursor() == LoadCursor(NULL, IDC_ARROW);
  printf("Sprite hover after Settings hand cursor: arrow=%d\n", arrow);
  CHECK(SDL_SetCursor(previous));
  SDL_DestroyCursor(hand);
  CHECK(SetCursorPos(previous_point.x, previous_point.y));
  CHECK(arrow);
}

/* Read the complete retained color FBO left as the read framebuffer by native
 * publication. This measures actual shader output before desktop composition. */
static PPVec2 artwork_center(void) {
  int w, h;
  CHECK(SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(pp_surface_window_id(surface)), &w, &h));
  GLint fbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbo);
  CHECK(fbo != 0);
  uint8_t *pixels = malloc((size_t)w * h * 4);
  CHECK(pixels);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  CHECK(glGetError() == GL_NO_ERROR);
  double weight = 0, xsum = 0, ysum = 0;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    double a = pixels[((size_t)y * w + x) * 4 + 3];
    weight += a; xsum += a * (x + .5); ysum += a * (h - y - .5);
    if (a > 0) {
      bool covered = false;
      for (size_t i = 0; i < retained.grab_count; i++) {
        PPGrabRect r = retained.grab_rects[i];
        if (x >= r.left && x < r.right && h - y - 1 >= r.top && h - y - 1 < r.bottom) covered = true;
      }
      CHECK(covered);
    }
  }
  free(pixels);
  CHECK(weight > 0);
  RECT bounds;
  CHECK(GetWindowRect(native(), &bounds));
  return (PPVec2){bounds.left + xsum / weight, bounds.top + ysum / weight};
}

static void stability(void) {
  API(pp_assets_create(&assets, &error));
  API(pp_project_create(assets, &project, &error));
  API(pp_renderer_create(&renderer, &error));
  API(pp_anchor_presenter_create(&presenter, renderer, project, &error));
  API(pp_surface_create_gl(&surface, &error));
  uint8_t pixels[32 * 32 * 4] = {0};
  for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
    double dx = x - 15.5, dy = y - 15.5;
    uint8_t alpha = (uint8_t)lround(255 * exp(-(dx * dx + dy * dy) / 24));
    memset(pixels + ((size_t)y * 32 + x) * 4, alpha, 4);
  }
  PPFrame frame = {pixels, NULL, 32, 32, 128};
  PPSourcePresentation source = {.frame = &frame, .content_id = 1,
      .source_revision = 1, .layout = {32, 32}, .visual_alpha = true};
  PPDisplay display = {1, {0, 0, 1920, 1080}, 1, 1};
  double max_error[3] = {0};
  const PPVec2 centers[] = {{300, 300}, {300.25, 300.75}, {-300.125, -300.875}};
  const double densities[] = {1, 1.5, 2};
  for (int d = 0; d < 3; d++) for (int c = 0; c < 3; c++)
  for (int kind = 0; kind < 3; kind++) for (int i = 0; i < 24; i++) {
    display.drawable_pixels_per_dip = densities[d];
    display.desktop_units_per_dip = densities[d];
    PPPose pose = {centers[c], kind == 1 ? 64 : 63 + i * .1, kind ? i * .2 : 0, 1};
    if (kind == 2) {
      pose.center.x += i * .125;
      pose.center.y -= i * .0625;
    }
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    PPVec2 actual = artwork_center();
    double drift = fmax(fabs(actual.x - pose.center.x), fabs(actual.y - pose.center.y));
    max_error[kind] = fmax(max_error[kind], drift);
  }
  printf("Rendered center maximum drift at 100/150/200%% density, integer/fractional/negative positions: scaling=%.6fpx rotation=%.6fpx combined movement=%.6fpx\n", max_error[0], max_error[1], max_error[2]);
  CHECK(max_error[0] < .02 && max_error[1] < .02 && max_error[2] < .02);
  PPPose pose = {{300.25, 300.75}, 64, 0, 1};
  API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
  PPRendererMetrics before = pp_renderer_metrics(retained.renderer);
  pose.center.x += 10; pose.center.y += 20;
  API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
  PPRendererMetrics after = pp_renderer_metrics(retained.renderer);
  CHECK(after.transform_draws == before.transform_draws && after.source_uploads == before.source_uploads && after.readbacks == 0);
  /* Exact half-pixel ties must choose the same local center on either side
   * of the desktop origin so integer translations can reuse the output. */
  pose.center = (PPVec2){300, 300};
  API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
  before = pp_renderer_metrics(retained.renderer);
  pose.center = (PPVec2){-300, -300};
  API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
  after = pp_renderer_metrics(retained.renderer);
  CHECK(after.transform_draws == before.transform_draws && after.source_uploads == before.source_uploads);
  /* A 1-pixel extent retains a 2x2 output. Reusing that output must not
   * change the native placement policy on the next presentation. */
  display.desktop_units_per_dip = display.drawable_pixels_per_dip = 1;
  pose = (PPPose){{300, 300}, 1, 0, 1};
  for (int i = 0; i < 3; i++) {
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    CHECK(retained.bounds.w == 2 && retained.bounds.h == 2);
    CHECK(pp_renderer_output_bytes(retained.renderer) == 2 * 2 * 4);
  }
  /* The transparent placement margin must preserve the supported 8192-pixel
   * artwork extent on either axis, including fractional desktop centers. */
  display.desktop_units_per_dip = display.drawable_pixels_per_dip = 2;
  source.layout = (PPVec2){4096, 1};
  pose = (PPPose){{300.25, 300.75}, 4096, 0, 1};
  for (int axis = 0; axis < 2; axis++) {
    pose.angle_deg = axis * 90;
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    CHECK((axis ? retained.bounds.h : retained.bounds.w) == 8193);
    CHECK((axis ? retained.bounds.w : retained.bounds.h) == 3);
    CHECK(pp_renderer_output_bytes(retained.renderer) == 8193 * 3 * 4);
    before = pp_renderer_metrics(retained.renderer);
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    after = pp_renderer_metrics(retained.renderer);
    CHECK(after.transform_draws == before.transform_draws);
  }
  puts("PASS tiny-output reuse and maximum artwork extent on both axes.");
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  atexit(cleanup);
  if (argc == 2 && !strcmp(argv[1], "--cursor")) cursor();
  else stability();
  return 0;
}
