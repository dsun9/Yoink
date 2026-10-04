/* Configuration adapted from BongoCat src/ui/backend/nuklear_config.h.
 * BongoCat contributors, AGPL-3.0-or-later; see docs/third-party-notices.md. */
#ifndef POINTY_NUKLEAR_CONFIG_H
#define POINTY_NUKLEAR_CONFIG_H
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
/* Keep a complete down/up pair actionable when both arrive in one frame. */
#define NK_BUTTON_TRIGGER_ON_RELEASE
#define NK_INPUT_MAX 4096
#ifdef _MSC_VER
#pragma warning(push)
/* Pinned Nuklear flag enums and C alignment macro. */
#pragma warning(disable : 5287 4116)
#endif
#include <nuklear.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
