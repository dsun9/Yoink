/* Experimental pinned-SDL transparent OpenGL window. SDL owns its native
 * compositor support; the application adds only nonactivating input routing. */
#include "windows_internal.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
static const wchar_t property[]=L"PointyPointer.DirectSurface";
void pp_windows_gl_destroy(PPSurface *surface) {
    if(surface->gl_window) SDL_DestroyWindow(surface->gl_window);
    surface->gl_window=NULL;surface->window=NULL;
    if(surface->gl_video_initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    surface->gl_video_initialized=false;
}
static bool fail(PPSurface *surface,PPError *error,int code,const char *message) {
    pp_surface_hide(surface);
    if(error) {error->code=code;snprintf(error->message,sizeof(error->message),"%s",message);}
    return false;
}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    PPSurface *surface=(PPSurface *)GetPropW(window,property);
    if(!surface) return DefWindowProcW(window,message,w,l);
    if(message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    pp_windows_record(surface,window,message,w,l);
    LRESULT result=CallWindowProcW(surface->gl_procedure,window,message,w,l);
    if(message==WM_NCDESTROY) RemovePropW(window,property);
    return result;
}
bool pp_surface_create_gl(PPSurface **out,PPError *error) {
    if(!out) return fail(NULL,error,PP_ERROR_ARGUMENT,"Missing direct surface output");
    *out=NULL;
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) return fail(NULL,error,PP_ERROR_PLATFORM,SDL_GetError());
    PPSurface *surface=calloc(1,sizeof(*surface));
    if(!surface) {SDL_QuitSubSystem(SDL_INIT_VIDEO);return fail(NULL,error,PP_ERROR_MEMORY,"Cannot allocate direct surface");}
    surface->gl_video_initialized=true;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    surface->gl_window=SDL_CreateWindow("PointyPointer direct GL diagnostic",1,1,
        SDL_WINDOW_OPENGL|SDL_WINDOW_TRANSPARENT|SDL_WINDOW_BORDERLESS|SDL_WINDOW_HIDDEN|
        SDL_WINDOW_NOT_FOCUSABLE|SDL_WINDOW_ALWAYS_ON_TOP|SDL_WINDOW_UTILITY);
    if(surface->gl_window) surface->window=SDL_GetPointerProperty(SDL_GetWindowProperties(surface->gl_window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
    if(!surface->window || !SetPropW(surface->window,property,surface)) {
        fail(surface,error,PP_ERROR_PLATFORM,"Cannot create transparent SDL OpenGL window");pp_surface_destroy(surface);return false;
    }
    SetLastError(0);
    surface->gl_procedure=(WNDPROC)SetWindowLongPtrW(surface->window,GWLP_WNDPROC,(LONG_PTR)procedure);
    if(!surface->gl_procedure || !pp_windows_input_add(surface,error)) {
        if(!surface->gl_procedure) {RemovePropW(surface->window,property);fail(surface,error,PP_ERROR_PLATFORM,"Cannot attach direct window input");}
        pp_surface_destroy(surface);return false;
    }
    *out=surface;return true;
}
uint32_t pp_surface_window_id(const PPSurface *surface) {
    return surface && surface->gl_window?SDL_GetWindowID(surface->gl_window):0;
}
bool pp_surface_set_grab_region(PPSurface *surface,const PPGrabRegion *region,PPError *error) {
    if(!surface||!surface->window||!IsWindow(surface->window))return fail(surface,error,PP_ERROR_PLATFORM,"Missing grab region window");
    if(!region) {
        if(!SetWindowRgn(surface->window,NULL,TRUE))return fail(surface,error,PP_ERROR_PLATFORM,"Cannot clear grab region");
        return true;
    }
    if(region->count>PP_GRAB_RECT_LIMIT)return fail(surface,error,PP_ERROR_ARGUMENT,"Grab region exceeds native cap");
    struct {RGNDATAHEADER header;RECT rects[PP_GRAB_RECT_LIMIT];} data={0};
    data.header.dwSize=sizeof(RGNDATAHEADER);data.header.iType=RDH_RECTANGLES;
    data.header.nCount=(DWORD)region->count;data.header.nRgnSize=(DWORD)(region->count*sizeof(RECT));
    for(size_t i=0;i<region->count;i++) {
        PPGrabRect r=region->rects[i];data.rects[i]=(RECT){r.left,r.top,r.right,r.bottom};
        if(!i)data.header.rcBound=data.rects[i];
        else {if(r.left<data.header.rcBound.left)data.header.rcBound.left=r.left;if(r.top<data.header.rcBound.top)data.header.rcBound.top=r.top;
            if(r.right>data.header.rcBound.right)data.header.rcBound.right=r.right;if(r.bottom>data.header.rcBound.bottom)data.header.rcBound.bottom=r.bottom;}
    }
    HRGN candidate=region->count?ExtCreateRegion(NULL,sizeof(RGNDATAHEADER)+data.header.nRgnSize,(RGNDATA *)&data):CreateRectRgn(0,0,0,0);
    if(!candidate)return fail(surface,error,PP_ERROR_PLATFORM,"Cannot allocate native grab region");
    /* The presenter publishes its GL frame immediately after the region. A
     * separate native paint invalidation needlessly discards compositor backing. */
    if(!SetWindowRgn(surface->window,candidate,FALSE)){DeleteObject(candidate);return fail(surface,error,PP_ERROR_PLATFORM,"Cannot publish native grab region");}
    surface->rectangular=false;return true;
}
bool pp_surface_place_gl(PPSurface *surface,PPRect bounds,bool interactive,PPError *error) {
    if(!surface || !surface->gl_window || !IsWindow(surface->window) ||
        !isfinite(bounds.x) || !isfinite(bounds.y) || bounds.x<-2147483647.0 || bounds.x>2147483647.0 ||
        bounds.y<-2147483647.0 || bounds.y>2147483647.0 || !isfinite(bounds.w) || !isfinite(bounds.h) ||
        bounds.w<1 || bounds.h<1 || bounds.w>16384 || bounds.h>16384)
        return fail(surface,error,PP_ERROR_ARGUMENT,"Invalid direct surface bounds");
    if(!interactive) {pp_surface_hide(surface);return true;}
    int width=(int)llround(bounds.w),height=(int)llround(bounds.h);
    /* SDL applies its pending floating size when a hidden window is shown.
     * Record the new size through SDL before the native ordering operation. */
    if((surface->width!=width || surface->height!=height) && !SDL_SetWindowSize(surface->gl_window,width,height))
        return fail(surface,error,PP_ERROR_PLATFORM,SDL_GetError());
    UINT order=IsWindowVisible(surface->window)?SWP_NOZORDER:0;
    HWND settings=pp_windows_settings_window();
    surface->gl_interactive=true;
    if(!SetWindowPos(surface->window,settings?settings:HWND_TOPMOST,(int)llround(bounds.x),(int)llround(bounds.y),
        width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW|order))
        return fail(surface,error,PP_ERROR_PLATFORM,"Cannot place direct OpenGL window");
    RECT actual;
    if(!GetWindowRect(surface->window,&actual)) return fail(surface,error,PP_ERROR_PLATFORM,"Cannot query direct window bounds");
    surface->origin=(POINT){actual.left,actual.top};surface->width=actual.right-actual.left;surface->height=actual.bottom-actual.top;
    return true;
}
