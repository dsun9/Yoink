#include "text_raster.h"
#include "bitmap_sample.h"
#include "limits.h"
#include <SDL3_ttf/SDL_textengine.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include <math.h>
#include <stdlib.h>

typedef struct RasterFont {
    TTF_Font *shaped;
    FT_Face base, native;
} RasterFont;

bool pp_text_artwork(TTF_Text *text,const PPTextRasterFont fonts[2],
        double shaping_size,double canonical_font_size,PPTextArtwork *out,PPError *error) {
    FT_Library library=NULL;
    FT_Face faces[2]={0};
    *out=(PPTextArtwork){0};
    if(FT_Init_FreeType(&library))
        return pp_media_error(error,PP_ERROR_MEMORY,"Could not initialize artwork inspection");
    bool ok=false;
    for(int i=0;i<2;i++) {
        if(!fonts[i].shaped_font) continue;
        if(FT_New_Memory_Face(library,fonts[i].bytes.data,(FT_Long)fonts[i].bytes.size,fonts[i].face_index,&faces[i]))
            goto failed_face;
        /* Match pinned SDL_ttf's size/strike choice. Loading with COLOR before
         * rasterization preserves embedded colored bitmap preference in a
         * hybrid face; a monochrome outline is not a replacement source. */
        if(FT_IS_SCALABLE(faces[i])) {
            if(FT_Set_Char_Size(faces[i],0,(FT_F26Dot6)lround(shaping_size*64),72,72)) goto failed_face;
        } else if(faces[i]->num_fixed_sizes<=0 ||
            FT_Select_Size(faces[i],SDL_min((int)shaping_size,faces[i]->num_fixed_sizes-1))) goto failed_face;
    }
    for(int i=0;i<text->internal->num_ops;i++) {
        const TTF_DrawOperation *op=&text->internal->ops[i];
        if(op->cmd==TTF_DRAW_COMMAND_NOOP) continue;
        if(op->cmd!=TTF_DRAW_COMMAND_COPY) {out->unknown_glyphs++;continue;}
        if(op->copy.src.w<=0 || op->copy.src.h<=0 || op->copy.dst.w<=0 || op->copy.dst.h<=0) continue;
        int index=op->copy.glyph_font==fonts[0].shaped_font?0:op->copy.glyph_font==fonts[1].shaped_font?1:-1;
        FT_Face face=index>=0?faces[index]:NULL;
        if(!face || FT_Load_Glyph(face,op->copy.glyph_index,FT_LOAD_DEFAULT|FT_LOAD_COLOR)) {
            out->unknown_glyphs++;continue;
        }
        FT_LayerIterator layers={0};FT_UInt layer,palette;
        bool color_layers=FT_Get_Color_Glyph_Layer(face,op->copy.glyph_index,&layer,&palette,&layers)!=0;
        if(face->glyph->format==FT_GLYPH_FORMAT_BITMAP) out->bitmap_glyphs++;
        else if(face->glyph->format==FT_GLYPH_FORMAT_OUTLINE || color_layers) out->scalable_glyphs++;
        else out->unknown_glyphs++;
    }
    out->bitmap_only=out->bitmap_glyphs>0&&!out->scalable_glyphs&&!out->unknown_glyphs;
    if(out->bitmap_only) out->useful_pixels_per_dip=shaping_size/canonical_font_size;
    ok=true;goto done;
failed_face:
    pp_media_error(error,PP_ERROR_FORMAT,"Could not inspect selected glyph artwork");
done:
    for(int i=0;i<2;i++) if(faces[i]) FT_Done_Face(faces[i]);
    FT_Done_FreeType(library);return ok;
}

