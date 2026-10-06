#ifndef PP_PERFORMANCE_PROBE_H
#define PP_PERFORMANCE_PROBE_H
#include <windows.h>
#include <stdbool.h>
enum PPProbeStage { PROBE_TEXT, PROBE_RASTER, PROBE_INSPECT, PROBE_FIND_FONT,
  PROBE_RENDERER_CREATE, PROBE_TRANSFORM, PROBE_UI_CREATE, PROBE_UPLOAD,
  PROBE_SWAP, PROBE_SURFACE, PROBE_DIB, PROBE_LAYERED, PROBE_READBACK,
  PROBE_TEXTURE, PROBE_COUNT };
typedef struct PPProbeMetric { unsigned count; double ms, max_ms; } PPProbeMetric;
extern PPProbeMetric pp_probe_metrics[PROBE_COUNT];
double pp_probe_clock(void);
void pp_probe_record(enum PPProbeStage stage, double start);
void pp_probe_font(const char *family, bool found, size_t bytes, double ms);
#endif
