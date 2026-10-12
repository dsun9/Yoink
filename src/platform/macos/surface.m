#import "macos_internal.h"
#include <math.h>
#include <stdlib.h>
PPSurface *pp_mac_surfaces;
void pp_surface_set_rectangular(PPSurface *surface,bool enabled) {if(surface) surface->rectangular=enabled;}
void pp_surface_inherit_order(PPSurface *replacement,PPSurface *previous) {
    if(replacement && previous && previous->panel.visible)
        [replacement->panel orderWindow:NSWindowBelow relativeTo:previous->panel.windowNumber];
}
bool pp_surface_create_gl(PPSurface **out,PPError *error) {
    if(out) *out=NULL;
    return pp_mac_error(error,PP_ERROR_PLATFORM,"Direct OpenGL native behavior is unverified on macOS; use the compatibility surface");
}
uint32_t pp_surface_window_id(const PPSurface *surface) {(void)surface;return 0;}
bool pp_surface_place_gl(PPSurface *surface,PPRect bounds,bool interactive,PPError *error) {
    (void)bounds;(void)interactive;pp_surface_hide(surface);
    return pp_mac_error(error,PP_ERROR_PLATFORM,"Direct OpenGL native behavior is unverified on macOS");
}
@implementation PPFrameView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef context=NSGraphicsContext.currentContext.CGContext;
    CGContextClearRect(context, NSRectToCGRect(self.bounds));
    if (_image) {
        CGContextSaveGState(context);
        CGContextTranslateCTM(context,0,self.bounds.size.height);
        CGContextScaleCTM(context,1,-1);
        CGContextSetInterpolationQuality(context,kCGInterpolationNone);
        CGContextDrawImage(context,NSRectToCGRect(self.bounds),_image);
        CGContextRestoreGState(context);
    }
}
@end
@implementation PPOverlayPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
/* SDL's Cocoa listener samples the shape on motion and sets a whole-window
 * ignore flag. Never let that stale sample replace WindowServer's current
 * per-pixel transparency. The native receiver acceptance test must verify
 * this WindowServer behavior (including alpha=1) on each supported Mac. */
