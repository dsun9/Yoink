#include <windows.h>
static HOOKPROC original_hook;
static unsigned max_arrival_ms,observed;
static LRESULT CALLBACK timed_hook(int code,WPARAM w,LPARAM l) {
  if(code==HC_ACTION) {
    const MSLLHOOKSTRUCT *mouse=(const MSLLHOOKSTRUCT *)l;
    if(mouse->dwExtraInfo==0x50504558) {
      unsigned wait=GetTickCount()-mouse->time;
      if(wait>max_arrival_ms)max_arrival_ms=wait;
      observed++;
    }
  }
  return original_hook(code,w,l);
}
static HHOOK WINAPI measured_hook(int kind,HOOKPROC proc,HINSTANCE module,DWORD thread) {
  original_hook=proc;return SetWindowsHookExW(kind,timed_hook,module,thread);
}
unsigned pp_test_exposure_input_wait(void){return max_arrival_ms;}
unsigned pp_test_exposure_input_count(void){return observed;}
#define SetWindowsHookExW measured_hook
#include "../../src/platform/windows/input.c"
