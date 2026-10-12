#include "pointy/text.h"
#include "pointy/platform.h"
#include "../../src/media/fonts_internal.h"
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error.message); exit(1); } } while (0)
static PPError error;
static PPBlob read_font(const char *path) {
    FILE *file=fopen(path,"rb"); CHECK(file);
    CHECK(!fseek(file,0,SEEK_END)); long size=ftell(file); CHECK(size>0); rewind(file);
    PPBlob b={malloc((size_t)size),(size_t)size}; CHECK(b.data);
    CHECK(fread(b.data,1,b.size,file)==b.size); fclose(file); return b;
}
static size_t visible(const PPFrame *f) {
    size_t n=0; for(size_t i=0;i<(size_t)f->width*f->height;i++) n+=f->content_alpha[i]>0; return n;
}
static uint64_t coverage_sum(const PPFrame *frame) {
    uint64_t sum = 0;
    for (size_t i = 0; i < (size_t)frame->width * frame->height; i++)
        sum += frame->content_alpha[i];
    return sum;
}
typedef struct FontLifecycle {
    PPBytes primary, fallback;
    SDL_Semaphore *ready, *start, *done;
    bool ok;
    uint64_t builds;
    PPError error;
} FontLifecycle;
static int SDLCALL font_lifecycle_worker(void *data) {
    FontLifecycle *work=data;PPAssets *assets=NULL;
    PPTextStyle style={.requested_family="Pointy Overhang",.font_size_dip=96,.color={30,120,230,210}};
    work->ok=pp_assets_create(&assets,&work->error)&&pp_asset_import(assets,work->primary,PP_ASSET_FONT,&style.primary_asset,&work->error)&&
        pp_asset_import(assets,work->fallback,PP_ASSET_FONT,&style.fallback_asset,&work->error);
    for(int i=0;i<32;i++) {
        SDL_SignalSemaphore(work->ready);SDL_WaitSemaphore(work->start);
        PPTextResult result={0};
        if(work->ok)work->ok=pp_text_render_result(assets,"A\xcc\x81\xf0\x9f\x91\x89\nAj",&style,1.5+i%3,&result,NULL,&work->error);
        pp_frame_destroy(&result.frame);SDL_SignalSemaphore(work->done);
    }
    work->builds=pp_assets_source_metrics(assets).raster_builds;pp_assets_destroy(assets);return 0;
}
static void test_concurrent_font_lifecycle(void) {
    PPBlob primary=read_font(PP_FONT_FIXTURES "/PointyOverhang.ttf"),fallback=read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    FontLifecycle work[2]={{0}};SDL_Thread *thread[2];
    for(int i=0;i<2;i++) {
        work[i].primary=(PPBytes){primary.data,primary.size};work[i].fallback=(PPBytes){fallback.data,fallback.size};
        work[i].ready=SDL_CreateSemaphore(0);work[i].start=SDL_CreateSemaphore(0);work[i].done=SDL_CreateSemaphore(0);
        CHECK(work[i].ready&&work[i].start&&work[i].done);thread[i]=SDL_CreateThread(font_lifecycle_worker,"font-lifecycle",work+i);CHECK(thread[i]);
    }
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));PPTextStyle style={.requested_family="Pointy Overhang",.font_size_dip=96,.color={30,120,230,210}};
    CHECK(pp_asset_import(assets,(PPBytes){primary.data,primary.size},PP_ASSET_FONT,&style.primary_asset,&error));CHECK(pp_asset_import(assets,(PPBytes){fallback.data,fallback.size},PP_ASSET_FONT,&style.fallback_asset,&error));
    for(int iteration=0;iteration<32;iteration++) {
        for(int i=0;i<2;i++)CHECK(SDL_WaitSemaphoreTimeout(work[i].ready,5000));
        for(int i=0;i<2;i++)SDL_SignalSemaphore(work[i].start);
        PPTextResult result={0};CHECK(pp_text_render_result(assets,"A\xcc\x81\xf0\x9f\x91\x89\nAj",&style,1.25+iteration%3,&result,NULL,&error));pp_frame_destroy(&result.frame);
        for(int i=0;i<2;i++) {CHECK(SDL_WaitSemaphoreTimeout(work[i].done,5000));CHECK(work[i].ok);}
    }
    for(int i=0;i<2;i++) {SDL_WaitThread(thread[i],NULL);CHECK(work[i].builds==32);SDL_DestroySemaphore(work[i].ready);SDL_DestroySemaphore(work[i].start);SDL_DestroySemaphore(work[i].done);}
    CHECK(pp_assets_source_metrics(assets).raster_builds==32);pp_assets_destroy(assets);pp_blob_destroy(&primary);pp_blob_destroy(&fallback);
    puts("PASS 96 concurrent isolated font open/render/close cycles with separate assets, text engines, faces and output ownership");
}
static void test_drawn_artwork_classification(void) {
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPBlob primary=read_font(PP_FONT_FIXTURES "/PointyOverhang.ttf");
    PPBlob fallback=read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    PPTextStyle style={.font_size_dip=96,.color={0,0,0,255}};
    CHECK(pp_asset_import(assets,(PPBytes){primary.data,primary.size},PP_ASSET_FONT,&style.primary_asset,&error));
    CHECK(pp_asset_import(assets,(PPBytes){fallback.data,fallback.size},PP_ASSET_FONT,&style.fallback_asset,&error));
    pp_blob_destroy(&primary);pp_blob_destroy(&fallback);
    PPFrame frame={0};PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets,"\xf0\x9f\x91\x89",&style,1,&frame,&diagnostics,&error));
    CHECK(diagnostics.artwork.bitmap_only&&diagnostics.artwork.bitmap_glyphs==1);
    CHECK(!diagnostics.artwork.scalable_glyphs&&!diagnostics.artwork.unknown_glyphs);
    CHECK(fabs(diagnostics.artwork.useful_pixels_per_dip-109.0/96)<1e-9);
    pp_frame_destroy(&frame);
    const char *texts[]={"A","A\xf0\x9f\x91\x89","\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb","\xf0\x9f\x91\x89\n\xf0\x9f\x91\x89","A\xcc\x81"};
    const size_t bitmaps[]={0,1,1,2,0},scalable[]={1,1,0,0,2};
    for(size_t i=0;i<sizeof(texts)/sizeof(texts[0]);i++) {
        CHECK(pp_text_render(assets,texts[i],&style,1,&frame,&diagnostics,&error));
        CHECK(diagnostics.artwork.bitmap_glyphs==bitmaps[i]&&diagnostics.artwork.scalable_glyphs==scalable[i]);
        CHECK(!diagnostics.artwork.unknown_glyphs&&diagnostics.artwork.bitmap_only==(i==2||i==3));
        pp_frame_destroy(&frame);
    }
    primary=read_font(PP_FONT_FIXTURES "/PointyDefaultFonts.ttc");
    CHECK(pp_asset_import(assets,(PPBytes){primary.data,primary.size},PP_ASSET_FONT,&style.primary_asset,&error));
    pp_blob_destroy(&primary);style.primary_face=1;
    CHECK(pp_text_render(assets,"\xf0\x9f\x91\x89",&style,1,&frame,&diagnostics,&error));
    CHECK(diagnostics.artwork.scalable_glyphs==1&&!diagnostics.artwork.bitmap_only&&!diagnostics.artwork.bitmap_glyphs);
    pp_frame_destroy(&frame);pp_assets_destroy(assets);
    puts("PASS drawn bitmap hand classification and selected useful strike");
}
static void test_hybrid_selected_bitmap(void) {
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPBlob bytes=read_font(PP_FONT_FIXTURES "/PointyHybrid.ttf");
    PPFontInfo info;CHECK(pp_font_inspect((PPBytes){bytes.data,bytes.size},0,&info,&error));
    CHECK(info.scalable&&info.bitmap_strikes&&info.embeddable&&!info.bitmap_only);
    PPTextStyle style={.font_size_dip=16,.color={0,255,0,255}};
    CHECK(pp_asset_import(assets,(PPBytes){bytes.data,bytes.size},PP_ASSET_FONT,&style.primary_asset,&error));
    pp_blob_destroy(&bytes);PPFrame frame={0};PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets,"B",&style,1,&frame,&diagnostics,&error));
    CHECK(diagnostics.artwork.bitmap_only&&diagnostics.artwork.bitmap_glyphs==1&&!diagnostics.artwork.scalable_glyphs);
    bool color=false;
    for(size_t i=0;i<(size_t)frame.width*frame.height;i++) if(frame.pixels[i*4+3]>0&&frame.pixels[i*4+1]>0&&!frame.pixels[i*4]&&!frame.pixels[i*4+2]) color=true;
    CHECK(color);pp_frame_destroy(&frame);
    CHECK(pp_text_render(assets,"A",&style,1,&frame,&diagnostics,&error));
    CHECK(!diagnostics.artwork.bitmap_only&&diagnostics.artwork.scalable_glyphs==1&&!diagnostics.artwork.bitmap_glyphs);pp_frame_destroy(&frame);
    CHECK(pp_text_render(assets,"AB",&style,1,&frame,&diagnostics,&error));
    CHECK(!diagnostics.artwork.bitmap_only&&diagnostics.artwork.scalable_glyphs==1&&diagnostics.artwork.bitmap_glyphs==1);
    pp_frame_destroy(&frame);
    PPTextResult enlarged={0};
    CHECK(pp_text_render_result(assets,"B",&style,6,&enlarged,&diagnostics,&error));
    CHECK(enlarged.artwork.bitmap_only&&enlarged.frame.width>=96);
    CHECK(visible(&enlarged.frame)>6000);
    pp_frame_destroy(&enlarged.frame);pp_assets_destroy(assets);
    puts("PASS hybrid face classifies selected bitmap, outline and mixed glyphs independently");
}
static void test_canonical_raster_geometry(void) {
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPBlob primary=read_font(PP_FONT_FIXTURES "/PointyOverhang.ttf");
    PPBlob fallback=read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    PPTextStyle style={.font_size_dip=96,.color={30,120,230,210}};
    CHECK(pp_asset_import(assets,(PPBytes){primary.data,primary.size},PP_ASSET_FONT,&style.primary_asset,&error));
    CHECK(pp_asset_import(assets,(PPBytes){fallback.data,fallback.size},PP_ASSET_FONT,&style.fallback_asset,&error));
    pp_blob_destroy(&primary);pp_blob_destroy(&fallback);
    PPTextResult canonical={0},sharp={0};
    CHECK(pp_text_render_result(assets,"A\xcc\x81",&style,1,&canonical,NULL,&error));
    CHECK(pp_text_render_result(assets,"A\xcc\x81",&style,1.5,&sharp,NULL,&error));
    CHECK(canonical.layout_size.x==58 && canonical.layout_size.y==122 && canonical.layout_origin.y==1);
    CHECK(sharp.layout_size.x==canonical.layout_size.x && sharp.layout_size.y==canonical.layout_size.y);
    CHECK(sharp.layout_origin.y==canonical.layout_origin.y && sharp.raster_origin_px.y==0 && sharp.raster_density==1.5);
    PPFrame fresh={0};CHECK(pp_text_render(assets,"A\xcc\x81",&style,1.5,&fresh,NULL,&error));
    CHECK(fresh.width==sharp.frame.width&&fresh.height==sharp.frame.height);
    CHECK(!memcmp(fresh.pixels,sharp.frame.pixels,fresh.stride*fresh.height));
    pp_frame_destroy(&fresh);pp_frame_destroy(&canonical.frame);pp_frame_destroy(&sharp.frame);pp_assets_destroy(assets);
    puts("PASS canonical text padding/baseline stays fixed across requested native resolution");
}

