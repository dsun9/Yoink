#include <SDL3/SDL.h>
static bool unavailable;
void pp_test_exposure_context_unavailable(bool value){unavailable=value;}
static bool make_current(SDL_Window *window,SDL_GLContext context){
  return unavailable?false:SDL_GL_MakeCurrent(window,context);
}
/* Inject only an OS/context-binding failure for the ownership regression.
 * Correctness and timing cases execute the real renderer and GL operations. */
#define SDL_GL_MakeCurrent make_current
#include "../../src/render/frame.c"
