# Windows sprite grab area validation

This record accompanies [issue #18](https://github.com/dsun9/Yoink/issues/18)
and the [accepted design](../superpowers/specs/2026-10-12-approximate-grab-area.md).
Measurements used the Release build on the Windows development host on
2026-10-12. The physical desktop had one 3840 by 2160 monitor at 150% density,
an NVIDIA GeForce RTX 5090, driver 610.62 and OpenGL 3.2. The default scalable
emoji face was Segoe UI Emoji Regular version 1.70.

## Implemented bounds

`src/core/grab.c` builds a source occupancy union and compresses uniform blocks
into a region quadtree. Depth 7 bounds it to 21,845 four-byte nodes and 128 cells
per source axis. Any nonzero alpha occupies a cell. GIFs use the union of all
composited frames, including while the displayed frame is blank and after finite
playback ends. Their asset cache shares the tree across previews and instances.

Projection includes canonical source mapping, linear filtering within the source
rectangle, two native pixels of rounding margin and a conservative envelope for
0.05-degree input-angle buckets. Visual angles stay continuous. Rotation occurs
before output coarsening. Normal output has 64 cells per axis; a list exceeding
1,024 rectangles coarsens conservatively to 32 cells. At 32 cells, no row can
contain more than 16 occupied runs, so 32 rows cannot exceed 512 rectangles.
Output rectangles are sorted and nonoverlapping. Windows applies them without
a native transform that could multiply their count.

Trees and cached rectangle arrays share an 8 MiB budget, separate from raster
and GPU budgets. Admission counts old and candidate allocations before mutation.
Tree accounting lasts until the last reference releases it. Protective rejection
preserves the accepted snapshot, artwork and region. Native/context failures hide
the affected presentation and cancel input. Source preparation never reads GPU
pixels or runs inside a native input callback.

Translation and animation frame changes reuse the accepted region. Changes to
rotation, size, density, mapping or source reproject it. An identical final list
skips GDI publication. `SetWindowRgn(..., FALSE)` avoids a redundant native paint
invalidation before the retained GL draw.

For an unrotated square at 1536 logical pixels, a normal output cell is about
24 logical pixels wide; fallback cells are about 48. At 96 logical pixels those
sizes are 1.5 and 3. Source cells, filtering, rounding and rotated extents add
tolerance. These cell sizes are not a maximum distance from the visible edge.
Small transparent gaps can remain interactive. The code has no explicit
full-rectangle fallback, although dense detail can fill every approximation cell.

## Performance acceptance

The complete existing performance gate passed all 96 cases and 1920 active
samples, plus 288 warmup samples. Cold publication covered 96 of 96 cases and
120 draws, all with GPU completion and no source/output mapping mismatches.
The matrix includes six artwork types, four sizes, two densities and two angles.
100% density is simulated on the physical 150% monitor.

| Measured gate | Result | Required limit |
| --- | ---: | ---: |
| Default Segoe emoji, 1536 DIP, physical 150%, zero-angle owner p95 | 9.098 ms | 16.7 ms |
| Same default emoji, 17.25-degree owner p95 | 3.928 ms | 16.7 ms |
| Maximum tagged input response across the run | 31 ms | 50 ms |
| Latest input to completed sharp output, maximum of 20 samples | 140.332 ms | 200 ms |

The owner-operation timing includes returned native swaps and all completed GPU
draws. The worst regression p95 among every artwork case was 20.949 ms. The
16.7 ms gate applies to the two required default-emoji cases; this result does
not establish that every artwork case stays below 16.7 ms. The probe reports
compositor-visible cadence as unobserved. Desktop pixel tests below check output
separately. There are no normal transform readbacks.

Local raw results are in `build/grab-performance-coarse-desktop.log` and
`build/grab-performance-coarse-summary.log`. Build output is intentionally ignored
by Git. Reproduce the gate from an idle desktop with:

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -Performance -LogPrefix grab-performance
```

The prototype first bounded only source rectangles and tried a native XFORM.
An alternating hidden, offscreen Win32 microprobe measured 1,024 axis-aligned
rectangles at create-and-publish p95 1.2804 ms with redraw requested. Transforming
1,024 rectangles by 17.25 degrees produced 9,227 native rectangles and p95
17.5333 ms. Transforming 2,048 produced 18,457 native rectangles and p95
42.3094 ms. GDI object counts returned to zero. These hidden-window measurements
motivated the final-rectangle cap; they do not establish visible GL performance.
The local prototype source and results are in `build/grab-prototype/`.

The visible gate rejected a finer 128-cell output version above 16.7 ms. The
accepted 64-cell output, cached rectangle lists, bounded angle envelope and
redundant-paint avoidance passed the unchanged rendering/input/sharp limits.

## Source preparation

An opt-in CPU test measured quadtree preparation on an 8192 by 6144 source with
cached alpha, 50,331,648 source pixels and 251,658,240 RGBA-plus-alpha bytes.
Each case had three warmups and 20 measured samples.

| Source | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Empty | 17.334 ms | 18.156 ms | 18.568 ms |
| Opaque | 0.188 ms | 0.265 ms | 0.270 ms |
| Checker alpha | 0.184 ms | 0.189 ms | 0.191 ms |

Empty source scanning is the worst of these fixtures because no cell can stop
early. A union of 4096 blank one-pixel frames took 0.036 ms and retained one
36-byte tree. The algorithm uses actual pixel dimensions for tiny canvases;
it does not rescan a one-pixel frame 16,384 times.

These are CPU preparation timings, independent of decode, source upload and
native publication. They are not a worst-case end-to-end import guarantee.
Run `build/windows-release/Release/pointy_grab_test.exe --timing` to reproduce.
Local results are in `build/grab-preparation-timing.log`.

## Correctness and regression coverage

The non-desktop suite passed 37 of 37 tests. The new `grab` test checks ring holes,
conservative coverage against the legacy alpha reference at six angles and two
sizes, alpha-1 diagonals, composited unions/common holes, rotated solid corners,
bounded fragmented output, canonical overhangs and faint boundary texels. It also
checks 4096 tiny blank/full frames, exact cache admission and reference lifetimes.
The presenter test verifies rejected grab candidates preserve the accepted
snapshot, source, region and upload count, then succeed after releasing credit.
It verifies Windows union/capture survives a blank frame without republishing
the region. Its compatibility branch preserves the existing macOS expectations.

All 36 registered desktop tests passed across supervised sequential batches,
including the editor and lifecycle checks. The final successful runs restored
input, cursor and foreground. Separate-process native tests verify first-click,
wheel and right-click delivery
through transparent areas; occupied rim input; overlap order; keyboard
nonactivation; capture beyond the silhouette; preview pass-through and visible
preview dragging; simulated mixed-density/disconnect behavior; stable GIF union;
source replacement rollback; UI ordering; bitmap/scalable quality; and retained
PNG output. Screen-capture tests check ordinary and large hand/PNG sprites at all
four display edges, including release and recovery of newly exposed pixels.

The anchor regression originally failed because its old assertion required a
transparent preview pixel to intercept input. Its assertion now requires the
independent receiver to receive that original click, while the visible shaft
continues to start a drag. The sprite-flow blank-frame and ring-hole assertions
also now express the accepted union and pass-through contract.

Desktop runs use `tests/platform/run_acceptance_desktop.ps1`, which releases
injected input and restores the saved cursor and foreground window. Pointer
interference and held-input launcher refusals were recorded as failed or
incomplete attempts. They are not included as passing validation. Native tests
must run one at a time on an idle desktop.

The final batch logs are `build/grab-native-regressions-desktop.log` for the first
nine native tests, `build/grab-native-rest-retry-desktop.log` for the next seven,
`build/grab-anchors-silhouette-desktop.log` for the corrected anchor expectation,
`build/grab-exposure-desktop.log` and `build/grab-exposure-large-desktop.log` for
the 17 exposure tests, and `build/grab-editor-lifecycle-retry-desktop.log` for the
editor and lifecycle checks. The earlier stopped batches retain their failure
reports. `build/grab-final-headless.log` records the 37 non-desktop tests. After
tightening the new grab test's output-axis assertion to the final 64/32 policy,
that test was rebuilt and passed again.

macOS native behavior and physical multiple-monitor coverage remain unverified.
The accepted scope keeps macOS rectangular routing and its existing blank-frame
hiding behavior until a separate native follow-up.
