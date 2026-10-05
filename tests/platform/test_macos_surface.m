/* Native acceptance: the same executable launches an independent Cocoa
 * receiver. CGEventPost sends original OS events; production never replays.
 * The receiver also restores input/focus if the test exits or times out. */
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <SDL3/SDL.h>
#include "pointy/platform.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
typedef struct Shared {
    atomic_int ready, done, restored, clicks, right, wheels, fullscreen, fullscreen_ready;
    int window_number;
    pid_t parent, previous_pid;
    double cursor_x, cursor_y, x, y;
} Shared;
static Shared *shared;
static pid_t child;
static PPSurface *surface, *other;
static SDL_Window *settings, *drawer;
static PPRenderer *renderer;
static int clicks, right_clicks, wheels, moves, ups, cancels;
static PPVec2 last_point;
static PPError error;
static double seconds(void) { return SDL_GetTicksNS()/1e9; }
static double top(void) { return NSMaxY(NSScreen.screens.firstObject.frame); }
static void mouse(CGEventType type, CGPoint point, CGMouseButton button) {
    CGEventRef e=CGEventCreateMouseEvent(NULL,type,point,button);
    if (!e) { fprintf(stderr,"Cannot allocate mouse event\n"); exit(1); }
    CGEventPost(kCGSessionEventTap,e); CFRelease(e);
}
static void pump(double duration) {
    double until=seconds()+duration;
    do {
        PPPlatformEvent e;
        while(pp_platform_poll(&e)) {
            if(e.surface==surface) {
                if(e.kind==PP_EVENT_BUTTON_DOWN && e.button==PP_BUTTON_LEFT) clicks++;
                if(e.kind==PP_EVENT_BUTTON_DOWN && e.button==PP_BUTTON_RIGHT) right_clicks++;
                if(e.kind==PP_EVENT_BUTTON_UP) ups++;
                if(e.kind==PP_EVENT_WHEEL) wheels++;
                if(e.kind==PP_EVENT_POINTER_MOVE) {moves++;last_point=e.desktop_point;}
                if(e.kind==PP_EVENT_CANCEL) cancels++;
            }
        }
        SDL_Delay(1);
    } while(seconds()<until);
}
static void cleanup(void) {
    pp_platform_drawer_close(false);
    pp_platform_settings_window(0);
    SDL_DestroyWindow(drawer); SDL_DestroyWindow(settings);
    pp_surface_destroy(other); pp_surface_destroy(surface);
    pp_renderer_destroy(renderer);
    if(shared && child>0) {
        atomic_store(&shared->done,1);
        int status=0;
        while(waitpid(child,&status,0)<0 && errno==EINTR) {}
        if(!atomic_load(&shared->restored)) {
            fprintf(stderr,"FAIL receiver could not verify original foreground/cursor restoration\n");
            _Exit(3);
        }
        munmap(shared,sizeof(*shared));
    }
}
#define REQUIRE(x) do { if(!(x)) {fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error.message);exit(1);} } while(0)
@interface ReceiverWindow : NSWindow
@end
@implementation ReceiverWindow
- (void)sendEvent:(NSEvent *)event {
    if(event.type==NSEventTypeLeftMouseDown) atomic_fetch_add(&shared->clicks,1);
    if(event.type==NSEventTypeRightMouseDown) atomic_fetch_add(&shared->right,1);
    if(event.type==NSEventTypeScrollWheel) atomic_fetch_add(&shared->wheels,1);
    [super sendEvent:event];
}
@end
static int receiver(const char *path) {
    int fd=open(path,O_RDWR);
    if(fd<0)return 2;
    shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);
    if(shared==MAP_FAILED)return 2;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    ReceiverWindow *window=[[ReceiverWindow alloc] initWithContentRect:
        NSMakeRect(shared->x,top()-shared->y-320,480,320)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    window.title=@"PointyPointer independent input receiver";
    window.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
    [NSApp finishLaunching];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    shared->window_number=(int)window.windowNumber;
    atomic_store(&shared->ready,1);
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:60];
    bool toggled=false;
    while(!atomic_load(&shared->done) && kill(shared->parent,0)==0 && deadline.timeIntervalSinceNow>0) {
        @autoreleasepool {
            if(atomic_load(&shared->fullscreen) && !toggled) {[window toggleFullScreen:nil];toggled=true;}
            if(toggled && (window.styleMask&NSWindowStyleMaskFullScreen)) atomic_store(&shared->fullscreen_ready,1);
            NSEvent *event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:.01]
                inMode:NSDefaultRunLoopMode dequeue:YES];
            if(event)[NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    }
    if(!atomic_load(&shared->done) && kill(shared->parent,0)==0) {
        fprintf(stderr,"FAIL native test hard timeout (60 seconds); terminating test parent\n");
        kill(shared->parent,SIGKILL);
    }
    CGPoint saved=CGPointMake(shared->cursor_x,shared->cursor_y);
    mouse(kCGEventLeftMouseUp,saved,kCGMouseButtonLeft);
    mouse(kCGEventRightMouseUp,saved,kCGMouseButtonRight);
    mouse(kCGEventOtherMouseUp,saved,kCGMouseButtonCenter);
    CGWarpMouseCursorPosition(saved);
    [window close];
    NSRunningApplication *previous=[NSRunningApplication runningApplicationWithProcessIdentifier:shared->previous_pid];
    [previous activateWithOptions:0];
    NSDate *restore_deadline=[NSDate dateWithTimeIntervalSinceNow:2];
    while(restore_deadline.timeIntervalSinceNow>0 && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier!=shared->previous_pid)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    CGEventRef position=CGEventCreate(NULL);CGPoint actual=CGEventGetLocation(position);CFRelease(position);
    bool restored=previous&&!previous.terminated && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier==shared->previous_pid
        && fabs(actual.x-saved.x)<1 && fabs(actual.y-saved.y)<1
        && !CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,kCGMouseButtonLeft)
        && !CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,kCGMouseButtonRight);
    atomic_store(&shared->restored,restored);
    printf("RESTORE buttons=released cursor/focus=%s original-pid=%d\n",restored?"restored":"FAILED",shared->previous_pid);
    return restored?0:3;
}
static void click_at(CGPoint point,bool right) {
    /* Deliberately no pump, timer or hover between arrival and first down. */
    mouse(kCGEventMouseMoved,point,kCGMouseButtonLeft);
    mouse(right?kCGEventRightMouseDown:kCGEventLeftMouseDown,point,right?kCGMouseButtonRight:kCGMouseButtonLeft);
    mouse(right?kCGEventRightMouseUp:kCGEventLeftMouseUp,point,right?kCGMouseButtonRight:kCGMouseButtonLeft);
    pump(.15);
}
static void stationary_click(void) {
    CGEventRef position=CGEventCreate(NULL);CGPoint p=CGEventGetLocation(position);CFRelease(position);
    mouse(kCGEventLeftMouseDown,p,kCGMouseButtonLeft);mouse(kCGEventLeftMouseUp,p,kCGMouseButtonLeft);pump(.15);
}
static void wheel(void) {
    CGEventRef event=CGEventCreateScrollWheelEvent(NULL,kCGScrollEventUnitLine,1,1);
    REQUIRE(event);CGEventPost(kCGSessionEventTap,event);CFRelease(event);pump(.15);
}
static PPFrame ring(int density,uint8_t alpha) {
    PPFrame f={.width=64*density,.height=64*density,.stride=(size_t)64*density*4};
    f.pixels=calloc(f.height,f.stride);f.content_alpha=calloc(f.width,f.height);
    REQUIRE(f.pixels&&f.content_alpha);
    for(int y=0;y<f.height;y++)for(int x=0;x<f.width;x++) {
        double r=hypot((x+.5)/density-32,(y+.5)/density-32);
        if(r>=18 && r<=29) {size_t i=(size_t)y*f.width+x;f.pixels[i*4]=alpha;f.pixels[i*4+3]=alpha;f.content_alpha[i]=255;}
    }
    return f;
}
static NSWindow *native(SDL_Window *w) {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,NULL);
}
int main(int argc,char **argv) { @autoreleasepool {
    if(argc==3 && !strcmp(argv[1],"--receiver"))return receiver(argv[2]);
    if(!CGPreflightPostEventAccess()) {
        fprintf(stderr,"NOT RUN: macos_surface needs permission to post TEST mouse events. Grant Accessibility to the invoking terminal/test binary, then rerun. Production pointer features do not request this permission.\n");return 2;
    }
    REQUIRE(!CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,kCGMouseButtonLeft));
    REQUIRE(!CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,kCGMouseButtonRight));
    [NSApplication sharedApplication];
    char path[]="/tmp/pointy-macos-surface-XXXXXX";
    int fd=mkstemp(path);REQUIRE(fd>=0);REQUIRE(!ftruncate(fd,sizeof(Shared)));
    shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);REQUIRE(shared!=MAP_FAILED);
    memset(shared,0,sizeof(*shared));shared->parent=getpid();shared->previous_pid=NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
    CGEventRef position=CGEventCreate(NULL);CGPoint saved=CGEventGetLocation(position);CFRelease(position);
    shared->cursor_x=saved.x;shared->cursor_y=saved.y;
    NSRect screen=NSScreen.screens.firstObject.visibleFrame;
    shared->x=NSMidX(screen)-240;shared->y=top()-NSMidY(screen)-160;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    char *args[]={argv[0],"--receiver",path,NULL};
    REQUIRE(!posix_spawn(&child,argv[0],NULL,NULL,args,environ));atexit(cleanup);
    double until=seconds()+5;
    while(!atomic_load(&shared->ready)&&seconds()<until)pump(.01);
    unlink(path);REQUIRE(atomic_load(&shared->ready));pump(.2);
    printf("macOS=%s architecture=%s scale=%.1f\n",NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String,
#if defined(__arm64__)
        "arm64",
#else
        "x86_64",
#endif
        NSScreen.screens.firstObject.backingScaleFactor);
    int density=(int)NSScreen.screens.firstObject.backingScaleFactor;
    PPDisplay *displays=NULL;size_t count=0;REQUIRE(pp_platform_displays(&displays,&count,&error));
    REQUIRE(count>0 && displays[0].desktop_units_per_dip==1 && displays[0].drawable_pixels_per_dip==density);pp_displays_free(displays);
    PPRect bounds={shared->x+100,shared->y+100,64,64};
    CGPoint hole=CGPointMake(bounds.x+32,bounds.y+32),rim=CGPointMake(bounds.x+57,bounds.y+32);
    PPFrame f=ring(density,255);REQUIRE(pp_surface_create(&surface,&error));pp_surface_set_instance_id(surface,41);
    REQUIRE(pp_surface_present(surface,&f,bounds,&error));pump(.2);
    pid_t foreground=NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;REQUIRE(foreground==child);
    click_at(hole,false);REQUIRE(atomic_load(&shared->clicks)==1 && clicks==0);
    click_at(rim,false);REQUIRE(atomic_load(&shared->clicks)==1 && clicks==1);
    REQUIRE(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier==foreground);
    click_at(hole,true);REQUIRE(atomic_load(&shared->right)==1 && right_clicks==0);
    click_at(rim,true);REQUIRE(atomic_load(&shared->right)==1 && right_clicks==1);
    wheel();REQUIRE(wheels==1 && atomic_load(&shared->wheels)==0);
    memset(f.pixels,0,f.stride*f.height);memset(f.content_alpha,0,(size_t)f.width*f.height);
    REQUIRE(pp_surface_present(surface,&f,bounds,&error));stationary_click();wheel();
    REQUIRE(atomic_load(&shared->clicks)==2 && clicks==1 && atomic_load(&shared->wheels)==1 && wheels==1);
    pp_frame_destroy(&f);f=ring(density,1);REQUIRE(pp_surface_present(surface,&f,bounds,&error));
    stationary_click();REQUIRE(clicks==2 && atomic_load(&shared->clicks)==2);
    wheel();REQUIRE(wheels==2 && atomic_load(&shared->wheels)==1);
    puts("PASS immediate arrival, ring hole, right/wheel, stationary frame changes and alpha=1");
    pp_frame_destroy(&f);f=ring(density,255);REQUIRE(pp_surface_present(surface,&f,bounds,&error));
    REQUIRE(pp_renderer_create(&renderer,&error));
    PPFrame rotated={0};
    REQUIRE(pp_renderer_transform(renderer,&f,64,37,1,density,&rotated,&error));
    PPRect rotated_bounds={bounds.x,bounds.y,(double)rotated.width/density,(double)rotated.height/density};
    REQUIRE(pp_surface_present(surface,&rotated,rotated_bounds,&error));
    int old_clicks=clicks,old_receiver=atomic_load(&shared->clicks);
    click_at(CGPointMake(rotated_bounds.x+.5,rotated_bounds.y+.5),false);
    REQUIRE(clicks==old_clicks && atomic_load(&shared->clicks)==old_receiver+1);
    bool found=false;
    for(int y=2;y<rotated.height-2 && !found;y+=density)for(int x=2;x<rotated.width-2 && !found;x+=density)
        if(rotated.pixels[(size_t)y*rotated.stride+x*4+3]==255) {
            click_at(CGPointMake(rotated_bounds.x+(x+.5)/density,rotated_bounds.y+(y+.5)/density),false);found=true;
        }
    REQUIRE(found && clicks==old_clicks+1 && atomic_load(&shared->clicks)==old_receiver+1);
    pp_frame_destroy(&rotated);REQUIRE(pp_surface_present(surface,&f,bounds,&error));
    puts("PASS shared OpenGL rotated-frame corner passthrough and visible-pixel capture");
    click_at(rim,false);
    mouse(kCGEventLeftMouseDown,rim,kCGMouseButtonLeft);pump(.1);REQUIRE(pp_platform_capture(surface,&error));
    CGPoint outside=CGPointMake(bounds.x+120,bounds.y+100);int before_moves=moves,before_ups=ups;
    mouse(kCGEventLeftMouseDragged,outside,kCGMouseButtonLeft);pump(.1);
    int before_wheel=wheels,receiver_wheel=atomic_load(&shared->wheels);
    wheel();REQUIRE(wheels==before_wheel+1 && atomic_load(&shared->wheels)==receiver_wheel);
    mouse(kCGEventLeftMouseUp,outside,kCGMouseButtonLeft);pump(.2);
    REQUIRE(moves>before_moves && ups==before_ups+1 && fabs(last_point.x-outside.x)<1 && fabs(last_point.y-outside.y)<1);
    REQUIRE(!pp_platform_capture(surface,NULL));
    click_at(rim,false);mouse(kCGEventLeftMouseDown,rim,kCGMouseButtonLeft);pump(.1);REQUIRE(pp_platform_capture(surface,&error));
    REQUIRE(pp_surface_create(&other,&error));pp_surface_cancel_input(other);REQUIRE(pp_platform_capture(surface,&error));
    int before_cancel=cancels;pp_surface_cancel_input(surface);pump(.01);REQUIRE(cancels==before_cancel && !pp_platform_capture(surface,NULL));
    mouse(kCGEventLeftMouseUp,rim,kCGMouseButtonLeft);pump(.1);
    mouse(kCGEventLeftMouseDown,rim,kCGMouseButtonLeft);pump(.1);REQUIRE(pp_platform_capture(surface,&error));
    pp_surface_hide(surface);pump(.01);REQUIRE(cancels==before_cancel+1 && !pp_platform_capture(surface,NULL));
    mouse(kCGEventLeftMouseUp,rim,kCGMouseButtonLeft);pump(.1);
    REQUIRE(pp_surface_present(surface,&f,bounds,&error));
    puts("PASS capture outside bounds, release, silent surface-specific cancellation and hide cancellation");
    settings=SDL_CreateWindow("PointyPointer test settings",100,100,SDL_WINDOW_HIDDEN);
    drawer=SDL_CreateWindow("PointyPointer test drawer",80,80,SDL_WINDOW_HIDDEN);
    REQUIRE(settings&&drawer);SDL_SetWindowPosition(settings,(int)bounds.x,(int)bounds.y);
    SDL_SetWindowPosition(drawer,(int)bounds.x,(int)bounds.y);
    pp_platform_settings_window(SDL_GetWindowID(settings));SDL_ShowWindow(settings);SDL_RaiseWindow(settings);pump(.15);
    pp_surface_raise(surface);pump(.01);
    REQUIRE([NSWindow windowNumberAtPoint:NSMakePoint(rim.x,top()-rim.y) belowWindowWithWindowNumber:0]==native(settings).windowNumber);
    pp_platform_drawer_window(SDL_GetWindowID(drawer));SDL_ShowWindow(drawer);SDL_RaiseWindow(drawer);pump(.15);
    REQUIRE([NSWindow windowNumberAtPoint:NSMakePoint(rim.x,top()-rim.y) belowWindowWithWindowNumber:0]==native(drawer).windowNumber);
    pp_platform_drawer_close(true);pump(.15);REQUIRE(NSApp.keyWindow==native(settings));
    pp_platform_drawer_window(SDL_GetWindowID(drawer));SDL_ShowWindow(drawer);SDL_RaiseWindow(drawer);pump(.15);
    click_at(CGPointMake(shared->x+300,shared->y+200),false);
    REQUIRE(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier==child);
    pp_platform_drawer_close(true);pump(.15);REQUIRE(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier==child);
    SDL_HideWindow(settings);pp_platform_settings_window(0);
    puts("PASS settings/drawer priority and conditional prior-focus restoration");
    if(argc==2 && !strcmp(argv[1],"--spaces")) {
        atomic_store(&shared->fullscreen,1);until=seconds()+8;
        while(!atomic_load(&shared->fullscreen_ready)&&seconds()<until)pump(.05);
        REQUIRE(atomic_load(&shared->fullscreen_ready));pump(1);
        REQUIRE(pp_surface_present(surface,&f,bounds,&error));pump(.1);
        int previous=clicks;click_at(rim,false);REQUIRE(clicks==previous+1);
        previous=atomic_load(&shared->clicks);click_at(hole,false);REQUIRE(atomic_load(&shared->clicks)==previous+1);
        puts("PASS fullscreen Space auxiliary overlay original-click routing");
    } else puts("NOT RUN fullscreen Space transition (rerun with --spaces); separate physical mixed-Retina traversal remains manual");
    if(density<2)puts("NOT RUN physical Retina raster acceptance (this display has scale 1)");
    pp_frame_destroy(&f);puts("PASS macos_surface automated cases");return 0;
} }
