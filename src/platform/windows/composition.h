#ifndef POINTY_WINDOWS_COMPOSITION_H
#define POINTY_WINDOWS_COMPOSITION_H
#include "pointy/types.h"
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PPComposition PPComposition;
#define PP_COMPOSITION_PROPERTY L"Yoink.Composition"
PPComposition *pp_composition_create(HWND proxy);
PPComposition *pp_composition_retain(PPComposition *composition);
void pp_composition_release(PPComposition *composition);
/* Optional WGL interop capability. A false result keeps the SDL compatibility
 * presenter selected before any source or output is accepted. */
bool pp_composition_initialize(PPComposition *composition);
/* Establish the process device during normal renderer/runtime startup. Each
 * native output still probes its actual current WGL context before selection. */
bool pp_composition_bootstrap(void);
bool pp_composition_active(const PPComposition *composition);
/* Stored metadata only; querying never writes to a stream. */
const char *pp_composition_description(const PPComposition *composition);
typedef bool (*PPCompositionFallback)(void *owner,uint32_t *window_id,PPError *error);
void pp_composition_set_fallback(PPComposition *composition,PPCompositionFallback fallback,void *owner);
bool pp_composition_needs_fallback(const PPComposition *composition);
bool pp_composition_select_fallback(PPComposition *composition,uint32_t *window_id,PPError *error);
void pp_composition_proxy(PPComposition *composition,HWND proxy);
bool pp_composition_prepare(PPComposition *composition,int width,int height,PPError *error);
bool pp_composition_begin(PPComposition *composition,int width,int height,unsigned int *framebuffer,int *x,int *y,PPError *error);
bool pp_composition_end(PPComposition *composition,bool drawn,PPError *error);
size_t pp_composition_output_bytes(const PPComposition *composition);
size_t pp_composition_candidate_bytes(const PPComposition *composition,int width,int height);
bool pp_composition_translate(PPComposition *composition,PPError *error);
void pp_composition_stage(PPComposition *composition,int x,int y);
void pp_composition_hide(PPComposition *composition);
bool pp_composition_order(PPComposition *composition);
HWND pp_composition_top(PPComposition *composition,HWND proxy);
enum PPCompositionStage {PP_COMPOSITION_BEGIN,PP_COMPOSITION_REGISTER,PP_COMPOSITION_LOCK,
    PP_COMPOSITION_COPY,PP_COMPOSITION_UNLOCK,PP_COMPOSITION_UNREGISTER,PP_COMPOSITION_END,PP_COMPOSITION_COMMIT};
typedef void (*PPCompositionObserver)(enum PPCompositionStage stage,double elapsed_ms);
void pp_composition_observe(PPComposition *composition,PPCompositionObserver observer);
void pp_composition_release_output(PPComposition *composition);
bool pp_composition_pending(const PPComposition *composition);
size_t pp_composition_quarantined_bytes(void);
void pp_composition_destroy(PPComposition *composition);
#ifdef __cplusplus
}
#endif
#endif
