#include "../../src/platform/windows/windows_internal.h"
#include "pointy/interaction.h"
#include "../test.h"
int main(void) {
    PPSurface a={0},b={0};a.instance_id=42;b.instance_id=43;
    PPGestureConfig config={4,4,6,6,.5,10,1.10,.120};
    PPInteraction state={0};state.displayed_pose=(PPPose){{0,0},64,0,1};
    PPInteractionResult result;
    PPPlatformEvent event={0};event.surface=&a;event.instance_id=42;event.kind=PP_EVENT_BUTTON_DOWN;event.button=PP_BUTTON_LEFT;
    pp_interaction_step(&state,&event,&config,&result);
    event.kind=PP_EVENT_POINTER_MOVE;event.desktop_point.x=10;pp_interaction_step(&state,&event,&config,&result);CHECK(state.dragging);
    event=(PPPlatformEvent){0};event.surface=&b;event.instance_id=43;event.kind=PP_EVENT_WHEEL;event.wheel_delta=1;
    for(int i=0;i<512;i++) CHECK(pp_windows_enqueue(event));
    /* Native cancellation clears capture before enqueuing cancellation. No
     * queued press/release remains to identify A except this incoming event. */
    event.surface=&a;event.instance_id=42;event.kind=PP_EVENT_CANCEL;event.wheel_delta=0;
    pp_windows_enqueue(event);
    bool cancelled=false;
    while(pp_platform_poll(&event)) {
        if(event.kind==PP_EVENT_CANCEL && event.instance_id==42) cancelled=true;
        pp_interaction_step(&state,&event,&config,&result);
    }
    CHECK(cancelled && !state.pressed && !state.dragging);
    puts("PASS saturated queue preserves incoming cancellation after native ownership ended.");return 0;
}
