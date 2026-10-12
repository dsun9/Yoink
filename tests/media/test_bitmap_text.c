#include "pointy/model.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <string.h>
static PPError error;
static PPAssetId import_font(PPAssets *assets,const char *path) {
    size_t length=0;void *bytes=SDL_LoadFile(path,&length);CHECK(bytes&&length);
    PPAssetId id;CHECK(pp_asset_import(assets,(PPBytes){bytes,length},PP_ASSET_FONT,&id,&error));SDL_free(bytes);return id;
}
int main(void) {
    PPAssets *assets=NULL;CHECK(pp_assets_create(&assets,&error));
    PPItem item={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,
        .text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
    item.text_style.fallback_asset=import_font(assets,PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    PPItemSnapshot *snapshot=NULL;CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error));
    PPVec2 layout=pp_item_snapshot_size(snapshot);CHECK(layout.x==120&&layout.y==113);
    uint64_t builds=pp_assets_source_metrics(assets).raster_builds;size_t canonical_bytes=pp_assets_external_raster_bytes(assets);
    const PPTextResult *source=NULL;CHECK(pp_item_snapshot_bitmap_source(snapshot,&source,&error));
    CHECK(source->artwork.bitmap_only&&source->frame.width==136&&source->frame.height==128&&!source->frame.content_alpha);
    CHECK(source->layout_size.x==120&&source->layout_size.y==113&&fabs(source->raster_density-109.0/96)<1e-9);
    CHECK(pp_assets_external_raster_bytes(assets)==canonical_bytes+136*128*4);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds+1);
    PPAsset *font=pp_asset_retain(assets,item.text_style.fallback_asset);PPBytes bytes=pp_asset_source(font);
    FT_Library library;FT_Face face;CHECK(!FT_Init_FreeType(&library));
    CHECK(!FT_New_Memory_Face(library,bytes.data,(FT_Long)bytes.size,0,&face));CHECK(!FT_Select_Size(face,face->num_fixed_sizes-1));
    CHECK(!FT_Load_Char(face,0x1f449,FT_LOAD_DEFAULT|FT_LOAD_COLOR));FT_Bitmap *bitmap=&face->glyph->bitmap;
    CHECK(bitmap->pixel_mode==FT_PIXEL_MODE_BGRA&&bitmap->width==136&&bitmap->rows==128);
    size_t edges=0;
    for(unsigned y=0;y<bitmap->rows;y++) for(unsigned x=0;x<bitmap->width;x++) {
        const uint8_t *original=bitmap->buffer+y*bitmap->pitch+x*4,*pixel=source->frame.pixels+y*source->frame.stride+x*4;
        CHECK(pixel[0]==original[2]&&pixel[1]==original[1]&&pixel[2]==original[0]&&pixel[3]==original[3]);
        CHECK(pixel[0]<=pixel[3]&&pixel[1]<=pixel[3]&&pixel[2]<=pixel[3]);edges+=pixel[3]>0&&pixel[3]<255;
    }
    CHECK(edges>0);FT_Done_Face(face);FT_Done_FreeType(library);pp_asset_release(font);
    PPItemSnapshot *retained=pp_item_snapshot_retain(snapshot);const PPTextResult *again=NULL;
    CHECK(pp_item_snapshot_bitmap_source(retained,&again,&error)&&again==source);
    const PPFrame *compatibility=NULL;double next;CHECK(pp_item_snapshot_frame(snapshot,0,1.237,&compatibility,&next,&error));
    CHECK(pp_item_snapshot_bitmap_source(snapshot,&again,&error)&&again==source&&again->frame.width==136);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds+2);
    pp_item_snapshot_release(retained);pp_item_snapshot_release(snapshot);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS bitmap source retains useful native detail, exact premultiplied edges, canonical layout, shared immutable ownership and exact CPU charge");
    return 0;
}
