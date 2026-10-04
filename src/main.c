#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pointy/ui.h"
#include <SDL3/SDL.h>
int pp_overlay_probe(const char *mode, unsigned duration_ms);
int main(int argc, char **argv) {
    if ((argc == 3 || argc == 5) && !strcmp(argv[1], "--overlay-probe")) {
        unsigned duration = 0;
        if (argc == 5) {
            if (strcmp(argv[3], "--duration-ms")) return 2;
            char *end; unsigned long parsed = strtoul(argv[4], &end, 10);
            if (*end || !parsed || parsed > 3600000) return 2;
            duration = (unsigned)parsed;
        }
        return pp_overlay_probe(argv[2], duration);
    }
    if (argc > 2 || (argc == 2 && strcmp(argv[1],"--settings"))) {
        fputs("Usage: PointyPointer [--settings | --overlay-probe ring|changing-alpha|anchors|emoji [--duration-ms N]]\n",stderr);
        return 2;
    }
    PPApp *app=NULL; PPSettings *settings=NULL; PPError error={0};
    if (!pp_app_create(&app,NULL,&error)) { fprintf(stderr,"%s\n",error.message); return 1; }
    /* Normal startup stays silent. Tray entry points and full sprite scheduling
     * join this lifecycle in Tasks 9/11; --settings exposes the native editor. */
    if (argc==2 && !pp_settings_create(&settings,app,&error)) {
        fprintf(stderr,"%s\n",error.message); pp_app_destroy(app); return 1;
    }
    while (!pp_app_ui(app)->quit_requested) {
        PPPlatformEvent event;
        while (pp_ui_poll(&event)) {
            if (event.kind==PP_EVENT_QUIT) pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_QUIT},&error);
            else if (settings) pp_settings_event(settings,&event);
        }
        while (pp_platform_poll(&event)) {
            if (event.kind==PP_EVENT_QUIT) pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_QUIT},&error);
        }
        if (settings) pp_settings_draw(settings);
        SDL_Delay(settings && pp_app_ui(app)->settings_open ? 16 : 50);
    }
    pp_settings_destroy(settings); pp_app_destroy(app);
    return 0;
}
