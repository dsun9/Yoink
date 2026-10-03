/* SDL GL loading/shader setup adapted from BongoCat src/render/gl_api.c,
 * copyright its contributors, AGPL-3.0-or-later (repository LICENSE). */
#include "gl_api.h"
#include <stdio.h>
#include <string.h>
bool pp_gl_load(PPGL *gl, PPError *error) {
    memset(gl, 0, sizeof(*gl));
#define LOAD(name, type) gl->name = (type)SDL_GL_GetProcAddress("gl" #name); \
    if (!gl->name) { if (error) { error->code = PP_ERROR_RENDER; snprintf(error->message, sizeof(error->message), "Missing OpenGL function gl%s", #name); } return false; }
    PP_GL_FUNCTIONS(LOAD)
#undef LOAD
    return true;
}
static GLuint compile(PPGL *gl, GLenum kind, const char *source, PPError *error) {
    GLuint shader = gl->CreateShader(kind); gl->ShaderSource(shader, 1, &source, NULL); gl->CompileShader(shader);
    GLint ok; gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    if (error) { error->code = PP_ERROR_RENDER; gl->GetShaderInfoLog(shader, sizeof(error->message), NULL, error->message); }
    gl->DeleteShader(shader); return 0;
}
GLuint pp_gl_program(PPGL *gl, const char *vertex, const char *fragment, PPError *error) {
    GLuint vs = compile(gl, GL_VERTEX_SHADER, vertex, error); if (!vs) return 0;
    GLuint fs = compile(gl, GL_FRAGMENT_SHADER, fragment, error);
    if (!fs) { gl->DeleteShader(vs); return 0; }
    GLuint program = gl->CreateProgram(); gl->AttachShader(program, vs); gl->AttachShader(program, fs);
    gl->BindFragDataLocation(program, 0, "color"); gl->BindFragDataLocation(program, 1, "coverage");
    gl->LinkProgram(program); gl->DeleteShader(vs); gl->DeleteShader(fs);
    GLint ok; gl->GetProgramiv(program, GL_LINK_STATUS, &ok); if (ok) return program;
    if (error) { error->code = PP_ERROR_RENDER; gl->GetProgramInfoLog(program, sizeof(error->message), NULL, error->message); }
    gl->DeleteProgram(program); return 0;
}