static void test_whitespace_validation(void) {
    PPAssets *assets = NULL;
    PPTextStyle style = {0};
    PPBlob primary = read_font(PP_FONT_FIXTURES "/PointyFixture.ttf");
    PPBlob fallback = read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_asset_import(assets, (PPBytes){primary.data,primary.size}, PP_ASSET_FONT, &style.primary_asset, &error));
    CHECK(pp_asset_import(assets, (PPBytes){fallback.data,fallback.size}, PP_ASSET_FONT, &style.fallback_asset, &error));
    pp_blob_destroy(&primary);
    pp_blob_destroy(&fallback);
    style.font_size_dip = 96;
    style.color[3] = 255;
    const char *whitespace[] = {"\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9", "\v", "\f",
        " \t\r\n\v\f\xc2\x85\xc2\xa0\xe1\x9a\x80\xe2\x80\x80\xe2\x80\x8a\xe2\x80\xa8\xe2\x80\xa9\xe2\x80\xaf\xe2\x81\x9f\xe3\x80\x80"};
    for (size_t i = 0; i < sizeof(whitespace)/sizeof(whitespace[0]); i++) {
        PPFrame frame = {0};
        PPTextDiagnostics diagnostics;
        bool rendered = pp_text_render(assets, whitespace[i], &style, 1, &frame, &diagnostics, &error);
        printf("whitespace case %zu: rendered=%d visible=%zu\n", i, rendered, visible(&frame));
        CHECK(!rendered && error.code == PP_ERROR_ARGUMENT);
        CHECK(!frame.pixels && !frame.content_alpha && diagnostics.missing_glyphs == 0);
    }
    PPFrame invisible = {0};
    PPTextDiagnostics diagnostics;
    style.color[3] = 0;
    CHECK(!pp_text_render(assets, "A", &style, 1, &invisible, &diagnostics, &error));
    CHECK(error.code == PP_ERROR_ARGUMENT && !invisible.pixels && !invisible.content_alpha);
    pp_assets_destroy(assets);
}

