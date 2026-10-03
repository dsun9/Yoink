#include "pointy/interaction.h"
#include <math.h>
double pp_ease_value(double start,double target,double elapsed,double duration) {
    if(duration<=0) return target;
    if(elapsed<=0) return start;
    if(elapsed>=duration) return target;
    double left=1-elapsed/duration;return start+(target-start)*(1-left*left*left);
}
