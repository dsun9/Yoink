#include "text_raster.h"
#include "limits.h"
#include <SDL3_ttf/SDL_textengine.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdlib.h>

typedef struct RasterFont {
    TTF_Font *shaped;
    FT_Face base, native;
} RasterFont;

static int floor26(FT_Pos value) { return (int)floor(value / 64.0); }
static int left_bearing(FT_GlyphSlot slot) {
    return slot->format == FT_GLYPH_FORMAT_BITMAP ? slot->bitmap_left : floor26(slot->metrics.horiBearingX);
}
static int top_bearing(FT_GlyphSlot slot) {
    return slot->format == FT_GLYPH_FORMAT_BITMAP ? slot->bitmap_top : floor26(slot->metrics.horiBearingY);
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

static bool draw_bitmap(PPFrame *out, const TTF_CopyOperation *op, double scale,
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
    int left = (int)floor(op->dst.x * scale), top = (int)floor(op->dst.y * scale);
    int right = (int)ceil((op->dst.x + op->dst.w) * scale);
    int bottom = (int)ceil((op->dst.y + op->dst.h) * scale);
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            double sx = (x + 0.5) / scale - op->dst.x + op->src.x - 0.5;
            double sy = (y + 0.5) / scale - op->dst.y + op->src.y - 0.5;
            int x0 = (int)floor(sx), y0 = (int)floor(sy);
            double fx = sx - x0, fy = sy - y0;
            uint8_t rgba[4];
            for (int c = 0; c < 4; c++) {
                double value = 0;
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int xx = x0 + dx, yy = y0 + dy;
                        if (xx >= op->src.x && yy >= op->src.y && xx < op->src.x + op->src.w &&
                            yy < op->src.y + op->src.h && xx >= 0 && yy >= 0 && xx < glyph.width && yy < glyph.height)
                            value += glyph.pixels[((size_t)yy * glyph.width + xx) * 4 + c] *
                                (dx ? fx : 1-fx) * (dy ? fy : 1-fy);
                    }
                }
                rgba[c] = (uint8_t)lround(value * color[3] / 255.0);
            }
            if (type == TTF_IMAGE_ALPHA)
                for (int c = 0; c < 3; c++) rgba[c] = (uint8_t)((color[c] * rgba[3] + 127) / 255);
            over(out, x, y, rgba);
        }
    }
    pp_frame_destroy(&glyph);
    return true;
}

bool pp_text_raster(TTF_Text *text, const PPTextRasterFont fonts[2],
        double shaping_size, double requested_size, const uint8_t color[4],
        PPFrame *out, PPError *error) {
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
    }
    out->width = (int)width;
    out->height = (int)height;
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
        if (!font->native) {
            if (!draw_bitmap(out, &op->copy, scale, color, error)) goto done;
            continue;
        }
        if (FT_Load_Glyph(font->base, op->copy.glyph_index, FT_LOAD_DEFAULT) ||
            FT_Load_Glyph(font->native, op->copy.glyph_index, FT_LOAD_DEFAULT | FT_LOAD_COLOR)) {
            pp_media_error(error, PP_ERROR_FORMAT, "Could not load shaped outline glyph");
            goto done;
        }
        /* Recover the shaped baseline from old bearing and crop, then apply
         * actual destination-size bearings. No outline raster is enlarged. */
        double baseline_x = op->copy.dst.x - op->copy.src.x - left_bearing(font->base->glyph);
        double baseline_y = op->copy.dst.y - op->copy.src.y + top_bearing(font->base->glyph);
        if (font->native->glyph->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(font->native->glyph, FT_RENDER_MODE_NORMAL)) {
            pp_media_error(error, PP_ERROR_RENDER, "Could not rasterize outline glyph");
            goto done;
        }
        int x = (int)lround(baseline_x * scale) + font->native->glyph->bitmap_left;
        int y = (int)lround(baseline_y * scale) - font->native->glyph->bitmap_top;
        if (!draw_outline(out, font->native->glyph, x, y, color, error)) goto done;
    }
    ok = true;
done:
    for (int i = 0; i < 2; i++) {
        if (raster_fonts[i].base) FT_Done_Face(raster_fonts[i].base);
        if (raster_fonts[i].native) FT_Done_Face(raster_fonts[i].native);
    }
    FT_Done_FreeType(library);
    if (!ok) pp_frame_destroy(out);
    return ok;
}
