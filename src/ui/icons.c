#include "backend.h"
#include <math.h>
/* Icons use a common 24-unit drawing box, independent of control dimensions.
 * Fonts never supply action or direction glyphs. */
void pp_ui_draw_icon(struct nk_command_buffer *canvas, struct nk_rect bounds,
                     PPUIIcon icon, struct nk_color color) {
  float x = bounds.x + (bounds.w - 24) / 2;
  float y = bounds.y + (bounds.h - 24) / 2;
#define LINE(a,b,c,d) nk_stroke_line(canvas,x+(a),y+(b),x+(c),y+(d),1.6f,color)
#define RECT(a,b,w,h) nk_stroke_rect(canvas,nk_rect(x+(a),y+(b),(w),(h)),1,1.6f,color)
  if (icon >= PP_UI_ICON_NORTHWEST && icon <= PP_UI_ICON_SOUTHEAST) {
    static const float directions[][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},
                                          {1,0},{-1,1},{0,1},{1,1}};
    const float *direction = directions[icon - PP_UI_ICON_NORTHWEST];
    float dx = direction[0], dy = direction[1];
    float length = sqrtf(dx * dx + dy * dy); dx /= length; dy /= length;
    float tip_x = 12 + 9 * dx, tip_y = 12 + 9 * dy;
    LINE(12 - 8 * dx, 12 - 8 * dy, tip_x, tip_y);
    LINE(tip_x, tip_y, tip_x - 6 * dx - 5 * dy, tip_y - 6 * dy + 5 * dx);
    LINE(tip_x, tip_y, tip_x - 6 * dx + 5 * dy, tip_y - 6 * dy - 5 * dx);
  } else switch (icon) {
  case PP_UI_ICON_NEW:
    RECT(5,3,14,18); LINE(12,8,12,16); LINE(8,12,16,12); break;
  case PP_UI_ICON_OPEN: {
    float points[] = {x+3,y+8,x+3,y+19,x+21,y+19,x+21,y+7,
                      x+11,y+7,x+8,y+4,x+3,y+4,x+3,y+8};
    nk_stroke_polyline(canvas, points, 8, 1.6f, color);
    LINE(3,10,21,10); break;
  }
  case PP_UI_ICON_SAVE:
    RECT(4,3,16,18); RECT(8,3,8,6); RECT(8,13,8,8);
    break;
  case PP_UI_ICON_SAVE_AS:
    /* Leave room for the arrow in the outline itself. No background eraser
     * can hide its pressed or selected foreground. */
    LINE(4,3,20,3); LINE(20,3,20,10); LINE(4,3,4,21); LINE(4,21,10,21);
    RECT(8,3,8,6); LINE(8,13,13,13); LINE(8,13,8,18);
    LINE(13,21,21,13); LINE(18,13,21,13); LINE(21,13,21,16);
    break;
  case PP_UI_ICON_CHEVRON_DOWN: LINE(7,10,12,15); LINE(12,15,17,10); break;
  case PP_UI_ICON_MONITOR:
    RECT(2,3,20,14); LINE(12,17,12,21); LINE(7,21,17,21); break;
  case PP_UI_ICON_STAR: case PP_UI_ICON_STAR_OUTLINE: {
    float points[20];
    for(int i=0;i<10;i++) {
      float angle = (float)(-1.57079632679 + i * .62831853072);
      float radius = i % 2 ? 4.5f : 10;
      points[i*2] = x+12+cosf(angle)*radius;
      points[i*2+1] = y+12+sinf(angle)*radius;
    }
    if (icon == PP_UI_ICON_STAR) {
      /* Nuklear fills convex shapes. Extend each arm into the center pentagon
       * so antialiasing at their internal edges cannot leave radial seams. */
      float center[10];
      for (int arm = 0; arm < 5; arm++) {
        int tip = arm * 2, left = (tip + 9) % 10, right = tip + 1;
        float tx = points[tip*2], ty = points[tip*2+1];
        nk_fill_triangle(canvas, tx, ty,
            tx + 1.25f * (points[left*2] - tx),
            ty + 1.25f * (points[left*2+1] - ty),
            tx + 1.25f * (points[right*2] - tx),
            ty + 1.25f * (points[right*2+1] - ty), color);
        center[arm*2] = points[right*2];
        center[arm*2+1] = points[right*2+1];
      }
      nk_fill_polygon(canvas, center, 5, color);
    } else nk_stroke_polygon(canvas,points,10,1.6f,color);
    break;
  }
  case PP_UI_ICON_DELETE:
    LINE(4,6,20,6); LINE(9,3,15,3); RECT(6,6,12,15);
    LINE(10,10,10,17); LINE(14,10,14,17); break;
  case PP_UI_ICON_CLOSE: LINE(5,5,19,19); LINE(19,5,5,19); break;
  case PP_UI_ICON_DRAG:
    for(int i=0;i<3;i++) for(int j=0;j<2;j++)
      nk_fill_circle(canvas,nk_rect(x+8+j*6,y+5+i*6,2,2),color);
    break;
  default: break;
  }
#undef RECT
#undef LINE
}
