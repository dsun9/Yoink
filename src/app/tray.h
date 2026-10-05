#ifndef POINTY_TRAY_H
#define POINTY_TRAY_H
#include "pointy/ui.h"
#include <SDL3/SDL.h>
struct PPTray { SDL_Tray *native; PPApp *app; bool pending; PPCommandKind command; };
bool pp_tray_create(PPTray **out, PPApp *app, PPError *error);
void pp_tray_destroy(PPTray *tray);
bool pp_tray_take(PPTray *tray, PPCommandKind *command);
void pp_tray_wait(unsigned milliseconds);
bool pp_tray_native_create(PPTray **out, PPApp *app, PPError *error);
void pp_tray_native_destroy(PPTray *tray);
#endif
