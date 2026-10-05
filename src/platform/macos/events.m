#import "macos_internal.h"
PPSurface *pp_mac_input_owner(void);
/* Fixed queue: hooks never allocate. Consecutive moves are coalesced. */
#define EVENT_CAPACITY 512
static PPPlatformEvent events[EVENT_CAPACITY];
static unsigned first,count;
static void discard_moves(PPSurface *keep_owner) {
    unsigned kept=0;
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.kind!=PP_EVENT_POINTER_MOVE || (keep_owner && event.surface==keep_owner))
            events[(first+kept++)%EVENT_CAPACITY]=event;
    }
    count=kept;
}
static void append_cancellation(PPPlatformEvent event) {
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent *queued=&events[(first+i)%EVENT_CAPACITY];
        if(queued->surface==event.surface && queued->instance_id==event.instance_id) return;
    }
    event.kind=PP_EVENT_CANCEL;event.button=PP_BUTTON_NONE;
    events[(first+count++)%EVENT_CAPACITY]=event;
}
static void cancel_discarded_gestures(PPPlatformEvent incoming,PPSurface *owner) {
    unsigned discarded=count;count=0;
    /* Compact in place: output never advances past the input cursor. Preserve
     * every affected left-button gesture, including releases queued after
     * native ownership ended. Deduplicate cancellations by surface and ID. */
    for(unsigned i=0;i<discarded;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.kind==PP_EVENT_CANCEL ||
            ((event.kind==PP_EVENT_BUTTON_DOWN || event.kind==PP_EVENT_BUTTON_UP) && event.button==PP_BUTTON_LEFT))
            append_cancellation(event);
    }
    if(owner && count<EVENT_CAPACITY) {
        PPPlatformEvent event=incoming;event.surface=owner;event.instance_id=owner->instance_id;
        append_cancellation(event);
    }
    if((incoming.kind==PP_EVENT_CANCEL || incoming.button==PP_BUTTON_LEFT) && count<EVENT_CAPACITY)
        append_cancellation(incoming);
    pp_platform_release_capture();
}
void pp_mac_enqueue(PPPlatformEvent event) {
    if(event.kind==PP_EVENT_POINTER_MOVE && count) {
        unsigned last=(first+count-1)%EVENT_CAPACITY;
        if(events[last].kind==PP_EVENT_POINTER_MOVE && events[last].surface==event.surface) {events[last]=event;return;}
    }
    if(count==EVENT_CAPACITY) {
        PPSurface *owner=pp_mac_input_owner();
        if(event.kind==PP_EVENT_POINTER_MOVE && event.surface!=owner) return;
        /* Sacrifice hover before owned movement or gesture boundaries. All
         * work is bounded by the fixed queue; no allocation in hook callbacks. */
        discard_moves(owner);
        if(count==EVENT_CAPACITY) discard_moves(NULL);
        if(count==EVENT_CAPACITY) {cancel_discarded_gestures(event,owner);return;}
    }
    events[(first+count++)%EVENT_CAPACITY]=event;return;
}
void pp_mac_forget(PPSurface *surface) {
    unsigned kept=0;
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.surface!=surface) events[(first+kept++)%EVENT_CAPACITY]=event;
    }
    count=kept;
}
bool pp_platform_poll(PPPlatformEvent *out) {
    if(!out)return false;
    /* Pump without draining SDL's UI queue; pp_ui_poll owns that queue. */
    @autoreleasepool { SDL_PumpEvents(); }
    if(!count)return false;
    *out=events[first];first=(first+1)%EVENT_CAPACITY;count--;return true;
}
