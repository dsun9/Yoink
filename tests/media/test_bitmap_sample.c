#include "../../src/media/bitmap_sample.h"
#include "../test.h"
#include <math.h>
#include <string.h>
/* Independent direct four-tap reference, deliberately keeping the old loop
 * structure. Exercise cropped edges, negative origins, alpha-only colorization,
 * overlapping premultiplied content and fractional scales. */
static void reference(PPFrame *out,const PPFrame *glyph,const PPBitmapSampling *s,double scale,int ox,int oy,const uint8_t color[4]) {
  for(int y=(int)floor(s->destination.y*scale);y<(int)ceil((s->destination.y+s->destination.h)*scale);y++)
    for(int x=(int)floor(s->destination.x*scale);x<(int)ceil((s->destination.x+s->destination.w)*scale);x++) {
      if(x+ox<0 || y+oy<0 || x+ox>=out->width || y+oy>=out->height) continue;
      double sx=(x+.5)/scale-s->destination.x+s->source.x-.5,sy=(y+.5)/scale-s->destination.y+s->source.y-.5;
      int ix=(int)floor(sx),iy=(int)floor(sy);double fx=sx-ix,fy=sy-iy;uint8_t rgba[4];
      for(int c=0;c<4;c++) {double value=0;
        for(int dy=0;dy<2;dy++) for(int dx=0;dx<2;dx++) {
          int xx=ix+dx,yy=iy+dy;
          if(xx>=s->source.x && yy>=s->source.y && xx<s->source.x+s->source.w && yy<s->source.y+s->source.h && xx>=0 && yy>=0 && xx<glyph->width && yy<glyph->height)
            value+=glyph->pixels[(size_t)yy*glyph->stride+xx*4+c]*(dx?fx:1-fx)*(dy?fy:1-fy);
        }
        rgba[c]=(uint8_t)lround(value*color[3]/255.0);
      }
      if(s->alpha_only) for(int c=0;c<3;c++) rgba[c]=(uint8_t)((color[c]*rgba[3]+127)/255);
      size_t at=(size_t)(y+oy)*out->width+x+ox;uint8_t *dst=out->pixels+at*4;
      for(int c=0;c<4;c++) dst[c]=(uint8_t)(rgba[c]+(dst[c]*(255-rgba[3])+127)/255);
      out->content_alpha[at]=dst[3];
    }
}
int main(void) {
  uint8_t source[4*3*4];
  for(int i=0;i<12;i++){source[i*4]=(uint8_t)(i*7);source[i*4+1]=(uint8_t)(i*9);source[i*4+2]=(uint8_t)(i*3);source[i*4+3]=(uint8_t)(i*19);}
  PPFrame glyph={source,NULL,4,3,16}; PPError error={0};
  double scales[]={.3,1,1.5,2.333,5};unsigned cases=0;
  for(int mode=0;mode<2;mode++) for(int crop=0;crop<2;crop++) for(int alpha=1;alpha<=255;alpha+=127)
    for(int n=0;n<5;n++) for(int origin=-3;origin<=3;origin+=3) {
      uint8_t actual[16*16*4],expected[16*16*4],coverage[16*16],expected_coverage[16*16];
      memset(actual,19,sizeof(actual));memcpy(expected,actual,sizeof(actual));memset(coverage,19,sizeof(coverage));memcpy(expected_coverage,coverage,sizeof(coverage));
      PPFrame a={actual,coverage,16,16,64},e={expected,expected_coverage,16,16,64};
      PPBitmapSampling sample={{crop,crop,4-crop,3-crop},{-1,-1,4-crop,3-crop},mode!=0};uint8_t color[]={31,83,211,(uint8_t)alpha};
      CHECK(pp_bitmap_sample(&a,&glyph,&sample,scales[n],origin,origin,color,&error));reference(&e,&glyph,&sample,scales[n],origin,origin,color);
      CHECK(!memcmp(actual,expected,sizeof(actual)));CHECK(!memcmp(coverage,expected_coverage,sizeof(coverage)));cases++;
    }
  printf("PASS %u CPU bitmap edge/crop/overlap/opacity reference cases\n",cases);return 0;
}
