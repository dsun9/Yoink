#import "macos_internal.h"
static PPSurface *owner,*accepted,*buttons[4];
static id global_monitor;
PPSurface *pp_mac_input_owner(void) {return owner?owner:accepted;}
bool pp_platform_pointer(PPVec2 *out) {
    if(!out)return false;*out=pp_mac_desktop(NSEvent.mouseLocation);return true;
}
void pp_platform_release_capture(void) {owner=NULL;accepted=NULL;buttons[PP_BUTTON_LEFT]=NULL;}
void pp_surface_cancel_input(PPSurface *s) {
    if(!s)return;
    if(owner==s)owner=NULL;
    if(accepted==s)accepted=NULL;
    for(int i=1;i<4;i++)if(buttons[i]==s)buttons[i]=NULL;
}
void pp_mac_cancel(PPSurface *s) {
    bool owned=owner==s||accepted==s;
    for(int i=1;i<4;i++)owned|=buttons[i]==s;
    pp_surface_cancel_input(s);
    if(owned)pp_mac_enqueue((PPPlatformEvent){.surface=s,.instance_id=s->instance_id,.kind=PP_EVENT_CANCEL});
}
bool pp_platform_capture(PPSurface *s,PPError *error) {
    if(!s||!s->panel.visible||accepted!=s)
        return pp_mac_error(error,PP_ERROR_ARGUMENT,"Capture requires an accepted sprite press");
    owner=s;return true;
}
void pp_mac_record(PPSurface *s,NSEvent *native) {
    if(!s)return;
    PPPlatformEvent e={.surface=s,.instance_id=s->instance_id,.timestamp=native.timestamp};
    e.desktop_point=pp_mac_desktop([native.window convertPointToScreen:native.locationInWindow]);
    if(native.modifierFlags&NSEventModifierFlagShift)e.modifiers|=PP_MOD_SHIFT;
    if(native.modifierFlags&NSEventModifierFlagControl)e.modifiers|=PP_MOD_CONTROL;
    if(native.modifierFlags&NSEventModifierFlagOption)e.modifiers|=PP_MOD_ALT;
    switch(native.type) {
    case NSEventTypeLeftMouseDown: case NSEventTypeRightMouseDown: case NSEventTypeOtherMouseDown: e.kind=PP_EVENT_BUTTON_DOWN;break;
    case NSEventTypeLeftMouseUp: case NSEventTypeRightMouseUp: case NSEventTypeOtherMouseUp:e.kind=PP_EVENT_BUTTON_UP;break;
    case NSEventTypeScrollWheel:e.kind=PP_EVENT_WHEEL;e.wheel_delta=native.scrollingDeltaY/(native.hasPreciseScrollingDeltas?10.0:1.0);break;
    default:e.kind=PP_EVENT_POINTER_MOVE;break;
    }
    if(e.kind==PP_EVENT_BUTTON_DOWN||e.kind==PP_EVENT_BUTTON_UP) {
        e.button=native.buttonNumber==0?PP_BUTTON_LEFT:native.buttonNumber==1?PP_BUTTON_RIGHT:PP_BUTTON_MIDDLE;
        if(e.kind==PP_EVENT_BUTTON_DOWN) {
            if(e.button==PP_BUTTON_LEFT) {if(accepted)pp_mac_cancel(accepted);accepted=s;}
            buttons[e.button]=s;
        } else {
            s=e.button==PP_BUTTON_LEFT&&owner?owner:buttons[e.button];
            buttons[e.button]=NULL;
            if(e.button==PP_BUTTON_LEFT)pp_platform_release_capture();
            if(!s)return;
        }
    } else if(owner||accepted) s=owner?owner:accepted;
    e.surface=s;e.instance_id=s->instance_id;pp_mac_enqueue(e);
}
void pp_mac_input_add(void) {
    if(global_monitor)return;
    /* Pointer events only. No keyboard monitoring or accessibility request.
     * This observes original outside clicks solely for drawer dismissal. */
    global_monitor=[NSEvent addGlobalMonitorForEventsMatchingMask:
        NSEventMaskLeftMouseDown|NSEventMaskRightMouseDown|NSEventMaskOtherMouseDown
        handler:^(NSEvent *event) {
            if(pp_mac_drawer())pp_mac_enqueue((PPPlatformEvent){.kind=PP_EVENT_BUTTON_DOWN,
                .desktop_point=pp_mac_desktop(NSEvent.mouseLocation),
                .button=event.buttonNumber==0?PP_BUTTON_LEFT:PP_BUTTON_RIGHT,.timestamp=event.timestamp});
        }];
}
void pp_mac_input_remove(void) {
    if(!pp_mac_surfaces&&global_monitor) {[NSEvent removeMonitor:global_monitor];global_monitor=nil;pp_platform_release_capture();}
}
void pp_platform_gesture_config(PPGestureConfig *out) {
    if(out)*out=(PPGestureConfig){3,3,4,4,NSEvent.doubleClickInterval,10,1.10,.120};
}
