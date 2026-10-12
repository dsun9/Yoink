/* Existing context-failure seam around the real retained renderer. */
#define SDL_GL_MakeCurrent pp_test_ui_make_current
#include "../../src/render/frame.c"
