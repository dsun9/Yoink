#include "performance_probe.h"
#define pp_text_render pp_raw_text_render
#define pp_text_render_result pp_raw_text_render_result
#define pp_text_render_result_bounded pp_raw_text_render_result_bounded
#include "../../src/media/text.c"
#undef pp_text_render
#undef pp_text_render_result
#undef pp_text_render_result_bounded
bool pp_text_render(PPAssets *assets, const char *text, const PPTextStyle *style,
                    double scale, PPFrame *out, PPTextDiagnostics *diagnostics, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_text_render(assets, text, style, scale, out, diagnostics, error);
  pp_probe_record(PROBE_TEXT, start); return ok;
}
bool pp_text_render_result_bounded(PPAssets *assets,const char *text,const PPTextStyle *style,double scale,size_t limit,PPTextResult *out,PPTextDiagnostics *diagnostics,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_render_result_bounded(assets,text,style,scale,limit,out,diagnostics,error);
  pp_probe_record(PROBE_TEXT,start);return ok;
}
bool pp_text_render_result(PPAssets *assets,const char *text,const PPTextStyle *style,double scale,PPTextResult *out,PPTextDiagnostics *diagnostics,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_render_result(assets,text,style,scale,out,diagnostics,error);
  pp_probe_record(PROBE_TEXT,start);return ok;
}
