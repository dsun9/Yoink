#include "performance_probe.h"
#define pp_ui_backend_create pp_raw_ui_backend_create
#define pp_ui_upload_frame pp_raw_ui_upload_frame
#define pp_ui_end pp_raw_ui_end
#define pp_platform_find_font pp_probe_find_font
#include "../../src/ui/backend.c"
#undef pp_ui_backend_create
#undef pp_ui_upload_frame
#undef pp_ui_end
#undef pp_platform_find_font
bool pp_platform_find_font(const char *, PPBlob *, PPError *);
bool pp_probe_find_font(const char *family, PPBlob *out, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_platform_find_font(family, out, error);
  pp_probe_record(PROBE_FIND_FONT, start);
  pp_probe_font(family,ok,out->size,pp_probe_clock()-start);
  return ok;
}
bool pp_ui_backend_create(PPUIBackend **out, const PPUIWindowOptions *options, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_ui_backend_create(out, options, error);
  pp_probe_record(PROBE_UI_CREATE, start); return ok;
}
bool pp_ui_upload_frame(PPUIBackend *ui, unsigned *texture, const PPFrame *frame, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_ui_upload_frame(ui, texture, frame, error);
  pp_probe_record(PROBE_UPLOAD, start); return ok;
}
bool pp_ui_end(PPUIBackend *ui, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_ui_end(ui, error);
  pp_probe_record(PROBE_SWAP, start); return ok;
}
