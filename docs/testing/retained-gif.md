# Retained GIF presentation

GIF sprites on Windows use native-resolution decoded canvases and direct transparent GL windows. The decoded asset remains immutable and shared. `pp_asset_select_frame` and `pp_item_snapshot_select_frame` return the selected frame index, revision `index + 1`, and next elapsed-time deadline. Selection depends only on the caller's clock. Looping back to a frame reuses its revision; finite playback keeps the final revision with an infinite deadline.

Ticket #10 shares this CPU policy with anchors, Settings and drawer. Each
consumer keeps its own clock; drawer samples frame zero. UI contexts remain
independent and never borrow native GIF GLuint names. Native/UI content sources
and UI output targets share the app's 256 MiB GPU ledger; shared entries charge
once and release only after their last live consumer. See
[preview verification](preview-retention.md).

Each presenter owns an explicit GL context group. Its hidden keeper context owns immutable RGBA textures, keyed by the full asset digest and selected revision. Direct sprite contexts join this group. Two sprites displaying the same source frame share one texture, while retaining separate clocks, poses, windows, draw state, and replacement ownership. PNG and bitmap-text textures remain owned by their sprite renderers within the compatible group.

An immutable cache miss uploads once and completes producer work before publication. Changing a consumer binding completes its previous use before dropping the old frame reference. A renderer destructor releases its bindings before the last shared reference can delete the texture. These completion waits establish ownership correctness; this ticket makes no claim that they remove swap pacing costs. Context-specific VAOs, framebuffers and timer queries remain separate.

The existing decoded-raster limits remain 256 MiB per resource, 512 MiB combined retained CPU rasters, 8192 pixels per axis and 4096 GIF frames. Unique retained GPU RGBA sources have a separate 256 MiB limit. Old and candidate sources overlap in this charge. The cache uploads selected frames on demand and immediately deletes entries with no sprite references. Deletion, replacement and scene clear release those references; presenter destruction then releases the keeper. Driver context and compositor allocations are outside the reported source-texture budget.

A rejected candidate preserves the accepted snapshot, pose, animation start and future GIF deadlines. A failed native or GL context closes its failed interceptor and releases capture while other sprites continue. A completely transparent frame hides its native window, releases accepted input, and retains its next frame deadline. Later visible frames become interactive again. Visible GIF input uses the window rectangle, including transparent interiors. The macOS compatibility branch follows the same visibility and rectangle policy, with native verification still pending.

## Deterministic checks

The source, scheduler, scene and presenter checks use the existing production content and presenter boundaries. The tests prove disposal compositing, finite-loop stopping, explicit revisions, independent starts, unchanged-frame and position-only reuse, unique GPU charging, candidate overlap, reference cleanup, PNG/GIF ownership transitions, rejected replacement clock recovery and failure isolation.

The preserved implementation first failed because the explicit selection API was absent and the compatibility path repeated uploads and readbacks. Fresh failure slices then showed a producer context failure aborting the presenter update, a rejected replacement losing the accepted frame deadline, and a native mock that did not cancel capture when placement became noninteractive. The fixes passed their focused tests. After the bitmap checkpoint and compatibility follow-up, the final Release source/scheduler/scene/text/bitmap/presenter/runtime checks passed 7/7 in 5.29 seconds. The production application, performance probe and GIF receiver also built successfully.

## Windows native evidence

The production GIF receiver extends the existing compatibility GIF test. It verifies actual composited frames at independent starts 1.00 and 1.06 with an observation at 1.08. Three sprites need two source uploads, three draws and 24 GPU bytes. Two later-start sprites share their red frame while the earlier sprite displays green. Deleting one sharing consumer preserves the other's pixels. Native checks cover first clicks in transparent rectangle interiors, outside input, overlapping window order, nonactivation, stationary-pointer transparent-frame changes, held-press cleanup, PNG/GIF ownership transitions, finite stopping and a zero-byte scene clear.

The first supervised run passed PNG and direct GL but failed the GIF color assertion. It expected pure green at the center of an even-width drawable and observed `(4, 255, 4)`. The corrected expectation accounts for the half destination pixel offset and linear interpolation between the known middle green texel and its transparent neighbor. The assertion retains its three-channel tolerance of 3. This changed the independent expected sample, without changing rendering behavior or suppressing the failed observation.

The corrected GIF-only native test passed in 6.26 seconds. The final supervised PNG/direct GL/GIF suite passed 3/3 in 21.30 seconds. The driver was NVIDIA GeForce RTX 5090, NVIDIA GL `3.2.0 NVIDIA 610.62`, with physical drawable density 1.50. Every supervisor reported released input, restored cursor and restored valid original foreground window. No test process remained.

Evidence outside the repository is retained under `C:/Users/Reon/.codex/scratch/yoink-issue-1/issue-6-evidence/`. It includes the first failure, corrected receiver pixels, final native suite, deterministic red/green logs and scoped performance output. Native macOS, physical mixed-display movement and fullscreen behavior were not run for this ticket.

## Scoped performance diagnostic

Build the Release probe and run its existing supervisor exclusively:

```powershell
cmake --build --preset windows-release --target pointy_performance_test --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Performance -GifRetained -Configuration Release -BuildDirectory build/windows-release -LogPrefix gif-retained
```

This diagnostic covers GIF at 96, 384, 768 and 1536 DIP, requested drawable densities 1.0 and 1.5, and rotations 0 and 17.25 degrees. Each case records cold presentation, discards three warmups and observes 20 complete warm samples. GIF time advances independently in each case. Draw timing includes explicit GPU completion; the timer query is resolved and deleted in its owning draw context. These offscreen native observations do not measure compositor-visible cadence.

The Windows run completed 16/16 cases and 320/320 samples, with 320 GPU observations and tagged native input in every case. Every case reported zero source raster builds and zero readbacks. Source uploads corresponded to selected-frame changes; transform draws corresponded to changing size/frame samples. Maximum tagged input wait was 32 ms. Worst complete-path p95 was 17.048 ms.

| GIF case | Complete median | Complete p95 | Complete maximum | GPU execution p95 | Completion wait p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1536 DIP, 150%, 0 degrees | 16.658 ms | 16.830 ms | 16.834 ms | 0.019 ms | 15.569 ms |
| 1536 DIP, 150%, 17.25 degrees | 16.651 ms | 17.048 ms | 17.091 ms | 0.034 ms | 16.034 ms |

The source-only diagnostic reports its own completeness result and explicitly records `overall_spec_gate=not-evaluated`. It does not run the default Segoe envelope or sharp-replacement phase and cannot replace the required 96-case acceptance command. These measured GIF p95 values exceed 16.7 ms; they are regression observations, not a claim that the complete specification meets its performance target. The unchanged full command and its 50 ms input and 200 ms sharp requirements are documented in [rendering performance acceptance](rendering-performance.md).
