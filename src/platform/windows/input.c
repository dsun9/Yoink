/* Original selective hook adapter. It observes OS input, never replays it.
 * Native alpha routing remains the authority for the topmost mouse target. */
#include "windows_internal.h"
#include "pointy/interaction.h"
#include <stdio.h>
static HHOOK hook;
static PPSurface *surfaces,*owner,*accepted_press;
static PPSurface *button_targets[4];
static bool capture_eligible;
HWND pp_windows_drawer_window(void);
static PPButton button(UINT message) {
    switch(message) {
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:return PP_BUTTON_LEFT;
    case WM_RBUTTONDOWN:case WM_RBUTTONUP:return PP_BUTTON_RIGHT;
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:return PP_BUTTON_MIDDLE;
    default:return PP_BUTTON_NONE;
    }
}
static PPSurface *target(POINT point) {
    HWND top=WindowFromPoint(point);
    for(PPSurface *s=surfaces;s;s=s->input_next) if(s->window==top && s->bits) {
        LONG x=point.x-s->origin.x,y=point.y-s->origin.y;
        if(x>=0 && y>=0 && x<s->width && y<s->height && s->bits[((size_t)y*s->width+x)*4+3]) return s;
        return NULL;
    }
    return NULL;
}
void pp_platform_release_capture(void) {owner=NULL;accepted_press=NULL;capture_eligible=false;}
void pp_windows_input_cancel(PPSurface *surface) {
    if(owner==surface || accepted_press==surface) {
        POINT point={0};GetCursorPos(&point);PPPlatformEvent event=pp_windows_event(surface,WM_CANCELMODE,point,0);
        pp_platform_release_capture();pp_windows_enqueue(event);
    }
    for(int i=1;i<4;i++) if(button_targets[i]==surface) button_targets[i]=NULL;
}
static LRESULT CALLBACK observe(int code,WPARAM w,LPARAM l) {
    if(code!=HC_ACTION) return CallNextHookEx(hook,code,w,l);
    MSLLHOOKSTRUCT *mouse=(MSLLHOOKSTRUCT *)l;UINT message=(UINT)w;
    if(message==WM_MOUSEMOVE) {
        /* Retain the trajectory even if press/move/release arrive in one
         * native pump before the application handles its accepted down. */
        PPSurface *s=owner?owner:accepted_press;
        if(s) pp_windows_enqueue(pp_windows_event(s,message,mouse->pt,0));
        return CallNextHookEx(hook,code,w,l);
    }
    if(message==WM_MOUSEWHEEL) {
        PPSurface *s=owner?owner:target(mouse->pt);
        if(s && pp_windows_enqueue(pp_windows_event(s,message,mouse->pt,mouse->mouseData))) return 1;
        return CallNextHookEx(hook,code,w,l);
    }
    PPButton b=button(message);if(b==PP_BUTTON_NONE) return CallNextHookEx(hook,code,w,l);
    bool down=message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN;
    HWND drawer=pp_windows_drawer_window();
    if(down&&drawer&&GetAncestor(WindowFromPoint(mouse->pt),GA_ROOT)!=drawer) {
        /* Observe the original outside click; do not consume or replay it. */
        PPPlatformEvent outside={.kind=PP_EVENT_BUTTON_DOWN,.desktop_point={mouse->pt.x,mouse->pt.y},.button=b};
        pp_windows_enqueue(outside);
    }
    PPSurface *s;
    if(down) {
        s=target(mouse->pt);
        if(b==PP_BUTTON_LEFT) {
            if(owner) pp_windows_input_cancel(owner);
            accepted_press=s;capture_eligible=s!=NULL;
        }
        button_targets[b]=s;
    } else {
        s=(b==PP_BUTTON_LEFT && owner)?owner:button_targets[b];button_targets[b]=NULL;
        if(b==PP_BUTTON_LEFT) pp_platform_release_capture();
    }
    if(s && pp_windows_enqueue(pp_windows_event(s,message,mouse->pt,0))) return 1;
    return CallNextHookEx(hook,code,w,l);
}
bool pp_windows_input_add(PPSurface *surface,PPError *error) {
    if(!hook) hook=SetWindowsHookExW(WH_MOUSE_LL,observe,GetModuleHandleW(NULL),0);
    if(!hook) {
        if(error) {error->code=PP_ERROR_PLATFORM;snprintf(error->message,sizeof(error->message),"Cannot install mouse observation (Win32 %lu)",GetLastError());}
        return false;
    }
    surface->input_next=surfaces;surfaces=surface;return true;
}
void pp_windows_input_remove(PPSurface *surface) {
    pp_windows_input_cancel(surface);
    PPSurface **link=&surfaces;while(*link && *link!=surface) link=&(*link)->input_next;
    if(*link) *link=surface->input_next;
    surface->input_next=NULL;
    if(!surfaces && hook) {pp_platform_release_capture();UnhookWindowsHookEx(hook);hook=NULL;}
}
bool pp_windows_input_owned(PPSurface *surface) {return owner==surface || accepted_press==surface;}
PPSurface *pp_windows_input_owner(void) {return owner?owner:accepted_press;}
void pp_windows_input_check(void) {
    if(owner && (!owner->window || !IsWindowVisible(owner->window))) pp_windows_input_cancel(owner);
    if(accepted_press && (!accepted_press->window || !IsWindowVisible(accepted_press->window))) pp_windows_input_cancel(accepted_press);
}
bool pp_platform_capture(PPSurface *surface,PPError *error) {
    if(!surface || !surface->window || !hook || !capture_eligible || accepted_press!=surface) {
        if(error) {error->code=PP_ERROR_ARGUMENT;snprintf(error->message,sizeof(error->message),"Capture requires an accepted sprite press");}
        return false;
    }
    owner=surface;return true;
}
void pp_surface_set_instance_id(PPSurface *surface,uint64_t id) {if(surface) surface->instance_id=id;}
void pp_surface_raise(PPSurface *surface) {
    if(!surface||!surface->window) return;
    for(HWND window=GetTopWindow(NULL);window;window=GetWindow(window,GW_HWNDNEXT)) {
        for(PPSurface *s=surfaces;s;s=s->input_next) if(s->window==window&&s->instance_id&&IsWindowVisible(window)) {
            if(s!=surface) {
                HWND before=GetWindow(window,GW_HWNDPREV);
                SetWindowPos(surface->window,before?before:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            }
            return;
        }
    }
}
void pp_platform_gesture_config(PPGestureConfig *out) {
    if(!out) return;
    *out=(PPGestureConfig){GetSystemMetrics(SM_CXDRAG)*.5,GetSystemMetrics(SM_CYDRAG)*.5,
        GetSystemMetrics(SM_CXDOUBLECLK)*.5,GetSystemMetrics(SM_CYDOUBLECLK)*.5,
        GetDoubleClickTime()/1000.0,10.0,1.10,.120};
}
