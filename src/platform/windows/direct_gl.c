/* SDL owns native input and the WGL context. The optional stable compositor
 * host publishes matching retained pixels and placement in one transaction. */
#include "windows_internal.h"
#include "composition.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
static const wchar_t property[]=L"PointyPointer.DirectSurface";
bool pp_surface_publish_gl_placement(PPSurface *surface,PPError *error) {
    return surface&&pp_composition_translate(surface->composition,error);
}
void pp_windows_gl_destroy(PPSurface *surface) {
    pp_composition_destroy(surface->composition);surface->composition=NULL;
    HWND native=surface->window;
    if(surface->gl_window) SDL_DestroyWindow(surface->gl_window);
    if(native&&IsWindow(native))DestroyWindow(native);
    surface->gl_window=NULL;surface->window=NULL;
    if(surface->gl_video_initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    surface->gl_video_initialized=false;
}
static LRESULT CALLBACK proxy_procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    return DefWindowProcW(window,message,w,l);
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
    if(pp_composition_active(surface->composition)) {
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);return 0;}
    }
    /* SDL's cursor is shared with Settings and the drawer. Select the normal
     * pointer before SDL handles sprite hover, including a previous UI hand. */
    if(message==WM_SETCURSOR && LOWORD(l)==HTCLIENT)
        SDL_SetCursor(SDL_GetDefaultCursor());
    pp_windows_record(surface,window,message,w,l);
    LRESULT result=CallWindowProcW(surface->gl_procedure,window,message,w,l);
    if(message==WM_NCDESTROY) RemovePropW(window,property);
    return result;
}
static bool compatibility_window(void *owner,uint32_t *id,PPError *error) {
    PPSurface *surface=owner;HWND previous=surface->window;
    pp_windows_input_remove(surface);RemovePropW(previous,PP_COMPOSITION_PROPERTY);RemovePropW(previous,property);
    SDL_DestroyWindow(surface->gl_window);if(IsWindow(previous))DestroyWindow(previous);
    surface->gl_window=SDL_CreateWindow("Yoink compatibility GL",1,1,
        SDL_WINDOW_OPENGL|SDL_WINDOW_TRANSPARENT|SDL_WINDOW_BORDERLESS|SDL_WINDOW_HIDDEN|
        SDL_WINDOW_NOT_FOCUSABLE|SDL_WINDOW_ALWAYS_ON_TOP|SDL_WINDOW_UTILITY);
    surface->window=surface->gl_window?SDL_GetPointerProperty(SDL_GetWindowProperties(surface->gl_window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL):NULL;
    if(!surface->window||!SetPropW(surface->window,property,surface)||!SetPropW(surface->window,PP_COMPOSITION_PROPERTY,surface->composition))
        return fail(surface,error,PP_ERROR_PLATFORM,"Cannot recreate compatibility window");
    surface->gl_procedure=(WNDPROC)SetWindowLongPtrW(surface->window,GWLP_WNDPROC,(LONG_PTR)procedure);
    pp_composition_proxy(surface->composition,surface->window);
    if(!surface->gl_procedure||!pp_windows_input_add(surface,error))return false;
    *id=SDL_GetWindowID(surface->gl_window);return true;
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
    /* Create an input HWND without visual backing and let SDL wrap its WGL
     * context. Omit SDL's transparency mode so it never enables alpha hit tests. */
    static const wchar_t proxy_class[]=L"Yoink.CompositionInput";
    WNDCLASSEXW existing={sizeof(existing)};HINSTANCE instance=GetModuleHandleW(NULL);
    if(!GetClassInfoExW(instance,proxy_class,&existing)) {
        WNDCLASSEXW type={0};type.cbSize=sizeof(type);type.style=CS_OWNDC;type.lpfnWndProc=proxy_procedure;
        type.hInstance=instance;type.lpszClassName=proxy_class;
        if(!RegisterClassExW(&type)){fail(surface,error,PP_ERROR_PLATFORM,"Cannot register direct input window");pp_surface_destroy(surface);return false;}
    }
    surface->window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        proxy_class,L"Yoink direct GL",WS_POPUP,0,0,1,1,NULL,NULL,instance,NULL);
    SDL_PropertiesID properties=SDL_CreateProperties();
    SDL_SetPointerProperty(properties,SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,surface->window);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN,true);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,true);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN,false);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN,true);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN,true);
    if(surface->window)surface->gl_window=SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if(surface->gl_window) surface->window=SDL_GetPointerProperty(SDL_GetWindowProperties(surface->gl_window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
    if(!surface->gl_window||!surface->window || !SetPropW(surface->window,property,surface)) {
        fail(surface,error,PP_ERROR_PLATFORM,"Cannot create transparent SDL OpenGL window");pp_surface_destroy(surface);return false;
    }
    SetLastError(0);
    surface->gl_procedure=(WNDPROC)SetWindowLongPtrW(surface->window,GWLP_WNDPROC,(LONG_PTR)procedure);
    if(!surface->gl_procedure || !pp_windows_input_add(surface,error)) {
        if(!surface->gl_procedure) {RemovePropW(surface->window,property);fail(surface,error,PP_ERROR_PLATFORM,"Cannot attach direct window input");}
        pp_surface_destroy(surface);return false;
    }
    surface->composition=pp_composition_create(surface->window);
    if(!surface->composition||!SetPropW(surface->window,PP_COMPOSITION_PROPERTY,surface->composition)) {
        fail(surface,error,PP_ERROR_MEMORY,"Cannot allocate composition presenter");pp_surface_destroy(surface);return false;
    }
    pp_composition_set_fallback(surface->composition,compatibility_window,surface);
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
    bool visible=IsWindowVisible(surface->window),composition=pp_composition_active(surface->composition);
    /* SDL applies its pending floating size when a hidden window is shown.
     * Record the new size through SDL before the native ordering operation. */
    if((surface->width!=width || surface->height!=height) && !SDL_SetWindowSize(surface->gl_window,width,height))
        return fail(surface,error,PP_ERROR_PLATFORM,SDL_GetError());
    UINT order=visible?SWP_NOZORDER:0;
    if(composition) {
        RECT sized;
        if(!GetWindowRect(surface->window,&sized))return fail(surface,error,PP_ERROR_PLATFORM,"Cannot query sized input window");
        /* The composition output owns all visible pixels. Keep SDL's sizing
         * notification, then move/order its input proxy without another resize
         * or preservation of the old WGL client pixels. */
        order|=SWP_NOCOPYBITS;
        if(sized.right-sized.left==width&&sized.bottom-sized.top==height)order|=SWP_NOSIZE;
    }
    HWND settings=pp_windows_settings_window();
    surface->gl_interactive=true;
    if(!SetWindowPos(surface->window,settings?settings:HWND_TOPMOST,(int)llround(bounds.x),(int)llround(bounds.y),
        width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW|order))
        return fail(surface,error,PP_ERROR_PLATFORM,"Cannot place direct OpenGL window");
    RECT actual;
    if(!GetWindowRect(surface->window,&actual)) return fail(surface,error,PP_ERROR_PLATFORM,"Cannot query direct window bounds");
    surface->origin=(POINT){actual.left,actual.top};surface->width=actual.right-actual.left;surface->height=actual.bottom-actual.top;
    pp_composition_stage(surface->composition,actual.left,actual.top);
    if(!pp_composition_order(surface->composition))return fail(surface,error,PP_ERROR_PLATFORM,"Cannot order direct presentation");
    return true;
}
