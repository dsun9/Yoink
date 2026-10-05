#import "macos_internal.h"
#include <stdlib.h>
/* Cocoa global points are bottom-left; our desktop origin is the top-left of
 * the primary screen, including negative origins on other displays. */
double pp_mac_top(void) {return NSMaxY(NSScreen.screens.firstObject.frame);}
PPVec2 pp_mac_desktop(NSPoint p) {return (PPVec2){p.x,pp_mac_top()-p.y};}
bool pp_platform_displays(PPDisplay **out,size_t *count,PPError *error) {
    if(!out||!count)return pp_mac_error(error,PP_ERROR_ARGUMENT,"Missing display outputs");
    *out=NULL;*count=0;
    NSArray<NSScreen *> *screens=NSScreen.screens;
    PPDisplay *items=calloc(screens.count,sizeof(*items));
    if(!items)return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot allocate displays");
    size_t i=0;
    for(NSScreen *screen in screens) {
        NSRect r=screen.frame;
        items[i++]=(PPDisplay){[screen.deviceDescription[@"NSScreenNumber"] unsignedLongLongValue],
            {r.origin.x,pp_mac_top()-NSMaxY(r),r.size.width,r.size.height},1,screen.backingScaleFactor};
    }
    *out=items;*count=i;return true;
}
void pp_displays_free(PPDisplay *items) {free(items);}
