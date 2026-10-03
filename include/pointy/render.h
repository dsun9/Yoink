#ifndef POINTY_RENDER_H
#define POINTY_RENDER_H
#include "pointy/types.h"
typedef struct PPRenderer PPRenderer;
/* Owned, top-down premultiplied RGBA8. content_alpha is tightly packed and
 * describes transformed content coverage before display opacity is applied. */
typedef struct PPFrame {
    uint8_t *pixels;
    uint8_t *content_alpha;
    int width, height;
    size_t stride;
} PPFrame;
void pp_frame_destroy(PPFrame *frame);
bool pp_renderer_create(PPRenderer **out, PPError *error);
/* Borrows source; out must be empty. longest_dip scales the unrotated size. */
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source,
    double longest_dip, double angle_deg, double opacity, double pixels_per_dip,
    PPFrame *out, PPError *error);
void pp_renderer_destroy(PPRenderer *renderer);
#endif
