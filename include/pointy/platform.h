#ifndef POINTY_PLATFORM_H
#define POINTY_PLATFORM_H
#include "pointy/render.h"
#include "pointy/grab.h"
#include "pointy/display.h"
#include <stdio.h>

/* UTF-8 paths; caller owns FILE handles and temporary_path (free). Temporary
 * creation is exclusive and in the target directory. Flush includes native
 * stable-storage flush. Replace requires all writer handles already closed. */
FILE *pp_platform_open_file(const char *path, const char *mode, PPError *error);
FILE *pp_platform_temporary_file(const char *target, char **temporary_path, PPError *error);
bool pp_platform_file_seek(FILE *file, uint64_t offset);
bool pp_platform_file_size(FILE *file, uint64_t *size);
bool pp_platform_flush_file(FILE *file, PPError *error);
bool pp_platform_replace_file(const char *temporary_path, const char *target_path, PPError *error);
bool pp_platform_remove_file(const char *path, PPError *error);
typedef struct PPBlob { uint8_t *data; size_t size; } PPBlob;
/* Owned bytes; import into PPAssets before freeing. Explicit font import uses
 * pp_asset_import(PP_ASSET_FONT) + pp_font_inspect for portable project bytes. */
bool pp_platform_find_font(const char *family, PPBlob *out, PPError *error);
/* Owned list of installed UTF-8 family names, each terminated by NUL. */
bool pp_platform_font_families(PPBlob *out, PPError *error);
void pp_blob_destroy(PPBlob *blob);
typedef struct PPSurface PPSurface;
typedef enum PPPlatformEventKind {
    PP_EVENT_POINTER_MOVE, PP_EVENT_BUTTON_DOWN, PP_EVENT_BUTTON_UP,
    PP_EVENT_WHEEL, PP_EVENT_QUIT, PP_EVENT_CANCEL,
    PP_EVENT_KEY_DOWN, PP_EVENT_KEY_UP, PP_EVENT_TEXT, PP_EVENT_COMPOSITION,
    PP_EVENT_WINDOW_CLOSE, PP_EVENT_WINDOW_RESIZE, PP_EVENT_FOCUS_LOST
} PPPlatformEventKind;
/* UI events use window_id and window_point (logical window coordinates).
 * Sprite events retain the existing surface/desktop coordinate contract. */
typedef enum PPKey {
    PP_KEY_NONE, PP_KEY_TAB, PP_KEY_ENTER, PP_KEY_ESCAPE, PP_KEY_SPACE,
    PP_KEY_LEFT, PP_KEY_RIGHT, PP_KEY_UP, PP_KEY_DOWN, PP_KEY_HOME, PP_KEY_END,
    PP_KEY_PAGE_UP, PP_KEY_PAGE_DOWN, PP_KEY_BACKSPACE, PP_KEY_DELETE,
    PP_KEY_A, PP_KEY_C, PP_KEY_V, PP_KEY_X, PP_KEY_Z, PP_KEY_S, PP_KEY_N, PP_KEY_O,
    PP_KEY_SHIFT, PP_KEY_CONTROL, PP_KEY_ALT
} PPKey;
typedef enum PPButton { PP_BUTTON_NONE, PP_BUTTON_LEFT, PP_BUTTON_RIGHT, PP_BUTTON_MIDDLE } PPButton;
enum { PP_MOD_SHIFT = 1, PP_MOD_CONTROL = 2, PP_MOD_ALT = 4 };
typedef struct PPPlatformEvent {
    PPSurface *surface;
    uint64_t instance_id; /* Zero is an unbound preview; stable IDs belong to the scene. */
    PPPlatformEventKind kind;
    PPVec2 desktop_point;
    PPButton button;
    unsigned modifiers;
    double wheel_delta, timestamp;
    uint32_t window_id, codepoint;
    PPVec2 window_point;
    PPKey key;
    bool repeat, composing;
    /* UI poll payload borrowed until the next poll. TEXT's first scalar has
     * the complete committed payload; subsequent scalars set continuation.
     * Composition ranges count UTF-8 characters, as specified by SDL. */
    const char *text_utf8;
    int composition_start, composition_length;
    bool text_continuation;
} PPPlatformEvent;
typedef enum PPDialogKind { PP_DIALOG_OPEN_PROJECT, PP_DIALOG_SAVE_PROJECT, PP_DIALOG_IMPORT_IMAGE, PP_DIALOG_IMPORT_FONT } PPDialogKind;
/* Result is borrowed only during callback; NULL path/error means cancellation.
 * The native callback copies into an owned queue; poll dispatches on main thread.
 * Cancel detaches callbacks for userdata, including results not yet delivered. */