static void test_destination_glyph_coverage(void) {
    PPAssets *assets = NULL;
    PPBlob bytes = read_font(PP_FONT_FIXTURES "/PointyOverhang.ttf");
    PPBlob fallback = read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    PPTextStyle style = {0};
    style.color[3] = 255;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_asset_import(assets, (PPBytes){bytes.data,bytes.size}, PP_ASSET_FONT, &style.primary_asset, &error));
    CHECK(pp_asset_import(assets, (PPBytes){fallback.data,fallback.size}, PP_ASSET_FONT, &style.fallback_asset, &error));
    pp_blob_destroy(&fallback);
    FT_Library library;
    FT_Face face;
    CHECK(!FT_Init_FreeType(&library));
    CHECK(!FT_New_Memory_Face(library, bytes.data, (FT_Long)bytes.size, 0, &face));
    const char *texts[] = {"j", "A\xcc\x81"};
    bool lost_coverage = false;
    for (int kind = 0; kind < 2; kind++) {
        for (int size = 8; size <= 30; size++) {
            style.font_size_dip = size;
            CHECK(!FT_Set_Char_Size(face, 0, size * 64, 72, 72));
            uint64_t native_sum = 0;
            const FT_ULong codes[] = {kind ? 'A' : 'j', 0x301};
            for (int glyph = 0; glyph < (kind ? 2 : 1); glyph++) {
                CHECK(!FT_Load_Char(face, codes[glyph], FT_LOAD_RENDER));
                FT_Bitmap *bitmap = &face->glyph->bitmap;
                CHECK(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
                for (unsigned y = 0; y < bitmap->rows; y++)
                    for (unsigned x = 0; x < bitmap->width; x++)
                        native_sum += bitmap->buffer[y * bitmap->pitch + x];
            }
            PPFrame frame = {0};
            PPTextDiagnostics diagnostics;
            CHECK(pp_text_render(assets, texts[kind], &style, 1, &frame, &diagnostics, &error));
            CHECK(!diagnostics.missing_glyphs);
            uint64_t retained = coverage_sum(&frame);
            if (retained != native_sum) {
                printf("native coverage %s size=%d: expected=%llu retained=%llu\n", texts[kind], size,
                    (unsigned long long)native_sum, (unsigned long long)retained);
                lost_coverage = true;
            }
            pp_frame_destroy(&frame);
        }
    }
    CHECK(!lost_coverage);
    puts("destination coverage: 46/46 native glyph alpha sums retained");
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    pp_assets_destroy(assets);
    pp_blob_destroy(&bytes);
}
static void embedding(void) {
    const uint16_t flags[]={0,8,2,4,0x108,0x208,6};
    const bool permitted[]={true,true,false,false,true,false,false};
    const PPFontEmbedding expected[]={PP_FONT_INSTALLABLE,PP_FONT_EDITABLE,PP_FONT_RESTRICTED,
        PP_FONT_PREVIEW_ONLY,PP_FONT_EDITABLE,PP_FONT_EDITABLE,PP_FONT_RESTRICTED};
    for(size_t i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        PPFontInfo info={0}; pp_font_embedding_flags(flags[i],&info);
        CHECK(info.embeddable==permitted[i]);
        CHECK(info.embedding==expected[i]);
        CHECK(info.no_subsetting==((flags[i]&0x100)!=0));
        CHECK(info.bitmap_only==((flags[i]&0x200)!=0));
    }
    PPBlob b=read_font(PP_FONT_FIXTURES "/PointyFixture.ttf"); PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){b.data,b.size},0,&info,&error));
    CHECK(info.embeddable && info.covers_latin_a && !info.covers_hand && info.unicode_glyphs==3);
    CHECK(!pp_font_inspect((PPBytes){b.data,b.size},1,&info,&error));
    CHECK(!pp_font_inspect((PPBytes){(uint8_t*)"bad",3},0,&info,&error));
    pp_blob_destroy(&b);
