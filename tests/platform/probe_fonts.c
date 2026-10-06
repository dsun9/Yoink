#include "performance_probe.h"
#define pp_font_inspect pp_raw_font_inspect
#include "../../src/media/fonts.c"
#undef pp_font_inspect
bool pp_font_inspect(PPBytes bytes, int face, PPFontInfo *out, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_font_inspect(bytes, face, out, error);
  pp_probe_record(PROBE_INSPECT, start); return ok;
}
