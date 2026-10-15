#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include "../../src/platform/windows/composition.h"
static bool reject_output,reject_source;
static GLenum allocation_error;
static unsigned source_allocations,output_rejections,candidate_deletions;
static GLuint candidate_source,candidate_output;
static unsigned native_output_rejections;
void pp_test_replacement_reject_output(bool reject) {
  reject_output=reject;reject_source=false;allocation_error=GL_NO_ERROR;
  source_allocations=output_rejections=candidate_deletions=0;
  candidate_source=candidate_output=0;
  native_output_rejections=0;
}
void pp_test_replacement_reject_source(void){pp_test_replacement_reject_output(false);reject_source=true;}
unsigned pp_test_replacement_source_allocations(void){return source_allocations;}
unsigned pp_test_replacement_output_rejections(void){return output_rejections;}
unsigned pp_test_replacement_candidate_deletions(void){return candidate_deletions;}
unsigned pp_test_replacement_expected_deletions(void){return native_output_rejections?1:2;}
bool pp_test_replacement_candidates_released(void){return !glIsTexture(candidate_source)&&!glIsTexture(candidate_output);}
static void APIENTRY allocate(GLenum target,GLint level,GLint internal,GLsizei width,
    GLsizei height,GLint border,GLenum format,GLenum type,const void *pixels) {
  GLint bound=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
  if(reject_source&&width==8&&height==4&&pixels) {
    candidate_source=(GLuint)bound;allocation_error=GL_OUT_OF_MEMORY;return;
  }
  if(reject_output&&width==145&&height==73&&!pixels) {
    candidate_output=(GLuint)bound;output_rejections++;allocation_error=GL_OUT_OF_MEMORY;return;
  }
  glTexImage2D(target,level,internal,width,height,border,format,type,pixels);
  if(reject_output&&width==8&&height==4&&pixels) {
    GLint allocated_width=0,allocated_height=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&allocated_width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&allocated_height);
    candidate_source=(GLuint)bound;source_allocations++;
    if(allocated_width!=width||allocated_height!=height)source_allocations=0;
  }
}
static GLenum APIENTRY get_error(void) {
  if(allocation_error){GLenum result=allocation_error;allocation_error=GL_NO_ERROR;return result;}
  return glGetError();
}
static void APIENTRY delete_textures(GLsizei count,const GLuint *textures) {
  for(GLsizei i=0;i<count;i++)if(textures[i]&&(textures[i]==candidate_source||textures[i]==candidate_output))candidate_deletions++;
  glDeleteTextures(count,textures);
}
static bool prepare_output(PPComposition *composition,int width,int height,PPError *error) {
  if(reject_output&&pp_composition_active(composition)&&width==145&&height==73) {
    output_rejections++;native_output_rejections++;
    if(error){error->code=PP_ERROR_MEMORY;snprintf(error->message,sizeof(error->message),"Injected composition output allocation failure");}
    return false;
  }
  return pp_composition_prepare(composition,width,height,error);
}
/* Fail only the selected source/output allocation. Budget reservations,
 * contexts, drawing, publication and capture use production code. */
#define glTexImage2D allocate
#define glGetError get_error
#define glDeleteTextures delete_textures
#define pp_composition_prepare prepare_output
#include "../../src/render/frame.c"
