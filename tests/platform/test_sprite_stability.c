#include "../../src/app/presenter_internal.h"
#include "../../src/render/gl_api.h"
#include "../../src/platform/windows/composition.h"
#include "../test.h"
#include <windows.h>
#include <dwmapi.h>
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
static bool reported_backend;
static HWND background;
static bool capture_changes;
static RECT capture_bounds;
static PPVec2 probe_center;
static double probe_longest;
static double compositor_drift;
static unsigned compositor_samples;
bool pp_test_desktop_capture_start(RECT rectangle);
unsigned char *pp_test_desktop_capture(RECT rectangle, bool require_new);

static uint8_t *completed_pixels;
static int completed_width,completed_height;
static void completed_draw(PPRenderer *r,int x,int y,int w,int h,bool top_down,void *context) {
  if(!reported_backend&&surface) {
    SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(surface));
    HWND native=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
    printf("PRESENTATION %s\n",pp_composition_description(GetPropW(native,PP_COMPOSITION_PROPERTY)));reported_backend=true;
  }
  (void)context;if(r!=retained.renderer)return;
  uint8_t *raw=malloc((size_t)w*h*4);CHECK(raw);
  glReadPixels(x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw);CHECK(glGetError()==GL_NO_ERROR);
  free(completed_pixels);completed_pixels=malloc((size_t)w*h*4);CHECK(completed_pixels);
  for(int row=0;row<h;row++)memcpy(completed_pixels+(size_t)row*w*4,raw+(size_t)(top_down?h-row-1:row)*w*4,(size_t)w*4);
  free(raw);completed_width=w;completed_height=h;
}
static HWND native(void) {
  SDL_Window *window = SDL_GetWindowFromID(pp_surface_window_id(surface));
  CHECK(window);
  return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

static void cleanup(void) {
  capture_changes = false;
  if (presenter) pp_retained_release(presenter, &retained);
  pp_surface_destroy(surface);
  if (background) DestroyWindow(background);
  pp_anchor_presenter_destroy(presenter);
  pp_renderer_destroy(renderer);
  pp_project_destroy(project);
  pp_assets_destroy(assets);free(completed_pixels);
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

/* The diagnostic observer captures each shader result while its compositor
 * update remains GL-readable. These pixels precede desktop composition. */
static PPVec2 artwork_center_channel(int channel) {
  int w, h;
  CHECK(SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(pp_surface_window_id(surface)), &w, &h));
  CHECK(completed_pixels&&completed_width==w&&completed_height==h);
  uint8_t *pixels=completed_pixels;
  double weight = 0, xsum = 0, ysum = 0;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    double a = pixels[((size_t)y * w + x) * 4 + channel];
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

  CHECK(weight > 0);
  RECT bounds;
  CHECK(GetWindowRect(native(), &bounds));
  return (PPVec2){bounds.left + xsum / weight, bounds.top + ysum / weight};
}

static PPVec2 artwork_center(void) { return artwork_center_channel(3); }

static PPVec2 desktop_center(void) {
  CHECK(SUCCEEDED(DwmFlush()));
  uint8_t *pixels = pp_test_desktop_capture(capture_bounds, true);
  CHECK(pixels);
  int w = capture_bounds.right - capture_bounds.left, h = capture_bounds.bottom - capture_bounds.top;
  double weight = 0, xsum = 0, ysum = 0;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    size_t index = ((size_t)y * w + x) * 4;
    double value = pixels[index + 1] + pixels[index + 2];
    weight += value; xsum += value * (x + .5); ysum += value * (y + .5);
  }
  free(pixels);
  CHECK(weight > 0);
  return (PPVec2){capture_bounds.left + xsum / weight, capture_bounds.top + ysum / weight};
}

void pp_test_sprite_placed(void) {
  if (capture_changes) {
    PPVec2 actual = desktop_center();
    double drift = fmax(fabs(actual.x - probe_center.x), fabs(actual.y - probe_center.y));
    compositor_drift = fmax(compositor_drift, drift);
    compositor_samples++;
    printf("Desktop center during window placement: %.6f,%.6f drift=%.6fpx\n", actual.x, actual.y, drift);
  }
}

