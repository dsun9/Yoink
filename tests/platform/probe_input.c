#include "performance_probe.h"
#include "../../src/app/raster_worker.h"
static HOOKPROC original_hook;
unsigned pp_probe_hook_count, pp_probe_hook_max_wait;
unsigned pp_probe_injected_hook_count, pp_probe_injected_max_wait, pp_probe_wheel_count;
double pp_probe_hook_max_ms;
PPRasterWorker *pp_probe_active_worker;
unsigned pp_probe_worker_input_count,pp_probe_worker_input_max_wait;
double pp_probe_last_input_qpc_ms;
unsigned pp_probe_last_input_kind,pp_probe_last_input_wait;
static LRESULT CALLBACK timed_hook(int code,WPARAM w,LPARAM l) {
  double start=pp_probe_clock();
  if(code==HC_ACTION) {
    MSLLHOOKSTRUCT *mouse=(MSLLHOOKSTRUCT *)l;
    DWORD wait=GetTickCount()-mouse->time;
    if(wait>pp_probe_hook_max_wait) pp_probe_hook_max_wait=wait;
    pp_probe_hook_count++;
    if(mouse->dwExtraInfo==PP_PROBE_INPUT_TAG) {
      pp_probe_last_input_qpc_ms=start;pp_probe_last_input_kind=(unsigned)w;pp_probe_last_input_wait=wait;
      pp_probe_injected_hook_count++;
      if(wait>pp_probe_injected_max_wait) pp_probe_injected_max_wait=wait;
      if(w==WM_MOUSEWHEEL) pp_probe_wheel_count++;
      /* Low-level hooks execute on the installing owner thread. Query only
       * synchronized status; no worker mutates the owner probe globals. */
      if(pp_probe_active_worker&&pp_raster_worker_status(pp_probe_active_worker).pending_jobs) {
        pp_probe_worker_input_count++;if(wait>pp_probe_worker_input_max_wait)pp_probe_worker_input_max_wait=wait;
      }
    }
  }
  LRESULT result=original_hook(code,w,l);
  double elapsed=pp_probe_clock()-start;
  if(elapsed>pp_probe_hook_max_ms) pp_probe_hook_max_ms=elapsed;
  return result;
}
static HHOOK WINAPI probe_hook(int kind,HOOKPROC proc,HINSTANCE module,DWORD thread) {
  original_hook=proc; return SetWindowsHookExW(kind,timed_hook,module,thread);
}
#define SetWindowsHookExW probe_hook
#include "../../src/platform/windows/input.c"
