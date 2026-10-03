#include "limits.h"
#include <plutosvg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static plutosvg_document_t *document(PPBytes b,PPError *e) {
    plutosvg_document_t *d=plutosvg_document_load_from_data((const char *)b.data,(int)b.size,-1,-1,NULL,NULL);
    if(!d) pp_media_error(e,PP_ERROR_FORMAT,"PlutoSVG could not parse the validated SVG"); return d;
}
bool pp_svg_preflight(PPBytes b,PPMediaInfo *info,PPError *e) {
    if(!pp_svg_validate(b,e)) return false; plutosvg_document_t *d=document(b,e); if(!d) return false;
    double w=ceil(plutosvg_document_get_width(d)),h=ceil(plutosvg_document_get_height(d)); plutosvg_document_destroy(d);
    if(!isfinite(w) || !isfinite(h) || w>PP_MEDIA_MAX_DIMENSION || h>PP_MEDIA_MAX_DIMENSION) return pp_media_error(e,PP_ERROR_RESOURCE_LIMIT,"SVG intrinsic dimensions exceed 8192");
    info->width=(int)w;info->height=(int)h;info->count=1;size_t bytes;return pp_media_size(info->width,info->height,1,&bytes,e);
}
bool pp_svg_raster(PPAsset *a,int w,int h,PPError *e) {
    size_t bytes; if(!pp_media_size(w,h,1,&bytes,e) || !pp_media_reserve(a,bytes,e)) return false;
    plutosvg_document_t *d=document(pp_asset_source(a),e); if(!d) return false;
    plutovg_surface_t *surface=plutosvg_document_render_to_surface(d,NULL,w,h,NULL,NULL,NULL); plutosvg_document_destroy(d);
    if(!surface) return pp_media_error(e,PP_ERROR_RENDER,"SVG rasterization failed");
    PPFrame *f=calloc(1,sizeof(*f)); if(f) { f->pixels=malloc((size_t)w*h*4); f->content_alpha=malloc((size_t)w*h); }
    if(!f || !f->pixels || !f->content_alpha) { if(f) { free(f->pixels);free(f->content_alpha);free(f); } plutovg_surface_destroy(surface);return pp_media_error(e,PP_ERROR_MEMORY,"Could not allocate SVG raster"); }
    f->width=w;f->height=h;f->stride=(size_t)w*4;
    unsigned char *data=plutovg_surface_get_data(surface);int stride=plutovg_surface_get_stride(surface);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) { uint32_t argb;memcpy(&argb,data+(size_t)y*stride+x*4,4);size_t i=(size_t)y*w+x;
        f->pixels[i*4]=(uint8_t)(argb>>16);f->pixels[i*4+1]=(uint8_t)(argb>>8);f->pixels[i*4+2]=(uint8_t)argb;f->pixels[i*4+3]=f->content_alpha[i]=(uint8_t)(argb>>24); }
    plutovg_surface_destroy(surface);pp_media_clear(a);a->frames=f;a->cache_bytes=bytes;a->owner->cache_bytes+=bytes;return true;
}
