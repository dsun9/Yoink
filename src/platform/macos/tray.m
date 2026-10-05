#import "macos_internal.h"
#include "pointy/app.h"
struct PPTray { SDL_Tray *native; PPApp *app; };
static void action(void *userdata,SDL_TrayEntry *entry) {
    PPTray *tray=userdata;
    PPCommand command={.kind=!strcmp(SDL_GetTrayEntryLabel(entry),"Quit")?PP_COMMAND_QUIT:PP_COMMAND_SHOW_SETTINGS};
    PPError error={0};pp_app_command(tray->app,&command,&error);
}
bool pp_platform_tray_create(PPTray **out,PPApp *app,PPError *error) {
    if(!out||!app)return pp_mac_error(error,PP_ERROR_ARGUMENT,"Missing tray output or app");
    *out=NULL;[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    PPTray *tray=calloc(1,sizeof(*tray));if(!tray)return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot allocate tray");
    tray->app=app;
    SDL_Surface *icon=SDL_CreateSurface(18,18,SDL_PIXELFORMAT_RGBA32);
    if(icon) {
        SDL_ClearSurface(icon,0,0,0,0);
        for(int y=3;y<15;y++)for(int x=4;x<12;x++)if(x<5+(y-3)/2) {
            uint8_t *p=(uint8_t *)icon->pixels+y*icon->pitch+x*4;p[0]=p[1]=p[2]=0;p[3]=255;
        }
        tray->native=SDL_CreateTray(icon,"PointyPointer");SDL_DestroySurface(icon);
    }
    SDL_TrayMenu *menu=tray->native?SDL_CreateTrayMenu(tray->native):NULL;
    SDL_TrayEntry *settings=menu?SDL_InsertTrayEntryAt(menu,-1,"Settings",SDL_TRAYENTRY_BUTTON):NULL;
    SDL_TrayEntry *quit=menu?SDL_InsertTrayEntryAt(menu,-1,"Quit",SDL_TRAYENTRY_BUTTON):NULL;
    if(!settings||!quit){pp_platform_tray_destroy(tray);return pp_mac_error(error,PP_ERROR_PLATFORM,SDL_GetError());}
    SDL_SetTrayEntryCallback(settings,action,tray);SDL_SetTrayEntryCallback(quit,action,tray);*out=tray;return true;
}
void pp_platform_tray_destroy(PPTray *tray) {if(tray){if(tray->native)SDL_DestroyTray(tray->native);free(tray);}}
