#import <Cocoa/Cocoa.h>
#import <SDL3/SDL.h>
#include "pointy/platform.h"
#include "pointy/interaction.h"
#include <stdlib.h>
#include <string.h>
@interface PPOverlayPanel : NSPanel
@property(nonatomic, assign) PPSurface *surface;
@end
@interface PPFrameView : NSView
@property(nonatomic, assign) CGImageRef image;
@end
struct PPSurface {
    SDL_Window *window;
    PPOverlayPanel *panel;
    PPFrameView *view;
    CGImageRef image;
    PPRect bounds;
    uint64_t instance_id;
    struct PPSurface *next;
};
extern PPSurface *pp_mac_surfaces;
NSWindow *pp_mac_window(uint32_t id);
double pp_mac_top(void);
PPVec2 pp_mac_desktop(NSPoint point);
void pp_mac_record(PPSurface *surface, NSEvent *event);
void pp_mac_cancel(PPSurface *surface);
void pp_mac_forget(PPSurface *surface);
void pp_mac_enqueue(PPPlatformEvent event);
void pp_mac_input_add(void);
void pp_mac_input_remove(void);
void pp_mac_prioritize(void);
NSWindow *pp_mac_drawer(void);
/* Keep this local: font/file-only consumers must not pull the surface object
 * (and its native hooks) into portable tests that provide presentation stubs. */
static inline bool pp_mac_error(PPError *error, int code, const char *message) {
    if (error) { error->code=code; snprintf(error->message,sizeof(error->message),"%s",message); }
    return false;
}
