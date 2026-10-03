#ifndef POINTY_PLATFORM_H
#define POINTY_PLATFORM_H
#include "pointy/render.h"
typedef struct PPSurface PPSurface;
typedef enum PPPlatformEventKind {
    PP_EVENT_POINTER_MOVE, PP_EVENT_BUTTON_DOWN, PP_EVENT_BUTTON_UP,
    PP_EVENT_WHEEL, PP_EVENT_QUIT
} PPPlatformEventKind;
typedef enum PPButton { PP_BUTTON_NONE, PP_BUTTON_LEFT, PP_BUTTON_RIGHT, PP_BUTTON_MIDDLE } PPButton;
enum { PP_MOD_SHIFT = 1, PP_MOD_CONTROL = 2, PP_MOD_ALT = 4 };
typedef struct PPPlatformEvent {
    PPSurface *surface;
    PPPlatformEventKind kind;
    PPVec2 desktop_point;
    PPButton button;
    unsigned modifiers;
    double wheel_delta, timestamp;
} PPPlatformEvent;
bool pp_surface_create(PPSurface **out, PPError *error);
/* Borrows frame. Desktop bounds use physical desktop pixels. Dimensions must
 * match the frame. Failure hides and destroys the native interceptor; caller
 * still releases the PPSurface owner with pp_surface_destroy. */
bool pp_surface_present(PPSurface *surface, const PPFrame *frame,
    PPRect desktop_bounds, PPError *error);
void pp_surface_destroy(PPSurface *surface);
/* Pumps native messages on the main thread, then pops one normalized event. */
bool pp_platform_poll(PPPlatformEvent *out);
#endif
