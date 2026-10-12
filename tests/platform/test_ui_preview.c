#include "../../src/ui/preview.h"
#include "../../src/ui/backend.h"
#include "../test.h"
#include <math.h>
#include <string.h>
static PPApp *app;
static PPUIBackend *ui;
static PPUIBackend *other_ui;
static PPItemSnapshot *snapshot;
static PPItemSnapshot *bitmap_snapshot;
static PPUIContentPreview preview;
static PPUIContentPreview fresh_preview;
static int restore_failure_countdown,restore_failures;
bool SDLCALL pp_test_ui_make_current(SDL_Window *window,SDL_GLContext context) {
  if(restore_failure_countdown&&window==ui->window&&context==ui->gl_context&&--restore_failure_countdown==0) {
    restore_failures++;SDL_SetError("Injected final UI context restoration failure");return false;
  }
  return SDL_GL_MakeCurrent(window,context);
}
static void cleanup(void) {
  pp_ui_preview_release(&fresh_preview);pp_ui_preview_release(&preview);
  pp_item_snapshot_release(bitmap_snapshot);bitmap_snapshot=NULL;
  pp_item_snapshot_release(snapshot);snapshot=NULL;
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
static PPItemSnapshot *png_snapshot(const char *path,uint64_t id,double longest) {
  PPError error={0};size_t bytes=0;void *png=SDL_LoadFile(path,&bytes);CHECK(png);
  PPItem item={.id=id,.kind=PP_ITEM_IMAGE,.image_longest_dip=longest,.opacity=1};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){png,bytes},PP_ASSET_PNG,&item.image_asset,&error));SDL_free(png);
  PPItemSnapshot *result=NULL;CHECK(pp_item_snapshot_create(pp_app_assets(app),&item,&result,&error));return result;
}
static void test_budget_rollback_and_selection(void) {
  PPError error={0};CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  PPUIWindowOptions options={.title="Preview admission",.geometry={0,0,160,120},.scale=1,.compact=true};
  CHECK(pp_ui_backend_create(&ui,&options,&error));double next;
  snapshot=png_snapshot(PP_MEDIA_FIXTURES "/alpha.png",1,96);
  CHECK(pp_ui_preview_update(ui,app,&preview,snapshot,96,0,1,1,0,&next,&error));
  PPContentSources *sources=pp_app_content_sources(app,&error);
  unsigned usable=preview.texture;CHECK(preview.width==96&&preview.height==32);
  CHECK(pp_content_gpu_resources(sources).retained_bytes==12300);
  /* Admission pressure has no external allocation: the ledger is the agreed
   * resource seam. A same-item replacement must keep its last usable pixels. */
  size_t pressure=pp_content_gpu_resources(sources).limit_bytes-12300;
  CHECK(pp_content_gpu_reserve(sources,pressure,&error));
  PPItemSnapshot *changed=png_snapshot(PP_MEDIA_FIXTURES "/retained-blue.png",1,144);
  CHECK(!pp_ui_preview_update(ui,app,&preview,changed,144,0,1,1,0,&next,&error));
  CHECK(error.code==PP_ERROR_RESOURCE_LIMIT&&preview.texture==usable&&glIsTexture(usable));
  CHECK(preview.width==96&&preview.height==32&&preview.snapshot==snapshot);
  CHECK(pp_content_gpu_resources(sources).retained_bytes==pressure+12300);
  uint8_t pixels[96*32*4];glBindTexture(GL_TEXTURE_2D,usable);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  CHECK(glGetError()==GL_NO_ERROR);size_t red=0;for(size_t i=0;i<96*32;i++)red+=pixels[i*4]>pixels[i*4+2];CHECK(red>0);
  pp_item_snapshot_release(changed);
  changed=png_snapshot(PP_MEDIA_FIXTURES "/retained-blue.png",2,144);
  CHECK(!pp_ui_preview_update(ui,app,&preview,changed,144,0,1,1,0,&next,&error));
  CHECK(error.code==PP_ERROR_RESOURCE_LIMIT&&!preview.texture&&!preview.width&&!preview.height&&!preview.snapshot);
  CHECK(!glIsTexture(usable));CHECK(pp_content_gpu_resources(sources).retained_bytes==pressure);
  pp_content_gpu_release(sources,pressure);
  CHECK(pp_ui_preview_update(ui,app,&preview,changed,144,0,1,1,0,&next,&error));
  CHECK(preview.texture&&preview.snapshot==changed);
  PPRendererMetrics before=pp_renderer_metrics(preview.renderer);
  CHECK(pp_ui_preview_update(ui,app,&preview,changed,144,0,1,1,0,&next,&error));
  PPRendererMetrics after=pp_renderer_metrics(preview.renderer);
  CHECK(before.source_uploads==after.source_uploads&&before.transform_draws==after.transform_draws&&after.readbacks==0);
  pp_item_snapshot_release(changed);pp_ui_preview_release(&preview);CHECK(!pp_content_gpu_resources(sources).retained_bytes);
  cleanup();puts("PASS rejected same-item replacement keeps usable pixels; rejected new selection clears stale pixels; retry and unchanged reuse release exact GPU charges");
}
static void test_new_bitmap_preview_recovers_after_gpu_rejection(void) {
  for(int preserve_old=0;preserve_old<2;preserve_old++) {
    PPError error={0};CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
    PPUIWindowOptions options={.title="Bitmap preview recovery",.geometry={0,0,160,120},.scale=1,.compact=true};
    CHECK(pp_ui_backend_create(&ui,&options,&error));double next;
    PPContentSources *sources=pp_app_content_sources(app,&error);CHECK(sources);
    size_t baseline=pp_assets_external_raster_bytes(pp_app_assets(app));
    GLuint usable=0;uint8_t old_pixels[96*32*4];
    if(preserve_old) {
      snapshot=png_snapshot(PP_MEDIA_FIXTURES "/alpha.png",1,96);
      CHECK(pp_ui_preview_update(ui,app,&preview,snapshot,96,0,1,1,0,&next,&error));
      usable=preview.texture;glBindTexture(GL_TEXTURE_2D,usable);
      glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,old_pixels);CHECK(glGetError()==GL_NO_ERROR);
    }
    size_t old_charge=pp_content_gpu_resources(sources).retained_bytes;
    size_t length=0;void *font=SDL_LoadFile(PP_MEDIA_FIXTURES "/../../../resources/fonts/NotoColorEmoji.ttf",&length);CHECK(font);
    PPItem item={.id=1,.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,
      .text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){font,length},PP_ASSET_FONT,&item.text_style.fallback_asset,&error));SDL_free(font);
    CHECK(pp_item_snapshot_create(pp_app_assets(app),&item,&bitmap_snapshot,&error));
    CHECK(pp_item_snapshot_text_result(bitmap_snapshot)->artwork.bitmap_only);
    size_t pressure=pp_content_gpu_resources(sources).limit_bytes-old_charge;
    CHECK(pp_content_gpu_reserve(sources,pressure,&error));
    CHECK(!pp_ui_preview_update(ui,app,&preview,bitmap_snapshot,96,0,1,1,0,&next,&error));
    CHECK(error.code==PP_ERROR_RESOURCE_LIMIT&&preview.texture==usable);
    CHECK(pp_content_gpu_resources(sources).retained_bytes==old_charge+pressure);
    pp_content_gpu_release(sources,pressure);error=(PPError){0};
    size_t retained=pp_assets_external_raster_bytes(pp_app_assets(app));
    CHECK(pp_ui_preview_update(ui,app,&fresh_preview,bitmap_snapshot,96,0,1,1,0,&next,&error));
    CHECK(fresh_preview.valid&&fresh_preview.texture&&fresh_preview.snapshot==bitmap_snapshot);
    CHECK(fresh_preview.source_width==136&&fresh_preview.source_height==128);
    size_t output_bytes=(size_t)fresh_preview.width*fresh_preview.height*4;
    uint8_t *pixels=malloc(output_bytes);CHECK(pixels);glBindTexture(GL_TEXTURE_2D,fresh_preview.texture);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
    size_t visible=0;for(size_t i=0;i<output_bytes;i+=4)visible+=pixels[i+3]>0;free(pixels);CHECK(visible>0);
    size_t recovered_charge=pp_content_gpu_resources(sources).retained_bytes;
    CHECK(recovered_charge==old_charge+69632+output_bytes);
    CHECK(pp_assets_external_raster_bytes(pp_app_assets(app))==retained&&pp_content_sources_raster_bytes(sources)==0);
    for(int tick=0;tick<3;tick++) {
      CHECK(pp_ui_preview_update(ui,app,&preview,bitmap_snapshot,96,0,1,1,0,&next,&error));
      CHECK(preview.texture==usable&&pp_content_gpu_resources(sources).retained_bytes==recovered_charge);
    }
    if(preserve_old) {
      uint8_t pixels_after[sizeof(old_pixels)];glBindTexture(GL_TEXTURE_2D,usable);
      glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels_after);CHECK(glGetError()==GL_NO_ERROR);
      CHECK(preview.snapshot==snapshot&&preview.width==96&&preview.height==32&&!memcmp(old_pixels,pixels_after,sizeof(old_pixels)));
    }
    pp_ui_preview_release(&preview);CHECK(pp_content_gpu_resources(sources).retained_bytes==recovered_charge-old_charge);
    CHECK(pp_ui_preview_update(ui,app,&fresh_preview,bitmap_snapshot,96,0,1,1,0,&next,&error));
    CHECK(fresh_preview.valid&&fresh_preview.texture&&pp_renderer_metrics(fresh_preview.renderer).readbacks==0);
    pp_ui_preview_release(&fresh_preview);CHECK(!pp_content_gpu_resources(sources).retained_bytes);
    pp_item_snapshot_release(bitmap_snapshot);bitmap_snapshot=NULL;pp_item_snapshot_release(snapshot);snapshot=NULL;
    CHECK(pp_assets_external_raster_bytes(pp_app_assets(app))==baseline);
    cleanup();
  }
  puts("PASS a new bitmap preview displays pixels after another preview's GPU admission failure, preserving old artwork and releasing exact CPU/GPU charges");
}
int main(void) {
  test_new_bitmap_preview_recovers_after_gpu_rejection();
  test_budget_rollback_and_selection();
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
