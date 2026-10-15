/* Native diagnostic only. Faults cross the real WGL interop boundary once;
 * allocation, rendering, cleanup and window ownership remain production code. */
#include <SDL3/SDL.h>
#include <windows.h>
#include <SDL3/SDL_opengl.h>
#include <cstring>
#include <dcomp.h>
static const char *control="";
static unsigned registrations,locks,unlocks,unregistrations;
static bool reject(unsigned call,const char *stage) {
    return (call==2&&!strcmp(control,stage))||(call==1&&!strncmp(control,"init-",5)&&!strcmp(control+5,stage));
}
extern "C" void pp_test_composition_control(const char *name) {
    control=name;registrations=locks=unlocks=unregistrations=0;
}
static HANDLE WINAPI register_control(HANDLE device,void *object,GLuint name,GLenum type,GLenum access) {
    if(reject(++registrations,"register")){SetLastError(ERROR_GEN_FAILURE);return nullptr;}
    typedef HANDLE (WINAPI *Call)(HANDLE,void *,GLuint,GLenum,GLenum);
    return ((Call)SDL_GL_GetProcAddress("wglDXRegisterObjectNV"))(device,object,name,type,access);
}
static BOOL WINAPI lock_control(HANDLE device,GLint count,HANDLE *objects) {
    if(!strcmp(control,"prepare-persistent-unregister")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    if(reject(++locks,"lock")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    typedef BOOL (WINAPI *Call)(HANDLE,GLint,HANDLE *);
    return ((Call)SDL_GL_GetProcAddress("wglDXLockObjectsNV"))(device,count,objects);
}
static BOOL WINAPI unlock_control(HANDLE device,GLint count,HANDLE *objects) {
    if(!strcmp(control,"persistent-unlock")||!strcmp(control,"init-persistent-unlock")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    if(reject(++unlocks,"unlock")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    typedef BOOL (WINAPI *Call)(HANDLE,GLint,HANDLE *);
    return ((Call)SDL_GL_GetProcAddress("wglDXUnlockObjectsNV"))(device,count,objects);
}
static BOOL WINAPI unregister_control(HANDLE device,HANDLE object) {
    if(!strcmp(control,"persistent-unregister")||!strcmp(control,"init-persistent-unregister")||!strcmp(control,"prepare-persistent-unregister")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    if(reject(++unregistrations,"unregister")){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    typedef BOOL (WINAPI *Call)(HANDLE,HANDLE);
    return ((Call)SDL_GL_GetProcAddress("wglDXUnregisterObjectNV"))(device,object);
}
static SDL_FunctionPointer get_proc(const char *name) {
    if(!strcmp(name,"wglGetExtensionsStringARB")&&!strcmp(control,"missing"))return nullptr;
    if(!strcmp(name,"wglDXRegisterObjectNV"))return (SDL_FunctionPointer)register_control;
    if(!strcmp(name,"wglDXLockObjectsNV"))return (SDL_FunctionPointer)lock_control;
    if(!strcmp(name,"wglDXUnlockObjectsNV"))return (SDL_FunctionPointer)unlock_control;
    if(!strcmp(name,"wglDXUnregisterObjectNV"))return (SDL_FunctionPointer)unregister_control;
    return SDL_GL_GetProcAddress(name);
}
#define SDL_GL_GetProcAddress get_proc
static int WINAPI virtual_metrics(int index) {
    int value=GetSystemMetrics(index);
    if(!strcmp(control,"layout-expanded")) {
        if(index==SM_XVIRTUALSCREEN||index==SM_YVIRTUALSCREEN)value-=64;
        if(index==SM_CXVIRTUALSCREEN||index==SM_CYVIRTUALSCREEN)value+=128;
    }
    return value;
}
#define GetSystemMetrics virtual_metrics
static HRESULT detach_control(IDCompositionVisual *visual,IDCompositionDevice *device) {
    if(!strcmp(control,"detach-content"))return E_FAIL;
    HRESULT hr=visual->SetContent(nullptr);
    if(SUCCEEDED(hr))hr=!strcmp(control,"detach-commit")?E_FAIL:device->Commit();
    return hr;
}
#define PP_COMPOSITION_DETACH detach_control
#include "../../src/platform/windows/composition.cpp"
