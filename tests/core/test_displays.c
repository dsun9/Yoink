#include "pointy/display.h"
#include "../test.h"
#include <math.h>
static bool near(double a,double b) {return fabs(a-b)<1e-8;}
static void test_anchor_layout_and_recovery(void) {
    PPDisplay displays[]={{1,{0,0,1920,1080},1,1},{2,{-2560,0,2560,1440},1.5,1.5}};
    const PPRect expected[]={{0,0,16,16},{952,0,16,16},{1904,0,16,16},{0,532,16,16},
        {1904,532,16,16},{0,1064,16,16},{952,1064,16,16},{1904,1064,16,16}};
    for(int a=0;a<PP_ANCHOR_COUNT;a++) {
        PPRect r=pp_anchor_bounds(displays,(PPAnchor)a,16);CHECK(near(r.x,expected[a].x)&&near(r.y,expected[a].y));CHECK(r.w==16&&r.h==16);
        r=pp_anchor_bounds(displays+1,(PPAnchor)a,16);CHECK(r.w==24&&r.h==24);CHECK(r.x>=-2560&&r.x+r.w<=0&&r.y>=0&&r.y+r.h<=1440);
    }
    PPRect r=pp_anchor_bounds(displays+1,PP_ANCHOR_TOP_CENTER,16);CHECK(r.x==-1292);
    r=pp_anchor_bounds(displays+1,PP_ANCHOR_CENTER_RIGHT,16);CHECK(r.x==-24&&r.y==708);
    PPVec2 p={-1700,456},local=pp_display_to_local(displays+1,p),back=pp_display_to_desktop(displays+1,local);
    CHECK(near(local.x,573.333333333333)&&near(local.y,304));CHECK(near(back.x,p.x)&&near(back.y,p.y));
    CHECK(pp_display_at(displays,2,(PPVec2){0,100})->id==1);CHECK(pp_display_at(displays,2,(PPVec2){-1,100})->id==2);
    PPDisplay portrait={3,{1920,-800,1080,1920},2,3};
    r=pp_anchor_bounds(&portrait,PP_ANCHOR_BOTTOM_CENTER,16);CHECK(r.x==2444&&r.y==1088&&r.w==32);
    r=pp_anchor_preview_bounds(&portrait,PP_ANCHOR_BOTTOM_RIGHT,96);CHECK(r.x==2808&&r.y==928&&r.w==192);
    p=pp_recover_center(displays,1,(PPVec2){-1000,1300},96);CHECK(p.x==48&&p.y==1032);
    p=pp_recover_center(displays,2,(PPVec2){10,10},96);CHECK(p.x==10&&p.y==10);
    p=pp_recover_center(NULL,0,(PPVec2){7,8},96);CHECK(p.x==7&&p.y==8);
    p=pp_display_drag_center(displays+1,(PPVec2){-100,300},(PPVec2){20,-10});CHECK(p.x==-130&&p.y==315);
    p=pp_display_drag_center(&portrait,(PPVec2){2000,300},(PPVec2){20,-10});CHECK(p.x==1960&&p.y==320);
}
static void test_reveal_transition_fade_and_shared_disable(void) {
    PPDisplay d[]={{1,{0,0,1920,1080},1,1},{2,{-2560,0,2560,1440},1.5,1.5}};PPAnchorRuntime r={0};
    pp_anchors_update(&r,d,2,(PPVec2){3,3},1);CHECK(!r.failed&&r.count==16);CHECK(r.entries[0].opacity==.35);
    pp_anchors_update(&r,d,2,(PPVec2){50,40},1.01);CHECK(r.entries[0].opacity==.35&&!r.entries[0].leaving);
    pp_anchors_update(&r,d,2,(PPVec2){300,300},1.02);CHECK(r.entries[0].opacity==.35);
    pp_anchors_update(&r,d,2,(PPVec2){300,300},1.08);CHECK(near(r.entries[0].opacity,.175));
    pp_anchors_update(&r,d,2,(PPVec2){300,300},1.141);CHECK(!r.entries[0].revealed&&r.entries[0].opacity==0);
    pp_anchors_enable(&r,PP_ANCHOR_TOP_LEFT,false);pp_anchors_update(&r,d,2,(PPVec2){3,3},2);CHECK(!r.entries[0].revealed);
    pp_anchors_update(&r,d,2,(PPVec2){-2550,10},2.1);CHECK(!r.entries[8].revealed);
    pp_anchors_enable(&r,PP_ANCHOR_TOP_LEFT,true);pp_anchors_update(&r,d,2,(PPVec2){3,3},3);r.entries[0].held=true;
    pp_anchors_update(&r,d,2,(PPVec2){300,300},4);CHECK(r.entries[0].opacity==.35);
    pp_anchors_update(&r,d+1,1,(PPVec2){300,300},5);CHECK(r.count==8&&r.entries[0].display_id==2);
    pp_anchors_destroy(&r);CHECK(!r.entries&&r.count==0);
}
static void test_same_id_geometry_invalidation(void) {
    PPDisplay original={1,{0,0,1920,1080},1,1},changed=original;
    CHECK(pp_displays_equal(&original,1,&changed,1));
    changed.bounds.x=200;CHECK(!pp_displays_equal(&original,1,&changed,1));
    changed=original;changed.bounds.w=1280;CHECK(!pp_displays_equal(&original,1,&changed,1));
    changed=original;changed.desktop_units_per_dip=1.5;CHECK(!pp_displays_equal(&original,1,&changed,1));
    changed=original;changed.drawable_pixels_per_dip=2;CHECK(!pp_displays_equal(&original,1,&changed,1));
    changed=original;changed.id=2;CHECK(!pp_displays_equal(&original,1,&changed,1));
    CHECK(!pp_displays_equal(&original,1,NULL,0));CHECK(pp_displays_equal(NULL,0,NULL,0));
}
int main(void) {test_anchor_layout_and_recovery();test_reveal_transition_fade_and_shared_disable();test_same_id_geometry_invalidation();puts("PASS display layout, native/logical conversion, recovery, scale grab, reveal fade and shared enable mask");return 0;}
