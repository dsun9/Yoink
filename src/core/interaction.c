#include "pointy/interaction.h"
#include <math.h>
#include <string.h>
void pp_interaction_step(PPInteraction *s,const PPPlatformEvent *e,const PPGestureConfig *c,PPInteractionResult *out) {
    if(!out) return;
    memset(out,0,sizeof(*out));if(!s||!e||!c) return;
    if(e->kind==PP_EVENT_CANCEL || e->kind==PP_EVENT_QUIT) {
        if(e->kind==PP_EVENT_CANCEL && e->instance_id!=s->instance_id) return;
        out->instance_id=s->instance_id;out->pose=s->displayed_pose;
        if(s->dragging) out->command=PP_COMMAND_CANCEL_DRAG;
        s->pressed=s->dragging=s->click_valid=false;return;
    }
    if(!s->target_valid || (!s->pressed && s->instance_id!=e->instance_id)) {
        s->instance_id=e->instance_id;s->target_pose=s->displayed_pose;s->target_valid=true;s->click_valid=false;
    }
    if(e->instance_id!=s->instance_id) return;
    out->instance_id=s->instance_id;out->from_pose=s->displayed_pose;out->pose=s->displayed_pose;
    if(e->kind==PP_EVENT_BUTTON_DOWN) {
        if(e->button==PP_BUTTON_RIGHT) {out->command=PP_COMMAND_DELETE;s->click_valid=false;}
        if(e->button==PP_BUTTON_LEFT && !s->pressed) {
            s->pressed=true;s->dragging=false;s->press_point=e->desktop_point;s->press_center=s->displayed_pose.center;
        }
    } else if(e->kind==PP_EVENT_POINTER_MOVE && s->pressed) {
        double dx=e->desktop_point.x-s->press_point.x,dy=e->desktop_point.y-s->press_point.y;
        if(s->dragging || fabs(dx)>c->drag_x || fabs(dy)>c->drag_y) {
            out->command=s->dragging?PP_COMMAND_MOVE_DRAG:PP_COMMAND_BEGIN_DRAG;
            s->dragging=true;s->click_valid=false;
            out->pose.center=(PPVec2){s->press_center.x+dx,s->press_center.y+dy};
            s->displayed_pose.center=out->pose.center;s->target_pose.center=out->pose.center;
        }
    } else if(e->kind==PP_EVENT_BUTTON_UP && e->button==PP_BUTTON_LEFT && s->pressed) {
        if(s->dragging || fabs(e->desktop_point.x-s->press_point.x)>c->drag_x ||
            fabs(e->desktop_point.y-s->press_point.y)>c->drag_y) {
            out->command=PP_COMMAND_END_DRAG;
            out->pose.center=(PPVec2){s->press_center.x+e->desktop_point.x-s->press_point.x,s->press_center.y+e->desktop_point.y-s->press_point.y};
            s->displayed_pose.center=out->pose.center;s->target_pose.center=out->pose.center;s->click_valid=false;
        } else {
            double dt=e->timestamp-s->last_click_time;
            if(s->click_valid && dt>=0 && dt<=c->double_click_seconds &&
                fabs(e->desktop_point.x-s->last_click_point.x)<=c->double_click_x &&
                fabs(e->desktop_point.y-s->last_click_point.y)<=c->double_click_y) {
                out->command=PP_COMMAND_OPEN_DRAWER;s->click_valid=false;
            } else {s->click_valid=true;s->last_click_time=e->timestamp;s->last_click_point=e->desktop_point;}
        }
        s->pressed=s->dragging=false;
    } else if(e->kind==PP_EVENT_WHEEL && isfinite(e->wheel_delta) && e->wheel_delta!=0) {
        out->easing_seconds=c->easing_seconds;
        if(e->modifiers&PP_MOD_ALT) {
            double factor=pow(c->resize_factor,e->wheel_delta);
            double target=s->target_pose.longest_dip*factor;
            if(!isfinite(target)||target<=0) return;
            s->target_pose.longest_dip=target;out->pose.longest_dip=target;out->command=PP_COMMAND_RESIZE;
        } else {
            double target=s->target_pose.angle_deg+c->rotation_step_deg*e->wheel_delta;
            if(!isfinite(target)) return;
            s->target_pose.angle_deg=target;out->pose.angle_deg=target;out->command=PP_COMMAND_ROTATE;
        }
    }
}
