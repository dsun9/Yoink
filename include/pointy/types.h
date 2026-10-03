#ifndef POINTY_TYPES_H
#define POINTY_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct PPVec2 { double x, y; } PPVec2;
typedef struct PPRect { double x, y, w, h; } PPRect;
typedef struct PPError { int code; char message[512]; } PPError;
enum { PP_ERROR_ARGUMENT = 1, PP_ERROR_MEMORY, PP_ERROR_PLATFORM, PP_ERROR_RENDER };
#endif