static int floor26(FT_Pos value) { return (int)floor(value / 64.0); }
static int left_bearing(FT_GlyphSlot slot) {
    return slot->format == FT_GLYPH_FORMAT_BITMAP ? slot->bitmap_left : floor26(slot->metrics.horiBearingX);
}
static int top_bearing(FT_GlyphSlot slot) {
    return slot->format == FT_GLYPH_FORMAT_BITMAP ? slot->bitmap_top : floor26(slot->metrics.horiBearingY);
}
static bool bitmap_operation(RasterFont *font,const TTF_CopyOperation *op) {
    if(!font->native) return true;
    /* A scalable face can contain a selected embedded bitmap. Keep that
     * shaped source instead of loading its empty/different native outline. */
    return !FT_Load_Glyph(font->base,op->glyph_index,FT_LOAD_DEFAULT|FT_LOAD_COLOR) &&
        font->base->glyph->format==FT_GLYPH_FORMAT_BITMAP;
}

static void over(PPFrame *out, int x, int y, const uint8_t rgba[4]) {
    if (x < 0 || y < 0 || x >= out->width || y >= out->height) return;
    size_t index = (size_t)y * out->width + x;
    uint8_t *dst = out->pixels + index * 4;
    for (int c = 0; c < 4; c++)
        dst[c] = (uint8_t)(rgba[c] + (dst[c] * (255 - rgba[3]) + 127) / 255);
    out->content_alpha[index] = dst[3];
}

static bool draw_outline(PPFrame *out, FT_GlyphSlot slot, int x, int y,
        const uint8_t color[4], PPError *error) {
    FT_Bitmap *bitmap = &slot->bitmap;
    if (!bitmap->width || !bitmap->rows) return true;
    size_t bytes;
    if (!pp_media_size((int)bitmap->width, (int)bitmap->rows, 1, &bytes, error)) return false;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY && bitmap->pixel_mode != FT_PIXEL_MODE_MONO &&
        bitmap->pixel_mode != FT_PIXEL_MODE_BGRA)
        return pp_media_error(error, PP_ERROR_FORMAT, "Unsupported glyph bitmap pixel mode");
    for (unsigned row = 0; row < bitmap->rows; row++) {
        const uint8_t *src = bitmap->buffer + (bitmap->pitch >= 0 ? row : bitmap->rows - 1 - row) * (size_t)abs(bitmap->pitch);
        for (unsigned col = 0; col < bitmap->width; col++) {
            uint8_t rgba[4];
            if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
                rgba[0] = (uint8_t)((src[col*4+2] * color[3] + 127) / 255);
                rgba[1] = (uint8_t)((src[col*4+1] * color[3] + 127) / 255);
                rgba[2] = (uint8_t)((src[col*4+0] * color[3] + 127) / 255);
                rgba[3] = (uint8_t)((src[col*4+3] * color[3] + 127) / 255);
            } else {
                unsigned coverage = bitmap->pixel_mode == FT_PIXEL_MODE_MONO ?
                    ((src[col/8] >> (7-col%8)) & 1) * 255 : src[col];
                rgba[3] = (uint8_t)((coverage * color[3] + 127) / 255);
                for (int c = 0; c < 3; c++) rgba[c] = (uint8_t)((color[c] * rgba[3] + 127) / 255);
            }
            over(out, x + (int)col, y + (int)row, rgba);
        }
    }
    return true;
}

