#include "pointy/platform.h"
#include "anchors.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int pp_text_probe(unsigned duration_ms);
int pp_overlay_probe(const char *mode, unsigned duration_ms) {
    if(!strcmp(mode,"emoji")) return pp_text_probe(duration_ms);
    if(!strcmp(mode,"anchors")) return pp_anchors_probe(duration_ms);
    bool changing = !strcmp(mode,"changing-alpha");
    if (!changing && strcmp(mode,"ring")) { fprintf(stderr,"Unknown probe mode: %s\n",mode); return 2; }
    PPRenderer *renderer = NULL; PPSurface *surface = NULL; PPError error = {0};
    PPFrame source = {0}, frame = {0}; int result = 1, clicks = 0, right = 0, wheels = 0;
    if (!pp_renderer_create(&renderer,&error) || !pp_surface_create(&surface,&error)) goto done;
    source.width = source.height = 64; source.stride = 256;
    source.pixels = calloc(64,256); source.content_alpha = calloc(64,64);
    if (!source.pixels || !source.content_alpha) { snprintf(error.message,sizeof(error.message),"Cannot allocate probe pixels"); goto done; }
    Uint64 start = SDL_GetTicks(), next = start; unsigned phase = 0;
    puts("Overlay probe at (220,220), rotated 37 degrees. Ctrl+C ends the probe.");
    while (!duration_ms || SDL_GetTicks()-start < duration_ms) {
        Uint64 now = SDL_GetTicks();
        if (now >= next) {
            memset(source.pixels,0,64*256); memset(source.content_alpha,0,64*64);
            for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
                double r = hypot(x-31.5,y-31.5);
                if (r >= 18 && r <= 29 && (!changing || !(phase%2))) {
                    size_t i=(size_t)y*64+x; source.pixels[i*4]=32; source.pixels[i*4+1]=180;
                    source.pixels[i*4+2]=255; source.pixels[i*4+3]=source.content_alpha[i]=255;
                }
            }
            pp_frame_destroy(&frame);
            if (!pp_renderer_transform(renderer,&source,64,37,1,1,&frame,&error) ||
                !pp_surface_present(surface,&frame,(PPRect){220,220,frame.width,frame.height},&error)) goto done;
            phase++; next=now+1000;
            if (!changing) next = UINT64_MAX;
        }
        PPPlatformEvent event;
        while (pp_platform_poll(&event)) {
            if (event.kind == PP_EVENT_QUIT) { result=0; goto done; }
            if (event.kind == PP_EVENT_BUTTON_DOWN) {
                if (event.button == PP_BUTTON_LEFT) clicks++;
                if (event.button == PP_BUTTON_RIGHT) right++;
            }
            if (event.kind == PP_EVENT_WHEEL) wheels++;
        }
        SDL_Delay(1);
    }
    result=0;
done:
    if (result) fprintf(stderr,"Overlay probe failed: %s\n",error.message);
    else printf("Probe %s completed: overlay left=%d right=%d wheel=%d\n",mode,clicks,right,wheels);
    pp_frame_destroy(&frame); pp_frame_destroy(&source); pp_surface_destroy(surface); pp_renderer_destroy(renderer);
    return result;
}
