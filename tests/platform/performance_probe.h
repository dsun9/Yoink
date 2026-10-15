#ifndef PP_PERFORMANCE_PROBE_H
#define PP_PERFORMANCE_PROBE_H
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
enum PPProbeStage { PROBE_TEXT, PROBE_RASTER, PROBE_INSPECT, PROBE_FIND_FONT,
  PROBE_RENDERER_CREATE, PROBE_TRANSFORM, PROBE_UI_CREATE, PROBE_UPLOAD,
  PROBE_SWAP, PROBE_SURFACE, PROBE_DIB, PROBE_LAYERED, PROBE_READBACK,
  PROBE_TEXTURE, PROBE_SOURCE_REQUEST, PROBE_GPU_SUBMIT, PROBE_GPU_EXECUTION,
  PROBE_GPU_COMPLETE, PROBE_NATIVE_SIZE, PROBE_NATIVE_PLACE, PROBE_CONTEXT,
  PROBE_GL_SWAP, PROBE_GL_FINISH, PROBE_QUERY_RESULT, PROBE_DRAW_STATE,
  PROBE_COMPOSITION_BEGIN,PROBE_INTEROP_REGISTER,PROBE_INTEROP_LOCK,PROBE_COMPOSITION_COPY,
  PROBE_INTEROP_UNLOCK,PROBE_INTEROP_UNREGISTER,PROBE_COMPOSITION_END,PROBE_COMPOSITION_COMMIT,PROBE_COUNT };
typedef struct PPProbeMetric { unsigned count; double ms, max_ms; } PPProbeMetric;
/* Workers record isolated stage timings; owner snapshots never race a render.
 * Successful source work is aggregated through presenter worker metrics. */
extern __declspec(thread) PPProbeMetric pp_probe_metrics[PROBE_COUNT];
extern bool pp_probe_completion_enabled;
#define PP_PROBE_INPUT_TAG ((ULONG_PTR)0x5050464d)
extern unsigned pp_probe_injected_hook_count, pp_probe_injected_max_wait;
extern unsigned pp_probe_wheel_count;
struct PPRasterWorker;
extern struct PPRasterWorker *pp_probe_active_worker;
extern unsigned pp_probe_worker_input_count,pp_probe_worker_input_max_wait;
extern double pp_probe_last_input_qpc_ms;
extern unsigned pp_probe_last_input_kind,pp_probe_last_input_wait;
extern const char *pp_probe_owner_phase;
extern double pp_probe_last_owner_end_ms,pp_probe_last_wheel_hook_ms;
extern volatile double pp_probe_move_send_ms,pp_probe_wheel_send_ms;
extern bool pp_probe_log_overflow;
/* Bounded owner diagnostic output. Active input never waits on stdout I/O. */
int pp_probe_printf(const char *format,...);
int pp_probe_puts(const char *text);
int pp_probe_fflush(FILE *stream);
bool pp_probe_finish_output(bool cleaned_up);
double pp_probe_clock(void);
void pp_probe_record(enum PPProbeStage stage, double start);
void pp_probe_font(const char *family, bool found, size_t bytes, double ms);
#endif
