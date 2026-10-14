#include "tray.h"
#include <stdlib.h>
#include <string.h>
static void action(void *userdata, SDL_TrayEntry *entry) {
  PPTray *tray = userdata;
  /* Native/menu callbacks must never recursively mutate the project. */
  if (!tray->pending) {
    tray->command = !strcmp(SDL_GetTrayEntryLabel(entry), "Quit") ? PP_COMMAND_QUIT : PP_COMMAND_SHOW_SETTINGS;
    tray->pending = true;
  }
}
bool pp_tray_native_create(PPTray **out, PPApp *app, PPError *error) {
  if (out) *out = NULL;
  if (!out || !app) return false;
  PPTray *tray = calloc(1, sizeof(*tray));
  if (!tray) return false;
  tray->app = app;
  SDL_Surface *icon = SDL_CreateSurface(32,32,SDL_PIXELFORMAT_RGBA32);
  if (icon) {
    SDL_ClearSurface(icon,0,0,0,0);
    for (int y=3;y<28;y++) for (int x=5;x<25;x++) if (x<6+(y-3)*3/4) {
      Uint8 *p=(Uint8 *)icon->pixels+y*icon->pitch+x*4;
      p[0]=255;p[1]=190;p[2]=45;p[3]=255;
    }
    tray->native = SDL_CreateTray(icon,"Yoink");
    SDL_DestroySurface(icon);
  }
  SDL_TrayMenu *menu = tray->native ? SDL_CreateTrayMenu(tray->native) : NULL;
  SDL_TrayEntry *settings = menu ? SDL_InsertTrayEntryAt(menu,-1,"Settings",SDL_TRAYENTRY_BUTTON) : NULL;
  SDL_TrayEntry *quit = menu ? SDL_InsertTrayEntryAt(menu,-1,"Quit",SDL_TRAYENTRY_BUTTON) : NULL;
  if (!settings || !quit) {
    if (error) { error->code=PP_ERROR_PLATFORM; SDL_snprintf(error->message,sizeof(error->message),"Tray: %s",SDL_GetError()); }
    pp_tray_native_destroy(tray); return false;
  }
  SDL_SetTrayEntryCallback(settings,action,tray);
  SDL_SetTrayEntryCallback(quit,action,tray);
  *out=tray; return true;
}
void pp_tray_native_destroy(PPTray *tray) {
  if (tray) { if (tray->native) SDL_DestroyTray(tray->native); free(tray); }
}
bool pp_tray_create(PPTray **out, PPApp *app, PPError *error) { return pp_platform_tray_create(out,app,error); }
void pp_tray_destroy(PPTray *tray) { pp_platform_tray_destroy(tray); }
bool pp_tray_take(PPTray *tray, PPCommandKind *command) {
  if (!tray || !tray->pending) return false;
  *command=tray->command; tray->pending=false; return true;
}
