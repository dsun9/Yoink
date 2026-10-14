/* Compare the two straight sides of real, focused controls at the content
 * clip boundary. Hidden native windows exercise Nuklear conversion and GL. */
#include "../../src/ui/backend.h"
#include "../test.h"
#include <math.h>

static PPUIBackend *ui;
static bool border_failures;
static void cleanup(void) { pp_ui_backend_destroy(ui); ui = NULL; }

static void draw(const char *path) {
  PPError error = {0};
  CHECK(pp_ui_begin(ui, &error));
  struct nk_context *ctx = &ui->context;
  if (nk_begin(ctx, "borders", nk_rect(0, 0, ui->width, ui->height),
               NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_static(ctx, 36, 220, 1);
    pp_ui_button(ui, "button", "Button", true);
    nk_layout_row_static(ctx, 36, 220, 1);
    static bool enabled = true;
    pp_ui_switch(ui, "switch", "Enabled", &enabled, true);
    nk_layout_row_static(ctx, 36, 220, 1);
    static char number[32] = "96";
    pp_ui_edit(ui, "edit", number, sizeof(number), false, true);
    nk_layout_row_static(ctx, 72, 220, 1);
    static char text[128] = "Text";
    pp_ui_edit(ui, "item-text", text, sizeof(text), true, true);
  }
  nk_end(ctx);
  ui->capture_path = path;
  CHECK(pp_ui_end(ui, &error));
}

static double edge_ink(SDL_Surface *image, PPRect rect, bool right) {
  double sx = image->w / ui->width, sy = image->h / ui->height;
  int edge = (int)lround((rect.x + (right ? rect.w : 0)) * sx);
  double ink = 0;
  for (int y = (int)((rect.y + 12) * sy);
       y < (int)((rect.y + rect.h - 12) * sy); y++) {
    for (int x = edge - (int)ceil(4 * sx);
         x <= edge + (int)ceil(4 * sx); x++) {
      Uint8 r, g, b, a;
      CHECK(SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a));
      /* Accent coverage, excluding the neutral field and canvas colors. */
      ink += fmax(0, (double)b - r - 20);
    }
  }
  return ink;
}

static double capsule_coverage(double x, double y, PPRect shape) {
  unsigned inside = 0;
  for (int sy = 0; sy < 16; sy++) for (int sx = 0; sx < 16; sx++) {
    double px = x + (sx + .5) / 16 - shape.x - shape.w / 2;
    double py = y + (sy + .5) / 16 - shape.y - shape.h / 2;
    double radius = shape.h / 2;
    double dx = fmax(0, fabs(px) - (shape.w / 2 - radius));
    if (dx * dx + py * py <= radius * radius) inside++;
  }
  return inside / 256.0;
}

static void smooth_switch(SDL_Surface *image, PPRect bounds) {
  double scale = image->w / ui->width;
  PPRect track = {(bounds.x + bounds.w - 54) * scale,
                  (bounds.y + (bounds.h - 24) / 2) * scale, 46 * scale, 24 * scale};
  PPRect thumb = {track.x + 24 * scale, track.y + 3 * scale, 18 * scale, 18 * scale};
  double track_error = 0, thumb_error = 0;
  for (int y = (int)track.y - 1; y < track.y + track.h + 1; y++) {
    for (int x = (int)track.x - 1; x < track.x + 18 * scale; x++) {
      Uint8 r, g, b, a;
      CHECK(SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a));
      double actual = (pp_ui_theme.field.r - (double)r) /
                      (pp_ui_theme.field.r - pp_ui_theme.accent.r);
      track_error = fmax(track_error, fabs(actual - capsule_coverage(x, y, track)));
    }
  }
  for (int y = (int)thumb.y - 1; y < thumb.y + thumb.h + 1; y++) {
    for (int x = (int)thumb.x - 1; x < thumb.x + thumb.w + 1; x++) {
      Uint8 r, g, b, a;
      CHECK(SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a));
      double track_alpha = capsule_coverage(x, y, track);
      double thumb_alpha = capsule_coverage(x, y, thumb);
      double background = pp_ui_theme.field.r * (1 - track_alpha) +
                          pp_ui_theme.accent.r * track_alpha;
      double expected = background * (1 - thumb_alpha) +
                        pp_ui_theme.surface.r * thumb_alpha;
      thumb_error = fmax(thumb_error, fabs(r - expected) /
                        (pp_ui_theme.surface.r - pp_ui_theme.accent.r));
    }
  }
  fprintf(stderr, "switch %.0f%% curved-edge error: track=%.3f thumb=%.3f\n",
          scale * 100, track_error, thumb_error);
  CHECK(track_error < .25 && thumb_error < .25);
}

int main(void) {
  atexit(cleanup);
  const float scales[] = {1, 1.5f, 2};
  const char *controls[] = {"button", "switch", "edit", "item-text"};
  for (size_t scale = 0; scale < 3; scale++) {
    PPError error = {0};
    PPUIWindowOptions options = {"Hidden control borders", {0, 0, 360, 360},
                                scales[scale], false, true};
    CHECK(pp_ui_backend_create(&ui, &options, &error));
    draw(NULL);
    for (size_t control = 0; control < 4; control++) {
      pp_ui_focus(ui, controls[control]);
      char path[512];
      snprintf(path, sizeof(path), PP_BORDER_OUTPUT "/control-border-%s-%d.bmp",
               controls[control], (int)(scales[scale] * 100));
      draw(path);
      PPRect rect;
      CHECK(pp_ui_control_rect(ui, controls[control], &rect));
      SDL_Surface *image = SDL_LoadBMP(path);
      CHECK(image);
      double left = edge_ink(image, rect, false);
      double right = edge_ink(image, rect, true);
      fprintf(stderr, "%s %.0f%% border coverage: left=%.0f right=%.0f\n",
              controls[control], scales[scale] * 100, left, right);
      CHECK(left > 0 && right > 0);
      if (fabs(left - right) / fmax(left, right) >= .2) border_failures = true;
      if (control == 1) smooth_switch(image, rect);
      SDL_DestroySurface(image);
    }
    cleanup();
  }
  CHECK(!border_failures);
  puts("PASS complete button, switch and text-field borders at 100/150/200%.");
  return 0;
}
