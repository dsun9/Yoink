#ifndef POINTY_PROJECT_STORE_H
#define POINTY_PROJECT_STORE_H
#include "pointy/model.h"
/* Loads a complete separate candidate. The caller swaps it into the app only on
 * success; *out is NULL on failure. assets must outlive the returned project.
 * Storage and asset mutation are synchronous on one thread: staging accounts
 * for the app's existing raster retention during validation. Late allocation
 * failure during transfer may leave unretained source imports in the manager,
 * owned until manager destruction; no active project or scene is modified. */
bool pp_project_load(const char *utf8_path, PPAssets *assets, PPProject **out, PPError *error);
bool pp_project_save(const PPProject *project, const char *utf8_path, PPError *error);
/* Test-only failure injection, borrowed for this call. Returning false at a
 * boundary aborts the operation; normal calls use native I/O at every boundary. */
typedef struct PPProjectStoreHooks {
    void *context;
    bool (*before_write)(void *, PPError *);
    bool (*before_flush)(void *, PPError *);
    bool (*before_replace)(void *, PPError *);
} PPProjectStoreHooks;
bool pp_project_save_with_hooks(const PPProject *project, const char *utf8_path,
                                const PPProjectStoreHooks *hooks, PPError *error);
#endif
