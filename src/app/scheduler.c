#include "scheduler.h"
double pp_next_wake(const PPSchedule *s, double now) {
  double next = fmin(fmin(s->pointer, s->easing), fmin(s->fade, s->gif));
  return fmax(now, fmin(next, fmin(s->ui, s->command)));
}
double pp_schedule_tick(double now, double end) {
  return now < end ? fmin(now + 1.0 / 60, end) : HUGE_VAL;
}
