/* Opt-in acceptance probe: explicit candidate path, no default resource changes.
 * Each invocation is externally bounded to 60 seconds. Stop after any measured
 * rendering stage exceeds 250ms; never escalate a slow sample. */
#include "pointy/text.h"
#include "pointy/model.h"
#include "pointy/app.h"
#include "pointy/project_store.h"
#include "pointy/platform.h"
#include "pointy/sha256.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static PPError error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s: %s\n",__LINE__,#x,error.message); exit(1); } } while(0)
static double now(void) { return SDL_GetPerformanceCounter()*1000.0/SDL_GetPerformanceFrequency(); }
static void bounded(const char *stage, double start) {
    double ms=now()-start;
    printf("STAGE %s ms=%.3f\n",stage,ms); fflush(stdout);
    if(ms>250) { puts("STOP measured stage exceeded 250ms"); exit(2); }
}
static PPBlob read_bytes(const char *path) {
    PPBlob b={0}; b.data=SDL_LoadFile(path,&b.size); CHECK(b.data); return b;
}
static PPAssetId import(PPAssets *assets, PPBlob b) {
    PPAssetId id; CHECK(pp_asset_import(assets,(PPBytes){b.data,b.size},PP_ASSET_FONT,&id,&error)); return id;
}
static void save(const char *directory,const char *name,const PPFrame *f) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.png",directory,name);
    size_t size=0; void *png=tdefl_write_image_to_png_file_in_memory(f->pixels,f->width,f->height,4,&size);
    CHECK(png); FILE *file=fopen(path,"wb"); CHECK(file && fwrite(png,1,size,file)==size); CHECK(!fclose(file)); mz_free(png);
}
static uint64_t check_pixels(const PPFrame *f,bool opaque) {
    uint64_t sum=0; size_t color=0,zero=0; int x0=f->width,y0=f->height,x1=-1,y1=-1;
    for(int y=0;y<f->height;y++) for(int x=0;x<f->width;x++) {
        size_t i=(size_t)y*f->width+x; const uint8_t *p=f->pixels+i*4;
        CHECK(p[0]<=p[3] && p[1]<=p[3] && p[2]<=p[3]);
        if(opaque) CHECK(p[3]==f->content_alpha[i]);
        sum+=p[3]; zero+=!p[3]; color+=p[3]>32 && (abs(p[0]-p[1])>20 || abs(p[1]-p[2])>20);
        if(p[3]) { x0=SDL_min(x0,x); y0=SDL_min(y0,y); x1=SDL_max(x1,x); y1=SDL_max(y1,y); }
    }
    CHECK(sum && color && zero);
    printf("PIXELS frame=%dx%d ink=%d,%d..%d,%d colored=%zu transparent=%zu alpha_sum=%llu\n",
        f->width,f->height,x0,y0,x1,y1,color,zero,(unsigned long long)sum);
    return sum;
}
static void native_pixels(const PPFrame *f,const FT_Bitmap *bm) {
    int fx=f->width,fy=f->height,bx=(int)bm->width,by=(int)bm->rows;
    for(int y=0;y<f->height;y++) for(int x=0;x<f->width;x++)
        if(f->content_alpha[(size_t)y*f->width+x]) { fx=SDL_min(fx,x); fy=SDL_min(fy,y); }
    for(unsigned y=0;y<bm->rows;y++) for(unsigned x=0;x<bm->width;x++)
        if(bm->buffer[y*bm->pitch+x*4+3]) { bx=SDL_min(bx,(int)x); by=SDL_min(by,(int)y); }
    for(unsigned y=0;y<bm->rows;y++) for(unsigned x=0;x<bm->width;x++) {
        const uint8_t *p=bm->buffer+y*bm->pitch+x*4;
        int dx=fx+(int)x-bx,dy=fy+(int)y-by;
        if(dx<0 || dy<0 || dx>=f->width || dy>=f->height) { CHECK(!p[3]); continue; }
        const uint8_t *q=f->pixels+((size_t)dy*f->width+dx)*4;
        CHECK(q[0]==p[2] && q[1]==p[1] && q[2]==p[0] && q[3]==p[3]);
    }
}
static const char *names[]={"hand","tone","vs16","zwj","flag","keycap"};
static const char *texts[]={"\xf0\x9f\x91\x89","\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd",
    "\xe2\x9d\xa4\xef\xb8\x8f","\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
    "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8","1\xef\xb8\x8f\xe2\x83\xa3"};
