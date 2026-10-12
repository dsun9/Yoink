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
    /* Canonical shaping retains line placement at the useful source density. */
    PPItem lines=item;lines.text="\xf0\x9f\x91\x89\n\xf0\x9f\x91\x89";PPItemSnapshot *multiline=NULL;
    CHECK(pp_item_snapshot_create(assets,&lines,&multiline,&error));const PPTextResult *line_source=NULL;
    CHECK(pp_item_snapshot_bitmap_source(multiline,&line_source,&error));
    CHECK(line_source->artwork.bitmap_glyphs==2&&line_source->frame.width==136&&line_source->frame.height==256);
    CHECK(line_source->layout_size.x==120&&line_source->layout_size.y==226&&!line_source->frame.content_alpha);
    for(int y=0;y<256;y++) {
        int original_y=y<128?y:y-128;
        CHECK(!memcmp(line_source->frame.pixels+(size_t)y*line_source->frame.stride,
            source->frame.pixels+(size_t)original_y*source->frame.stride,136*4));
    }
    PPItem joined=item;joined.text="\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";PPItemSnapshot *cluster=NULL;
    CHECK(pp_item_snapshot_create(assets,&joined,&cluster,&error));const PPTextResult *cluster_source=NULL;
    CHECK(pp_item_snapshot_bitmap_source(cluster,&cluster_source,&error));
    CHECK(cluster_source->artwork.bitmap_glyphs==1&&cluster_source->frame.width==136&&cluster_source->frame.height==128);
    CHECK(cluster_source->layout_size.x==120&&cluster_source->layout_size.y==113);
    pp_item_snapshot_release(multiline);pp_item_snapshot_release(cluster);
    /* Candidate coverage also fits the combined budget before allocation.
     * Pressure rejects the request without a build or invalidating old pixels. */
    PPItemSnapshot *pressured=NULL;CHECK(pp_item_snapshot_create(assets,&item,&pressured,&error));
    const PPFrame *old=&pp_item_snapshot_text_result(pressured)->frame;const uint8_t *old_pixels=old->pixels;
    size_t first_pressure=256u*1024u*1024u;
    size_t second_pressure=512u*1024u*1024u-pp_assets_raster_bytes(assets)-first_pressure-87039;
    CHECK(pp_assets_reserve_raster(assets,first_pressure,&error));CHECK(pp_assets_reserve_raster(assets,second_pressure,&error));
    builds=pp_assets_source_metrics(assets).raster_builds;const PPTextResult *rejected=source;
    CHECK(!pp_item_snapshot_bitmap_source(pressured,&rejected,&error)&&!rejected&&error.code==PP_ERROR_RESOURCE_LIMIT);
    CHECK(pp_assets_source_metrics(assets).raster_builds==builds&&old->pixels==old_pixels&&old->width==120&&old->height==113);
    pp_assets_release_raster(assets,second_pressure);pp_assets_release_raster(assets,first_pressure);
    CHECK(pp_item_snapshot_bitmap_source(pressured,&rejected,&error)&&rejected->frame.width==136);
    first_pressure=256u*1024u*1024u;second_pressure=512u*1024u*1024u-pp_assets_raster_bytes(assets)-first_pressure;
    CHECK(pp_assets_reserve_raster(assets,first_pressure,&error));CHECK(pp_assets_reserve_raster(assets,second_pressure,&error));
    CHECK(pp_assets_raster_bytes(assets)==512u*1024u*1024u);builds=pp_assets_source_metrics(assets).raster_builds;
    CHECK(pp_item_snapshot_bitmap_source(pressured,&again,&error)&&again==rejected&&pp_assets_source_metrics(assets).raster_builds==builds);
    pp_assets_release_raster(assets,second_pressure);pp_assets_release_raster(assets,first_pressure);pp_item_snapshot_release(pressured);
    PPItem mixed=item;mixed.text="A\xf0\x9f\x91\x89";mixed.text_style.primary_asset=import_font(assets,PP_FONT_FIXTURES "/PointyOverhang.ttf");
    PPItemSnapshot *mixed_snapshot=NULL;CHECK(pp_item_snapshot_create(assets,&mixed,&mixed_snapshot,&error));
    builds=pp_assets_source_metrics(assets).raster_builds;CHECK(!pp_item_snapshot_bitmap_source(mixed_snapshot,&again,&error)&&!again);
    CHECK(!pp_item_snapshot_text_result(mixed_snapshot)->artwork.bitmap_only&&pp_assets_source_metrics(assets).raster_builds==builds);
    CHECK(pp_item_snapshot_frame(mixed_snapshot,0,2.25,&compatibility,&next,&error)&&compatibility->width>120);
    pp_item_snapshot_release(mixed_snapshot);
    pp_item_snapshot_release(retained);pp_item_snapshot_release(snapshot);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
    puts("PASS bitmap source retains exact native premultiplied detail, clusters/line placement, immutable ownership, bounded candidate overlap, recovery and mixed-outline exclusion");
    return 0;
}
