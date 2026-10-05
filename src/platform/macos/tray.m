#import "macos_internal.h"
#include "../../app/tray.h"
bool pp_platform_tray_create(PPTray **out, PPApp *app, PPError *error) {
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  return pp_tray_native_create(out,app,error);
}
void pp_platform_tray_destroy(PPTray *tray) { pp_tray_native_destroy(tray); }
void pp_tray_wait(unsigned milliseconds) { SDL_WaitEventTimeout(NULL,(Sint32)milliseconds); }
