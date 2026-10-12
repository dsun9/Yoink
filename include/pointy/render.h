#ifndef POINTY_RENDER_H
#define POINTY_RENDER_H
#include "pointy/types.h"
typedef struct PPRenderer PPRenderer;
typedef struct PPSharedTexture PPSharedTexture;
struct PPFrame;
/* Explicit context group owner. It outlives joined renderers and textures.
 * All operations stay on the owning application thread. */
bool pp_renderer_create_texture_owner(PPRenderer **out,PPError *error);
bool pp_renderer_create_shared_window(PPRenderer **out,uint32_t window_id,PPRenderer *owner,PPError *error);
bool pp_shared_texture_create(PPRenderer *owner,const struct PPFrame *frame,PPSharedTexture **out,PPError *error);
bool pp_renderer_bind_shared(PPRenderer *renderer,PPSharedTexture *texture,uint64_t content_id,uint64_t revision,PPError *error);
void pp_shared_texture_destroy(PPSharedTexture *texture);
typedef struct PPRendererMetrics {
    uint64_t source_uploads, transform_draws, readbacks;
} PPRendererMetrics;
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
/* Direct presentation is opt-in until native feasibility has passed. Borrows
 * an SDL OpenGL window by ID; destroy the renderer before the owning surface. */
bool pp_renderer_create_for_window(PPRenderer **out, uint32_t window_id, PPError *error);
/* Retains one RGBA texture, no coverage texture. A matching nonzero content ID
 * and revision reuses the upload. Source bytes are borrowed only during upload. */
bool pp_renderer_upload_source(PPRenderer *renderer, const PPFrame *source,
    uint64_t content_id, uint64_t revision, PPError *error);
/* Draws the retained source into the window and swaps; never returns CPU pixels.
 * layout_size defines the canonical unrotated aspect, independent of source
 * raster dimensions. Window drawable dimensions must match its extent. */
bool pp_renderer_draw(PPRenderer *renderer, PPVec2 layout_size, double longest_dip,
    double angle_deg, double opacity, double pixels_per_dip, PPError *error);
/* Maps the source into a canonical local rectangle while keeping pose center,
 * layout aspect and size fixed. Bounds include overhang beyond that layout. */
bool pp_renderer_draw_source(PPRenderer *renderer, PPVec2 layout_size, PPRect source_rect,
    double longest_dip, double angle_deg, double opacity, double pixels_per_dip, PPError *error);
/* Waits for this context's submitted GPU work. This is a diagnostic completion
 * boundary, not a visible-compositor frame-rate guarantee. */
bool pp_renderer_complete(PPRenderer *renderer, PPError *error);
PPRendererMetrics pp_renderer_metrics(const PPRenderer *renderer);
/* Borrows source; out must be empty. longest_dip scales the unrotated size. */
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source,
    double longest_dip, double angle_deg, double opacity, double pixels_per_dip,
    PPFrame *out, PPError *error);
/* Explicit diagnostic/export readback with the same canonical mapping as
 * retained drawing. Normal native presentation uses draw_source instead. */
bool pp_renderer_transform_source(PPRenderer *renderer,const PPFrame *source,
    PPVec2 layout,PPRect source_rect,double longest_dip,double angle_deg,
    double opacity,double pixels_per_dip,PPFrame *out,PPError *error);
void pp_renderer_destroy(PPRenderer *renderer);
#endif