static void compatibility(PPAssets *assets,PPTextStyle style,PPBlob candidate,const char *out) {
    FT_Library library; FT_Face face;
    CHECK(!FT_Init_FreeType(&library)); CHECK(!FT_New_Memory_Face(library,candidate.data,(FT_Long)candidate.size,0,&face));
    CHECK(!FT_Set_Char_Size(face,0,96*64,72,72));
    hb_font_t *font=hb_ft_font_create_referenced(face);
    for(int k=0;k<6;k++) {
        hb_buffer_t *b=hb_buffer_create(); hb_buffer_add_utf8(b,texts[k],-1,0,-1);
        hb_buffer_guess_segment_properties(b); hb_shape(font,b,NULL,0);
        unsigned count; hb_glyph_info_t *glyphs=hb_buffer_get_glyph_infos(b,&count);
        printf("CLUSTER %s glyphs=%u glyph=%u\n",names[k],count,count?glyphs[0].codepoint:0);
        CHECK(count==1 && glyphs[0].codepoint);
        CHECK(!FT_Load_Glyph(face,glyphs[0].codepoint,FT_LOAD_DEFAULT));
        printf("BASE metrics=%ldx%ld bearing=%ld,%ld bitmap=%ux%u\n",face->glyph->metrics.width,face->glyph->metrics.height,
            face->glyph->metrics.horiBearingX,face->glyph->metrics.horiBearingY,face->glyph->bitmap.width,face->glyph->bitmap.rows);
        CHECK(!FT_Load_Glyph(face,glyphs[0].codepoint,FT_LOAD_COLOR|FT_LOAD_RENDER));
        FT_Bitmap *bm=&face->glyph->bitmap; CHECK(bm->pixel_mode==FT_PIXEL_MODE_BGRA);
        uint64_t reference=0;
        for(unsigned y=0;y<bm->rows;y++) for(unsigned x=0;x<bm->width;x++) reference+=bm->buffer[y*bm->pitch+x*4+3];
        PPFrame f={0}; PPTextDiagnostics d; double start=now();
        CHECK(pp_text_render(assets,texts[k],&style,1,&f,&d,&error)); bounded("compat_text",start);
        CHECK(!d.fallback_runs && !d.missing_glyphs && !d.missing_requested_families);
        CHECK(check_pixels(&f,true)==reference); native_pixels(&f,bm); save(out,names[k],&f);
        /* Color must ignore requested RGB; alpha must scale every channel. */
        PPTextStyle translucent=style; translucent.color[0]=230; translucent.color[1]=10; translucent.color[2]=180; translucent.color[3]=128;
        PPFrame half={0}; start=now(); CHECK(pp_text_render(assets,texts[k],&translucent,1,&half,&d,&error)); bounded("compat_alpha",start);
        CHECK(half.width==f.width && half.height==f.height);
        for(size_t i=0;i<f.stride*f.height;i++) CHECK(half.pixels[i]==(f.pixels[i]*128+127)/255);
        check_pixels(&half,true); pp_frame_destroy(&half); pp_frame_destroy(&f); hb_buffer_destroy(b);
    }
    hb_font_destroy(font); FT_Done_Face(face); FT_Done_FreeType(library);
    PPFrame rejected={0}; PPTextDiagnostics diagnostics; style.font_size_dip=9000;
    CHECK(!pp_text_render(assets,texts[0],&style,1,&rejected,&diagnostics,&error));
    CHECK(error.code==PP_ERROR_RESOURCE_LIMIT && !rejected.pixels && !rejected.content_alpha);
}
static void ladder(PPAssets *assets,PPTextStyle style,double density,const char *out,const char *label) {
    PPItem item={0}; item.kind=PP_ITEM_TEXT; item.text=texts[0]; item.text_style=style; item.opacity=1;
    PPItemSnapshot *snapshot=NULL; double start=now();
    CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error)); bounded("snapshot_create",start);
    PPVec2 canonical=pp_item_snapshot_size(snapshot); PPRenderer *renderer=NULL; start=now();
    CHECK(pp_renderer_create(&renderer,&error));
    printf("SETUP renderer_create ms=%.3f (separate cold initialization)\n",now()-start);
    double sizes[]={96,384,1536};
    for(int k=0;k<3;k++) {
        const PPFrame *source; double next;
        printf("SAMPLE %s dip=%.0f density=%.2f canonical=%.3fx%.3f\n",label,sizes[k],density,canonical.x,canonical.y);
        start=now(); CHECK(pp_item_snapshot_frame(snapshot,0,sizes[k]*density/fmax(canonical.x,canonical.y),&source,&next,&error));
        double source_ms=now()-start; bounded("snapshot_source",start);
        check_pixels(source,true); char name[128]; snprintf(name,sizeof(name),"%s-%.0f-%.0f-source",label,sizes[k],density*100); save(out,name,source);
        printf("RETAINED bytes=%zu\n",pp_assets_raster_bytes(assets));
        for(int angle=0;angle<=27;angle+=27) {
            PPFrame final={0}; start=now();
            CHECK(pp_renderer_transform(renderer,source,sizes[k],angle,1,density,&final,&error));
            double frame_ms=source_ms+now()-start; bounded("final_transform",start);
            printf("FRAME source_plus_transform_ms=%.3f\n",frame_ms);
            if(frame_ms>250) { puts("STOP combined render exceeded 250ms"); exit(2); }
            check_pixels(&final,true); snprintf(name,sizeof(name),"%s-%.0f-%.0f-angle%d",label,sizes[k],density*100,angle); save(out,name,&final);
            pp_frame_destroy(&final);
        }
    }
    pp_renderer_destroy(renderer); pp_item_snapshot_release(snapshot); CHECK(!pp_assets_raster_bytes(assets));
}
static void mixed(PPAssets *assets,PPAssetId candidate,const char *out,bool ui_font) {
    PPBlob latin={0};
    if(ui_font) CHECK(pp_platform_find_font("Arial",&latin,&error));
    else latin=read_bytes(PP_SCALABLE_ROOT "/tests/fixtures/fonts/PointyFixture.ttf");
    PPFontInfo info; CHECK(pp_font_inspect((PPBytes){latin.data,latin.size},0,&info,&error));
    PPTextStyle style={0}; style.primary_asset=import(assets,latin); style.fallback_asset=candidate; style.requested_family=info.family; style.color[3]=255;
    SDL_free(latin.data);
    const char *text="A \xf0\x9f\x91\x89\xf0\x9f\x8f\xbd\nA \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    if(ui_font) text="Hello \xf0\x9f\x91\x89\xf0\x9f\x8f\xbd\nWorld \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    double sizes[]={96,384,1536};
    /* The controlled fallback ladder already hit its 250ms limit at 1536.
     * Inspect real UI-font text only at the two safe lower sizes. */
    for(int i=0;i<(ui_font?2:3);i++) {
        style.font_size_dip=sizes[i]; PPFrame f={0}; PPTextDiagnostics d; double start=now();
        printf("MIXED family=%s font_dip=%.0f density=1\n",info.family,sizes[i]);
        CHECK(pp_text_render(assets,text,&style,1,&f,&d,&error)); bounded("mixed_scalable_fallback",start);
        CHECK(d.fallback_runs==2 && !d.missing_glyphs && !d.missing_requested_families);
        check_pixels(&f,true); char name[80]; snprintf(name,sizeof(name),"mixed%s-%.0f",ui_font?"-ui":"",sizes[i]); save(out,name,&f); pp_frame_destroy(&f);
    }
}
static void portable(PPAssets *assets,PPTextStyle style,const char *out,const char *notice_path) {
    PPBlob notice=read_bytes(notice_path),ofl=read_bytes(PP_SCALABLE_ROOT "/resources/licenses/NotoEmoji-OFL.txt");
    PPProject *project=NULL,*loaded=NULL; PPAssets *fresh=NULL;
    CHECK(pp_project_create(assets,&project,&error));
    CHECK(pp_project_set_name(project,"Twemoji scalable emoji example",&error));
    CHECK(pp_project_set_notice(project,style.primary_asset,(char*)notice.data,&error));
    CHECK(pp_project_set_notice(project,style.fallback_asset,(char*)ofl.data,&error));
    for(int k=0;k<6;k++) {
        PPItem item={0}; item.kind=PP_ITEM_TEXT; item.text=texts[k]; item.text_style=style; item.opacity=1;
        CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
    }
    char path[1024]; snprintf(path,sizeof(path),"%s/twemoji-example.pointy",out);
    double start=now(); CHECK(pp_project_save(project,path,&error)); printf("STORAGE portable_save ms=%.3f\n",now()-start);
    CHECK(pp_assets_create(&fresh,&error)); start=now(); CHECK(pp_project_load(path,fresh,&loaded,&error)); printf("STORAGE portable_load ms=%.3f\n",now()-start);
    const PPAnchorList *items=pp_project_anchor(loaded,PP_ANCHOR_TOP_LEFT); CHECK(items->count==6);
    CHECK(!strcmp(pp_project_notice(loaded,style.primary_asset),(char*)notice.data));
    CHECK(!strcmp(pp_project_notice(loaded,style.fallback_asset),(char*)ofl.data));
    PPAssetId ids[]={style.primary_asset,style.fallback_asset};
    for(int i=0;i<2;i++) {
        PPAsset *a=pp_asset_retain(assets,ids[i]),*b=pp_asset_retain(fresh,ids[i]); CHECK(a && b);
        PPBytes x=pp_asset_source(a),y=pp_asset_source(b); CHECK(x.size==y.size && !memcmp(x.data,y.data,x.size));
        PPFontInfo info; CHECK(pp_font_inspect(y,0,&info,&error) && info.embeddable);
        printf("PORTABLE whole_font_bytes=%zu family=%s notice_exact=1\n",y.size,info.family);
        pp_asset_release(a); pp_asset_release(b);
    }
    for(int k=0;k<6;k++) {
        const PPItem *item=pp_project_item(loaded,items->item_ids[k]); CHECK(!strcmp(item->text,texts[k]));
        CHECK(!strcmp(item->text_style.requested_family,style.requested_family));
        PPFrame a={0},b={0}; PPTextDiagnostics d;
        CHECK(pp_text_render(assets,texts[k],&style,1,&a,&d,&error));
        CHECK(pp_text_render(fresh,item->text,&item->text_style,1,&b,&d,&error));
        CHECK(a.width==b.width && a.height==b.height && !memcmp(a.pixels,b.pixels,a.stride*a.height));
        pp_frame_destroy(&a); pp_frame_destroy(&b);
    }
    pp_project_destroy(loaded); pp_assets_destroy(fresh); pp_project_destroy(project);
    SDL_free(notice.data); SDL_free(ofl.data); puts("PORTABLE six UTF-8 strings and rendered pixels identical after fresh load");
}
static void application_import(const char *path,PPAssetId expected,const char *out) {
    PPApp *app=NULL; PPAppOptions options={"",false}; double start=now();
    CHECK(pp_app_create(&app,&options,&error));
    printf("SETUP application_create ms=%.3f\n",now()-start);
    const PPItem *item=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT);
    uint64_t id=item->id; PPAssetId fallback=item->text_style.fallback_asset;
    PPCommand command={0}; command.kind=PP_COMMAND_IMPORT_FONT; command.path=path;
    command.item_id=id; command.font_face=0;
    CHECK(pp_app_command(app,&command,&error));
    item=pp_project_item(pp_app_project(app),id);
    CHECK(!memcmp(&item->text_style.primary_asset,&expected,sizeof(expected)));
    CHECK(!memcmp(&item->text_style.fallback_asset,&fallback,sizeof(fallback)));
    CHECK(!strcmp(item->text_style.requested_family,"Twemoji Mozilla") && item->text_style.primary_face==0);
    PPFrame frame={0}; PPTextDiagnostics d; start=now();
    CHECK(pp_text_render(pp_app_assets(app),item->text,&item->text_style,1,&frame,&d,&error)); bounded("application_import_text",start);
    CHECK(!d.missing_glyphs && !d.fallback_runs && !d.missing_requested_families);
    check_pixels(&frame,true); save(out,"app-import-hand",&frame); pp_frame_destroy(&frame); pp_app_destroy(app);
    puts("IMPORT actual application command selects full candidate as primary, retains Noto fallback and renders color");
}
int main(int argc,char **argv) {
    CHECK(argc>=5);
    CHECK(SDL_CreateDirectory(argv[2]));
    PPBlob candidate=read_bytes(argv[1]),noto=read_bytes(PP_SCALABLE_ROOT "/resources/fonts/NotoColorEmoji.ttf");
    uint8_t digest[32]; char hex[65]; pp_sha256(candidate.data,candidate.size,digest);
    for(int i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",digest[i]);
    CHECK(candidate.size==1474284 && !strcmp(hex,"6d90152ee0d29e82fe2a87793af5aa4b7ad13e6538360889e141e81ed299ee8e"));
    printf("SOURCE sha256=%s\n",hex);
    PPAssets *assets=NULL; CHECK(pp_assets_create(&assets,&error));
    PPFontInfo info; CHECK(pp_font_inspect((PPBytes){candidate.data,candidate.size},0,&info,&error));
    printf("FONT family=%s bytes=%zu scalable=%d color=%d embeddable=%d\n",info.family,candidate.size,info.bitmap_em_size==0,info.color,info.embeddable);
    CHECK(info.bitmap_em_size==0 && info.color && info.embeddable);
    PPTextStyle style={0}; style.primary_asset=import(assets,candidate); style.fallback_asset=import(assets,noto);
    style.requested_family=info.family; style.font_size_dip=96; style.color[3]=255;
    if(!strcmp(argv[3],"compat")) compatibility(assets,style,candidate,argv[2]);
    else if(!strcmp(argv[3],"mixed")) mixed(assets,style.primary_asset,argv[2],false);
    else if(!strcmp(argv[3],"mixed-ui")) mixed(assets,style.primary_asset,argv[2],true);
    else if(!strcmp(argv[3],"portable")) { CHECK(argc==6); portable(assets,style,argv[2],argv[5]); }
    else if(!strcmp(argv[3],"import")) application_import(argv[1],style.primary_asset,argv[2]);
    else {
        if(!strcmp(argv[3],"noto")) { style.primary_asset=style.fallback_asset; style.requested_family="Noto Color Emoji"; }
        ladder(assets,style,atof(argv[4]),argv[2],argv[3]);
    }
    pp_assets_destroy(assets); SDL_free(candidate.data); SDL_free(noto.data); puts("PASS scalable emoji probe"); return 0;
}
