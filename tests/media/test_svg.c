#include "pointy/assets.h"
#include "../test.h"
#include <string.h>
static bool import(PPAssets *assets,const char *s,PPAssetId *id,PPError *e) { return pp_asset_import(assets,(PPBytes){(const uint8_t *)s,strlen(s)},PP_ASSET_SVG,id,e); }
static void test_source_rebuild_observations(void) {
    PPAssets *assets=NULL; PPError e={0}; PPAssetId id;
    CHECK(pp_assets_create(&assets,&e));
    CHECK(import(assets,"<svg width='8' height='4'><rect width='8' height='4' fill='red'/></svg>",&id,&e));
    CHECK(pp_assets_source_metrics(assets).raster_builds==1);
    PPAsset *a=pp_asset_retain(assets,id); const PPFrame *f; double next;
    CHECK(pp_asset_frame(a,0,8,4,&f,&next,&e));
    CHECK(pp_assets_source_metrics(assets).raster_builds==1);
    CHECK(pp_asset_frame(a,0,32,16,&f,&next,&e));
    CHECK(pp_assets_source_metrics(assets).raster_builds==2);
    CHECK(!pp_asset_frame(a,0,8193,16,&f,&next,&e));
    CHECK(pp_assets_source_metrics(assets).raster_builds==2);
    pp_asset_release(a); pp_assets_destroy(assets);
}
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
static void expect_rejected(PPAssets *assets,const char *s) {
    PPAssetId id; PPError e={0};
    if(import(assets,s,&id,&e)) { fprintf(stderr,"Unexpected SVG acceptance: %s\n",s); CHECK(false); }
    CHECK(e.code==PP_ERROR_FORMAT && e.message[0]);
}
static void test_svg_opacity_semantics(void) {
    PPAssets *assets; PPError e={0}; CHECK(pp_assets_create(&assets,&e));
    /* Two opaque rectangles overlap. SVG group opacity .5 must apply once to
     * their combined image; this renderer applies it to each drawing instead. */
    expect_rejected(assets,"<svg width='8' height='4'><g opacity='.5'><rect width='6' height='4' fill='red'/><rect x='2' width='6' height='4' fill='blue'/></g></svg>");
    expect_rejected(assets,"<svg width='8' height='4' opacity='.5'><rect width='6' height='4'/><rect x='2' width='6' height='4'/></svg>");
    expect_rejected(assets,"<svg width='8' height='4'><defs><g id='pair'><rect width='6' height='4'/><rect x='2' width='6' height='4'/></g></defs><use href='#pair' opacity='.5'/></svg>");
    expect_rejected(assets,"<svg width='8' height='8'><rect x='2' y='2' width='4' height='4' fill='red' stroke='blue' stroke-width='2' opacity='.5'/></svg>");
    expect_rejected(assets,"<svg width='8' height='8'><defs><rect id='p' x='2' y='2' width='4' height='4' opacity='.5'/></defs><use href='#p' stroke='blue' stroke-width='2'/></svg>");
    PPAssetId id; const PPFrame *f; double next;
    CHECK(import(assets,"<svg width='8' height='4'><g opacity='1'><rect width='8' height='4' fill='red' opacity='.5'/></g></svg>",&id,&e));
    PPAsset *a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->content_alpha[2*8+4]>=127 && f->content_alpha[2*8+4]<=128); pp_asset_release(a);
    CHECK(import(assets,"<svg width='8' height='4'><g fill-opacity='50%'><rect width='6' height='4' fill='red'/><rect x='2' width='6' height='4' fill='blue'/></g></svg>",&id,&e));
    a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->content_alpha[2*8+1]>=127 && f->content_alpha[2*8+1]<=128); CHECK(f->content_alpha[2*8+3]>=190 && f->content_alpha[2*8+3]<=192); pp_asset_release(a);
    CHECK(import(assets,"<svg width='8' height='8'><rect x='2' y='2' width='4' height='4' fill='none' stroke='blue' stroke-width='2' opacity='50%'/></svg>",&id,&e));
    a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->content_alpha[2*8+4]>=127 && f->content_alpha[2*8+4]<=128); pp_asset_release(a);
    pp_assets_destroy(assets);
}
static void test_svg_paint_value_grammar(void) {
    PPAssets *assets; PPError e={0}; CHECK(pp_assets_create(&assets,&e));
    const char *bad[]={"fill='color(display-p3 1 0 0)'","stroke='context-stroke'","fill='context-fill'","color='lab(50% 0 0)'","stop-color='color(display-p3 1 0 0)'","fill='red trailing'","fill='rgb(1 0 0 / .5)'","stroke-linejoin='arcs'","stroke-linecap='triangle'","fill-rule='positive'","stroke-width='2em'","stroke-dasharray='2em 3'","stroke-miterlimit='calc(2+2)'"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { char s[512]; snprintf(s,sizeof(s),"<svg width='4' height='4'><rect width='4' height='4' %s/></svg>",bad[i]); expect_rejected(assets,s); }
    PPAssetId id; const PPFrame *f; double next;
    const char *colors[]={"red","#f00","#ff0000","#ff000080","rgb(255,0,0)","rgba(255,0,0,.5)","hsl(0,100%,50%)","currentColor"};
    for(size_t i=0;i<sizeof(colors)/sizeof(colors[0]);i++) { char s[512]; snprintf(s,sizeof(s),"<svg width='4' height='4' color='red'><rect width='4' height='4' fill='%s'/></svg>",colors[i]); CHECK(import(assets,s,&id,&e)); PPAsset *a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->pixels[0]>0 && f->pixels[1]==0 && f->pixels[2]==0 && f->content_alpha[0]>0); pp_asset_release(a); }
    CHECK(import(assets,"<svg width='4' height='4'><defs><linearGradient id='g'><stop offset='0' stop-color='rgb(255,0,0)'/><stop offset='1' stop-color='red'/></linearGradient></defs><g fill='url(#g) red'><rect width='4' height='4' fill='inherit'/></g></svg>",&id,&e));
    PPAsset *a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->pixels[0]==255 && f->content_alpha[0]==255); pp_asset_release(a);
    CHECK(import(assets,"<svg width='8' height='8' color='red'><rect x='2' y='2' width='4' height='4' fill='none' fill-rule='evenodd' stroke='currentColor' stroke-width='1pt' stroke-linecap='round' stroke-linejoin='bevel' stroke-miterlimit='4' stroke-dasharray='1,2px' stroke-dashoffset='-1px'/></svg>",&id,&e));
    a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); bool red=false;
    for(int i=0;i<64;i++) if(f->content_alpha[i]) { CHECK(f->pixels[i*4]>0 && f->pixels[i*4+1]==0 && f->pixels[i*4+2]==0); red=true; }
    CHECK(red); pp_asset_release(a); pp_assets_destroy(assets);
}
static void test_svg_numeric_paint_grammar(void) {
    PPAssets *assets; PPError e={0}; CHECK(pp_assets_create(&assets,&e));
    expect_rejected(assets,"<svg width='4' height='4'><g opacity='0.'><rect width='4' height='4'/></g></svg>");
    const char *bad[]={"fill-opacity='0.'","stroke-opacity='0.'","stop-opacity='0.'","stroke-width='0.'","stroke-dashoffset='-0.'","stroke-dasharray='1 0.'","stroke-miterlimit='4.'","fill-opacity='1.e-1'","fill-opacity='1e+'","fill-opacity='0.1234567890123456789012345678901234567890123456789'","fill-opacity='0e100'","fill-opacity='1000000000000000000000000000000000000000e-39'"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { char s[512]; snprintf(s,sizeof(s),"<svg width='4' height='4'><rect width='4' height='4' %s/></svg>",bad[i]); expect_rejected(assets,s); }
    const char *numbers[]={".5","+0.5","5e-1","5E-1","50%","+5.0e1%"};
    for(size_t i=0;i<sizeof(numbers)/sizeof(numbers[0]);i++) {
        char s[512]; snprintf(s,sizeof(s),"<svg width='4' height='4'><rect width='4' height='4' fill='red' fill-opacity='%s' stroke-width='1.0px' stroke-dashoffset='-1.0e-1px'/></svg>",numbers[i]);
        PPAssetId id; const PPFrame *f; double next; CHECK(import(assets,s,&id,&e)); PPAsset *a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->content_alpha[5]>=127 && f->content_alpha[5]<=128 && f->pixels[20]==f->content_alpha[5]); pp_asset_release(a);
    }
    PPAssetId id; const PPFrame *f; double next;
    CHECK(import(assets,"<svg width='4' height='4'><g opacity='0.0'><rect width='4' height='4'/></g></svg>",&id,&e));
    PPAsset *a=pp_asset_retain(assets,id); CHECK(pp_asset_frame(a,0,0,0,&f,&next,&e)); CHECK(f->content_alpha[5]==0); pp_asset_release(a); pp_assets_destroy(assets);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--opacity")) test_svg_opacity_semantics();
    else if(argc==2 && !strcmp(argv[1],"--paint")) test_svg_paint_value_grammar();
    else if(argc==2 && !strcmp(argv[1],"--numeric")) test_svg_numeric_paint_grammar();
    else { test_source_rebuild_observations(); test_svg_validation(); test_svg_opacity_semantics(); test_svg_paint_value_grammar(); test_svg_numeric_paint_grammar(); }
    puts("PASS SVG: supported rendering and paint grammar; unsupported opacity compositing and paint values rejected."); return 0;
}
