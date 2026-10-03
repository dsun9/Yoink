#include "pointy/platform.h"
#include <windows.h>
#include <shellscalingapi.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct Enumeration {PPDisplay *items;size_t count;bool failed;} Enumeration;
static BOOL CALLBACK monitor(HMONITOR handle,HDC dc,LPRECT rect,LPARAM context) {
    (void)dc;(void)rect;Enumeration *e=(Enumeration *)context;MONITORINFOEXW info={0};info.cbSize=sizeof(info);
    UINT x=96,y=96;
    if(!GetMonitorInfoW(handle,(MONITORINFO *)&info)||FAILED(GetDpiForMonitor(handle,MDT_EFFECTIVE_DPI,&x,&y))) {e->failed=true;return FALSE;}
    PPDisplay *items=realloc(e->items,(e->count+1)*sizeof(*items));if(!items) {e->failed=true;return FALSE;}e->items=items;
    uint64_t id=14695981039346656037ULL;
    for(const wchar_t *p=info.szDevice;*p;p++) {id^=(uint16_t)*p;id*=1099511628211ULL;}
    RECT b=info.rcMonitor;double scale=x/96.0;
    items[e->count++]=(PPDisplay){id,{b.left,b.top,b.right-b.left,b.bottom-b.top},scale,scale};return TRUE;
}
bool pp_platform_displays(PPDisplay **out,size_t *count,PPError *error) {
    if(!out||!count) {if(error) {error->code=PP_ERROR_ARGUMENT;snprintf(error->message,sizeof(error->message),"Missing display outputs");}return false;}
    *out=NULL;*count=0;Enumeration e={0};
    DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool ok=previous&&EnumDisplayMonitors(NULL,NULL,monitor,(LPARAM)&e)&&!e.failed&&e.count;
    if(previous) SetThreadDpiAwarenessContext(previous);
    if(!ok) {free(e.items);if(error) {error->code=PP_ERROR_PLATFORM;snprintf(error->message,sizeof(error->message),"Cannot enumerate physical monitors with per-monitor DPI");}return false;}
    *out=e.items;*count=e.count;return true;
}
void pp_displays_free(PPDisplay *items) {free(items);}
bool pp_platform_pointer(PPVec2 *out) {
    if(!out) return false;POINT p;DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool ok=previous&&GetCursorPos(&p);if(previous) SetThreadDpiAwarenessContext(previous);if(ok) *out=(PPVec2){p.x,p.y};return ok;
}
