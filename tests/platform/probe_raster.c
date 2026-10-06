#include "performance_probe.h"
#define pp_text_raster pp_raw_text_raster
#include "../../src/media/text_raster.c"
#undef pp_text_raster
bool pp_text_raster(TTF_Text *text, const PPTextRasterFont fonts[2],
  double shaping, double requested, const uint8_t color[4], PPFrame *out, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_text_raster(text, fonts, shaping, requested, color, out, error);
  pp_probe_record(PROBE_RASTER, start); return ok;
}
