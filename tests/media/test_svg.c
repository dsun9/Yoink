#include "pointy/assets.h"
#include "../test.h"
#include <string.h>
static bool import(PPAssets *assets,const char *s,PPAssetId *id,PPError *e) { return pp_asset_import(assets,(PPBytes){(const uint8_t *)s,strlen(s)},PP_ASSET_SVG,id,e); }
static void test_svg_validation(void) {
    PPAssets *assets; PPError e={0}; PPAssetId id; CHECK(pp_assets_create(&assets,&e));
    const char *valid="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><metadata><note>owned</note></metadata><defs><linearGradient id='paint'><stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient><path id='p' d='M0 0L4 0L4 4L0 4Z'/></defs><g transform='translate(2 2)'><use href='#p' fill='url(#paint)'/><rect x='8' y='0' width='2' height='2'/><circle cx='8' cy='8' r='2'/><ellipse cx='2' cy='8' rx='1' ry='2'/><polygon points='0,12 4,12 2,14'/><polyline points='6,12 8,14 10,12' fill='none' stroke='black'/><line x1='12' y1='8' x2='12' y2='12' stroke='black'/></g></svg>";
    CHECK(import(assets,valid,&id,&e)); PPAsset *a=pp_asset_retain(assets,id); const PPFrame *f; double next;
    CHECK(pp_asset_frame(a,0,16,16,&f,&next,&e)); CHECK(f->content_alpha[3*16+3]==255 && f->content_alpha[0]==0); CHECK(f->pixels[(3*16+3)*4]>0 && f->pixels[(3*16+3)*4+2]>0);
    CHECK(pp_asset_frame(a,0,64,64,&f,&next,&e)); CHECK(f->width==64 && f->content_alpha[12*64+8]==255 && f->content_alpha[12*64+7]==0);
    const PPFrame *previous=f; CHECK(!pp_asset_frame(a,0,8193,64,&f,&next,&e) && e.code==PP_ERROR_RESOURCE_LIMIT && !f);
    CHECK(previous->width==64 && previous->content_alpha[12*64+8]==255); pp_asset_release(a);
    const char *bad[]={"<script/>","<text>Hello</text>","<filter/>","<mask/>","<clipPath/>","<rect width='10' height='10' clip-path='url(#c)'/>","<use href='http://example.org/a.svg#p'/>","<rect width='1' height='1' fill='url(http://example.org/p)'/>","<rect width='1' height='1' paint-order='stroke'/>","<rect style='filter:blur(2px)'/>","<g id='a'><use href='#a'/></g>","<use href='#missing'/>","<rect onclick='alert(1)'/>","<image href='data:image/png;base64,x'/>"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { char s[2048]; snprintf(s,sizeof(s),"<svg width='16' height='16'>%s</svg>",bad[i]); CHECK(!import(assets,s,&id,&e)); CHECK(e.code==PP_ERROR_FORMAT || e.code==PP_ERROR_RESOURCE_LIMIT); CHECK(e.message[0]); }
    CHECK(!import(assets,"<!DOCTYPE svg [<!ENTITY x 'payload'>]><svg width='1' height='1'>&x;</svg>",&id,&e));
    CHECK(!import(assets,"<svg width='1' height='1'><metadata><script>alert(1)</script></metadata></svg>",&id,&e));
    CHECK(!import(assets,"<svg width='1' height='1'><defs><rect id='p'/><linearGradient id='g' href='#p'/></defs><rect width='1' height='1' fill='url(#g)'/></svg>",&id,&e));
    char nested[4096]="<svg width='1' height='1'>"; for(int i=0;i<129;i++) strcat(nested,"<g>"); for(int i=0;i<129;i++) strcat(nested,"</g>"); strcat(nested,"</svg>"); CHECK(!import(assets,nested,&id,&e) && e.code==PP_ERROR_RESOURCE_LIMIT);
    char expanded[20000]="<svg width='1' height='1'><defs><rect id='p' width='1' height='1'/></defs>"; for(int i=0;i<257;i++) strcat(expanded,"<use href='#p'/>"); strcat(expanded,"</svg>"); CHECK(!import(assets,expanded,&id,&e) && e.code==PP_ERROR_RESOURCE_LIMIT);
    CHECK(!import(assets,"<svg width='8193' height='1'/>",&id,&e) && e.code==PP_ERROR_RESOURCE_LIMIT);
    pp_assets_destroy(assets);
}
int main(void) { test_svg_validation(); puts("PASS SVG: supported shapes, transforms, gradients, local use, sharp rerasterization, unsafe/unsupported features and expansion limits."); return 0; }
