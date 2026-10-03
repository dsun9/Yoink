#include "pointy/interaction.h"
#include "../test.h"
#include <math.h>
#include <string.h>
static PPPlatformEvent event(PPPlatformEventKind kind, double x, double y, double time) {
    PPPlatformEvent e = {0}; e.kind = kind; e.instance_id = 42;
    e.desktop_point = (PPVec2){x,y}; e.button = PP_BUTTON_LEFT; e.timestamp = time; return e;
}
static void test_gestures_and_transforms(void) {
    uint8_t alpha[64*64] = {0}; uint8_t pixels[64*64*4] = {0};
    for (int y=0;y<64;y++) for(int x=0;x<64;x++) {
        double r=hypot(x-31.5,y-31.5); if(r>=18 && r<=29) alpha[y*64+x]=255;
    }
    PPFrame source={pixels,alpha,64,64,256}; PPPose pose={{200,300},128,90,1};
    CHECK(!pp_content_hit(&source,&pose,1.5,(PPVec2){200,300}));
    CHECK(pp_content_hit(&source,&pose,1.5,(PPVec2){200,375}));
    CHECK(!pp_content_hit(&source,&pose,1.5,(PPVec2){400,300}));
    pose.opacity=0; CHECK(!pp_content_hit(&source,&pose,1.5,(PPVec2){200,375}));
    CHECK(!pp_content_hit(&source,&pose,0,(PPVec2){200,300}));
    PPGestureConfig config={4,4,6,6,0.5,10,1.10,0.120};
    PPInteraction state={0}; state.displayed_pose=(PPPose){{100,200},96,355,1};
    PPInteractionResult out={0}; PPPlatformEvent e=event(PP_EVENT_BUTTON_DOWN,110,205,1);
    pp_interaction_step(&state,&e,&config,&out); CHECK(out.command==PP_COMMAND_NONE);
    e=event(PP_EVENT_POINTER_MOVE,114,209,1.1); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_NONE);
    e=event(PP_EVENT_POINTER_MOVE,115,209,1.2); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_BEGIN_DRAG && out.instance_id==42);
    CHECK(out.pose.center.x==105 && out.pose.center.y==204);
    e=event(PP_EVENT_BUTTON_UP,115,209,1.3); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_END_DRAG);
    e=event(PP_EVENT_BUTTON_DOWN,115,209,1.4); pp_interaction_step(&state,&e,&config,&out);
    e=event(PP_EVENT_BUTTON_UP,115,209,1.45); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_NONE); /* A drag must not seed double click. */
    e=event(PP_EVENT_BUTTON_DOWN,116,209,1.6); pp_interaction_step(&state,&e,&config,&out);
    e=event(PP_EVENT_BUTTON_UP,116,209,1.65); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_OPEN_DRAWER);
    e=event(PP_EVENT_WHEEL,0,0,2); e.wheel_delta=1;
    pp_interaction_step(&state,&e,&config,&out); CHECK(out.command==PP_COMMAND_ROTATE && out.pose.angle_deg==365);
    state.displayed_pose.angle_deg=pp_ease_value(355,365,.030,.120);
    double displayed=state.displayed_pose.angle_deg;
    e.wheel_delta=.5; pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.pose.angle_deg==370 && out.from_pose.angle_deg==displayed);
    CHECK(pp_ease_value(out.from_pose.angle_deg,out.pose.angle_deg,0,.120)==displayed);
    e.modifiers=PP_MOD_ALT; e.wheel_delta=1; pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_RESIZE); double up=out.pose.longest_dip/96;
    e.wheel_delta=-1; pp_interaction_step(&state,&e,&config,&out);
    CHECK(fabs(up*(out.pose.longest_dip/(96*up))-1)<1e-12);
    CHECK(fabs(out.pose.longest_dip-96)<1e-12);
    e=event(PP_EVENT_BUTTON_DOWN,0,0,3); e.button=PP_BUTTON_RIGHT;
    pp_interaction_step(&state,&e,&config,&out); CHECK(out.command==PP_COMMAND_DELETE);
    e=event(PP_EVENT_BUTTON_DOWN,110,205,4); pp_interaction_step(&state,&e,&config,&out);
    e=event(PP_EVENT_POINTER_MOVE,120,205,4.1); pp_interaction_step(&state,&e,&config,&out);
    e=event(PP_EVENT_CANCEL,0,0,4.15);e.instance_id=99;
    pp_interaction_step(&state,&e,&config,&out);CHECK(out.command==PP_COMMAND_NONE && state.dragging);
    e=event(PP_EVENT_CANCEL,120,205,4.2); pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_CANCEL_DRAG);
    CHECK(pp_ease_value(0,10,.120,.120)==10);
    CHECK(pp_ease_value(0,10,-1,.120)==0);
    CHECK(pp_ease_value(0,10,.060,.120)==8.75);
    CHECK(pp_ease_value(0,10,1,0)==10);
    /* Coalescing/OS delivery can omit the last move; release displacement still
     * ends a drag and must never seed a double click. */
    state=(PPInteraction){0};state.displayed_pose=(PPPose){{100,200},96,0,1};
    e=event(PP_EVENT_BUTTON_DOWN,110,205,5);pp_interaction_step(&state,&e,&config,&out);
    e=event(PP_EVENT_BUTTON_UP,140,215,5.1);pp_interaction_step(&state,&e,&config,&out);
    CHECK(out.command==PP_COMMAND_END_DRAG && out.pose.center.x==130 && out.pose.center.y==210);
    CHECK(!state.click_valid);
}
int main(void) {test_gestures_and_transforms(); puts("PASS transforms, drag thresholds, click exclusion, wheel targets, reciprocal resize, easing.");return 0;}