static bool draw_bitmap(PPFrame *out, const TTF_CopyOperation *op, double scale, int origin_x, int origin_y,
        const uint8_t color[4], PPError *error) {
    TTF_ImageType type;
    SDL_Surface *surface = TTF_GetGlyphImageForIndex(op->glyph_font, op->glyph_index, &type);
    if (!surface) return pp_media_error(error, PP_ERROR_RENDER, SDL_GetError());
    size_t bytes;
    PPFrame glyph = {0};
    bool ok = pp_media_size(surface->w, surface->h, 1, &bytes, error);
    if (ok && type == TTF_IMAGE_COLOR) {
        /* FreeType/TTF glyph images already contain premultiplied BGRA color.
         * Convert channel order, retaining edge color without a second alpha
         * multiplication. Monochrome white-alpha glyphs need premultiplying. */
        SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (!rgba) {
            ok = pp_media_error(error, PP_ERROR_RENDER, SDL_GetError());
        } else {
            glyph.width = rgba->w;
            glyph.height = rgba->h;
            glyph.stride = (size_t)rgba->w * 4;
            size_t pixels = (size_t)rgba->w * rgba->h;
            glyph.pixels = malloc(pixels * 4);
            glyph.content_alpha = malloc(pixels);
            if (!glyph.pixels || !glyph.content_alpha) {
                ok = pp_media_error(error, PP_ERROR_MEMORY, "Could not allocate color glyph raster");
            } else {
                for (int y = 0; y < rgba->h; y++) {
                    const uint8_t *src = (const uint8_t *)rgba->pixels + (size_t)y * rgba->pitch;
                    for (int x = 0; x < rgba->w; x++) {
                        size_t index = (size_t)y * rgba->w + x;
                        for (int c = 0; c < 4; c++) glyph.pixels[index*4+c] = src[x*4+c];
                        glyph.content_alpha[index] = src[x*4+3];
                    }
                }
            }
            SDL_DestroySurface(rgba);
        }
    } else if (ok) {
        ok = pp_image_copy(surface, &glyph, error);
    }
    SDL_DestroySurface(surface);
    if (!ok) {
        pp_frame_destroy(&glyph);
        return false;
    }
    PPBitmapSampling sample = {
        {op->src.x,op->src.y,op->src.w,op->src.h},
        {op->dst.x,op->dst.y,op->dst.w,op->dst.h},type==TTF_IMAGE_ALPHA};
    ok = pp_bitmap_sample(out,&glyph,&sample,scale,origin_x,origin_y,color,error);
    pp_frame_destroy(&glyph);
    return ok;
}

static bool native_glyph(RasterFont *font, const TTF_CopyOperation *op, double scale,
        int *x, int *y, PPError *error) {
    if (FT_Load_Glyph(font->base, op->glyph_index, FT_LOAD_DEFAULT) ||
        FT_Load_Glyph(font->native, op->glyph_index, FT_LOAD_DEFAULT | FT_LOAD_COLOR))
        return pp_media_error(error, PP_ERROR_FORMAT, "Could not load shaped outline glyph");
    /* SDL_ttf measures COLR0 using the composite, whose bearings can differ
     * from an empty or unrelated base outline. Recover that same baseline. */
    FT_LayerIterator layers = {0};
    FT_UInt layer, palette;
    if (FT_Get_Color_Glyph_Layer(font->base, op->glyph_index, &layer, &palette, &layers) &&
        (FT_Load_Glyph(font->base, op->glyph_index, FT_LOAD_COLOR) ||
         FT_Render_Glyph(font->base->glyph, FT_RENDER_MODE_NORMAL)))
        return pp_media_error(error, PP_ERROR_RENDER, "Could not measure shaped COLR0 glyph");
    /* Recover the shaped baseline from old bearing and crop, then apply
     * actual destination-size bearings. No outline raster is enlarged. */
    double baseline_x = op->dst.x - op->src.x - left_bearing(font->base->glyph);
    double baseline_y = op->dst.y - op->src.y + top_bearing(font->base->glyph);
    if (font->native->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
        FT_Render_Glyph(font->native->glyph, FT_RENDER_MODE_NORMAL))
        return pp_media_error(error, PP_ERROR_RENDER, "Could not rasterize outline glyph");
    double native_x = round(baseline_x * scale) + font->native->glyph->bitmap_left;
    double native_y = round(baseline_y * scale) - font->native->glyph->bitmap_top;
    /* Layout includes (0,0); origins beyond these limits cannot fit a valid
     * union. Check before converting floating positions to integer pixels. */
    if (!isfinite(native_x) || !isfinite(native_y) ||
        fabs(native_x) > PP_MEDIA_MAX_DIMENSION || fabs(native_y) > PP_MEDIA_MAX_DIMENSION)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Destination glyph origin exceeds raster limits");
    *x = (int)native_x;
    *y = (int)native_y;
    FT_Bitmap *bitmap = &font->native->glyph->bitmap;
    if (bitmap->width && bitmap->rows) {
        size_t bytes;
        if (!pp_media_size((int)bitmap->width, (int)bitmap->rows, 1, &bytes, error)) return false;
    }
    return true;
}

