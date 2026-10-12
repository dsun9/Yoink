# Scalable text retention and worker ownership

[Ticket #8](https://github.com/dsun9/Yoink/issues/8) keeps a sprite's canonical
layout separate from its current raster. Text shaping uses the canonical font
size or the selected bitmap strike. Destination outlines and supported color
layers use the requested resolution. A raster carries canonical layout size
and origin, physical raster origin, density, artwork class and owned RGBA plus
source alpha. The source rectangle maps those texels back into the canonical
layout. Native bounds include overhang without changing the pose center or
logical aspect.

On Windows, scalable and mixed text use retained GL presentation. Resize input
immediately requests the latest endpoint. Fractional easing, rotation, opacity
and translation use the available texture. A completed endpoint uploads and
publishes with the current pose on the next owner update, including while
easing continues. There is no debounce or 200-ms publication delay. Static
updates and translation alone do not draw, upload or regenerate content.

The available source keeps its own canonical mapping independently of an
explicit compatibility raster. Equal immutable artwork/target consumers share
one CPU endpoint; each native/UI context retains suitable GPU storage. A candidate overlaps
the previous usable source until upload succeeds. Source failure preserves the
usable source and records a notice. A broken native context removes the failed
sprite and releases input, textures, CPU state and reservations.

## Service, cancellation and limits

The app owns one content service across project/runtime/UI changes. A standalone
presenter owns its service. Worker requests carry stable content-entry IDs and
service generation/serial; consumer targets separately retain immutable snapshot
revision, endpoint size and drawable density. `pp_content_sources_poll` is the
sole bounded dispatcher. Results must still match the live consumer before GPU
publication. Replacement, deletion, external size changes, density
changes, display removal and presenter destruction cancel obsolete work.

The owner deep-copies text, the requested family, every style value, complete
font bytes and face indices. Worker callbacks own their private asset manager,
fonts, streams, text engine, text objects and destination FreeType libraries.
They do not create model snapshots or change the shared asset manager. The
checked SDL_ttf generated-copy patch locks shared-library face destruction with
the same upstream mutex used for face creation. The service keeps an SDL_ttf
initialization reference until its worker finishes.

The queue admits one executing job and one waiting job. Repeated requests for
one sprite replace its waiting job and mark its executing job obsolete. A full
waiting slot for another sprite defers presenter admission while preserving
available artwork, including a newly pulled sprite. The presenter stores only
the latest target key and retries when the slot is available. It copies no new
capture and reserves no candidate during deferral. The worker request API still
rejects unavailable admission as a resource error. An executing generator finishes with owned data after
cancellation; it never reads a retired presenter or project. Its output is
discarded and its successful source work still counts. Cancellation does not
join during project replacement. Final runtime shutdown releases native
interception first, cancels work, joins the worker and frees its resources.

Completion notification carries no model or presenter pointer. On Windows the
worker posts WM_NULL to the owner thread as well as its SDL event. This wakes
the tray's MsgWaitForMultipleObjectsEx even when SDL_WaitEvent has not armed an
SDL wake window. Queue/status synchronization ends before either external wake
call. The headless production probe first failed to wake within five seconds,
then passed after the native signal was added.

The limits are explicit:

- Retained CPU rasters and admitted candidates share the existing 512 MiB asset
  budget, including snapshots, asset caches and native compatibility copies.
  Each candidate is at most 64 MiB. The owner reserves it before dispatch and
  releases or transfers its exact charge once. The conservative reservation
  includes 32 physical pixels on each edge for destination hinting. A larger
  glyph union rejects before the final frame allocation.
- Native/UI content sources and UI output targets share the 256 MiB app limit,
  including old/candidate storage. Equal CPU entries are charged once and
  compatible native GIF textures share their source charge. See
  [shared ownership](../architecture/rendering-content.md).
- Each immutable capture is at most 32 MiB and all captures together are at
  most 64 MiB. Each copied font is at most 16 MiB; text is at most 64 KiB and
  the requested family is at most 4 KiB. A private worker import copies only
  the active capture's font bytes.
- Worker pixel scratch has its own 384 MiB policy, independent of retained CPU
  reservations. Bounded text limits cumulative shaped glyph RGBA bounds to
  16 MiB and each canonical/destination face's glyph buffer to 32 MiB before
  destination rendering. The scratch allowance includes face buffers, shaped
  glyph images, color conversion buffers, private font bytes and a cancelled
  candidate. Library object bookkeeping is constrained by the bounded source
  and operation inputs rather than charged to the retained raster budget.

The policy deliberately permits temporary softness while a sharp endpoint
prepares. It retains one usable endpoint per sprite; it does not retain an
unbounded ladder of historical sizes. Resource rejection preserves the last
usable pose and also resets the interaction's rejected wheel target, so the
next accepted wheel starts from that pose.

`raster_worker.h` is generator-independent. A callback receives an owned
capture and candidate limit, and returns an owned frame with canonical
geometry, error and private source-metric delta. SVG can use these queues,
tags, reservations and cancellation without importing or rendering an
intrinsic-size image first. A new generator must enforce the same scratch
policy and return owned pixels. The service validates returned geometry and
candidate bytes before making a completion available.

## Deterministic checks

The existing presenter test controls the production font/content boundary to
hold a real worker request. It verifies immediate repeated wheel requests,
publication before easing settles, twenty-target coalescing, resize during
drag, grabbed-point preservation at a density change, stale completed target
rejection, replacement, deletion, combined CPU pressure, failed generation,
failed GL publication and project teardown before a held capture completes.
It checks source/upload/draw/readback reuse and exact reservation cleanup.

Scalable color Twemoji, mixed original overhang outlines with Noto clusters,
and an original hybrid outline/bitmap face match a fresh requested-resolution
render byte-for-byte in RGBA and source coverage at 768 DIP and 150% density.
Canonical layout, origin, padding, baseline and pose remain stable. Rotation,
opacity and translation reuse the source. The font lifecycle check executes
96 synchronized renders across three threads with separate managers and font
objects. The bounded-scratch check rejects a small requested output whose
canonical glyph work exceeds its scratch policy.

Probe stage accumulators are thread-local. The headless
`performance_worker_isolation` check renders through the instrumented worker
and verifies that owner stage observations do not change. Synchronized worker
status exposes successful source builds, including discarded successes, plus
preparation time, pending jobs, candidate reservations and retained cache bytes.
Service request/completion/source counters cover its runtime lifetime across
project generations. Owner-rejected completion counts and native/sharp counters
cover the presenter lifetime. The complete-path probe combines owner and private
worker source counts.

## Real GL quality evidence

The supervised Windows `scalable_quality` check passed on 2026-10-11 in
0.74 seconds, then passed its final regression in 0.61 seconds. It compares the entire worker export with a fresh GL export in
RGBA and coverage at zero and 17.25-degree rotation for scalable color,
mixed clusters/multiline/overhang and hybrid outline/bitmap artwork. An
independent bilinear reference in canonical coordinates checks every output
pixel with opacity 0.63. Its accepted maximum channel difference was 1.

The initial diagnostic deliberately using source-pixel aspect failed canonical
extent. After mapped export was implemented, the strict hybrid fractional
comparison failed at a source boundary. The independent double coordinate was
v=-1.154075697e-8, while GLSL float rounded it to zero and sampled the partial
border. That failed observation is preserved. The final test retains the
two-unit channel tolerance for other pixels. Within two float epsilons at
1.0, a boundary pixel must match either a whole transparent pixel or the
independently sampled premultiplied border vector. No case may contain more
than four such pixels. The observed maximum was two; the raw hybrid boundary
difference of 35 is reported separately. Full worker/fresh GL export equality
still includes every boundary pixel.

This verifies source quality, mapping, shader output and explicit export
readback. It does not measure visible compositor cadence. Normal retained
presentation performs no transformed CPU readback. The quality supervisor
reported released input, restored cursor and matching saved/actual foreground
HWND 9903110. Earlier failing runs and their recovery records remain in the
outside-worktree evidence directory.

## Timing observations and platform scope

The focused scalable matrix retains the standard size/density/rotation ladder,
three warmups and twenty samples. It uses the retained sprite path for
scalable/mixed text and includes private source preparation before warm samples.
The full 96-case command is unchanged. `-ScalableRetained` runs the default
Segoe subset and native sharp phase, enforcing its focused 16.7/50/200-ms
targets. `-ScalableSharp` separately prewarms the actual 1536-DIP, 150% source
and context before twenty tagged fractional Alt-wheel inputs. It cannot replace
the warm matrix or the full acceptance command.

The first focused matrix failed and stopped after 1/16 cases and 20/320 warm
samples. The first 96-DIP, 100%, zero-rotation case had warm p95 16.815 ms,
one private source build, two source uploads including the sharp replacement,
and zero readbacks. The second case's cold presentation took 152.312 ms,
including 123.084 ms in the instrumented transform/completion boundary. A tagged
input wait of 140 ms exceeded the 100-ms protective stop. No largest-envelope
or native sharp samples completed in that matrix. The supervisor restored input,
cursor and saved/actual foreground HWND 525984. This is a failing cold-context
and incomplete timing observation, not a successful performance result.

A headless local default-Segoe worker diagnostic at 1536 DIP and 150% completed
one source build in 98.805 ms. It excludes native publication and does not prove
the sharp deadline.

The first separate native sharp run completed twenty wheel observations with
maximum completed publication 167.327 ms. Its reported zero input wait covered
the wheel's own arrival; it had no recurring tagged movement during generation
and cannot establish worker responsiveness. A strengthened synchronous run with
movement during pending work failed before its first observation at 172 ms input
wait. Helper-only wheel injection then confirmed that injection itself returned
within a few milliseconds while the owner update stalled. All failures and their
exact supervisor recovery records remain separate.

Phase attribution found a first resized cached draw's post-swap glFinish wait
of 180.164 ms, with shader execution 0.014 ms, native resize 0.813 ms and swap 0.022 ms.
Requesting direct-context swap interval 0 succeeded from 1 to 0 but left a 167.986 ms
finish wait. A cooperative post-swap fence wait with a requested one-millisecond
timeout blocked 191.793 ms in its flush operation. A fence placed before swap
signaled in 0.436 ms; the residual required glFinish still blocked 170.782 ms.
These unsuccessful production experiments were reverted. The strict complete
path commands retain GPU completion after every measured draw. Their failed
observations remain preserved; the final matrix was not rerun after the error
policy change below.

`-ScalableWorkerInput` is a separate production-loop observation. It uses normal
retained draws during easing, requires a tagged movement while each job is
pending, and measures actual glFinish after the final matching sharp publication
and returned swap. It retains the 50 ms arrival and 200 ms completed-sharp limits,
but does not measure every easing draw's completed GPU cost or replace either
the warm matrix or 96-case acceptance. Its first run failed before any sample:
normal draw state took 150.594 ms with no synchronous completion, readback or
timer-query wait, and tagged movement arrived 141 ms late. Exact saved/actual
foreground HWND 67688 and input/cursor restoration passed. Further attribution
isolated GetUniformLocation at 158.754 ms. Caching eight uniform locations once
after program linking removed that hot query, but the wait moved to glGetError
at 151.983 ms. Those failed observations remain separate.

Direct Windows renderers now optionally request a debug context. Creation falls
back to the previous plain context if that request fails. The renderer uses a
synchronous error callback only after verifying the actual debug flag,
GL_KHR_debug and both callback/control entry points. Its error state belongs to
that renderer and context. The callback runs on the owning thread before the
generating GL call returns, and destruction unregisters it before freeing that
state. Unsupported contexts keep glGetError. Transactional source uploads and
explicit completion still query errors. This preserves failure handling without
the supported direct draw's hot synchronous query. The
[KHR_debug specification](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt)
requires a debug context for guaranteed messages and defines synchronous
callback ownership in sections 5.5 and 5.5.7.

The final native PNG presenter regression generated a real GL_INVALID_ENUM
while a sprite owned an accepted press. The resulting draw failure removed that
sprite, released its GPU source and capture, and allowed the next native click
through to the receiver. The PNG/error and scalable quality checks passed two
of two in 7.86 seconds. Their supervisor restored input, cursor and exact
original/actual foreground HWND 525984.

The final separate production-loop worker-input check passed twenty of twenty
samples at actual 1536 DIP and 150%, using Segoe UI Emoji 1.70 and the verified
callback policy. Every sample observed five to seven tagged movements while
its job was pending. Maximum pending-worker arrival was 16 ms, all-tagged
arrival 32 ms and completed matching sharp publication 183.397 ms. Maximum
owner update was 32.307 ms; final sharp completion was at most 0.743 ms. Initial
private preparation was 99.798 ms. The supervisor restored input, cursor and
exact original/actual HWND 67688. This passes the worker-input and completed
sharp observation's unchanged 50/200 limits. It does not establish the complete
warm rendering gate, full 96-case acceptance or visible compositor cadence.

The state-attribution run saved a foreground window that disappeared during
the run. Input/cursor restoration passed, but foreground restoration was
unavailable and the supervisor correctly failed. The coordinator later restored
the previously verified Codex window and confirmed its exact HWND. Two other
attempts refused held desktop input before starting a test. All such guard and
recovery outcomes remain recorded, alongside the successful final run.

On completion the owner publishes the new source at the current displayed pose
before advancing easing. The same update can draw again at the advanced pose.
Tags and source ownership remain valid, but this duplicate publication is a
separate pacing concern for the final performance ticket. The measured first
stall above occurred in one cached draw before the worker completion, so that
duplicate draw did not cause the observed initial wait.

The shared service and deterministic font/presenter checks compile for the
supported platforms. The mocked presenter explicitly enables the retained
boundary in its test build. The macOS product adapter keeps its existing
compatibility presentation path; it does not dispatch retained text workers
until a direct surface exists. This ticket has no executed macOS native
acceptance, physical mixed-display or fullscreen evidence.
