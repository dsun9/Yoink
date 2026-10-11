#ifndef POINTY_PRESENTATION_METRICS_H
#define POINTY_PRESENTATION_METRICS_H
#include <stdint.h>
/* Successful native publications and resize endpoint publications. A sharp
 * observation belongs to the latest accepted resize input, immutable snapshot
 * revision and drawable density. The caller timestamps its observation after
 * GPU completion; these counters alone do not establish compositor visibility. */
typedef struct PPPresentationMetrics {
    uint64_t native_publications, sharp_publications;
    uint64_t last_sharp_instance_id, last_sharp_content_revision;
    double last_sharp_input_timestamp, last_sharp_longest_dip;
    double last_sharp_drawable_density;
} PPPresentationMetrics;
#endif
