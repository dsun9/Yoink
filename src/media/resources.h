#ifndef POINTY_RESOURCES_H
#define POINTY_RESOURCES_H
#include <stddef.h>
/* Fixed bundled paths, relative to the executable (SDL uses Resources on Mac).
 * Returned data uses SDL_free. No working-directory or source-tree fallback. */
void *pp_resource_load(const char *relative, size_t *size);
#endif
