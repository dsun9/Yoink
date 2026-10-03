#include "limits.h"
#include <SDL3_image/SDL_image.h>
#include <string.h>
static unsigned le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static bool skip_blocks(PPBytes b,size_t *p) { while(*p<b.size) { unsigned n=b.data[(*p)++]; if(!n) return true; if(n>b.size-*p) return false; *p+=n; } return false; }
bool pp_gif_preflight(PPBytes b,PPMediaInfo *info,PPError *e) {
    if(b.size<13 || (memcmp(b.data,"GIF87a",6) && memcmp(b.data,"GIF89a",6))) return pp_media_error(e,PP_ERROR_FORMAT,"Invalid GIF header");
    info->width=(int)le16(b.data+6);info->height=(int)le16(b.data+8);size_t bytes;
    if(!pp_media_size(info->width,info->height,1,&bytes,e)) return false;
    size_t p=13; if(b.data[10]&128) p+=3*((size_t)2<<(b.data[10]&7)); double delay=.01;
    while(p<b.size) { unsigned tag=b.data[p++];
        if(tag==';') { if(!info->count) break; return pp_media_size(info->width,info->height,info->count,&bytes,e); }
        if(tag=='!') { if(p>=b.size) break; unsigned label=b.data[p++];
            if(label==0xf9) { if(b.size-p<6 || b.data[p]!=4 || b.data[p+5]!=0 || ((b.data[p+1]>>2)&7)>3) break; unsigned d=le16(b.data+p+2); delay=d?d*.01:.01; p+=6; }
            else if(label==0x01) return pp_media_error(e,PP_ERROR_FORMAT,"GIF plain text rendering is unsupported");
            else if(!skip_blocks(b,&p)) break;
        } else if(tag==',') { if(b.size-p<9) break; unsigned x=le16(b.data+p),y=le16(b.data+p+2),w=le16(b.data+p+4),h=le16(b.data+p+6),flags=b.data[p+8]; p+=9;
            if(!w || !h || x+w>(unsigned)info->width || y+h>(unsigned)info->height) return pp_media_error(e,PP_ERROR_FORMAT,"GIF frame lies outside its canvas");
            if(++info->count>PP_MEDIA_MAX_FRAMES || !pp_media_size(info->width,info->height,info->count,&bytes,e)) return pp_media_error(e,PP_ERROR_RESOURCE_LIMIT,"GIF decoded frames exceed resource limits");
            info->delays[info->count-1]=delay; delay=.01;
            if(flags&128) p+=3*((size_t)2<<(flags&7)); if(p>=b.size || b.data[p]<2 || b.data[p]>8) break; p++; if(!skip_blocks(b,&p)) break;
        } else break;
    }
    return pp_media_error(e,PP_ERROR_FORMAT,"Truncated or invalid GIF blocks");
}
bool pp_gif_decode(PPAsset *a,PPError *e) {
    if(!pp_media_allocate(a,a->info.count,e)) return false;
    SDL_IOStream *io=SDL_IOFromConstMem(a->source,a->source_size); if(!io) return pp_media_error(e,PP_ERROR_MEMORY,SDL_GetError());
    IMG_AnimationDecoder *decoder=IMG_CreateAnimationDecoder_IO(io,true,"GIF"); if(!decoder) return pp_media_error(e,PP_ERROR_FORMAT,SDL_GetError());
    SDL_PropertiesID properties=IMG_GetAnimationDecoderProperties(decoder); a->plays=(uint64_t)SDL_GetNumberProperty(properties,IMG_PROP_METADATA_LOOP_COUNT_NUMBER,1); a->duration=0; bool ok=true;
    for(size_t i=0;i<a->info.count;i++) { SDL_Surface *surface=NULL; Uint64 duration;
        if(!IMG_GetAnimationDecoderFrame(decoder,&surface,&duration)) { ok=pp_media_error(e,PP_ERROR_FORMAT,SDL_GetError());break; }
        if(surface->w!=a->info.width || surface->h!=a->info.height) ok=pp_media_error(e,PP_ERROR_FORMAT,"GIF decoder canvas mismatch");
        else ok=pp_image_copy(surface,&a->frames[i],e); SDL_DestroySurface(surface); if(!ok) break; a->duration+=a->info.delays[i];
    }
    if(ok) { SDL_Surface *extra=NULL;Uint64 duration; if(IMG_GetAnimationDecoderFrame(decoder,&extra,&duration)) { SDL_DestroySurface(extra);ok=pp_media_error(e,PP_ERROR_FORMAT,"GIF decoder frame count mismatch"); }
        else if(IMG_GetAnimationDecoderStatus(decoder)!=IMG_DECODER_STATUS_COMPLETE) ok=pp_media_error(e,PP_ERROR_FORMAT,SDL_GetError()); }
    IMG_CloseAnimationDecoder(decoder); return ok;
}
