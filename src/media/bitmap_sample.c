#include "bitmap_sample.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool pp_bitmap_sample(PPFrame *out, const PPFrame *glyph, const PPBitmapSampling *sample,
                      double scale, int origin_x, int origin_y, const uint8_t color[4], PPError *error) {
    int left = (int)floor(sample->destination.x * scale), top = (int)floor(sample->destination.y * scale);
    int right = (int)ceil((sample->destination.x + sample->destination.w) * scale);
    int bottom = (int)ceil((sample->destination.y + sample->destination.h) * scale);
    /* A fixed bitmap supplies the same source columns on every output row.
     * Keep coordinate conversion and clip tests outside the channel/pixel loop.
     * This bounded scratch is transient, like the decoded glyph above. */
    typedef struct BitmapColumn { int x[2]; double weight[2]; } BitmapColumn;
    BitmapColumn *columns = malloc((size_t)(right-left) * sizeof(*columns));
    if (!columns) {
        if (error) {
            error->code=PP_ERROR_MEMORY;
            snprintf(error->message,sizeof(error->message),"Could not allocate bitmap sampling columns");
        }
        return false;
    }
    for (int x = left; x < right; x++) {
        double sx = (x + 0.5) / scale - sample->destination.x + sample->source.x - 0.5;
        int x0 = (int)floor(sx);
        BitmapColumn *column = &columns[x-left];
        column->weight[1] = sx-x0; column->weight[0] = 1-column->weight[1];
        for (int dx=0; dx<2; dx++) {
            int xx=x0+dx;
            column->x[dx] = xx>=sample->source.x && xx<sample->source.x+sample->source.w && xx>=0 && xx<glyph->width ? xx : -1;
        }
    }
    const uint8_t transparent[4] = {0};
    for (int y = top; y < bottom; y++) {
        if (y+origin_y<0 || y+origin_y>=out->height) continue;
        double sy = (y + 0.5) / scale - sample->destination.y + sample->source.y - 0.5;
        int y0 = (int)floor(sy);
        double fy = sy-y0;
        const uint8_t *rows[2] = {NULL,NULL};
        for (int dy=0; dy<2; dy++) {
            int yy=y0+dy;
            if (yy>=sample->source.y && yy<sample->source.y+sample->source.h && yy>=0 && yy<glyph->height)
                rows[dy]=glyph->pixels+(size_t)yy*glyph->stride;
        }
        for (int x = left; x < right; x++) {
            if (x+origin_x<0 || x+origin_x>=out->width) continue;
            const BitmapColumn *column=&columns[x-left];
            const uint8_t *a=rows[0] && column->x[0]>=0 ? rows[0]+column->x[0]*4 : transparent;
            const uint8_t *b=rows[0] && column->x[1]>=0 ? rows[0]+column->x[1]*4 : transparent;
            const uint8_t *c=rows[1] && column->x[0]>=0 ? rows[1]+column->x[0]*4 : transparent;
            const uint8_t *d=rows[1] && column->x[1]>=0 ? rows[1]+column->x[1]*4 : transparent;
            uint8_t rgba[4];
            for (int channel = 0; channel < 4; channel++) {
                /* Preserve the original tap order and floating operations so
                 * premultiplied edge alpha/color round to identical bytes. */
                double value = a[channel]*column->weight[0]*(1-fy);
                value += b[channel]*column->weight[1]*(1-fy);
                value += c[channel]*column->weight[0]*fy;
                value += d[channel]*column->weight[1]*fy;
                rgba[channel] = color[3]==255 ? (uint8_t)(value+0.5) : (uint8_t)(value * color[3] / 255.0 + 0.5);
            }
            if (sample->alpha_only)
                for (int c = 0; c < 3; c++) rgba[c] = (uint8_t)((color[c] * rgba[3] + 127) / 255);
            size_t index=(size_t)(y+origin_y)*out->width+x+origin_x;
            uint8_t *destination=out->pixels+index*4;
            if (!destination[3]) {
                memcpy(destination,rgba,4);
            } else {
                for (int channel=0;channel<4;channel++)
                    destination[channel]=(uint8_t)(rgba[channel]+(destination[channel]*(255-rgba[3])+127)/255);
            }
            out->content_alpha[index]=destination[3];
        }
    }
    free(columns);
    return true;
}
