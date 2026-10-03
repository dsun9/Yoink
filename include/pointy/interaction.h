#ifndef POINTY_INTERACTION_H
#define POINTY_INTERACTION_H
#include "pointy/platform.h"
typedef struct PPPose {
    PPVec2 center; /* Physical desktop coordinates. */
    double longest_dip, angle_deg, opacity;
} PPPose;
typedef struct PPGestureConfig {
    double drag_x, drag_y, double_click_x, double_click_y, double_click_seconds;
    double rotation_step_deg, resize_factor, easing_seconds;
} PPGestureConfig;
typedef enum PPInteractionCommand {
    PP_COMMAND_NONE, PP_COMMAND_BEGIN_DRAG, PP_COMMAND_MOVE_DRAG,
    PP_COMMAND_END_DRAG, PP_COMMAND_CANCEL_DRAG, PP_COMMAND_ROTATE,
    PP_COMMAND_RESIZE, PP_COMMAND_DELETE, PP_COMMAND_OPEN_DRAWER
} PPInteractionCommand;
/* Zero initialize. The caller updates displayed_pose from the scene before
 * each step. target_pose accumulates wheel input immediately, independently
 * of the displayed animation. Use one record per interacting instance; reset
 * after externally replacing its pose. Wheel commands change only their
 * indicated property, and retarget its easing from from_pose to pose. */
typedef struct PPInteraction {
    PPPose displayed_pose, target_pose;
    uint64_t instance_id;
    PPVec2 press_point, press_center, last_click_point;
    double last_click_time;
    bool target_valid, pressed, dragging, click_valid;
} PPInteraction;
typedef struct PPInteractionResult {
    PPInteractionCommand command;
    uint64_t instance_id;
    PPPose from_pose, pose;
    double easing_seconds;
} PPInteractionResult;
bool pp_content_hit(const PPFrame *source, const PPPose *pose,
    double desktop_units_per_dip, PPVec2 desktop_point);
void pp_interaction_step(PPInteraction *state, const PPPlatformEvent *event,
    const PPGestureConfig *config, PPInteractionResult *out);
double pp_ease_value(double start, double target, double elapsed_seconds, double duration_seconds);
/* OS drag/double-click thresholds plus the application's default wheel/ease values. */
void pp_platform_gesture_config(PPGestureConfig *out);
#endif