static void elongated_stability(PPDisplay display) {
  uint8_t pixels[128 * 16 * 4] = {0};
  for (int y = 0; y < 16; y++) for (int x = 0; x < 128; x++) {
    double dy = y - 7.5;
    for (int channel = 0; channel < 2; channel++) {
      double dx = x - (channel ? 103.5 : 23.5);
      uint8_t value = (uint8_t)lround(255 * exp(-(dx * dx + dy * dy) / 6));
      pixels[((size_t)y * 128 + x) * 4 + channel] = value;
      pixels[((size_t)y * 128 + x) * 4 + 3] += value;
    }
  }
  PPFrame frame = {pixels, NULL, 128, 16, 512};
  PPSourcePresentation source = {.frame = &frame, .content_id = 2,
      .source_revision = 1, .layout = {128, 16}, .visual_alpha = true};
  if (background) {
    display.desktop_units_per_dip = display.drawable_pixels_per_dip = 1.5;
    PPPose pose = {probe_center, probe_longest, 17.25, 1};
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    CHECK(pp_test_desktop_capture_start(capture_bounds));
    PPVec2 initial = desktop_center();
    printf("Initial desktop center: %.6f,%.6f\n", initial.x, initial.y);
    CHECK(fabs(initial.x - pose.center.x) < .1 && fabs(initial.y - pose.center.y) < .1);
    capture_changes = true;
    pose.angle_deg += 1;
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    PPVec2 completed = desktop_center();
    compositor_samples++;
    compositor_drift = fmax(compositor_drift, fmax(fabs(completed.x - pose.center.x), fabs(completed.y - pose.center.y)));
    pose.longest_dip += 16;
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    capture_changes = false;
    completed = desktop_center();
    compositor_samples++;
    compositor_drift = fmax(compositor_drift, fmax(fabs(completed.x - pose.center.x), fabs(completed.y - pose.center.y)));
    printf("Compositor placement maximum drift=%.6fpx samples=%u\n", compositor_drift, compositor_samples);
    CHECK(compositor_samples >= 2 && compositor_drift < .1);
    return;
  }
  double max_error[2] = {0};
  for (int d = 0; d < 3; d++) for (int kind = 0; kind < 2; kind++)
  for (int i = 0; i < 24; i++) {
    display.desktop_units_per_dip = display.drawable_pixels_per_dip = 1 + d * .5;
    PPPose pose = {{1600.25, 900.75}, kind ? 1536 : 1536 + i * .1,
        kind ? 17.25 + i * .02 : 17.25, 1};
    API(pp_retained_present(presenter, surface, &retained, source, pose, &display, &error));
    double angle = pose.angle_deg * 3.14159265358979323846 / 180;
    for (int channel = 0; channel < 2; channel++) {
      double offset = (channel ? 40 : -40) * pose.longest_dip * display.desktop_units_per_dip / 128;
      PPVec2 expected = {pose.center.x + offset * cos(angle), pose.center.y + offset * sin(angle)};
      PPVec2 actual = artwork_center_channel(channel);
      double drift = fmax(fabs(actual.x - expected.x), fabs(actual.y - expected.y));
      max_error[kind] = fmax(max_error[kind], drift);
    }
  }
  printf("Large elongated sprite landmark drift: scaling=%.6fpx rotation=%.6fpx\n", max_error[0], max_error[1]);
  CHECK(max_error[0] < .02 && max_error[1] < .02);
}