static bool raster(TTF_Text *text, const PPTextRasterFont fonts[2],
        double shaping_size, double requested_size, const uint8_t color[4],
        size_t max_bytes,PPFrame *out, PPVec2 *extent,PPVec2 *origin_px, PPError *error) {
    FT_Library library = NULL;
    RasterFont raster_fonts[2] = {0};
    bool ok = false;
    double scale = requested_size / shaping_size;
    double width = ceil(text->internal->w * scale), height = ceil(text->internal->h * scale);
    if (width > PP_MEDIA_MAX_DIMENSION || height > PP_MEDIA_MAX_DIMENSION)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Scaled text exceeds 8192 pixels per axis");
    size_t bytes;
    if (!pp_media_size((int)width, (int)height, 1, &bytes, error)) return false;
    if (FT_Init_FreeType(&library)) return pp_media_error(error, PP_ERROR_MEMORY, "Could not initialize glyph rasterizer");
    for (int i = 0; i < 2; i++) {
        raster_fonts[i].shaped = fonts[i].shaped_font;
        if (!fonts[i].shaped_font || !TTF_FontIsScalable(fonts[i].shaped_font)) continue;
        if (FT_New_Memory_Face(library, fonts[i].bytes.data, (FT_Long)fonts[i].bytes.size,
                fonts[i].face_index, &raster_fonts[i].base) ||
            FT_New_Memory_Face(library, fonts[i].bytes.data, (FT_Long)fonts[i].bytes.size,
                fonts[i].face_index, &raster_fonts[i].native) ||
            FT_Set_Char_Size(raster_fonts[i].base, 0, (FT_F26Dot6)lround(shaping_size * 64), 72, 72) ||
            FT_Set_Char_Size(raster_fonts[i].native, 0, (FT_F26Dot6)lround(requested_size * 64), 72, 72)) {
            pp_media_error(error, PP_ERROR_FORMAT, "Could not open scalable glyph face at requested resolution");
            goto done;
        }
        if(max_bytes!=SIZE_MAX) {
            /* Face boxes bound native color/outline glyph buffers before a
             * load/render can allocate them. Canonical glyph caches are also
             * limited by the text-engine operation preflight. */
            FT_Face face=raster_fonts[i].base;
            double em=fmax(shaping_size,requested_size)/face->units_per_EM;
            double glyph_w=ceil((face->bbox.xMax-face->bbox.xMin)*em)+2;
            double glyph_h=ceil((face->bbox.yMax-face->bbox.yMin)*em)+2;
            if(!isfinite(glyph_w)||!isfinite(glyph_h)||glyph_w<=0||glyph_h<=0||glyph_w*glyph_h>32.0*1024*1024/4) {
                pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Bounded glyph face exceeds 32 MiB of raster scratch");goto done;
            }
        }
    }
    /* Native hinting can extend beyond scaled shaping bounds. Preflight the
     * actual destination glyph boxes before allocating, then translate every
     * draw operation by the same origin. Keep layout padding/newline bounds. */
    int left = 0, top = 0, right = (int)width, bottom = (int)height;
    for (int i = 0; i < text->internal->num_ops; i++) {
        const TTF_DrawOperation *op = &text->internal->ops[i];
        if (op->cmd != TTF_DRAW_COMMAND_COPY) continue;
        RasterFont *font = op->copy.glyph_font == raster_fonts[0].shaped ? &raster_fonts[0] : &raster_fonts[1];
        int x, y, glyph_right, glyph_bottom;
        if (!bitmap_operation(font,&op->copy)) {
            if (!native_glyph(font, &op->copy, scale, &x, &y, error)) goto done;
            FT_Bitmap *bitmap = &font->native->glyph->bitmap;
            if (!bitmap->width || !bitmap->rows) continue;
            glyph_right = x + (int)bitmap->width;
            glyph_bottom = y + (int)bitmap->rows;
        } else {
            x = (int)floor(op->copy.dst.x * scale);
            y = (int)floor(op->copy.dst.y * scale);
            glyph_right = (int)ceil((op->copy.dst.x + op->copy.dst.w) * scale);
            glyph_bottom = (int)ceil((op->copy.dst.y + op->copy.dst.h) * scale);
        }
        left = SDL_min(left, x);
        top = SDL_min(top, y);
        right = SDL_max(right, glyph_right);
        bottom = SDL_max(bottom, glyph_bottom);
    }
    if (!pp_media_size(right - left, bottom - top, 1, &bytes, error)) goto done;
    int origin_x = -left, origin_y = -top;
    if (origin_px) *origin_px = (PPVec2){origin_x, origin_y};
    if(extent) *extent=(PPVec2){right-left,bottom-top};
    if(!out) {ok=true;goto done;}
    if((size_t)(right-left)*(bottom-top)>max_bytes/5) {
        pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Text candidate exceeds reserved worker raster budget");goto done;
    }
    out->width = right - left;
    out->height = bottom - top;
    out->stride = (size_t)out->width * 4;
    size_t pixels = (size_t)out->width * out->height;
    out->pixels = calloc(pixels, 4);
    out->content_alpha = calloc(pixels, 1);
    if (!out->pixels || !out->content_alpha) {
        pp_media_error(error, PP_ERROR_MEMORY, "Could not allocate owned text raster");
        goto done;
    }
    for (int i = 0; i < text->internal->num_ops; i++) {
        const TTF_DrawOperation *op = &text->internal->ops[i];
        if (op->cmd != TTF_DRAW_COMMAND_COPY) continue;
        RasterFont *font = op->copy.glyph_font == raster_fonts[0].shaped ? &raster_fonts[0] : &raster_fonts[1];
        if (bitmap_operation(font,&op->copy)) {
            if (!draw_bitmap(out, &op->copy, scale, origin_x, origin_y, color, error)) goto done;
            continue;
        }
        int x, y;
        if (!native_glyph(font, &op->copy, scale, &x, &y, error)) goto done;
        if (!draw_outline(out, font->native->glyph, x + origin_x, y + origin_y, color, error)) goto done;
    }
    ok = true;
done:
    for (int i = 0; i < 2; i++) {
        if (raster_fonts[i].base) FT_Done_Face(raster_fonts[i].base);
        if (raster_fonts[i].native) FT_Done_Face(raster_fonts[i].native);
    }
    FT_Done_FreeType(library);
    if (!ok && out) pp_frame_destroy(out);
    return ok;
}
bool pp_text_raster_measure(TTF_Text *text,const PPTextRasterFont fonts[2],
        double shaping_size,double requested_size,PPVec2 *extent,PPVec2 *origin,PPError *error) {
    return raster(text,fonts,shaping_size,requested_size,NULL,SIZE_MAX,NULL,extent,origin,error);
}
bool pp_text_raster_measure_bounded(TTF_Text *text,const PPTextRasterFont fonts[2],
        double shaping_size,double requested_size,size_t max_bytes,PPVec2 *extent,PPVec2 *origin,PPError *error) {
    return raster(text,fonts,shaping_size,requested_size,NULL,max_bytes,NULL,extent,origin,error);
}
bool pp_text_raster_bounded(TTF_Text *text,const PPTextRasterFont fonts[2],
        double shaping_size,double requested_size,const uint8_t color[4],size_t max_bytes,
        PPFrame *out,PPVec2 *origin,PPError *error) {
    return raster(text,fonts,shaping_size,requested_size,color,max_bytes,out,NULL,origin,error);
}
bool pp_text_raster_origin(TTF_Text *text,const PPTextRasterFont fonts[2],
        double shaping_size,double requested_size,const uint8_t color[4],PPFrame *out,PPVec2 *origin,PPError *error) {
    return pp_text_raster_bounded(text,fonts,shaping_size,requested_size,color,SIZE_MAX,out,origin,error);
}

bool pp_text_raster(TTF_Text *text, const PPTextRasterFont fonts[2],
        double shaping_size, double requested_size, const uint8_t color[4],
        PPFrame *out, PPError *error) {
    return pp_text_raster_origin(text, fonts, shaping_size, requested_size,
                                color, out, NULL, error);
}
