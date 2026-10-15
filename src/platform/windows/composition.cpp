#include "composition.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <new>
#include <cstdio>
#include <cstring>
#include <cstdarg>
using Microsoft::WRL::ComPtr;
typedef HANDLE (WINAPI *OpenDX)(void *);
typedef BOOL (WINAPI *CloseDX)(HANDLE);
typedef HANDLE (WINAPI *RegisterDX)(HANDLE,void *,GLuint,GLenum,GLenum);
typedef BOOL (WINAPI *UnregisterDX)(HANDLE,HANDLE);
typedef BOOL (WINAPI *LockDX)(HANDLE,GLint,HANDLE *);
typedef const char *(WINAPI *Extensions)(HDC);
struct PPComposition {
    unsigned references=1;
    HWND proxy=nullptr,host=nullptr;
    int origin_x=0,origin_y=0,desktop_w=0,desktop_h=0,x=0,y=0,w=0,h=0,candidate_w=0,candidate_h=0;
    int update_x=0,update_y=0,update_w=0,update_h=0;
    int published_x=0,published_y=0;
    bool published=false,detach_pending=false;
    bool initialized=false,active=false;
    char description[1024]="backend=SDL compatibility coherent-publication=unverified stage=not-initialized";
    ComPtr<ID3D11Device> device;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<IDCompositionSurface> output,candidate;
    HANDLE interop=nullptr;
    OpenDX open_dx=nullptr;CloseDX close_dx=nullptr;RegisterDX register_dx=nullptr;
    UnregisterDX unregister_dx=nullptr;LockDX lock_dx=nullptr,unlock_dx=nullptr;
    PFNGLGENRENDERBUFFERSPROC gen_rb=nullptr;
    PFNGLDELETERENDERBUFFERSPROC delete_rb=nullptr;
    PFNGLGENFRAMEBUFFERSPROC gen_fb=nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC delete_fb=nullptr;
    PFNGLBINDFRAMEBUFFERPROC bind_fb=nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC attach_rb=nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC status_fb=nullptr;
    PPCompositionObserver observer=nullptr;
    ComPtr<IDXGISurface> update;
    ComPtr<ID3D11Texture2D> update_texture;
    IDCompositionSurface *drawing=nullptr;
    GLuint update_rb=0,update_fb=0;
    HANDLE update_object=nullptr;
    bool update_locked=false;
    HGLRC update_context=nullptr;
    PPComposition *quarantined_next=nullptr;
    PPCompositionFallback fallback=nullptr;
    void *fallback_owner=nullptr;
};
static PPComposition *quarantined;
static size_t quarantined_bytes;
static INIT_ONCE device_once=INIT_ONCE_STATIC_INIT;
static ComPtr<ID3D11Device> shared_device;
static HRESULT device_result=E_FAIL;
static BOOL CALLBACK initialize_device(PINIT_ONCE,void *,void **) {
    /* The device has no retained logical pixels and owns no immediate-context
     * work. Each sprite keeps its own composition device and interop handle. */
    device_result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&shared_device,nullptr,nullptr);
    return TRUE;
}
extern "C" bool pp_composition_bootstrap(void) {
    auto extensions=(Extensions)SDL_GL_GetProcAddress("wglGetExtensionsStringARB");
    const char *names=extensions?extensions(wglGetCurrentDC()):nullptr;
    if(!names||!strstr(names,"WGL_NV_DX_interop2"))return false;
    InitOnceExecuteOnce(&device_once,initialize_device,nullptr,nullptr);
    return SUCCEEDED(device_result);
}
extern "C" size_t pp_composition_quarantined_bytes(void){return quarantined_bytes;}
static void describe(PPComposition *c,const char *format,...) {
    va_list arguments;va_start(arguments,format);vsnprintf(c->description,sizeof(c->description),format,arguments);va_end(arguments);
}
extern "C" const char *pp_composition_description(const PPComposition *c) {return c?c->description:"backend=SDL no-native-composition";}
extern "C" void pp_composition_set_fallback(PPComposition *c,PPCompositionFallback fallback,void *owner){c->fallback=fallback;c->fallback_owner=owner;}
extern "C" bool pp_composition_needs_fallback(const PPComposition *c){return c&&!c->active&&c->fallback;}
extern "C" bool pp_composition_select_fallback(PPComposition *c,uint32_t *id,PPError *error){auto callback=c->fallback;c->fallback=nullptr;return callback&&callback(c->fallback_owner,id,error);}
extern "C" void pp_composition_proxy(PPComposition *c,HWND proxy){c->proxy=proxy;}
static double clock_ms() {LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return q.QuadPart*1000.0/f.QuadPart;}
static void observed(PPComposition *c,PPCompositionStage stage,double start) {if(c->observer)c->observer(stage,clock_ms()-start);}
extern "C" void pp_composition_observe(PPComposition *c,PPCompositionObserver observer) {if(c)c->observer=observer;}
static bool failure(PPError *error,HRESULT hr,const char *message) {
    if(error){error->code=hr==E_OUTOFMEMORY?PP_ERROR_MEMORY:PP_ERROR_RENDER;
        snprintf(error->message,sizeof(error->message),"%s (HRESULT %08lx, Win32 %lu)",message,hr,GetLastError());}
    return false;
}
static LRESULT CALLBACK host_procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    return DefWindowProcW(window,message,w,l);
}
extern "C" PPComposition *pp_composition_create(HWND proxy) {
    PPComposition *c=new(std::nothrow) PPComposition;
    if(c)c->proxy=proxy;
    return c;
}
extern "C" PPComposition *pp_composition_retain(PPComposition *c){if(c)c->references++;return c;}
static void release_device(PPComposition *c) {
    /* A failed driver unlock/unregister must never hand still-owned storage
     * back to DX. The final release quarantines this record if cleanup cannot
     * complete. Windows reclaims that exceptional allocation on process exit. */
    if(c->update_locked||c->update_object||c->detach_pending)return;
    c->candidate.Reset();c->output.Reset();c->visual.Reset();c->target.Reset();c->composition.Reset();
    if(c->interop&&c->close_dx)c->close_dx(c->interop);
    c->interop=nullptr;c->device.Reset();
    if(c->host)DestroyWindow(c->host);c->host=nullptr;c->active=false;
}
extern "C" bool pp_composition_initialize(PPComposition *c) {
    if(!c)return false;if(c->initialized)return c->active;c->initialized=true;
    auto extensions=(Extensions)SDL_GL_GetProcAddress("wglGetExtensionsStringARB");
    const char *names=extensions?extensions(wglGetCurrentDC()):nullptr;
    if(!names||!strstr(names,"WGL_NV_DX_interop2")){
        describe(c,"backend=SDL compatibility coherent-publication=unverified stage=extension GL=%s",glGetString(GL_RENDERER));return false;}
#define LOAD(member,type,name) c->member=(type)SDL_GL_GetProcAddress(name);if(!c->member){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=entrypoint name=%s",name);return false;}
    LOAD(open_dx,OpenDX,"wglDXOpenDeviceNV");LOAD(close_dx,CloseDX,"wglDXCloseDeviceNV");
    LOAD(register_dx,RegisterDX,"wglDXRegisterObjectNV");LOAD(unregister_dx,UnregisterDX,"wglDXUnregisterObjectNV");
    LOAD(lock_dx,LockDX,"wglDXLockObjectsNV");LOAD(unlock_dx,LockDX,"wglDXUnlockObjectsNV");
    LOAD(gen_rb,PFNGLGENRENDERBUFFERSPROC,"glGenRenderbuffers");LOAD(delete_rb,PFNGLDELETERENDERBUFFERSPROC,"glDeleteRenderbuffers");
    LOAD(gen_fb,PFNGLGENFRAMEBUFFERSPROC,"glGenFramebuffers");LOAD(delete_fb,PFNGLDELETEFRAMEBUFFERSPROC,"glDeleteFramebuffers");
    LOAD(bind_fb,PFNGLBINDFRAMEBUFFERPROC,"glBindFramebuffer");LOAD(attach_rb,PFNGLFRAMEBUFFERRENDERBUFFERPROC,"glFramebufferRenderbuffer");
    LOAD(status_fb,PFNGLCHECKFRAMEBUFFERSTATUSPROC,"glCheckFramebufferStatus");
#undef LOAD
    pp_composition_bootstrap();c->device=shared_device;HRESULT hr=device_result;
    ComPtr<IDXGIDevice> dxgi;if(SUCCEEDED(hr))hr=c->device.As(&dxgi);
    if(SUCCEEDED(hr))c->interop=c->open_dx(c->device.Get());
    if(!c->interop){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=open_device HRESULT=%08lx Win32=%lu GL=%s",hr,GetLastError(),glGetString(GL_RENDERER));release_device(c);return false;}
    /* Registration and lock prove actual GL/DX device compatibility. Driver
     * advertising alone does not establish a usable adapter pair. */
    D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=1;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    hr=c->device->CreateTexture2D(&desc,nullptr,&c->update_texture);
    c->gen_rb(1,&c->update_rb);c->update_context=wglGetCurrentContext();
    c->update_object=SUCCEEDED(hr)?c->register_dx(c->interop,c->update_texture.Get(),c->update_rb,GL_RENDERBUFFER,0x0001):nullptr;
    c->update_locked=c->update_object&&c->lock_dx(c->interop,1,&c->update_object);
    bool compatible=c->update_locked;DWORD probe_error=GetLastError();
    if(c->update_locked) {
        bool released=c->unlock_dx(c->interop,1,&c->update_object)!=FALSE;
        if(!released){compatible=false;probe_error=GetLastError();released=c->unlock_dx(c->interop,1,&c->update_object)!=FALSE;}
        c->update_locked=!released;
    }
    if(c->update_object&&!c->update_locked) {
        bool released=c->unregister_dx(c->interop,c->update_object)!=FALSE;
        if(!released){compatible=false;probe_error=GetLastError();released=c->unregister_dx(c->interop,c->update_object)!=FALSE;}
        if(released)c->update_object=nullptr;
    }
    if(!c->update_locked&&!c->update_object){c->delete_rb(1,&c->update_rb);c->update_rb=0;c->update_texture.Reset();c->update_context=nullptr;}
    if(!compatible){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=registration_probe HRESULT=%08lx owned=%d Win32=%lu GL_error=%04x",hr,c->update_object!=nullptr,probe_error,glGetError());release_device(c);return false;}
    hr=DCompositionCreateDevice(dxgi.Get(),__uuidof(IDCompositionDevice),(void **)c->composition.GetAddressOf());
    static const wchar_t host_class[]=L"Yoink.CompositionHost";
    WNDCLASSEXW existing={sizeof(existing)};HINSTANCE instance=GetModuleHandleW(nullptr);
    if(!GetClassInfoExW(instance,host_class,&existing)) {
        WNDCLASSEXW type={};type.cbSize=sizeof(type);type.lpfnWndProc=host_procedure;
        type.hInstance=instance;type.lpszClassName=host_class;
        if(!RegisterClassExW(&type)){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=host_class Win32=%lu",GetLastError());release_device(c);return false;}
    }
    c->origin_x=GetSystemMetrics(SM_XVIRTUALSCREEN);c->origin_y=GetSystemMetrics(SM_YVIRTUALSCREEN);
    c->desktop_w=GetSystemMetrics(SM_CXVIRTUALSCREEN);c->desktop_h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    c->host=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        host_class,L"Yoink composition",WS_POPUP,c->origin_x,c->origin_y,c->desktop_w,c->desktop_h,nullptr,nullptr,instance,nullptr);
    if(!c->host||!SetLayeredWindowAttributes(c->host,0,255,LWA_ALPHA)){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=host_window Win32=%lu",GetLastError());release_device(c);return false;}
    if(SUCCEEDED(hr))hr=c->composition->CreateTargetForHwnd(c->host,TRUE,&c->target);
    if(SUCCEEDED(hr))hr=c->composition->CreateVisual(&c->visual);
    if(SUCCEEDED(hr))hr=c->target->SetRoot(c->visual.Get());
    if(FAILED(hr)){describe(c,"backend=SDL compatibility coherent-publication=unverified stage=composition_target HRESULT=%08lx Win32=%lu",hr,GetLastError());release_device(c);return false;}
    /* The input HWND was created without a redirection bitmap. It receives
     * no artwork swaps and Windows hit tests its configured native region. */
    c->active=true;
    ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapter_desc={};
    if(SUCCEEDED(dxgi->GetAdapter(&adapter)))adapter->GetDesc(&adapter_desc);
    describe(c,"backend=DirectComposition WGL_NV_DX_interop2 direct-logical-output=1 GPU-copy=0 GL=%s DXGI=%ls vendor=%04x device=%04x",
        glGetString(GL_RENDERER),adapter_desc.Description,adapter_desc.VendorId,adapter_desc.DeviceId);
    return true;
}
extern "C" bool pp_composition_active(const PPComposition *c){return c&&c->active;}
extern "C" bool pp_composition_prepare(PPComposition *c,int w,int h,PPError *error) {
    if(!pp_composition_active(c))return true;
    if(c->candidate&&c->candidate_w==w&&c->candidate_h==h)return true;
    if(c->output&&c->w==w&&c->h==h){c->candidate.Reset();c->candidate_w=c->candidate_h=0;return true;}
    ComPtr<IDCompositionSurface> candidate;
    HRESULT hr=c->composition->CreateSurface(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&candidate);
    if(FAILED(hr))return failure(error,hr,"Cannot allocate composition output");
    c->candidate=candidate;c->candidate_w=w;c->candidate_h=h;return true;
}
extern "C" size_t pp_composition_output_bytes(const PPComposition *c) {
    return c?(c->output?(size_t)c->w*c->h*4:0)+(c->candidate?(size_t)c->candidate_w*c->candidate_h*4:0):0;
}
extern "C" size_t pp_composition_candidate_bytes(const PPComposition *c,int w,int h) {
    if(c&&((c->candidate&&c->candidate_w==w&&c->candidate_h==h)||(c->output&&c->w==w&&c->h==h)))return 0;
    return (size_t)w*h*4;
}
extern "C" void pp_composition_stage(PPComposition *c,int x,int y){if(c){c->x=x;c->y=y;}}
extern "C" bool pp_composition_order(PPComposition *c) {
    if(!pp_composition_active(c))return true;
    HWND above=GetWindow(c->proxy,GW_HWNDPREV);
    if(above==c->host)return true;
    if(SetWindowPos(c->host,above?above:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))return true;
    pp_composition_hide(c);ShowWindow(c->proxy,SW_HIDE);return false;
}
extern "C" HWND pp_composition_top(PPComposition *c,HWND proxy) {
    if(!pp_composition_active(c))return proxy;
    /* Native GL window notifications can reorder its proxy independently.
     * Raise relative to whichever member currently comes first. */
    for(HWND window=GetTopWindow(nullptr);window;window=GetWindow(window,GW_HWNDNEXT)) {
        if(window==c->host||window==proxy)return window;
    }
    return proxy;
}
static bool commit(PPComposition *c,PPError *error) {
    int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND old_host=nullptr;ComPtr<IDCompositionTarget> old_target;ComPtr<IDCompositionVisual> old_visual;
    HRESULT hr=S_OK;
    if(c->origin_x!=x||c->origin_y!=y||c->desktop_w!=width||c->desktop_h!=height) {
        /* This HWND has never displayed a tree based on the old desktop origin.
         * Its first content-bearing commit contains matching content/offsets.
         * A layout transition may be blank until DWM accepts that first tree. */
        HWND host=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
            L"Yoink.CompositionHost",L"Yoink composition",WS_POPUP,x,y,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!host||!SetLayeredWindowAttributes(host,0,255,LWA_ALPHA)){if(host)DestroyWindow(host);return failure(error,E_FAIL,"Cannot create replacement composition host");}
        ComPtr<IDCompositionTarget> target;ComPtr<IDCompositionVisual> visual;
        hr=c->composition->CreateTargetForHwnd(host,TRUE,&target);
        if(SUCCEEDED(hr))hr=c->composition->CreateVisual(&visual);
        if(SUCCEEDED(hr))hr=target->SetRoot(visual.Get());
        if(FAILED(hr)){DestroyWindow(host);return failure(error,hr,"Cannot prepare replacement composition target");}
        old_host=c->host;old_target=c->target;old_visual=c->visual;
        pp_composition_hide(c);old_target->SetRoot(nullptr);
        c->host=host;c->target=target;c->visual=visual;
        c->origin_x=x;c->origin_y=y;c->desktop_w=width;c->desktop_h=height;
    }
    IDCompositionSurface *content=c->candidate?c->candidate.Get():c->output.Get();
    hr=c->visual->SetContent(content);
    if(SUCCEEDED(hr))hr=c->visual->SetOffsetX((float)(c->x-c->origin_x));
    if(SUCCEEDED(hr))hr=c->visual->SetOffsetY((float)(c->y-c->origin_y));
    if(SUCCEEDED(hr)){double start=c->observer?clock_ms():0;hr=c->composition->Commit();observed(c,PP_COMPOSITION_COMMIT,start);}
    if(old_host)DestroyWindow(old_host);
    if(FAILED(hr))return failure(error,hr,"Cannot commit composition pose");
    if(!IsWindowVisible(c->host))ShowWindow(c->host,SW_SHOWNOACTIVATE);
    if(!IsWindowVisible(c->host)){ShowWindow(c->proxy,SW_HIDE);return failure(error,E_FAIL,"Cannot show composition output");}
    if(!pp_composition_order(c))return failure(error,E_FAIL,"Cannot order composition windows");
    c->published_x=c->x;c->published_y=c->y;c->published=true;c->detach_pending=false;return true;
}
extern "C" bool pp_composition_translate(PPComposition *c,PPError *error) {
    if(!pp_composition_active(c)||!c->output)return true;
    if(c->published&&c->published_x==c->x&&c->published_y==c->y&&IsWindowVisible(c->host)&&
       c->origin_x==GetSystemMetrics(SM_XVIRTUALSCREEN)&&c->origin_y==GetSystemMetrics(SM_YVIRTUALSCREEN)&&
       c->desktop_w==GetSystemMetrics(SM_CXVIRTUALSCREEN)&&c->desktop_h==GetSystemMetrics(SM_CYVIRTUALSCREEN))return true;
    return commit(c,error);
}
extern "C" bool pp_composition_begin(PPComposition *c,int w,int h,GLuint *framebuffer,int *x,int *y,PPError *error) {
    if(!pp_composition_active(c))return failure(error,E_FAIL,"Invalid composition update state");
    if(c->drawing) {
        if(c->update_locked&&c->update_w==w&&c->update_h==h&&c->update_context==wglGetCurrentContext()) {
            c->bind_fb(GL_FRAMEBUFFER,c->update_fb);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
            *framebuffer=c->update_fb;*x=c->update_x;*y=c->update_y;return true;
        }
        return failure(error,E_FAIL,"Previous composition update is still owned by GL");
    }
    if(!pp_composition_prepare(c,w,h,error))return false;
    c->drawing=c->candidate?c->candidate.Get():c->output.Get();
    c->update_context=wglGetCurrentContext();
    POINT offset={};double start=c->observer?clock_ms():0;
    HRESULT hr=c->drawing->BeginDraw(nullptr,__uuidof(IDXGISurface),(void **)c->update.GetAddressOf(),&offset);
    observed(c,PP_COMPOSITION_BEGIN,start);
    if(FAILED(hr)){c->drawing=nullptr;c->candidate.Reset();c->candidate_w=c->candidate_h=0;return failure(error,hr,"Cannot begin composition update");}
    hr=c->update.As(&c->update_texture);
    c->gen_rb(1,&c->update_rb);c->gen_fb(1,&c->update_fb);
    start=c->observer?clock_ms():0;
    if(SUCCEEDED(hr))c->update_object=c->register_dx(c->interop,c->update_texture.Get(),c->update_rb,GL_RENDERBUFFER,0x0001);
    observed(c,PP_COMPOSITION_REGISTER,start);start=c->observer?clock_ms():0;
    c->update_locked=c->update_object&&c->lock_dx(c->interop,1,&c->update_object);
    observed(c,PP_COMPOSITION_LOCK,start);
    if(c->update_locked) {
        c->bind_fb(GL_FRAMEBUFFER,c->update_fb);c->attach_rb(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,c->update_rb);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(c->status_fb(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE&&glGetError()==GL_NO_ERROR) {
            /* NV interop exposes raw top-down DX rows. The shader uses the
             * exact atlas offset, with no clear outside the logical update. */
            c->update_x=offset.x;c->update_y=offset.y;c->update_w=w;c->update_h=h;
            *framebuffer=c->update_fb;*x=offset.x;*y=offset.y;return true;
        }
    }
    pp_composition_end(c,false,nullptr);
    if(!c->drawing){c->candidate.Reset();c->candidate_w=c->candidate_h=0;}
    return failure(error,E_FAIL,"Cannot bind composition update");
}
extern "C" bool pp_composition_end(PPComposition *c,bool drawn,PPError *error) {
    if(!c||!c->drawing)return failure(error,E_FAIL,"Missing composition update");
    if(c->update_context!=wglGetCurrentContext())return failure(error,E_FAIL,"Composition update context is unavailable");
    c->bind_fb(GL_FRAMEBUFFER,0);bool ok=drawn;
    double start=c->observer?clock_ms():0;
    if(c->update_locked) {
        bool released=c->unlock_dx(c->interop,1,&c->update_object)!=FALSE;
        if(!released){ok=false;released=c->unlock_dx(c->interop,1,&c->update_object)!=FALSE;}
        c->update_locked=!released;
    }
    observed(c,PP_COMPOSITION_UNLOCK,start);start=c->observer?clock_ms():0;
    if(c->update_locked){pp_composition_hide(c);return failure(error,E_FAIL,"Cannot release composition GL ownership");}
    if(c->update_object) {
        bool released=c->unregister_dx(c->interop,c->update_object)!=FALSE;
        if(!released){ok=false;released=c->unregister_dx(c->interop,c->update_object)!=FALSE;}
        if(released)c->update_object=nullptr;
    }
    observed(c,PP_COMPOSITION_UNREGISTER,start);
    if(c->update_object){pp_composition_hide(c);return failure(error,E_FAIL,"Cannot unregister composition GL output");}
    c->delete_fb(1,&c->update_fb);c->delete_rb(1,&c->update_rb);c->update_fb=c->update_rb=0;
    c->update_texture.Reset();c->update.Reset();
    IDCompositionSurface *surface=c->drawing;c->drawing=nullptr;
    c->update_context=nullptr;
    start=c->observer?clock_ms():0;HRESULT hr=surface->EndDraw();observed(c,PP_COMPOSITION_END,start);
    if(!ok||FAILED(hr))return failure(error,FAILED(hr)?hr:E_FAIL,"Cannot complete composition update");
    if(!commit(c,error))return false;
    if(c->candidate){c->output=c->candidate;c->candidate.Reset();c->w=c->candidate_w;c->h=c->candidate_h;c->candidate_w=c->candidate_h=0;}
    return true;
}
extern "C" void pp_composition_hide(PPComposition *c){if(c&&c->host)ShowWindow(c->host,SW_HIDE);}
#ifndef PP_COMPOSITION_DETACH
static HRESULT detach_output(IDCompositionVisual *visual,IDCompositionDevice *device) {
    HRESULT hr=visual->SetContent(nullptr);
    return SUCCEEDED(hr)?device->Commit():hr;
}
#define PP_COMPOSITION_DETACH detach_output
#endif
extern "C" void pp_composition_release_output(PPComposition *c) {
    if(!c)return;
    pp_composition_hide(c);
    if(c->drawing){pp_composition_end(c,false,nullptr);if(c->drawing)return;}
    if(c->visual&&FAILED(PP_COMPOSITION_DETACH(c->visual.Get(),c->composition.Get()))) {
        /* The visual may still reference this output. Keep both the native
         * references and logical charge until a later detach or publication. */
        c->detach_pending=true;return;
    }
    c->detach_pending=false;
    c->candidate.Reset();c->output.Reset();c->w=c->h=c->candidate_w=c->candidate_h=0;
}
extern "C" bool pp_composition_pending(const PPComposition *c){return c&&(c->drawing||c->update_object||c->detach_pending);}
extern "C" void pp_composition_release(PPComposition *c){if(c&&!--c->references){
    if(c->update_object||c->detach_pending){
        pp_composition_hide(c);if(c->host)DestroyWindow(c->host);c->host=nullptr;c->proxy=nullptr;
        size_t bytes=pp_composition_output_bytes(c);
        /* Capability probes own one pixel even before logical output exists.
         * Later content owners include this process-lived charge at admission. */
        quarantined_bytes+=bytes?bytes:4;
        c->quarantined_next=quarantined;quarantined=c;return;
    }
    release_device(c);delete c;
}}
extern "C" void pp_composition_destroy(PPComposition *c){if(c){
    if(c->drawing){if(c->host)DestroyWindow(c->host);c->host=nullptr;c->active=false;}
    else release_device(c);
    c->proxy=nullptr;pp_composition_release(c);
}}
