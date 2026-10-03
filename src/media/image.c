#include "limits.h"
#include <SDL3_image/SDL_image.h>
#include <stdlib.h>
#include <string.h>
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
bool pp_image_preflight(PPBytes b,PPMediaInfo *info,PPError *e) {
    if(b.size<33 || memcmp(b.data,"\x89PNG\r\n\x1a\n",8) || be32(b.data+8)!=13 || memcmp(b.data+12,"IHDR",4)) return pp_media_error(e,PP_ERROR_FORMAT,"Invalid PNG header");
    uint32_t w=be32(b.data+16),h=be32(b.data+20); if(w>PP_MEDIA_MAX_DIMENSION || h>PP_MEDIA_MAX_DIMENSION) return pp_media_error(e,PP_ERROR_RESOURCE_LIMIT,"PNG dimensions exceed 8192");
    info->width=(int)w; info->height=(int)h; info->count=1; size_t bytes;
    if(!pp_media_size(info->width,info->height,1,&bytes,e)) return false;
    /* PNG assets are static; APNG must not silently discard animation. */
    for(size_t p=8;p+12<=b.size;) { uint32_t n=be32(b.data+p); if(n>b.size-p-12) return pp_media_error(e,PP_ERROR_FORMAT,"Truncated PNG chunk");
        if(!memcmp(b.data+p+4,"acTL",4)) return pp_media_error(e,PP_ERROR_FORMAT,"Animated PNG is unsupported; import GIF animation"); p+=(size_t)n+12;
    }
    return true;
}
bool pp_image_copy(SDL_Surface *surface,PPFrame *f,PPError *e) {
    SDL_Surface *rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32); if(!rgba) return pp_media_error(e,PP_ERROR_FORMAT,SDL_GetError());
    size_t n=(size_t)rgba->w*rgba->h; f->pixels=malloc(n*4); f->content_alpha=malloc(n);
    if(!f->pixels || !f->content_alpha) { free(f->pixels);free(f->content_alpha);memset(f,0,sizeof(*f));SDL_DestroySurface(rgba);return pp_media_error(e,PP_ERROR_MEMORY,"Could not allocate RGBA frame"); }
    f->width=rgba->w; f->height=rgba->h; f->stride=(size_t)rgba->w*4;
    for(int y=0;y<rgba->h;y++) for(int x=0;x<rgba->w;x++) { const uint8_t *s=(const uint8_t *)rgba->pixels+(size_t)y*rgba->pitch+x*4; size_t i=(size_t)y*rgba->w+x;
        for(int c=0;c<3;c++) f->pixels[i*4+c]=(uint8_t)((s[c]*s[3]+127)/255); f->pixels[i*4+3]=f->content_alpha[i]=s[3]; }
    SDL_DestroySurface(rgba); return true;
}
bool pp_image_decode(PPAsset *a,PPError *e) {
    if(!pp_media_allocate(a,1,e)) return false;
    SDL_IOStream *io=SDL_IOFromConstMem(a->source,a->source_size); if(!io) return pp_media_error(e,PP_ERROR_MEMORY,SDL_GetError());
    SDL_Surface *surface=IMG_LoadTyped_IO(io,true,"PNG"); if(!surface) return pp_media_error(e,PP_ERROR_FORMAT,SDL_GetError());
    bool ok=surface->w==a->info.width && surface->h==a->info.height;
    if(ok) ok=pp_image_copy(surface,&a->frames[0],e); else pp_media_error(e,PP_ERROR_FORMAT,"PNG decoder dimension mismatch"); SDL_DestroySurface(surface); return ok;
}