- (void)setIgnoresMouseEvents:(BOOL)ignore { (void)ignore; [super setIgnoresMouseEvents:NO]; }
- (void)sendEvent:(NSEvent *)event {
    switch (event.type) {
    case NSEventTypeLeftMouseDown: case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown: case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown: case NSEventTypeOtherMouseUp:
    case NSEventTypeMouseMoved: case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged: case NSEventTypeOtherMouseDragged:
    case NSEventTypeScrollWheel: pp_mac_record(_surface,event); return;
    default: [super sendEvent:event];
    }
}
@end
size_t pp_surface_raster_bytes(int width,int height) {
    /* One RGBA provider, plus SDL_SetWindowShape's four-byte ARGB copy. CGImage borrows
     * its provider storage. Cocoa/WindowServer backing and GPU are OS-managed. */
    return (size_t)width*height*8;
}
static void release_native(PPSurface *s) {
    if (!s) return;
    pp_mac_cancel(s);
    [s->panel orderOut:nil];
    s->view.image=NULL;
    if(s->window) SDL_DestroyWindow(s->window);
    s->window=NULL;
    [s->panel close]; [s->panel release]; s->panel=nil;
    [s->view release]; s->view=nil;
    if(s->image) CGImageRelease(s->image);
    s->image=NULL;
    pp_mac_forget(s);
}
bool pp_surface_create(PPSurface **out,PPError *error) {
    if(!out) return pp_mac_error(error,PP_ERROR_ARGUMENT,"Missing surface output");
    *out=NULL;
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) return pp_mac_error(error,PP_ERROR_PLATFORM,SDL_GetError());
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    PPSurface *s=calloc(1,sizeof(*s));
    if(!s) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot allocate overlay"); }
    s->panel=[[PPOverlayPanel alloc] initWithContentRect:NSMakeRect(0,0,1,1)
        styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
    s->panel.releasedWhenClosed=NO; s->panel.surface=s;
    s->panel.opaque=NO; s->panel.backgroundColor=NSColor.clearColor;
    s->panel.hasShadow=NO; s->panel.hidesOnDeactivate=NO;
    s->panel.level=NSFloatingWindowLevel;
    s->panel.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary;
    s->view=[[PPFrameView alloc] initWithFrame:NSMakeRect(0,0,1,1)];
    s->panel.contentView=s->view;
    SDL_PropertiesID props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER,s->panel);
    SDL_SetPointerProperty(props,SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER,s->view);
    SDL_SetBooleanProperty(props,SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN,true);
    SDL_SetBooleanProperty(props,SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,true);
    SDL_SetBooleanProperty(props,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,true);
    s->window=SDL_CreateWindowWithProperties(props); SDL_DestroyProperties(props);
    if(!s->panel || !s->window) {
        release_native(s); free(s); SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return pp_mac_error(error,PP_ERROR_PLATFORM,"Cannot create Cocoa overlay");
    }
    s->next=pp_mac_surfaces; pp_mac_surfaces=s; pp_mac_input_add(); *out=s; return true;
}
static void free_pixels(void *info,const void *data,size_t size) { (void)info;(void)size;free((void *)data); }
bool pp_surface_present(PPSurface *s,const PPFrame *frame,PPRect b,PPError *error) {
    if(!s || !s->window || !frame || !frame->pixels || !frame->content_alpha ||
       frame->width<=0 || frame->height<=0 || frame->width>8192 || frame->height>8192 ||
       frame->stride<(size_t)frame->width*4 || !isfinite(b.x)||!isfinite(b.y)||!isfinite(b.w)||!isfinite(b.h)||b.w<=0||b.h<=0) {
        release_native(s); return pp_mac_error(error,PP_ERROR_ARGUMENT,"Invalid Cocoa frame or bounds");
    }
    size_t bytes=(size_t)frame->width*frame->height*4;
    uint8_t *pixels=malloc(bytes);
    if(!pixels) {release_native(s);return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot copy Cocoa frame");}
    for(int y=0;y<frame->height;y++) memcpy(pixels+(size_t)y*frame->width*4,frame->pixels+(size_t)y*frame->stride,(size_t)frame->width*4);
    CGDataProviderRef provider=CGDataProviderCreateWithData(NULL,pixels,bytes,free_pixels);
    if(!provider) {free(pixels);release_native(s);return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot create image provider");}
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(frame->width,frame->height,8,32,(size_t)frame->width*4,color,
        kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big,provider,NULL,false,kCGRenderingIntentDefault);
    CGColorSpaceRelease(color); CGDataProviderRelease(provider);
    SDL_Surface *shape=SDL_CreateSurfaceFrom(frame->width,frame->height,SDL_PIXELFORMAT_RGBA32,frame->pixels,(int)frame->stride);
    bool ok=image && (s->rectangular?SDL_SetWindowShape(s->window,NULL):(shape && SDL_SetWindowShape(s->window,shape)));
    SDL_DestroySurface(shape);
    if(!ok) {if(image)CGImageRelease(image);release_native(s);return pp_mac_error(error,PP_ERROR_PLATFORM,"Cannot present Cocoa alpha shape");}
    bool visible=s->panel.visible;
    s->view.image=image;
    if(s->image)CGImageRelease(s->image);
    s->image=image; s->bounds=b;
    [s->panel setFrame:NSMakeRect(b.x,pp_mac_top()-b.y-b.h,b.w,b.h) display:NO];
    s->view.frame=NSMakeRect(0,0,b.w,b.h);
    [s->view setNeedsDisplay:YES];
    [s->panel displayIfNeeded];
    [s->panel flushWindow];
    if(!visible) [s->panel orderFrontRegardless];
    pp_mac_prioritize();
    return true;
}
void pp_surface_hide(PPSurface *s) {if(s) {pp_mac_cancel(s);[s->panel orderOut:nil];}}
void pp_surface_destroy(PPSurface *s) {
    if(!s)return;
    release_native(s);
    PPSurface **link=&pp_mac_surfaces;while(*link&&*link!=s)link=&(*link)->next;
    if(*link)*link=s->next;
    free(s);pp_mac_input_remove();SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
void pp_surface_raise(PPSurface *s) {
    if(!s || !s->panel.visible)return;
    /* Raise only within application sprites. Do not jump over an unrelated
     * application's floating window on every accepted sprite press. */
    for(NSWindow *window in NSApp.orderedWindows) {
        for(PPSurface *candidate=pp_mac_surfaces;candidate;candidate=candidate->next) {
            if(candidate->panel==window && candidate->instance_id && window.visible) {
                if(candidate!=s) [s->panel orderWindow:NSWindowAbove relativeTo:window.windowNumber];
                pp_mac_prioritize();return;
            }
        }
    }
}
void pp_surface_set_instance_id(PPSurface *s,uint64_t id) {if(s)s->instance_id=id;}
