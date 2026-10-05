#ifndef POINTY_SCHEDULER_H
#define POINTY_SCHEDULER_H
#include <math.h>
typedef struct PPSchedule {
  double pointer, easing, fade, gif, ui, command;
} PPSchedule;
/* Absolute monotonic deadlines; HUGE_VAL means no scheduled work. */
double pp_next_wake(const PPSchedule *schedule, double now);
double pp_schedule_tick(double now, double end);
#endif
