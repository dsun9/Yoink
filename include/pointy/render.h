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
    uint64_t source_uploads, transform_draws, readbacks, presentation_refreshes;
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
/* UI adapter borrows its owning window/context. Destroy before that context.
 * It restores the caller's context and GL state on every draw/failure. */
bool pp_renderer_create_for_ui(PPRenderer **out,uint32_t window_id,void *context,PPError *error);
/* Retained top-down premultiplied color texture in the borrowed UI context.
 * A UI adapter converts after its final linear sample at the blend boundary.
 * No coverage attachment, CPU transform or readback.
 * Requires that owning UI context to be current (inside its frame). Another
 * current context is rejected without changing it. A false return preserves
 * the old output. Caller reserves source and old/candidate output bytes. */
bool pp_renderer_preview_source(PPRenderer *renderer,const PPFrame *source,
    uint64_t content_id,uint64_t source_revision,PPVec2 layout,PPRect source_rect,
    double longest_dip,double angle_deg,double opacity,double density,
    unsigned *texture,int *width,int *height,PPError *error);
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
/* Reserve candidate bytes before drawing, including overlap with output_bytes.
 * A changed extent replaces the old target only after successful publication. */
size_t pp_renderer_output_candidate_bytes(const PPRenderer *renderer,int width,int height);
size_t pp_renderer_output_bytes(const PPRenderer *renderer);
void pp_renderer_release_output(PPRenderer *renderer);
/* Re-publish complete retained color after native exposure, without a source
 * upload, transform draw, default-framebuffer readback or CPU pixel copy. */
bool pp_renderer_refresh(PPRenderer *renderer,PPError *error);
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
/* Color-only Cocoa compatibility publication. Produces one RGBA readback;
 * source coverage remains owned for source validation/grab mapping and is
 * never transformed for rectangular presentation. */
bool pp_renderer_transform_color_source(PPRenderer *renderer,const PPFrame *source,
    PPVec2 layout,PPRect source_rect,double longest_dip,double angle_deg,
    double opacity,double pixels_per_dip,PPFrame *out,PPError *error);
void pp_renderer_destroy(PPRenderer *renderer);
/* Consumes renderer. False transfers undeletable shared objects to its group
 * owner; the caller must keep their GPU charge until that owner is destroyed. */
bool pp_renderer_destroy_retained(PPRenderer *renderer);
#endif
