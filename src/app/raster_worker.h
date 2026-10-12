#ifndef POINTY_RASTER_WORKER_H
#define POINTY_RASTER_WORKER_H
#include "pointy/text.h"

/* All APIs except the generator callback run on the owning presenter thread.
 * Captures contain owned values only. The worker never touches a snapshot,
 * project, owner asset manager, reservation or GL object. */
typedef struct PPRasterWorker PPRasterWorker;
#define PP_WORKER_CANDIDATE_LIMIT (64u * 1024 * 1024)
#define PP_WORKER_SCRATCH_LIMIT (384u * 1024 * 1024)
typedef struct PPRasterKey {
  uint64_t generation, instance_id, content_revision, serial;
  double longest_dip, density;
} PPRasterKey;
typedef bool (*PPRasterGenerate)(void *capture, size_t limit,
    PPTextResult *result, PPSourceMetrics *delta, PPError *error);
typedef struct PPRasterCapture {
  void *data;
  size_t bytes;
  PPRasterGenerate generate;
  void (*destroy)(void *capture);
} PPRasterCapture;
typedef struct PPRasterCompletion {
  PPRasterKey key;
  PPTextResult result;
  PPError error;
  PPAssets *owner_assets;
  size_t reserved_bytes;
  bool success;
} PPRasterCompletion;
typedef struct PPRasterStatus {
  uint64_t requests, completions, stale_results, source_builds;
  size_t pending_jobs, reserved_bytes, capture_bytes;
  double source_ms, source_max_ms;
} PPRasterStatus;
/* One executing job and at most one waiting job. Captures, including font
 * bytes, are capped at 32 MiB each, 64 MiB total. Generation scratch has one
 * active generator. Candidates have a 64 MiB cap. Text bounds intermediate
 * pixel/font buffers separately at 384 MiB; additional generators must enforce
 * this scratch policy before allocation. */
bool pp_raster_worker_create(PPRasterWorker **out, PPError *error);
/* Takes capture ownership even on rejection. Reserves the candidate on owner
 * assets before making it available to the worker. New same-instance work
 * cancels older work; a full queue for another instance rejects the request. */
bool pp_raster_worker_request(PPRasterWorker *worker, PPRasterKey key,
    PPRasterCapture capture, PPAssets *assets, size_t candidate_limit, PPError *error);
void pp_raster_worker_cancel(PPRasterWorker *worker, uint64_t generation,
    uint64_t instance_id); /* instance zero cancels the entire generation. */
bool pp_raster_worker_poll(PPRasterWorker *worker, uint64_t generation,
    PPRasterCompletion *out);
PPRasterStatus pp_raster_worker_status(const PPRasterWorker *worker);
/* Release on the owner, or take its exact reservation into a retained cache. */
void pp_raster_completion_release(PPRasterCompletion *completion);
size_t pp_raster_completion_take(PPRasterCompletion *completion);
/* Final shutdown: cancel first, release native interception, then join. A
 * runtime keeps this service across project replacement, which never joins. */
void pp_raster_worker_destroy(PPRasterWorker *worker);
#endif
