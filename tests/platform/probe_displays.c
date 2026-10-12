/* Production physical enumeration, with explicitly reported drawable-density
 * simulation for the acceptance ladder. Monitor bounds stay physical; both
 * density ratios change together so native and GL extents remain coherent. */
#include "pointy/platform.h"
uint64_t pp_probe_density_display;
double pp_probe_drawable_density;
#define pp_platform_displays pp_raw_platform_displays
#include "../../src/platform/windows/displays.c"
#undef pp_platform_displays
bool pp_platform_displays(PPDisplay **out,size_t *count,PPError *error) {
  if(!pp_raw_platform_displays(out,count,error))return false;
  for(size_t i=0;i<*count;i++)if((*out)[i].id==pp_probe_density_display&&pp_probe_drawable_density>0) {
    (*out)[i].drawable_pixels_per_dip=pp_probe_drawable_density;
    (*out)[i].desktop_units_per_dip=pp_probe_drawable_density;
  }
  return true;
}
