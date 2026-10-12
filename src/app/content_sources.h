#ifndef POINTY_CONTENT_SOURCES_INTERNAL_H
#define POINTY_CONTENT_SOURCES_INTERNAL_H
#include "pointy/content.h"
#include "raster_worker.h"
/* Presenter/probe adapter to the shared service. A supplied worker is borrowed;
 * normal app/standalone construction lets the service create it lazily. */
bool pp_content_sources_create_worker(PPContentSources **out,PPAssets *assets,
    PPRasterWorker *worker,uint64_t generation,PPError *error);
PPRasterWorker *pp_content_sources_worker(const PPContentSources *sources);
uint64_t pp_content_sources_generation(const PPContentSources *sources);
void pp_content_sources_detach_worker(PPContentSources *sources);
PPRasterKey pp_content_target(const PPContentConsumer *consumer);
bool pp_content_requested(const PPContentConsumer *consumer);
bool pp_content_retry(const PPContentConsumer *consumer);
bool pp_content_matches(const PPContentConsumer *consumer,PPItemSnapshot *snapshot,double longest,double density);
bool pp_content_error(PPContentConsumer *consumer,PPError *error);
bool pp_content_capture(PPItemSnapshot *snapshot,double longest,double density,
    PPRasterCapture *capture,size_t *limit,PPError *error);
#endif
