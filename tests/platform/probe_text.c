#include "performance_probe.h"
#define pp_text_render pp_raw_text_render
#include "../../src/media/text.c"
#undef pp_text_render
bool pp_text_render(PPAssets *assets, const char *text, const PPTextStyle *style,
                    double scale, PPFrame *out, PPTextDiagnostics *diagnostics, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_text_render(assets, text, style, scale, out, diagnostics, error);
  pp_probe_record(PROBE_TEXT, start); return ok;
}
