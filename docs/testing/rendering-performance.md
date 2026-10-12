# Bounded rendering performance acceptance

This is the repeatable Windows acceptance protocol for [issue #1](https://github.com/dsun9/Yoink/issues/1) and [ticket #3](https://github.com/dsun9/Yoink/issues/3). It uses the production presenter, content pipeline, OpenGL renderer, native surface, and input hook. The initial implementation is expected to fail until the rendering changes meet the budgets. A successful source-only diagnostic is insufficient.

Build the Release probe, then run the tracked supervisor from the repository root:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target pointy_performance_test --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Performance -Configuration Release -BuildDirectory build/windows-release -LogPrefix rendering
```

The last command returns the probe's exit code. Its normal hard timeout is 180 seconds. The probe stops scheduling work after 145 seconds, leaving time for input and process cleanup. The supervisor terminates the owned process tree on timeout, releases injected input, restores the cursor and original foreground window, and fails if restoration fails. It refuses an already held input state. Run desktop commands exclusively; the acceptance runner also holds a named desktop mutex. CTest's native tests remain serialized. The probe is deliberately opt-in and is not a hosted CI timing test.

The log and stderr are `build/rendering-desktop.log` and `build/rendering-desktop-stderr.log`. Use `-BuildDirectory` for an isolated checkout's build. A Debug build cannot satisfy acceptance. Do not compare its timings with the Release gate.

## Repeated protocol and required envelope

The matrix has 96 required cases. It includes the default emoji, explicit bundled bitmap Noto, PNG, GIF, SVG, and mixed text. Each source runs at 96, 384, 768, and 1536 DIP, drawable densities 1.0 and 1.5, and rotations 0 and 17.25 degrees. These are requested drawable densities in the presenter; they do not establish physical mixed-display behavior.

Each case records its first presentation separately, discards three warmup presentations, then records 20 complete warm samples. The warm poses include a small fractional size and position change. GIF samples advance an independent instance clock. The source snapshot's canonical generation and the renderer's cold creation are separate records. Every case must provide all 20 samples and tagged native input observations. An unsupported source, allocation failure, or partial case fails completeness.

The complete warm gate is the nearest-rank p95 of 20 samples, the nineteenth sorted sample. The required envelope is the default Segoe UI Emoji sprite at 1536 DIP and 150% drawable density. Both listed rotations must fit 16.7 ms. Other source cases report their own median, p95, and maximum; their worst p95 is separate from the required envelope and does not impose a universal 60 Hz guarantee on all content. The selected default font's actual family and version are reported. A Noto fallback is usable diagnostic content but cannot satisfy the required Segoe case.

The input gate is a maximum of 50 ms from the tagged native event's injection timestamp to the low-level hook's arrival. An injector sends bounded alternating one-pixel movement every 10 ms during the matrix. Only tagged probe input establishes the observation; ordinary pointer activity cannot substitute for it. After a fast case, the probe pumps for at most 50 ms to obtain an input observation. This pumping is outside the frame samples.

The sharp gate uses 20 real tagged Alt-wheel inputs on a visible default sprite through the production presenter event/update path. Wheel samples alternate fractional enlargement and reduction. Each sample requires a sharp publication matching that sprite's latest endpoint, immutable content revision, and drawable density. The maximum time from sending the latest wheel input through GPU completion and the matching native publication must be at most 200 ms. Easing intermediates do not count as sharp publications. If native routing produces no matching resize observation, acceptance fails. A controlled request at the portable presenter seam proves ownership and endpoint behavior, but does not substitute for this native observation.

## Timing boundaries

The log separates these costs:

- Source snapshot creation and first-use presentation are cold observations. Font lookup and source raster generation appear in their own production-source stages.
- `source_request` includes raster generation or cached source acquisition for the requested pose. It is part of the complete frame time.
- `cpu_draw_submission` is the CPU duration of submitting the GL draw. It cannot establish completion.
- `gpu_execution` comes from a GL elapsed-time query around the transform draw. It excludes CPU generation and native presentation. Unsupported GPU timer queries leave required GPU observations missing and fail acceptance.
- `cpu_wait_gpu_completion` explicitly finishes GL work. Its CPU wait is included in the complete frame. The existing compatibility path also performs synchronous readbacks, which have their own records. A short wait after readback means the readback already paid the synchronization cost.
- `native_publication` covers the native presentation API, including the layered-window bitmap copy in the current compatibility path.
- `complete` starts before the production presenter requests its source and ends after GL completion and the native publication call return. It includes generation, upload, transformation, readback where used, and native submission. Stage times overlap and must not be added as if they were independent.

Each case identifies its backend. PNG uses `direct-retained-gl` through `pp_sprite_present`, including the sprite's retained renderer; other sources currently use `layered-gl-readback`. The aggregate presenter renderer counters include active and retired direct contexts as well as the shared compatibility renderer. GPU timer queries are allocated, resolved and deleted in the owning draw context before another sprite context becomes current. Direct and compatibility draws both explicitly complete GPU work in this diagnostic. Submission time alone cannot satisfy acceptance.

Native API completion is not proof of when DWM displayed the pixels, display scanout, visible compositor cadence, fullscreen behavior, or an absence of dropped compositor frames. The matrix uses offscreen native surfaces to measure production costs without covering the desktop for the whole ladder. The sharp phase uses a visible surface to verify native input routing and endpoint publication. The report explicitly marks visible compositor evidence as unobserved. Physical mixed-display and fullscreen checks remain separate. macOS build and native runtime acceptance remain unverified until executed on a Mac.

For a scoped PNG diagnostic, append `-PngRetained` to the supervised `-Performance` command. This runs 16 PNG cases, checks all 320 warm samples, GPU observations and tagged native input observations, and identifies `overall_spec_gate=not-evaluated`. It skips the default-emoji sharp phase and cannot replace the unchanged 96-case acceptance command. See [retained PNG evidence](retained-png.md) for the observation and the still failing full gate.

## Failure and portable observations

The command fails for the required budget breach, input or sharp deadline breach, a frame above the 250 ms protective limit, a tagged input wait above the 100 ms protective limit, a protective stop, a timeout, missing required native/GPU observations, and incomplete cases. Protective limits bound escalation; they do not weaken the 50 ms or 16.7 ms acceptance budgets. Cleanup failure returns a failing supervisor result even if the probe itself passed.

The tracked `performance_runner` CTest invokes synthetic protocol controls through the same launcher before any desktop API calls. It checks child success and failure, each budget breach, protective stop, missing observations, incomplete cases, and a hard timeout. These controls test gate and exit propagation. Their synthetic PASS is not a machine acceptance result.

For later reuse tests, `pp_assets_source_metrics` exposes successful source raster builds. Text/SVG generation and PNG/GIF cache decode increment it; cache hits, GIF frame selection, and rejected requests do not. Immutable `pp_item_snapshot_revision` values identify a snapshot through retained references. A replacement snapshot receives a new process-local revision.

`pp_renderer_metrics` exposes semantic source uploads, transform draws, and readbacks. One source upload counts the content operation, including any associated coverage, rather than a particular number of GL calls. Compatibility and direct draw APIs both report these counters. Translation-only and static reuse checks can compare these counts without depending on texture allocation or shader implementation details.

`pp_anchor_presenter_metrics` and `pp_runtime_presentation_metrics` expose native publication and sharp publication counts, plus the latest matching resize input timestamp, sprite ID, source revision, logical endpoint size, and drawable density. Observing a counter requires the caller to timestamp after explicit completion. Counters do not imply compositor visibility. Presenter counts reset with presenter lifetime. Worker publication must preserve the same latest-input/revision/density contract when asynchronous raster preparation is added.

## Initial Windows observation

On 2026-10-11 the instrumented legacy presenter, before installed Segoe adoption, exited with code 1 after a protective tagged input-arrival stop at 109 ms. The report identified the selected default as Noto Color Emoji 2.051. The required Segoe timing was unavailable. Fifteen of 96 cases completed, with 303 of 1920 warm samples. The fallback's 1536 DIP, 150%, zero-rotation complete path had median 80.741 ms, p95 93.183 ms, and maximum 98.823 ms. Its source-request p95 was 69.207 ms, GPU draw p95 0.027 ms, GPU-readback p95 8.768 ms, and native-publication p95 14.747 ms. The fractional rotated case stopped after three measured samples. Remaining sources and native sharp samples were incomplete and failed acceptance.

This run used the RTX 5090 and NVIDIA GL version `3.2.0 NVIDIA 610.62` on the Windows machine described in [Windows acceptance](windows.md). The supervisor verified released input, restored cursor, and restored the valid original foreground window. No probe process remained. This is failing compatibility-path evidence; it does not measure the new default Segoe envelope, direct GL adoption, or compositor cadence.
