#include "performance_probe.h"
static HOOKPROC original_hook;
unsigned pp_probe_hook_count, pp_probe_hook_max_wait;
double pp_probe_hook_max_ms;
static LRESULT CALLBACK timed_hook(int code,WPARAM w,LPARAM l) {
  double start=pp_probe_clock();
  if(code==HC_ACTION) {
    MSLLHOOKSTRUCT *mouse=(MSLLHOOKSTRUCT *)l;
    DWORD wait=GetTickCount()-mouse->time;
    if(wait>pp_probe_hook_max_wait) pp_probe_hook_max_wait=wait;
    pp_probe_hook_count++;
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