typedef void (*PPDialogCallback)(void *userdata, const char *utf8_path, const PPError *error);
void pp_platform_file_dialog(PPDialogKind kind, const char *suggested_path, PPDialogCallback callback, void *userdata);
void pp_platform_poll_dialogs(void);
void pp_platform_cancel_file_dialogs(void *userdata);
void pp_platform_dialog_parent(uint32_t window_id);
/* Register the settings window above sprites. Zero unregisters. Native dialogs
 * are owned by this window; neither their client area nor settings is an overlay
 * input target. This does not raise/activate unrelated windows. */
void pp_platform_settings_window(uint32_t window_id);
/* Drawer priority and prior foreground ownership. Outside dismissal never
 * restores focus over the user's new target. No keyboard hook is installed. */
void pp_platform_drawer_window(uint32_t window_id);
void pp_platform_drawer_close(bool restore_focus);
void pp_surface_raise(PPSurface *surface);
bool pp_surface_create(PPSurface **out, PPError *error);
/* Experimental transparent SDL OpenGL surface, using the existing native input
 * ownership adapter. macOS returns an explicit unverified-backend failure. */
bool pp_surface_create_gl(PPSurface **out, PPError *error);
uint32_t pp_surface_window_id(const PPSurface *surface);
/* Place or move the same direct window in native desktop units. interactive
 * must be false for zero opacity. False hides and cancels input ownership.
 * Failure hides the interceptor; renderer must be destroyed before surface. */
bool pp_surface_place_gl(PPSurface *surface, PPRect desktop_bounds,
    bool interactive, PPError *error);
/* Publish staged integer translation without drawing retained content. */
bool pp_surface_publish_gl_placement(PPSurface *surface,PPError *error);
/* Windows native input/visual exclusion. Sorted final rectangles are capped
 * before native publication; NULL restores ordinary rectangle routing. */
bool pp_surface_set_grab_region(PPSurface *surface,const PPGrabRegion *region,PPError *error);
/* Rectangle input policy is independent of visible alpha. Compatibility
 * adapters may retain CPU pixels; direct GL windows already use this policy. */
void pp_surface_set_rectangular(PPSurface *surface,bool enabled);
/* Put a successfully prepared replacement into the previous surface's native
 * order without activation, before destroying the previous surface. */
void pp_surface_inherit_order(PPSurface *replacement,PPSurface *previous);
/* Retained native CPU raster bytes for a validated width/height (<=8192).
 * Presenter reserves this before allocating the candidate native copies. */
size_t pp_surface_raster_bytes(int width, int height);
/* Borrows frame. Bounds are native desktop units; frame dimensions are drawable
 * pixels. The adapter maps the raster to those bounds. Failure destroys the interceptor; caller
 * still releases the PPSurface owner with pp_surface_destroy. */
bool pp_surface_present(PPSurface *surface, const PPFrame *frame,
    PPRect desktop_bounds, PPError *error);
void pp_surface_destroy(PPSurface *surface);
/* Hiding cancels an accepted pending press as well as active capture. */
void pp_surface_hide(PPSurface *surface);
bool pp_platform_displays(PPDisplay **out, size_t *count, PPError *error);
void pp_displays_free(PPDisplay *items);
bool pp_platform_pointer(PPVec2 *out);
void pp_surface_set_instance_id(PPSurface *surface, uint64_t instance_id);
/* Application-owned background drag tracking. Only an accepted left press can
 * acquire it. A press already released during native pumping cannot acquire
 * capture; its queued trajectory still completes normally. Release,
 * cancellation and surface destruction clear ownership. */
bool pp_platform_capture(PPSurface *surface, PPError *error);
void pp_platform_release_capture(void);
/* Cancel only this surface's accepted press, capture and button routing.
 * Does not enqueue a cancellation event; the caller resets its logical
 * gesture. Other surfaces' input ownership is preserved. Main thread only. */
void pp_surface_cancel_input(PPSurface *surface);
/* Pumps native messages on the main thread, then pops one normalized event. */
bool pp_platform_poll(PPPlatformEvent *out);
typedef struct PPApp PPApp;
typedef struct PPTray PPTray;
bool pp_platform_tray_create(PPTray **out, PPApp *app, PPError *error);
void pp_platform_tray_destroy(PPTray *tray);
#endif
