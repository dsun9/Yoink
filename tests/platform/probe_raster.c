#include "performance_probe.h"
#define pp_text_raster pp_raw_text_raster
#define pp_text_raster_origin pp_raw_text_raster_origin
#define pp_text_raster_bounded pp_raw_text_raster_bounded
#define pp_text_raster_measure pp_raw_text_raster_measure
#define pp_text_raster_measure_bounded pp_raw_text_raster_measure_bounded
#include "../../src/media/text_raster.c"
#undef pp_text_raster
#undef pp_text_raster_origin
#undef pp_text_raster_bounded
#undef pp_text_raster_measure
#undef pp_text_raster_measure_bounded
bool pp_text_raster(TTF_Text *text, const PPTextRasterFont fonts[2],
  double shaping, double requested, const uint8_t color[4], PPFrame *out, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_text_raster(text, fonts, shaping, requested, color, out, error);
  pp_probe_record(PROBE_RASTER, start); return ok;
}
bool pp_text_raster_origin(TTF_Text *text,const PPTextRasterFont fonts[2],double shaping,double requested,const uint8_t color[4],PPFrame *out,PPVec2 *origin,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_raster_origin(text,fonts,shaping,requested,color,out,origin,error);pp_probe_record(PROBE_RASTER,start);return ok;
}
bool pp_text_raster_bounded(TTF_Text *text,const PPTextRasterFont fonts[2],double shaping,double requested,const uint8_t color[4],size_t limit,PPFrame *out,PPVec2 *origin,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_raster_bounded(text,fonts,shaping,requested,color,limit,out,origin,error);pp_probe_record(PROBE_RASTER,start);return ok;
}
bool pp_text_raster_measure(TTF_Text *text,const PPTextRasterFont fonts[2],double shaping,double requested,PPVec2 *extent,PPVec2 *origin,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_raster_measure(text,fonts,shaping,requested,extent,origin,error);pp_probe_record(PROBE_RASTER,start);return ok;
}
bool pp_text_raster_measure_bounded(TTF_Text *text,const PPTextRasterFont fonts[2],double shaping,double requested,size_t limit,PPVec2 *extent,PPVec2 *origin,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_text_raster_measure_bounded(text,fonts,shaping,requested,limit,extent,origin,error);pp_probe_record(PROBE_RASTER,start);return ok;
}
