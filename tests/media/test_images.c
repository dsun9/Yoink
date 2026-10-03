#include "pointy/assets.h"
#include "pointy/sha256.h"
#include "../test.h"
#include <math.h>
#include <string.h>
static PPBytes fixture(const char *name) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",PP_MEDIA_FIXTURES,name);
    FILE *f=fopen(path,"rb"); CHECK(f); CHECK(!fseek(f,0,SEEK_END)); long n=ftell(f); CHECK(n>0); rewind(f);
    uint8_t *p=malloc((size_t)n); CHECK(p); CHECK(fread(p,1,(size_t)n,f)==(size_t)n); fclose(f); return (PPBytes){p,(size_t)n};
}
static PPAsset *load(PPAssets *assets,const char *name,PPAssetKind kind) {
    PPBytes b=fixture(name); PPAssetId id; PPError e={0};
    if(!pp_asset_import(assets,b,kind,&id,&e)) { fprintf(stderr,"import %s: %s\n",name,e.message); CHECK(false); }
    free((void *)b.data); PPAsset *a=pp_asset_retain(assets,id); CHECK(a); return a;
}
static const PPFrame *frame(PPAsset *a,double t,double *next) {
    const PPFrame *f=NULL; PPError e={0}; CHECK(pp_asset_frame(a,t,0,0,&f,next,&e)); return f;
}
static void test_image_frames_and_limits(void) {
    PPAssets *assets=NULL; PPError e={0}; CHECK(pp_assets_create(&assets,&e));
    PPBytes b=fixture("alpha.png"); PPAssetId x,y; CHECK(pp_asset_import(assets,b,PP_ASSET_PNG,&x,&e)); CHECK(pp_asset_import(assets,b,PP_ASSET_PNG,&y,&e)); CHECK(!memcmp(&x,&y,sizeof(x)));
    PPAsset *a=pp_asset_retain(assets,x); free((void *)b.data); double next; const PPFrame *f=frame(a,0,&next);
    CHECK(f->width==3 && f->height==1 && f->stride==12); CHECK(f->pixels[0]==128 && f->pixels[1]==64 && f->pixels[2]==32 && f->pixels[3]==128); CHECK(f->content_alpha[0]==128 && isinf(next));
    CHECK(f->content_alpha[1]==0 && f->content_alpha[2]==0); for(int i=4;i<12;i++) CHECK(f->pixels[i]==0); pp_asset_release(a);
    a=load(assets,"disposal.gif",PP_ASSET_GIF);
    f=frame(a,0,&next); CHECK(f->pixels[0]==255 && f->content_alpha[1]==0); CHECK(fabs(next-.05)<1e-8);
    const PPFrame *other=frame(a,.06,&next); CHECK(other->pixels[5]==255 && f->content_alpha[1]==0);
    f=frame(a,.11,&next); CHECK(f->content_alpha[1]==0 && f->pixels[10]==255);
    f=frame(a,.151,&next); CHECK(f->pixels[5]==255 && f->content_alpha[2]==0); CHECK(fabs(next-.16)<1e-8);
    f=frame(a,.161,&next); CHECK(f->content_alpha[1]==0); pp_asset_release(a);
    a=load(assets,"finite.gif",PP_ASSET_GIF); f=frame(a,.11,&next); CHECK(f->pixels[0]==255); f=frame(a,.25,&next); CHECK(f->pixels[5]==255 && isinf(next)); pp_asset_release(a);
    b=fixture("alpha.png"); uint8_t *p=(uint8_t *)b.data;
    p[16]=0; p[17]=0; p[18]=0x20; p[19]=1; CHECK(!pp_asset_import(assets,b,PP_ASSET_PNG,&x,&e) && e.code==PP_ERROR_RESOURCE_LIMIT);
    p[19]=0; p[20]=0; p[21]=0; p[22]=0x20; p[23]=0; CHECK(!pp_asset_import(assets,b,PP_ASSET_PNG,&x,&e) && e.code==PP_ERROR_RESOURCE_LIMIT); free(p);
    b=fixture("disposal.gif"); size_t header=13+12+19, block=b.size-header-1; uint8_t *many=malloc(header+block*1025+1); CHECK(many); memcpy(many,b.data,header);
    for(size_t i=0;i<1025;i++) memcpy(many+header+i*block,b.data+header,block); many[header+block*1025]=';';
    CHECK(!pp_asset_import(assets,(PPBytes){many,header+block*1025+1},PP_ASSET_GIF,&x,&e) && e.code==PP_ERROR_RESOURCE_LIMIT); free(many); free((void *)b.data);
    const uint8_t font[]={1,2,3}; CHECK(pp_asset_import(assets,(PPBytes){font,3},PP_ASSET_FONT,&x,&e)); a=pp_asset_retain(assets,x); CHECK(pp_asset_source(a).size==3 && pp_asset_source(a).data!=font); CHECK(!pp_asset_frame(a,0,0,0,&f,&next,&e)); pp_asset_release(a);
    pp_assets_destroy(assets);
}
static void test_total_budget_and_eviction(void) {
    PPAssets *assets; PPError e={0}; CHECK(pp_assets_create(&assets,&e)); PPAsset *held[6]; PPAssetId ids[7];
    for(int i=0;i<7;i++) { char name[32]; snprintf(name,sizeof(name),"budget-%d.png",i); PPBytes b=fixture(name);
        bool ok=pp_asset_import(assets,b,PP_ASSET_PNG,&ids[i],&e); free((void *)b.data);
        if(i<6) { CHECK(ok); held[i]=pp_asset_retain(assets,ids[i]); CHECK(held[i]); } else CHECK(!ok && e.code==PP_ERROR_RESOURCE_LIMIT);
    }
    pp_asset_release(held[0]); PPBytes b=fixture("budget-6.png"); CHECK(pp_asset_import(assets,b,PP_ASSET_PNG,&ids[6],&e)); free((void *)b.data);
    PPAsset *seventh=pp_asset_retain(assets,ids[6]); CHECK(seventh); PPAsset *evicted=pp_asset_retain(assets,ids[0]); const PPFrame *f; double next;
    CHECK(!pp_asset_frame(evicted,0,0,0,&f,&next,&e) && e.code==PP_ERROR_RESOURCE_LIMIT);
    pp_asset_release(seventh); CHECK(pp_asset_frame(evicted,0,0,0,&f,&next,&e)); CHECK(f->pixels[0]==1);
    pp_asset_release(evicted); for(int i=1;i<6;i++) pp_asset_release(held[i]); pp_assets_destroy(assets);
}
static void test_sha256(void) {
    uint8_t digest[32]; const uint8_t empty[]={0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    pp_sha256(NULL,0,digest); CHECK(!memcmp(digest,empty,32));
    const uint8_t abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    pp_sha256((const uint8_t *)"abc",3,digest); CHECK(!memcmp(digest,abc,32));
}
int main(void) { test_sha256(); test_image_frames_and_limits(); test_total_budget_and_eviction(); puts("PASS images: SHA-256, premultiplication, disposal 2/3, clocks, finite loops, limits, owned bytes, global budget and eviction."); return 0; }
