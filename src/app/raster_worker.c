#include "raster_worker.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct RasterJob {
  PPRasterCompletion completion;
  PPRasterCapture capture;
  bool running, done, cancelled;
  struct RasterJob *next;
} RasterJob;
struct PPRasterWorker {
  SDL_Mutex *mutex;
  SDL_Condition *wake;
  SDL_Thread *thread;
  RasterJob *jobs;
  PPRasterStatus status;
  bool stop;
  Uint32 wake_type;
};
static bool failure(PPError *error, int code, const char *message) {
  if(error) {error->code=code;snprintf(error->message,sizeof(error->message),"%s",message);}return false;
}
void pp_raster_completion_release(PPRasterCompletion *c) {
  pp_frame_destroy(&c->result.frame);
  pp_assets_release_raster(c->owner_assets,c->reserved_bytes);
  *c=(PPRasterCompletion){0};
}
size_t pp_raster_completion_take(PPRasterCompletion *c) {
  size_t exact=c->result.frame.stride*(size_t)c->result.frame.height+
    (size_t)c->result.frame.width*c->result.frame.height;
  pp_assets_release_raster(c->owner_assets,c->reserved_bytes-exact);
  c->reserved_bytes=0;c->owner_assets=NULL;return exact;
}
static void release_budget(PPRasterWorker *w,RasterJob *j) {
  w->status.reserved_bytes-=j->completion.reserved_bytes;
  pp_assets_release_raster(j->completion.owner_assets,j->completion.reserved_bytes);
  j->completion.owner_assets=NULL;j->completion.reserved_bytes=0;
}
static void free_job(PPRasterWorker *w,RasterJob *j) {
  w->status.capture_bytes-=j->capture.bytes;
  j->capture.destroy(j->capture.data);pp_frame_destroy(&j->completion.result.frame);free(j);
}
static int SDLCALL run(void *data) {
  PPRasterWorker *w=data;
  bool fonts_initialized=TTF_Init();
  SDL_LockMutex(w->mutex);
  for(;;) {
    RasterJob *job=NULL;
    for(RasterJob *j=w->jobs;j;j=j->next) if(!j->done&&!j->running&&!j->cancelled) {job=j;break;}
    if(!job) {if(w->stop) break;SDL_WaitCondition(w->wake,w->mutex);continue;}
    job->running=true;size_t limit=job->completion.reserved_bytes;
    SDL_UnlockMutex(w->mutex);
    PPSourceMetrics delta={0};
    Uint64 start=SDL_GetPerformanceCounter();
    if(fonts_initialized)job->completion.success=job->capture.generate(job->capture.data,limit,&job->completion.result,&delta,&job->completion.error);
    else failure(&job->completion.error,PP_ERROR_RENDER,"Could not initialize worker font library");
    double ms=(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
    SDL_LockMutex(w->mutex);
    job->done=true;w->status.pending_jobs--;w->status.completions++;w->status.source_builds+=delta.raster_builds;
    w->status.source_ms+=ms;if(ms>w->status.source_max_ms)w->status.source_max_ms=ms;
    if(job->cancelled) {
      if(job->completion.success) w->status.stale_results++;
      RasterJob **link=&w->jobs;while(*link!=job)link=&(*link)->next;*link=job->next;free_job(w,job);
    }
    /* Wake the event loop without any model or presenter pointer payload. */
    SDL_Event event={0};event.type=w->wake_type;if(w->wake_type!=(Uint32)-1)SDL_PushEvent(&event);
  }
  SDL_UnlockMutex(w->mutex);if(fonts_initialized)TTF_Quit();return 0;
}
bool pp_raster_worker_create(PPRasterWorker **out,PPError *error) {
  *out=NULL;PPRasterWorker *w=calloc(1,sizeof(*w));
  if(!w)return failure(error,PP_ERROR_MEMORY,"Cannot allocate raster worker");
  w->mutex=SDL_CreateMutex();w->wake=SDL_CreateCondition();
  w->wake_type=SDL_RegisterEvents(1);
  if(w->mutex&&w->wake)w->thread=SDL_CreateThread(run,"pointy-raster",w);
  if(!w->thread) {SDL_DestroyCondition(w->wake);SDL_DestroyMutex(w->mutex);free(w);return failure(error,PP_ERROR_MEMORY,"Cannot start raster worker");}
  *out=w;return true;
}
static void cancel_locked(PPRasterWorker *w,uint64_t generation,uint64_t instance) {
  RasterJob **link=&w->jobs;
  while(*link) {
    RasterJob *j=*link;
    if((generation&&j->completion.key.generation!=generation) || (instance&&j->completion.key.instance_id!=instance)) {link=&j->next;continue;}
    release_budget(w,j);j->cancelled=true;
    if(j->running&&!j->done) {link=&j->next;continue;}
    if(!j->done)w->status.pending_jobs--;
    else if(j->completion.success)w->status.stale_results++;
    *link=j->next;free_job(w,j);
  }
}
void pp_raster_worker_cancel(PPRasterWorker *w,uint64_t generation,uint64_t instance) {
  if(!w)return;SDL_LockMutex(w->mutex);cancel_locked(w,generation,instance);SDL_UnlockMutex(w->mutex);
}
bool pp_raster_worker_request(PPRasterWorker *w,PPRasterKey key,PPRasterCapture capture,
    PPAssets *assets,size_t limit,PPError *error) {
  if(!w||!capture.data||!capture.generate||!capture.destroy||!limit||capture.bytes>32u*1024*1024) {
    if(capture.destroy)capture.destroy(capture.data);return failure(error,PP_ERROR_RESOURCE_LIMIT,"Raster worker capture exceeds 32 MiB or has invalid arguments");
  }
  SDL_LockMutex(w->mutex);
  cancel_locked(w,key.generation,key.instance_id);
  size_t waiting=0;for(RasterJob *j=w->jobs;j;j=j->next)if(!j->running&&!j->done)waiting++;
  if(w->stop||waiting||w->status.capture_bytes>64u*1024*1024-capture.bytes) {
    SDL_UnlockMutex(w->mutex);capture.destroy(capture.data);return failure(error,PP_ERROR_RESOURCE_LIMIT,"Raster worker queue is full; previous source remains usable");
  }
  RasterJob *job=calloc(1,sizeof(*job));
  if(!job||!pp_assets_reserve_raster(assets,limit,error)) {
    free(job);SDL_UnlockMutex(w->mutex);capture.destroy(capture.data);return false;
  }
  job->completion=(PPRasterCompletion){.key=key,.owner_assets=assets,.reserved_bytes=limit};job->capture=capture;
  RasterJob **tail=&w->jobs;while(*tail)tail=&(*tail)->next;*tail=job;
  w->status.requests++;w->status.pending_jobs++;w->status.reserved_bytes+=limit;w->status.capture_bytes+=capture.bytes;
  SDL_SignalCondition(w->wake);SDL_UnlockMutex(w->mutex);return true;
}
bool pp_raster_worker_poll(PPRasterWorker *w,uint64_t generation,PPRasterCompletion *out) {
  if(!w)return false;SDL_LockMutex(w->mutex);RasterJob **link=&w->jobs;
  while(*link&&(!(*link)->done||(*link)->completion.key.generation!=generation))link=&(*link)->next;
  RasterJob *job=*link;if(!job) {SDL_UnlockMutex(w->mutex);return false;}
  *link=job->next;*out=job->completion;job->completion.result=(PPTextResult){0};
  w->status.reserved_bytes-=out->reserved_bytes;free_job(w,job);SDL_UnlockMutex(w->mutex);return true;
}
PPRasterStatus pp_raster_worker_status(const PPRasterWorker *worker) {
  if(!worker)return(PPRasterStatus){0};PPRasterWorker *w=(PPRasterWorker *)worker;
  SDL_LockMutex(w->mutex);PPRasterStatus status=w->status;SDL_UnlockMutex(w->mutex);return status;
}
void pp_raster_worker_destroy(PPRasterWorker *w) {
  if(!w)return;SDL_LockMutex(w->mutex);w->stop=true;
  cancel_locked(w,0,0);
  SDL_SignalCondition(w->wake);SDL_UnlockMutex(w->mutex);SDL_WaitThread(w->thread,NULL);
  SDL_DestroyCondition(w->wake);SDL_DestroyMutex(w->mutex);free(w);
}
