#include "../../src/ui/preview.h"
#include "../../src/ui/backend.h"
#include "../test.h"
#include <math.h>
#include <string.h>
static PPApp *app;
static PPUIBackend *ui;
static PPUIBackend *other_ui;
static PPItemSnapshot *snapshot;
static PPUIContentPreview preview;
static int restore_failure_countdown,restore_failures;
bool SDLCALL pp_test_ui_make_current(SDL_Window *window,SDL_GLContext context) {
  if(restore_failure_countdown&&window==ui->window&&context==ui->gl_context&&--restore_failure_countdown==0) {
    restore_failures++;SDL_SetError("Injected final UI context restoration failure");return false;
  }
  return SDL_GL_MakeCurrent(window,context);
}
static void cleanup(void) {
  pp_ui_preview_release(&preview);pp_item_snapshot_release(snapshot);snapshot=NULL;
  pp_ui_backend_destroy(other_ui);other_ui=NULL;
  pp_ui_backend_destroy(ui);ui=NULL;pp_app_destroy(app);app=NULL;
}
#undef CHECK
#define CHECK(expr) do {if(!(expr)){fprintf(stderr,"%s:%d: CHECK(%s) failed\n",__FILE__,__LINE__,#expr);cleanup();exit(1);}}while(0)
static void assert_ui_state(GLuint texture) {
  GLint value=0,viewport[4]={0};
  CHECK(SDL_GL_GetCurrentWindow()==ui->window&&SDL_GL_GetCurrentContext()==ui->gl_context);
  glGetIntegerv(GL_CURRENT_PROGRAM,&value);CHECK(value==(GLint)ui->program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&value);CHECK(value==(GLint)ui->vao);
  glGetIntegerv(GL_VIEWPORT,viewport);CHECK(viewport[0]==3&&viewport[1]==5&&viewport[2]==77&&viewport[3]==44);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&value);CHECK(value==GL_TEXTURE3);
  glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);CHECK(value==(GLint)texture);
  glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);CHECK(value==8);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);CHECK(value==7);
  CHECK(glIsEnabled(GL_BLEND)&&glIsEnabled(GL_SCISSOR_TEST)&&glIsEnabled(GL_CULL_FACE)&&glIsEnabled(GL_DEPTH_TEST)&&glIsEnabled(GL_DITHER));
  CHECK(glGetError()==GL_NO_ERROR);
}
static double sample_alpha(const uint8_t *pixels,int width,int height,double u,double v) {
  double x=u*width-.5,y=v*height-.5;int left=(int)floor(x),top=(int)floor(y);
  double value=0;
  for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {
    int sx=left+dx,sy=top+dy;if(sx<0)sx=0;if(sx>=width)sx=width-1;if(sy<0)sy=0;if(sy>=height)sy=height-1;
    double weight=(dx?x-floor(x):1-(x-floor(x)))*(dy?y-floor(y):1-(y-floor(y)));
    value+=pixels[((size_t)sy*width+sx)*4+3]*weight;
  }
  return value;
}
static void test_restore_failure_preserves_usable_output(void) {
  PPError error={0};CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  PPUIWindowOptions options={.title="Preview rollback",.geometry={0,0,160,120},.scale=1,.compact=true};
  CHECK(pp_ui_backend_create(&ui,&options,&error));
  size_t bytes=0;void *png=SDL_LoadFile(PP_MEDIA_FIXTURES "/alpha.png",&bytes);CHECK(png);
  PPItem item={.id=1,.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){png,bytes},PP_ASSET_PNG,&item.image_asset,&error));SDL_free(png);
  CHECK(pp_item_snapshot_create(pp_app_assets(app),&item,&snapshot,&error));double next;
  CHECK(pp_ui_preview_update(ui,app,&preview,snapshot,96,0,1,1,0,&next,&error));
  CHECK(preview.width==96&&preview.height==32);
  GLuint independent=0;glGenTextures(1,&independent);ui->gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,independent);
  ui->gl.ActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,independent);ui->gl.UseProgram(ui->program);ui->gl.BindVertexArray(ui->vao);
  glViewport(3,5,77,44);glEnable(GL_BLEND);glEnable(GL_SCISSOR_TEST);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glEnable(GL_DITHER);
  glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,7);
  CHECK(pp_ui_preview_update(ui,app,&preview,snapshot,96,0,.5,1,0,&next,&error));assert_ui_state(independent);
  unsigned usable=preview.texture;size_t accepted=pp_content_gpu_resources(pp_app_content_sources(app,&error)).retained_bytes;
  CHECK(accepted==12300);
  restore_failure_countdown=2;
  CHECK(!pp_ui_preview_update(ui,app,&preview,snapshot,144,0,1,1,0,&next,&error));
  CHECK(restore_failures==1&&error.code==PP_ERROR_RENDER);
  CHECK(preview.texture==usable&&preview.width==96&&preview.height==32);
  assert_ui_state(independent);
  CHECK(pp_content_gpu_resources(pp_app_content_sources(app,&error)).retained_bytes==accepted);
  CHECK(SDL_GL_MakeCurrent(ui->window,ui->gl_context));CHECK(glIsTexture(usable));
  CHECK(pp_ui_backend_create(&other_ui,&options,&error));CHECK(SDL_GL_GetCurrentContext()==other_ui->gl_context);
  CHECK(!pp_ui_preview_update(ui,app,&preview,snapshot,144,0,1,1,0,&next,&error));CHECK(error.code==PP_ERROR_ARGUMENT);
  CHECK(SDL_GL_GetCurrentContext()==other_ui->gl_context&&preview.texture==usable);
  CHECK(pp_content_gpu_resources(pp_app_content_sources(app,&error)).retained_bytes==accepted);
  CHECK(SDL_GL_MakeCurrent(ui->window,ui->gl_context));glDeleteTextures(1,&independent);
  CHECK(pp_renderer_metrics(preview.renderer).readbacks==0);
  pp_ui_preview_release(&preview);CHECK(!pp_content_gpu_resources(pp_app_content_sources(app,&error)).retained_bytes);
  cleanup();puts("PASS failed final UI context restore preserves the usable output and exact old GPU charge");
}
int main(void) {
  test_restore_failure_preserves_usable_output();
  PPError error={0};CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  PPUIWindowOptions options={.title="Preview quality",.geometry={0,0,96,96},.scale=1.25f,.compact=true};
  CHECK(pp_ui_backend_create(&ui,&options,&error));
  const char *svg="<svg width='13.5' height='7.5'><rect width='6.75' height='7.5' fill='red' opacity='.5'/></svg>";
  PPItem item={.id=1,.kind=PP_ITEM_IMAGE,.image_longest_dip=13.5,.opacity=1};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
  CHECK(pp_item_snapshot_create(pp_app_assets(app),&item,&snapshot,&error));
  double next;CHECK(pp_ui_preview_update(ui,app,&preview,snapshot,13.5,13.75,1,1.25,0,&next,&error));
  CHECK(preview.texture&&preview.width>0&&preview.height>0);
  CHECK(pp_renderer_metrics(preview.renderer).readbacks==0);
  size_t bytes=(size_t)preview.width*preview.height*4;
  uint8_t *texture=malloc(bytes);CHECK(texture);
  CHECK(SDL_GL_MakeCurrent(ui->window,ui->gl_context));glBindTexture(GL_TEXTURE_2D,preview.texture);
  glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,texture);CHECK(glGetError()==GL_NO_ERROR);
  CHECK(pp_ui_begin(ui,&error));
  CHECK(nk_begin(&ui->context,"quality",nk_rect(0,0,ui->width,ui->height),NK_WINDOW_NO_SCROLLBAR));
  struct nk_command_buffer *canvas=nk_window_get_canvas(&ui->context);
  nk_fill_rect(canvas,nk_rect(16,16,64,64),0,nk_rgba(0,0,0,255));
  struct nk_rect requested=nk_rect(25.25f,25.5f,(float)(preview.width/1.25),(float)(preview.height/1.25));
  /* Nuklear stores image commands in integer logical units. At this window's
   * 1.25 scale, the actual 25 logical origin is 31.25 drawable pixels. */
  struct nk_rect bounds=nk_rect((short)requested.x,(short)requested.y,
      (unsigned short)requested.w,(unsigned short)requested.h);
  struct nk_image image=nk_image_id((int)preview.texture);
  nk_draw_image(canvas,requested,&image,nk_rgba(255,255,255,255));nk_end(&ui->context);
  ui->capture_path=PP_PREVIEW_OUTPUT "/ui-preview-edge.bmp";CHECK(pp_ui_end(ui,&error));
  SDL_Surface *proof=SDL_LoadBMP(PP_PREVIEW_OUTPUT "/ui-preview-edge.bmp");CHECK(proof);
  SDL_Surface *rgba=SDL_ConvertSurface(proof,SDL_PIXELFORMAT_RGBA32);CHECK(rgba);
  double sx=rgba->w/ui->width,sy=rgba->h/ui->height;size_t samples=0;double worst=0;
  for(int y=0;y<rgba->h;y++)for(int x=0;x<rgba->w;x++) {
    double lx=(x+.5)/sx,ly=(y+.5)/sy;
    if(lx<bounds.x||lx>=bounds.x+bounds.w||ly<bounds.y||ly>=bounds.y+bounds.h)continue;
    double alpha=sample_alpha(texture,preview.width,preview.height,(lx-bounds.x)/bounds.w,(ly-bounds.y)/bounds.h);
    if(alpha<2||alpha>120)continue;
    const uint8_t *pixel=(const uint8_t *)rgba->pixels+(size_t)y*rgba->pitch+x*4;
    double difference=fabs(pixel[0]-alpha);if(difference>worst)worst=difference;samples++;
  }
  printf("Fractional translucent red samples=%zu maximum-color-error=%.3f\n",samples,worst);fflush(stdout);
  SDL_DestroySurface(rgba);SDL_DestroySurface(proof);free(texture);
  CHECK(samples>=4);CHECK(worst<=2);
  CHECK(!ui->visible&&(SDL_GetWindowFlags(ui->window)&SDL_WINDOW_HIDDEN));
  cleanup();puts("PASS fractional density/placement preserves translucent source color across actual Nuklear linear sampling");return 0;
}