#ifdef _WIN32
    CHECK(!pp_platform_find_font("Pointy No Such Family 234982",&b,&error));
    CHECK(!b.data && !b.size);
    if(pp_platform_find_font("Aptos",&b,&error)) {
        CHECK(pp_font_inspect((PPBytes){b.data,b.size},0,&info,&error));
        printf("Aptos registry/Office lookup: %s\n",info.family); pp_blob_destroy(&b);
    } else puts("Aptos unavailable: emoji-only default still tested");
#endif
}
static void test_text_fallback_and_embedding(void) {
    PPAssets *assets=NULL; CHECK(pp_assets_create(&assets,&error));
    PPBlob latin=read_font(PP_FONT_FIXTURES "/PointyFixture.ttf");
    PPBlob emoji=read_font(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    PPTextStyle s={0}; s.requested_family="Pointy Fixture"; s.font_size_dip=96; s.color[0]=20;s.color[1]=80;s.color[2]=140;s.color[3]=255;
    CHECK(pp_asset_import(assets,(PPBytes){latin.data,latin.size},PP_ASSET_FONT,&s.primary_asset,&error));
    CHECK(pp_asset_import(assets,(PPBytes){emoji.data,emoji.size},PP_ASSET_FONT,&s.fallback_asset,&error));
    pp_blob_destroy(&latin); pp_blob_destroy(&emoji);
    PPFrame frame={0},large={0}; PPTextDiagnostics d;
    CHECK(pp_text_render(assets,"A\xf0\x9f\x91\x89",&s,1,&frame,&d,&error));
    CHECK(d.fallback_runs==1 && d.missing_glyphs==0 && !d.missing_requested_families);
    size_t colored=0,tinted=0,gaps=0,edges=0;
    for(size_t i=0;i<(size_t)frame.width*frame.height;i++) {
        const uint8_t *p=frame.pixels+i*4;
        colored+=p[3]>0 && p[0]>p[2]+20;
        tinted+=p[3]==255 && p[0]==20 && p[1]==80 && p[2]==140;
        gaps+=p[3]==0; edges+=p[3]>0 && p[3]<255;
        CHECK(p[0]<=p[3] && p[1]<=p[3] && p[2]<=p[3]);
        CHECK(p[3]==frame.content_alpha[i]);
    }
    CHECK(colored>100 && tinted>100 && gaps>100 && edges>10);
    SDL_Surface *surface=SDL_CreateSurfaceFrom(frame.width,frame.height,SDL_PIXELFORMAT_RGBA32,frame.pixels,(int)frame.stride);
    CHECK(surface && SDL_SaveBMP(surface,PP_TEXT_IMAGE)); SDL_DestroySurface(surface);
    CHECK(pp_text_render(assets,"A\xf0\x9f\x91\x89",&s,2,&large,&d,&error));
    CHECK(abs(large.width-frame.width*2)<=4 && abs(large.height-frame.height*2)<=4);
    surface=SDL_CreateSurfaceFrom(large.width,large.height,SDL_PIXELFORMAT_RGBA32,large.pixels,(int)large.stride);
    CHECK(surface && SDL_SaveBMP(surface,PP_TEXT_IMAGE ".double.bmp")); SDL_DestroySurface(surface);
    CHECK(visible(&large)>visible(&frame)*3); pp_frame_destroy(&large); pp_frame_destroy(&frame);
    /* Destination outline coverage stays sharp at 384 px em with fixed emoji
     * fallback configured: an enlarged 109 px outline has much wider edges. */
    CHECK(pp_text_render(assets,"A",&s,4,&large,&d,&error));
    size_t partial = 0, rows = 0;
    for (int y = large.height / 3; y < large.height * 2 / 3; y++) {
        for (int x = 0; x < large.width; x++) {
            uint8_t alpha = large.content_alpha[(size_t)y * large.width + x];
            partial += alpha > 0 && alpha < 255;
        }
        rows++;
    }
    CHECK(partial <= rows * 4);
    surface=SDL_CreateSurfaceFrom(large.width,large.height,SDL_PIXELFORMAT_RGBA32,large.pixels,(int)large.stride);
    CHECK(surface && SDL_SaveBMP(surface,PP_TEXT_IMAGE ".large.bmp")); SDL_DestroySurface(surface);
    pp_frame_destroy(&large);
    const char *cases[]={"A\xcc\x81","A\nA","\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd","\xf0\x9f\x91\x89\xef\xb8\x8f","\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"};
    for(size_t i=0;i<5;i++) {
        CHECK(pp_text_render(assets,cases[i],&s,1,&frame,&d,&error));
        CHECK(d.missing_glyphs==0 && visible(&frame)>0);
        if(i>=2) CHECK(d.fallback_runs==1);
        if(i==1) CHECK(frame.height>150);
        if(i==4) CHECK(frame.width<160); /* One shaped ZWJ glyph, no separate woman/laptop. */
        pp_frame_destroy(&frame);
    }
    CHECK(pp_text_render(assets,"\xf4\x8f\xbf\xbf",&s,1,&frame,&d,&error)); CHECK(d.missing_glyphs>0); pp_frame_destroy(&frame);
    CHECK(!pp_text_render(assets,"",&s,1,&frame,&d,&error));
    CHECK(!pp_text_render(assets," \t\n",&s,1,&frame,&d,&error));
    CHECK(!pp_text_render(assets,"\xc2\xa0\xe2\x80\x83",&s,1,&frame,&d,&error));
    PPAssetId saved_fallback=s.fallback_asset;
    memset(&s.fallback_asset,0,sizeof(s.fallback_asset));
    CHECK(!pp_text_render(assets,"\xc2\xa0\xe2\x80\x83",&s,1,&frame,&d,&error));
    s.fallback_asset=saved_fallback;
    CHECK(!pp_text_render(assets,"\xc0\xaf",&s,1,&frame,&d,&error));
    s.requested_family="Aptos"; memset(&s.primary_asset,0,sizeof(s.primary_asset));
    CHECK(pp_text_render(assets,"\xf0\x9f\x91\x89",&s,1.5,&frame,&d,&error));
    CHECK(d.missing_requested_families==1 && d.fallback_runs==1 && !d.missing_glyphs); pp_frame_destroy(&frame);
    s.font_size_dip=9000; CHECK(!pp_text_render(assets,"A",&s,1,&frame,&d,&error)); CHECK(error.code==PP_ERROR_RESOURCE_LIMIT);
    s.font_size_dip=96; s.requested_family="Pointy Fixture Wide";
    PPBlob collection=read_font(PP_FONT_FIXTURES "/PointyFixture.ttc"); PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){collection.data,collection.size},1,&info,&error)); CHECK(!strcmp(info.family,s.requested_family));
    CHECK(pp_asset_import(assets,(PPBytes){collection.data,collection.size},PP_ASSET_FONT,&s.primary_asset,&error)); pp_blob_destroy(&collection);
    s.primary_face=1; CHECK(pp_text_render(assets,"A",&s,1,&frame,&d,&error)); CHECK(!d.missing_requested_families && !d.fallback_runs);
    int wide=frame.width;pp_frame_destroy(&frame);s.primary_face=0;
    CHECK(pp_text_render(assets,"A",&s,1,&frame,&d,&error));CHECK(frame.width<wide && d.missing_requested_families==1);pp_frame_destroy(&frame);
    CHECK(pp_text_render(assets,"A",&s,1,&frame,&d,&error));
    pp_assets_destroy(assets);
    CHECK(visible(&frame)>0); /* Owned text frame outlives all borrowed font data. */
    pp_frame_destroy(&frame);
}
int main(int argc, char **argv) {
    if(argc==2&&!strcmp(argv[1],"font-lifecycle")) {test_concurrent_font_lifecycle();return 0;}
    if(argc==2&&!strcmp(argv[1],"worker-limits")) {
        PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));PPBlob font=read_font(PP_FONT_FIXTURES "/PointyOverhang.ttf");
        PPTextStyle style={.requested_family="Pointy Overhang",.font_size_dip=96,.color={20,80,140,255}};
        CHECK(pp_asset_import(assets,(PPBytes){font.data,font.size},PP_ASSET_FONT,&style.primary_asset,&error));pp_blob_destroy(&font);
        char text[4041];size_t index=0;for(int line=0;line<40;line++) {for(int column=0;column<100;column++)text[index++]='A';text[index++]='\n';}text[index]=0;
        PPTextResult result={0};CHECK(!pp_text_render_result_bounded(assets,text,&style,.01,1024*1024,&result,NULL,&error));
        CHECK(error.code==PP_ERROR_RESOURCE_LIMIT&&!result.frame.pixels&&!pp_assets_source_metrics(assets).raster_builds);
        pp_assets_destroy(assets);puts("PASS bounded text rejects excessive canonical glyph scratch before the small destination allocation");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"canonical")) {test_canonical_raster_geometry();return 0;}
    if(argc==2&&!strcmp(argv[1],"artwork")) {test_drawn_artwork_classification();return 0;}
    if(argc==2&&!strcmp(argv[1],"hybrid")) {test_hybrid_selected_bitmap();return 0;}
    if (argc == 2 && !strcmp(argv[1], "whitespace")) {
        test_whitespace_validation();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "bounds")) {
        test_destination_glyph_coverage();
        return 0;
    }
    embedding();
    test_text_fallback_and_embedding();
    test_whitespace_validation();
    test_destination_glyph_coverage();
    puts("text: fallback, color, clusters, embedding, collection, bounds PASS");
    return 0;
}
