#include "../../app/tray.h"
#include <windows.h>
/* Pinned SDL handles TaskbarCreated with NIM_ADD/NIM_SETVERSION. */
bool pp_platform_tray_create(PPTray **out, PPApp *app, PPError *error) {
  return pp_tray_native_create(out,app,error);
}
void pp_platform_tray_destroy(PPTray *tray) { pp_tray_native_destroy(tray); }
void pp_tray_wait(unsigned milliseconds) {
  MsgWaitForMultipleObjectsEx(0,NULL,milliseconds,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
}