static void setup_stability(void) {
  API(pp_assets_create(&assets, &error));
  API(pp_project_create(assets, &project, &error));
  API(pp_renderer_create(&renderer, &error));
  printf("ENV configuration=%s presenter=shared-retained GPU_vendor=%s GPU_renderer=%s GL_version=%s\n",
      PP_STABILITY_CONFIGURATION, glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
  API(pp_anchor_presenter_create(&presenter, renderer, project, &error));
  API(pp_surface_create_gl(&surface, &error));
}

static void stability(void) {
  setup_stability();
  PPDisplay display = {1, {0, 0, 1920, 1080}, 1, 1};
  if (background) { elongated_stability(display); return; }
  uint8_t pixels[32 * 32 * 4] = {0};
  for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
    double dx = x - 15.5, dy = y - 15.5;
    uint8_t alpha = (uint8_t)lround(255 * exp(-(dx * dx + dy * dy) / 24));
    memset(pixels + ((size_t)y * 32 + x) * 4, alpha, 4);
  }
  PPFrame frame = {pixels, NULL, 32, 32, 128};
  PPSourcePresentation source = {.frame = &frame, .content_id = 1,
      .source_revision = 1, .layout = {32, 32}, .visual_alpha = true};
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
  pose.angle_deg=45;
  API(pp_retained_present(presenter,surface,&retained,source,pose,&display,&error));
  size_t large_output=(size_t)retained.bounds.w*(size_t)retained.bounds.h*4;
  CHECK(large_output>128*1024*1024&&large_output<PP_RETAINED_GPU_LIMIT);
  CHECK(pp_renderer_output_bytes(retained.renderer)==large_output);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==retained.gpu_bytes+large_output);
  printf("PASS admitted elongated 8192-pixel artwork at45degrees output=%.0fx%.0f logical_bytes=%zu GPU_limit=%u\n",
      retained.bounds.w,retained.bounds.h,large_output,PP_RETAINED_GPU_LIMIT);
  /* Releasing an anchor consumer may keep its native window alive for reuse. */
  pp_retained_release(presenter,&retained);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==0);
  source.frame=&frame;
  API(pp_retained_present(presenter,surface,&retained,source,(PPPose){{300,300},96,0,1},&display,&error));
  CHECK(pp_renderer_output_bytes(retained.renderer)==retained.output_bytes);
  elongated_stability(display);
}

int main(int argc, char **argv) { pp_renderer_diagnostic_observer(completed_draw,NULL);
  setvbuf(stdout, NULL, _IONBF, 0);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  atexit(cleanup);
  if (argc == 2 && !strcmp(argv[1], "--compositor")) {
    POINT pointer;
    CHECK(GetCursorPos(&pointer));
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    CHECK(GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY), &monitor));
    int width = monitor.rcMonitor.right - monitor.rcMonitor.left;
    int height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    int capture_w = min(2800, width - 40), capture_h = min(1100, height - 40);
    CHECK(capture_w >= 800 && capture_h >= 500);
    probe_center = (PPVec2){monitor.rcMonitor.left + width * .5 + .25, monitor.rcMonitor.top + height * .5 + .75};
    capture_bounds = (RECT){(LONG)(probe_center.x - capture_w * .5), (LONG)(probe_center.y - capture_h * .5),
        (LONG)(probe_center.x - capture_w * .5) + capture_w, (LONG)(probe_center.y - capture_h * .5) + capture_h};
    probe_longest = fmin(1536, fmin(capture_w * .85 / 1.5, capture_h * 1.5));
    printf("PROBE monitor=%ld,%ld,%ld,%ld capture=%ld,%ld,%ld,%ld actual_dpi=%u simulated_density=1.5 center=%.2f,%.2f longest_dip=%.2f\n",
        monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right, monitor.rcMonitor.bottom,
        capture_bounds.left, capture_bounds.top, capture_bounds.right, capture_bounds.bottom,
        GetDpiForSystem(), probe_center.x, probe_center.y, probe_longest);
    WNDCLASSW cls = {0};cls.lpfnWndProc = DefWindowProcW;cls.hInstance = GetModuleHandle(NULL);
    cls.lpszClassName = L"PointySpriteJitterBackground";cls.hbrBackground = GetStockObject(BLACK_BRUSH);
    CHECK(RegisterClassW(&cls));
    background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls.lpszClassName, L"Sprite jitter test",
        WS_POPUP | WS_VISIBLE, capture_bounds.left, capture_bounds.top,
        capture_bounds.right - capture_bounds.left, capture_bounds.bottom - capture_bounds.top,
        NULL, NULL, cls.hInstance, NULL);
    CHECK(background);CHECK(UpdateWindow(background));
    printf("PROBE selected_monitor_actual_dpi=%u\n", GetDpiForWindow(background));
  }
  if (argc == 2 && !strcmp(argv[1], "--cursor")) cursor();
  else stability();
  return 0;
}
