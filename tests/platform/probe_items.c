#include "performance_probe.h"
#define pp_item_snapshot_frame pp_raw_item_snapshot_frame
#include "../../src/core/items.c"
#undef pp_item_snapshot_frame
bool pp_item_snapshot_frame(PPItemSnapshot *snapshot,double elapsed,double scale,
    const PPFrame **out,double *next,PPError *error) {
  double start=pp_probe_clock();
  bool ok=pp_raw_item_snapshot_frame(snapshot,elapsed,scale,out,next,error);
  pp_probe_record(PROBE_SOURCE_REQUEST,start); return ok;
}
