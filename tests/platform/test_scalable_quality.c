/* Real GL diagnostic export comparison. The fresh reference is independently
 * sampled in canonical coordinates; no desktop input injection is needed. */
#include "../../src/app/presenter_internal.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>
static PPError error;
static PPAssetId font(PPAssets *assets,const char *path) {
  size_t size=0;void *bytes=SDL_LoadFile(path,&size);CHECK(bytes&&size);PPAssetId id={0};
  CHECK(pp_asset_import(assets,(PPBytes){bytes,size},PP_ASSET_FONT,&id,&error));SDL_free(bytes);return id;
}
static int clamp(int value,int limit) {return value<0?0:value>=limit?limit-1:value;}
static double channel(const PPFrame *source,double x,double y,int c) {
  int ix=(int)floor(x),iy=(int)floor(y);double fx=x-ix,fy=y-iy;
  int x0=clamp(ix,source->width),x1=clamp(ix+1,source->width),y0=clamp(iy,source->height),y1=clamp(iy+1,source->height);
  double top=source->pixels[(size_t)y0*source->stride+x0*4+c]*(1-fx)+source->pixels[(size_t)y0*source->stride+x1*4+c]*fx;
  double bottom=source->pixels[(size_t)y1*source->stride+x0*4+c]*(1-fx)+source->pixels[(size_t)y1*source->stride+x1*4+c]*fx;
  return top*(1-fy)+bottom*fy;
}
int main(void) {
  PPAssets *assets=NULL;PPRenderer *renderer=NULL;CHECK(pp_assets_create(&assets,&error));CHECK(pp_renderer_create(&renderer,&error));
  PPAssetId overhang=font(assets,PP_FONT_FIXTURES "/PointyOverhang.ttf"),hybrid=font(assets,PP_FONT_FIXTURES "/PointyHybrid.ttf");
  PPAssetId twemoji=font(assets,PP_RESOURCES "/optional-fonts/Twemoji.Mozilla-v0.7.0.ttf"),noto=font(assets,PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
  for(int kind=0;kind<6;kind++) {
    PPItem item={.kind=PP_ITEM_TEXT,.text="A\xcc\x81j\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\nA",.opacity=1,
      .text_style={.requested_family="Pointy Overhang",.primary_asset=overhang,.fallback_asset=noto,.font_size_dip=96,.color={30,120,230,210}}};
    if(kind==0) {item.text="\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd\n\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";item.text_style.primary_asset=twemoji;item.text_style.fallback_asset=(PPAssetId){0};item.text_style.requested_family="Twemoji Mozilla";}
    if(kind==2) {item.text="AB\nA";item.text_style.primary_asset=hybrid;item.text_style.fallback_asset=(PPAssetId){0};item.text_style.font_size_dip=16;item.text_style.requested_family="Pointy Hybrid";}
    if(kind>=3) {
      const char *svg=kind==3?
        "<svg width='13' height='7' viewBox='0 0 13 7'><rect x='2' y='1' width='9' height='5' fill='#ef6f21' fill-opacity='.5'/><path d='M3 5L6 2L10 5' fill='none' stroke='#286ec8' stroke-width='.3'/></svg>":
        kind==4?"<svg width='96' height='64' viewBox='10 20 96 64'><defs><linearGradient id='g'><stop offset='0' stop-color='#ef6f21'/><stop offset='1' stop-color='#286ec8'/></linearGradient></defs><rect x='14' y='24' width='88' height='56' rx='12' fill='url(#g)'/><path d='M26 52L78 52M62 36L78 52L62 68' fill='none' stroke='white' stroke-width='6'/></svg>":
        "<svg width='13.5' height='7.5'><rect x='2' y='1' width='9' height='5' fill='#ef6f21' fill-opacity='.5'/><path d='M3 5L6 2L10 5' fill='none' stroke='#286ec8' stroke-width='.3'/></svg>";
      item=(PPItem){.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
      CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
    }
    PPItemSnapshot *snapshot=NULL;CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error));PPVec2 layout=pp_item_snapshot_size(snapshot);
    PPAnchorPresenter presenter={.assets=assets,.generation=1};PPSprite sprite={.id=1};PPSceneInstance instance={.snapshot=snapshot};PPDisplay display={.drawable_pixels_per_dip=1.5};
    CHECK(pp_scalable_request(&presenter,&sprite,&instance,&display,384,&error));
    for(int i=0;i<5000&&pp_raster_worker_status(presenter.worker).pending_jobs;i++)SDL_Delay(1);
    PPRasterCompletion completion={0};CHECK(pp_raster_worker_poll(presenter.worker,1,&completion));CHECK(completion.success);
    PPTextResult fresh={0};
    if(kind>=3) {
      const PPFrame *frame=NULL;double next;CHECK(pp_item_snapshot_frame(snapshot,0,384*1.5/fmax(layout.x,layout.y),&frame,&next,&error));
      fresh=(PPTextResult){.frame=*frame,.layout_size=layout,.raster_density=384*1.5/fmax(layout.x,layout.y)};
      fresh.frame.pixels=malloc(frame->stride*frame->height);fresh.frame.content_alpha=malloc((size_t)frame->width*frame->height);
      CHECK(fresh.frame.pixels&&fresh.frame.content_alpha);
      memcpy(fresh.frame.pixels,frame->pixels,frame->stride*frame->height);memcpy(fresh.frame.content_alpha,frame->content_alpha,(size_t)frame->width*frame->height);
    } else CHECK(pp_text_render_result(assets,item.text,&item.text_style,384*1.5/fmax(layout.x,layout.y),&fresh,NULL,&error));
    CHECK(completion.result.frame.width==fresh.frame.width&&completion.result.frame.height==fresh.frame.height);
    CHECK(!memcmp(completion.result.frame.pixels,fresh.frame.pixels,fresh.frame.stride*fresh.frame.height));
    PPRect rect={fresh.layout_origin.x-fresh.raster_origin_px.x/fresh.raster_density,fresh.layout_origin.y-fresh.raster_origin_px.y/fresh.raster_density,
      fresh.frame.width/fresh.raster_density,fresh.frame.height/fresh.raster_density};
    if(kind>=3)rect=(PPRect){0,0,layout.x,layout.y};
    for(int angle=0;angle<2;angle++) {
      double degrees=angle?17.25:0,radians=degrees*3.14159265358979323846/180,c=cos(radians),s=sin(radians),scale=384*1.5/fmax(layout.x,layout.y),opacity=.63;
      PPFrame output={0};CHECK(pp_renderer_transform_source(renderer,&completion.result.frame,layout,rect,384,degrees,opacity,1.5,&output,&error));
      PPFrame reference={0};CHECK(pp_renderer_transform_source(renderer,&fresh.frame,layout,rect,384,degrees,opacity,1.5,&reference,&error));
      CHECK(output.width==reference.width&&output.height==reference.height);
      CHECK(!memcmp(output.pixels,reference.pixels,output.stride*output.height));
      CHECK(!memcmp(output.content_alpha,reference.content_alpha,(size_t)output.width*output.height));pp_frame_destroy(&reference);
      double right=fmax(layout.x,rect.x+rect.w)-layout.x*.5,left=fmin(0,rect.x)-layout.x*.5;
      double bottom=fmax(layout.y,rect.y+rect.h)-layout.y*.5,top=fmin(0,rect.y)-layout.y*.5;
      double extent_x=fmax(fmax(fabs(c*left-s*top),fabs(c*left-s*bottom)),fmax(fabs(c*right-s*top),fabs(c*right-s*bottom)));
      double extent_y=fmax(fmax(fabs(s*left+c*top),fabs(s*left+c*bottom)),fmax(fabs(s*right+c*top),fabs(s*right+c*bottom)));
      CHECK(output.width==(int)ceil(extent_x*2*scale-1e-9)&&output.height==(int)ceil(extent_y*2*scale-1e-9));
      unsigned max_error=0,raw_max_error=0;size_t edges=0,visible=0,numerical_edges=0;int worst_x=0,worst_y=0,worst_actual=0,worst_expected=0,worst_channel=0;double worst_u=0,worst_v=0;
      for(int y=0;y<output.height;y++)for(int x=0;x<output.width;x++) {
        double dx=x+.5-output.width*.5,dy=y+.5-output.height*.5;
        double local_x=(c*dx+s*dy)/scale+layout.x*.5,local_y=(-s*dx+c*dy)/scale+layout.y*.5;
        double u=(local_x-rect.x)/rect.w,v=(local_y-rect.y)/rect.h;
        /* A double reference cannot choose the GLSL float branch at an exact
         * source boundary. The measured failing v was -1.154e-8, rounded to
         * zero by GL. Within two float ulps at 1.0, require either transparent
         * or the independently sampled boundary color. All other pixels keep
         * the two-channel-unit comparison, including partial-alpha edges. */
        const double envelope=2.0/8388608.0;
        bool numerical_edge=u>=-envelope&&u<=1+envelope&&v>=-envelope&&v<=1+envelope&&
          (fabs(u)<=envelope||fabs(u-1)<=envelope||fabs(v)<=envelope||fabs(v-1)<=envelope);
        numerical_edges+=numerical_edge;
        size_t index=(size_t)y*output.width+x;const uint8_t *pixel=output.pixels+index*4;
        unsigned pixel_difference=0,boundary_difference=0,transparent_difference=0;
        for(int ch=0;ch<4;ch++) {
          int expected=(u<0||v<0||u>1||v>1)?0:(int)lround(channel(&fresh.frame,u*fresh.frame.width-.5,v*fresh.frame.height-.5,ch)*opacity);
          unsigned difference=(unsigned)abs((int)pixel[ch]-expected);if(difference>raw_max_error) {raw_max_error=difference;worst_x=x;worst_y=y;worst_actual=pixel[ch];worst_expected=expected;worst_channel=ch;worst_u=u;worst_v=v;}
          pixel_difference=SDL_max(pixel_difference,difference);
          if(numerical_edge) {
            int boundary=(int)lround(channel(&fresh.frame,fmin(1,fmax(0,u))*fresh.frame.width-.5,fmin(1,fmax(0,v))*fresh.frame.height-.5,ch)*opacity);
            boundary_difference=SDL_max(boundary_difference,(unsigned)abs((int)pixel[ch]-boundary));
            transparent_difference=SDL_max(transparent_difference,(unsigned)pixel[ch]);
          }
        }
        if(numerical_edge)pixel_difference=SDL_min(pixel_difference,SDL_min(boundary_difference,transparent_difference));
        if(pixel_difference>max_error)max_error=pixel_difference;
        visible+=pixel[3]>0;edges+=pixel[3]>0&&pixel[3]<132;CHECK(pixel[0]<=pixel[3]&&pixel[1]<=pixel[3]&&pixel[2]<=pixel[3]);
      }
      printf("QUALITY kind=%d angle=%.2f max_channel_error=%u raw_max_error=%u numerical_boundary_pixels=%zu visible=%zu edges=%zu raster=%dx%d canonical=%.0fx%.0f\n",kind,degrees,max_error,raw_max_error,numerical_edges,visible,edges,output.width,output.height,layout.x,layout.y);
      if(raw_max_error>2)printf("QUALITY difference x=%d y=%d channel=%d actual=%d expected=%d u=%.17g v=%.17g source=%dx%d rect=%.17g,%.17g,%.17g,%.17g\n",worst_x,worst_y,worst_channel,worst_actual,worst_expected,worst_u,worst_v,fresh.frame.width,fresh.frame.height,rect.x,rect.y,rect.w,rect.h);
      CHECK(max_error<=2&&numerical_edges<=4&&visible>100&&edges>10);pp_frame_destroy(&output);
    }
    pp_frame_destroy(&fresh.frame);pp_raster_completion_release(&completion);pp_raster_worker_destroy(presenter.worker);pp_item_snapshot_release(snapshot);
  }
  CHECK(!pp_assets_external_raster_bytes(assets));pp_renderer_destroy(renderer);pp_assets_destroy(assets);
  puts("PASS settled worker color/mixed/hybrid/SVG sources match fresh zero/fractional GL output, canonical padding/viewport/overhang, premultiplied alpha and edge colors");return 0;
}
