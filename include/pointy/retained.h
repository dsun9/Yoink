#ifndef POINTY_RETAINED_H
#define POINTY_RETAINED_H
#include "pointy/render.h"
/* Per-presenter retained RGBA source textures, including old/candidate overlap.
 * Native compositor backing and GL driver/context memory are OS managed. */
#define PP_RETAINED_GPU_LIMIT ((size_t)256 * 1024 * 1024)
typedef struct PPGpuResources {size_t retained_bytes, peak_bytes, limit_bytes;} PPGpuResources;
#endif
