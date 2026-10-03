#ifndef POINTY_DISPLAY_H
#define POINTY_DISPLAY_H
#include "pointy/types.h"
typedef enum PPAnchor {
    PP_ANCHOR_TOP_LEFT, PP_ANCHOR_TOP_CENTER, PP_ANCHOR_TOP_RIGHT,
    PP_ANCHOR_CENTER_LEFT, PP_ANCHOR_CENTER_RIGHT,
    PP_ANCHOR_BOTTOM_LEFT, PP_ANCHOR_BOTTOM_CENTER, PP_ANCHOR_BOTTOM_RIGHT,
    PP_ANCHOR_COUNT
} PPAnchor;
typedef struct PPDisplay {
    uint64_t id;
    PPRect bounds; /* Native desktop units; never drawable raster coordinates. */
    double desktop_units_per_dip, drawable_pixels_per_dip;
} PPDisplay;
typedef struct PPAnchorReveal {
    uint64_t display_id;
    PPAnchor anchor;
    PPRect target, preview;
    double opacity, leave_time;
    bool revealed, leaving, held;
} PPAnchorReveal;
/* Zero initialize. A shared enabled mask applies to every display. Configure
 * disabled/empty anchors through pp_anchors_enable; item definitions live elsewhere. */
typedef struct PPAnchorRuntime {
    PPAnchorReveal *entries;
    size_t count;
    bool enabled[PP_ANCHOR_COUNT], configured, failed;
    double activation_dip, preview_dip[PP_ANCHOR_COUNT], easing_seconds;
    bool layout_configured;
} PPAnchorRuntime;
PPRect pp_anchor_bounds(const PPDisplay *display, PPAnchor anchor, double size_dip);
PPRect pp_anchor_preview_bounds(const PPDisplay *display, PPAnchor anchor, double size_dip);
PPVec2 pp_display_to_local(const PPDisplay *display, PPVec2 desktop);
PPVec2 pp_display_to_desktop(const PPDisplay *display, PPVec2 local_dip);
const PPDisplay *pp_display_at(const PPDisplay *displays, size_t count, PPVec2 point);
bool pp_displays_equal(const PPDisplay *a,size_t a_count,const PPDisplay *b,size_t b_count);
PPVec2 pp_display_drag_center(const PPDisplay *display, PPVec2 pointer, PPVec2 grab_dip);
PPVec2 pp_recover_center(const PPDisplay *displays, size_t count, PPVec2 old_center, double longest_dip);
void pp_anchors_enable(PPAnchorRuntime *runtime, PPAnchor anchor, bool enabled);
/* Borrowed project settings can configure geometry without per-display lists. */
void pp_anchors_layout(PPAnchorRuntime *runtime, double activation_dip,
    const double preview_dip[PP_ANCHOR_COUNT], double easing_seconds);
void pp_anchors_update(PPAnchorRuntime *runtime, const PPDisplay *displays, size_t count, PPVec2 pointer, double now);
void pp_anchors_destroy(PPAnchorRuntime *runtime);
#endif
