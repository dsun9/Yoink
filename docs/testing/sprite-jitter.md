# Rotation and zoom jitter diagnosis

Issue [#32](https://github.com/dsun9/Yoink/issues/32) implements coherent
Windows presentation through a stable DirectComposition host. Its validation
appears below the issue 31 baseline. The original diagnosis and baseline
measurements remain here to explain the observed failure.

On 2026-10-14 the user reported small sideways jumps when rotating or zooming
text or emoji, especially large elongated artwork.

The completed GL frame follows the requested pose accurately. The extended
`sprite_stability` test places two colored landmarks in 8:1 artwork, presents
it at 1536 DIP, and measures each landmark against the requested rotation and
scale at 100%, 150%, and 200% drawable density. This checks points away from
the rotation center, where angular or scale errors would be amplified.
The final Debug run measured maximum landmark errors of 0.003963 pixels for
zoom and 0.004770 pixels for rotation, below the 0.02-pixel regression limit.

The opt-in desktop probe exposes a separate intermediate frame. It captures
the actual Windows desktop after `pp_surface_place_gl` has completed its
native `SetWindowPos` call and before the presenter publishes its next GL
frame. It deliberately waits at that boundary to raise the reproduction rate.
It uses the real surface, renderer, and retained presenter. Source rasterization,
worker completion, wheel delivery, and easing are unnecessary to reproduce it.

On the available 3840x2160 Windows desktop, at 150% simulated density, a
1536-DIP elongated sprite centered at 1600.25,900.75 gave these results:

| Operation | Visible center before the next GL frame | Maximum axis error |
| --- | --- | ---: |
| Initial presentation | 1600.265953,900.754834 | 0.015953 px |
| Rotation from 17.25 to 18.25 degrees | 1603.265953,881.754834 | 18.995166 px |
| Zoom from 1536 to 1552 DIP | 1588.261838,895.754793 | 11.988162 px |

The old image moves with the new native window origin. The next GL publication
restores its correct desktop position. Larger extent changes produce larger
origin changes, explaining why elongated sprites make this failure easier to
see. The probe samples before the next grab-region update, so that update is
not required to produce the jump.

## Reproduce

Build and run from the repository root with the mouse and keyboard released:

```powershell
cmake --build build/windows-dev --config Debug --target pointy_sprite_stability_test --parallel 4 -- /nodeReuse:false
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -BuildDirectory build/windows-dev -SpriteJitter -LogPrefix sprite-jitter
```

The supervisor applies its desktop mutex, bounded child process, and verified
cursor/input/foreground restoration. The probe displays a temporary black
background and colored markers. It chooses a capture rectangle inside the
monitor under the cursor, including monitors with a negative desktop origin.
It preserves 1536 DIP when the monitor has enough space and reduces the
workload on smaller monitors. The log records the resulting size and rectangle.
It exits with failure when the visible center moves by at least 0.1 pixels.
It is deliberately excluded from ordinary CTest acceptance while the
placement/publication issue remains unresolved.

## Findings and limits

The relevant production sequence is in `src/app/retained.c`: native placement,
grab-region publication, GL transform draw, then buffer swap. Native window
geometry and GL content reach the compositor through separate operations.
The existing small-square tests read the completed FBO, which cannot observe
the intermediate desktop frame.

Combining the visible move and resize, preserving overlap with
`WM_NCCALCSIZE`/`WVR_VALIDRECTS`, and trying `SWP_NOREDRAW` and `SWP_NOCOPYBITS`
did not reliably remove the captured displacement. All experimental production
changes were reverted.

The original diagnosis included no production fix. A fix must prevent the compositor from showing
an old frame at a new origin, while preserving transparent input routing,
GPU budgets, and transform responsiveness. A stable render viewport or a
presentation mechanism that commits geometry and content together are possible
design directions; neither has been implemented or validated here. Simply
rounding the sprite center differently does not address the observed race.

The deliberate pause measures the intermediate displacement, not its duration
or frequency during ordinary interaction. The same native presenter is used by
text, emoji, PNG, GIF, and SVG, but this isolated probe uses synthetic artwork.
Actual wheel-driven text cadence, other GPUs, physical mixed-density displays,
and macOS behavior remain unverified by this diagnosis.

The final Debug build of `pointy_sprite_stability_test` and `PointyPointer`
succeeded. Supervised `sprite_stability` and `sprite_cursor` both passed.
The final minimized desktop probe reproduced the 18.995166-pixel rotation
and 11.988162-pixel zoom displacements. Its log is
`build/sprite-jitter-final-red-desktop.log`; the passing suite log is
`build/sprite-jitter-final-stability-desktop.log`. Both runs verified desktop
restoration. No temporary production instrumentation remains.

## Ordinary wheel desktop regression

Issue [#31](https://github.com/dsun9/Yoink/issues/31) extends the diagnosis with
actual wheel delivery through `SendInput`, the native input hooks, runtime,
easing and retained presenter. This mode does not pause native placement.
It uses the actual display density. The synthetic probe above deliberately
sets both density ratios to 1.5, independently of the physical monitor.

The ordinary scenario displays `MMMM MMMM MMMM MMMM` and eight pointing-hand
emoji. The text has an 11.23:1 canonical aspect ratio. The emoji use the default
Windows font selection and an elongated single line. Each source receives six
alternating one-notch rotation inputs and six alternating Alt-wheel zoom
inputs. The initial angle is 17.25 degrees, rotation steps are one degree,
zoom steps are one percent, the nominal post-input wait is 200 milliseconds,
and easing lasts 120 milliseconds. Mouse-position rotation uses a visible
artwork pixel away from the center. Logs contain each input timestamp,
requested endpoint, actual eased pose, rotation pivot and native rectangle.

A separate thread polls fresh Desktop Duplication images. Pointer-only updates
do not count as new presentations. `LastPresentTime` attributes an image to a
workload's QPC interval, so setup and source-replacement frames cannot enter
the active workload under the collector's current tag.

For every transformed presentation, the application thread reads the completed
GL color output and records its brightness centroid, principal-axis angle and
centered second moment. The capture uses the same brightness weights. The
angle and second moment identify the displayed shape without using its
desktop location. The measured displacement is the captured centroid minus
the matching completed image's desktop centroid. Off-center artwork, intended
pivot movement and easing therefore use their own expected centers. A previous
image at a new origin retains its shape and produces a nonzero residual.
Candidates must precede the capture and be within 250 milliseconds. Equal
shape scores choose the latest candidate, avoiding an earlier repeated pose.

After easing stops, an unchanged visible retained image can receive a fresh
state-reference timestamp without another readback. It uses the cached actual
completed pixels and current accepted native bounds. These entries do not
count as completed GL draws or distinct easing poses. The 250-millisecond
matching limit stays the same across long settled intervals and deliberate
placement pauses. The probe buffers its log until exit so redirected
per-character output does not delay the owner between easing updates.

Each workload needs at least ten distinct captured poses during active easing,
ten matched desktop images and ten completed GL poses. Every active blank or
unmatched desktop image prevents a passing result. A shape difference above
0.15 pixels is unmatched, rather than being reported as an accurate translation.
The actual displacement gate remains less than 0.1 pixels. Native placement
callback counts are not a passing condition. This continues to observe content
if a fix retains a stable native viewport.

Use the supervised runner with released input. Each command waits for its
child before restoring the desktop or returning. Run commands sequentially.

```powershell
cmake --preset windows-release
cmake --build build/windows-release --config Release --target pointy_sprite_stability_test pointy_sprite_wheel_test --parallel 4 -- /nodeReuse:false
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -BuildDirectory build/windows-release -SpriteJitter -LogPrefix sprite-jitter-synthetic
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -BuildDirectory build/windows-release -SpriteWheel -LogPrefix sprite-jitter-natural
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -BuildDirectory build/windows-release -TestRegex '^sprite_(stability|cursor)$' -LogPrefix sprite-jitter-gl
```

The synthetic and ordinary desktop regressions are opt-in and fail against
the issue 31 production baseline. Completed-GL accuracy and cursor checks
remain ordinary passing correctness tests. A failing desktop regression must
not be reclassified as a failing completed-GL check.

Three additional opt-in controls use the same supervised process:

| Runner option | Expected result against unchanged production |
| --- | --- |
| `-SpriteWheelDefectControl` | The real native placement callback pauses for 35 ms. The independent collector detects the old image at the new origin. |
| `-SpriteWheelControl shift` | Real captured image bytes move 3 pixels right and 19 pixels up. The measurement rejects that translation without depending on placement callbacks. |
| `-SpriteWheelControl empty` | The collector deliberately drops every image. The meaningful-sample requirement fails even when the displacement maximum is zero. |

## Issue 31 validation on 2026-10-14

Release on Windows used the retained transparent OpenGL backend, NVIDIA
GeForce RTX 5090, NVIDIA driver 610.62 and GL 3.2. The selected physical monitor
was 3840 by 2160 at desktop origin 0,0 and 144 DPI. The monitor-relative capture
rectangle was 520,530 to 3320,1630. Ordinary input used actual desktop and
drawable density 1.5, with no simulated density. Both probes used 1536 DIP and
center 1920.25,1080.75.

The preserved deterministic probe reproduced 18.995166 pixels after rotation
and 11.988162 pixels after enlargement. Its four samples include two completed
presentations as well as the two paused placement samples. Completed samples
remain mandatory even if a future fix makes no native placement calls.

One ordinary run produced the following measurements. The observations are
specific to this instrumented workload and GPU.

| Workload | Completed GL poses | Matched desktop images | Distinct easing poses captured | Unmatched images | Displaced images | Largest residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Text rotation | 32 | 59 | 25 | 0 | 0 | 0.000000 px |
| Text zoom | 31 | 55 | 24 | 0 | 9 | 6.000000 px |
| Emoji rotation | 31 | 48 | 24 | 2 | 3 | 6.034627 px |
| Emoji zoom | 29 | 53 | 22 | 0 | 10 | 6.000609 px |

All 24 native wheel events arrived. The collector obtained 246 fresh desktop
images, including setup images outside the active intervals. Every workload
had meaningful samples. Two emoji-rotation images had shape errors 0.573589
and 0.898837 pixels and lower brightness mass than any matched completed
image. Their exact translation is unresolved. The run records them as
incomplete desktop observations and fails, alongside the independently
identified displaced images. It does not discard these images to pass.

The application performed 119 reference readbacks, with a measured total of
611.389 milliseconds and mean of 5.138 milliseconds. Reading output pixels
and logging poses can change the owner thread's cadence. This is an ordinary
input-path regression with measured overhead. Its counts do not estimate the
frequency or duration of jitter in an uninstrumented application. The
deliberately paused control provides no natural-frequency estimate either.

The placement-pause control at 1536 DIP captured twelve distinct easing poses
per workload and found maxima 10.000000, 9.082197, 10.000000 and 9.048124 pixels.
The shifted-byte control produced a 19-pixel residual in otherwise accurate
images, and every matched image failed its displacement gate. The empty-image
control observed zero desktop images and failed its meaningful-sample gate.

Supervised `sprite_stability` and `sprite_cursor` passed 2 of 2 checks in
3.76 seconds. Completed large-artwork landmark maxima remained 0.003963 pixels
for zoom and 0.004770 pixels for rotation, under the independent 0.02-pixel
gate. The supervisor logged released input, restored cursor and original
foreground for the passing suite and all completed expected-failure runs.
A separate 500-millisecond desktop-child timeout terminated the owned tree,
waited for its exit and verified the same restoration. Launcher controls
propagated exit 7, exit 0 and timeout exit 2.

Raw logs use the `ticket31-final-natural`, `ticket31-final-synthetic`,
`ticket31-final-gl`, `ticket31-final-placement-control`, `ticket31-shift-control`,
`ticket31-empty-control` and `ticket31-owned-timeout` prefixes under `build/`.
The implementation handoff preserves copies and SHA-256 hashes outside the
worktree before cleanup. No production fix is included in issue 31.

## Issue 32 validation on 2026-10-14

Windows now uses a stable DirectComposition visual host and a tight native
input proxy. The canonical source shader draws directly into the locked
`BeginDraw` output. A completed update and its matching visual offsets enter
one composition commit. Integer translation reuses those pixels. The native
input proxy has no artwork front buffer. The mechanism and cleanup policy are
recorded in [ADR 0002](../adr/0002-coherent-windows-sprite-presentation.md).

The available machine selected `DirectComposition WGL_NV_DX_interop2`, with
the NVIDIA GeForce RTX 5090 for both GL and DXGI. It keeps one logical native
output, performs no GPU copy, and adds no production readback, compositor
wait or interaction sleep. SDL compatibility fallback remains available when
the actual register/lock/unlock/unregister capability probe fails; coherent
publication on that fallback remains unverified.

The deterministic desktop probe reduced the rotation residual from
18.995166 to 0.015953 pixels and the zoom residual from 11.988162 to 0.011838
pixels. Its final maximum across four mandatory samples was 0.024175 pixels.
The ordinary wheel run captured the following results through the final
production backend, with 24 native wheel events and no placement pause.

| Workload | Completed GL poses | Matched desktop images | Distinct easing poses | Unmatched or blank | Displaced | Largest residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Text rotation | 70 | 56 | 42 | 0 | 0 | 0.000000 px |
| Text zoom | 61 | 59 | 42 | 0 | 0 | 0.000000 px |
| Emoji rotation | 66 | 54 | 46 | 0 | 0 | 0.000000 px |
| Emoji zoom | 62 | 68 | 45 | 0 | 0 | 0.000000 px |

The collector obtained 325 fresh images including setup. Its 267 output
readbacks took 1,154.174 milliseconds total, or 4.323 milliseconds on average.
The shifted-byte control still failed with 19-pixel residuals in all 242
matched images. The empty-image control failed with zero observations and
zero meaningful workloads, despite a zero displacement maximum.

The deliberate 35-millisecond wheel placement pause also passed. It captured
21, 19, 21 and 23 distinct easing poses for text rotation, text zoom, emoji
rotation and emoji zoom. All 230 matched active desktop images had zero
measured displacement; no active image was blank or unmatched. The collector
obtained 304 fresh images including setup. The 123 actual output readbacks
took 528.165 milliseconds total, or 4.294 milliseconds on average. These
measurements retain the original 0.1-pixel displacement, 0.15-pixel shape,
250-millisecond freshness and ten-distinct-easing-pose gates.

The full Release performance matrix completed all 96 source/size/density/
rotation cases, 288 warmups and 1,920 active samples. The 96 cold cases had
120 draws, 120 GPU observations and 120 completed publications, with zero
accounting mismatches. The required largest default Segoe warm cases had a
worst complete-owner p95 of 11.833 milliseconds, below 16.7 milliseconds.
The worst p95 across all regression cases was 19.072 milliseconds. Maximum
native input wait was 31 milliseconds, below 50, and all 20 native sharp
updates matched their input and source revision, with a maximum latency of
138.322 milliseconds, below 200. The probe buffered 592,904 bytes within its
8 MiB capacity and flushed after cleanup, with no overflow. Its boundary is
selected-backend publication and GPU completion; compositor-visible cadence
and scanout remain unobserved.

Earlier runs exposed a 113-millisecond cold native-device initialization on
the input owner and a redundant show call on already-visible visual hosts.
The shared device now initializes in ordinary renderer/runtime startup,
synchronized once per process. Each sprite still probes its current WGL
context and owns its own composition tree and interop handle. Startup time
remains reported separately by the same performance protocol; none of the
96 cold source/output cases or input limits changed.

Simulated 100%, 150% and 200% landmark checks, injected desktop metrics,
capability and cleanup fault controls are distinct from physical display and
driver-removal evidence. Physical mixed-density migration, hotplug, other
GPU families, SDL-fallback coherence and native macOS execution remain
unverified. Instrumented desktop observations do not estimate production
frame cadence or jitter frequency.

## Issue 33 final placement verification

The final issue-33 code retains SDL sizing and skips a redundant native resize
when the input proxy already has the requested dimensions. Its selected
backend remains the stable DirectComposition host with direct logical output.
The following Release checks ran exclusively on the same RTX 5090, NVIDIA
610.62 driver and single 3840 by 2160 display at physical 150% density.
The original displacement, shape, freshness and meaningful-sample limits were
unchanged.

The deterministic independent-desktop check passed with four observations and
maximum drift .024175 pixels. Rotation and zoom observations during placement
were .015953 and .011838 pixels. The separate completed-GL landmark test
passed its existing .02-pixel gate at simulated 100%, 150% and 200% density.

The ordinary native wheel run passed all 24 events with 245 fresh desktop
images, including setup. Every matched active image had zero displacement,
and none was blank or unmatched.

| Workload | Completed GL poses | Matched desktop images | Distinct easing poses |
| --- | ---: | ---: | ---: |
| Text rotation | 71 | 51 | 45 |
| Text zoom | 63 | 55 | 42 |
| Emoji rotation | 69 | 52 | 46 |
| Emoji zoom | 62 | 51 | 43 |

Its 273 actual output readbacks took 1111.384 ms, mean 4.071 ms. These
instrumented captures do not estimate production frame cadence.

The shifted-byte control failed as required. All 214 matched images had a
19-pixel residual. The empty-image control failed with zero desktop images
and zero meaningful workloads despite zero reported displacement. The
deliberate 35-ms native placement pause passed all 24 wheel events with 218
fresh images, 186 matched active images and 22/19/23/21 distinct easing poses
in workload order. No active image was blank, unmatched or displaced. Its
120 output readbacks took 503.226 ms, mean 4.194 ms.

The final prefixes are `ticket33-synthetic-sizing4`, `ticket33-wheel-sizing4`,
`ticket33-wheel-shift-sizing4`, `ticket33-wheel-empty-sizing4` and
`ticket33-wheel-pause-sizing4`. Each supervisor restored released input,
the exact saved cursor and foreground HWND. Their logs, earlier failures and
SHA-256 checksums are preserved in the external issue-33 evidence archive.
The final full matrix also passed 13.747-ms required warm p95, 31-ms input
maximum and 135.227-ms sharp maximum; its complete per-angle observations
and earlier timing failures are in [rendering performance](rendering-performance.md).

Physical mixed-display traversal/hotplug, fullscreen behavior, other GPU
families, SDL fallback coherence and native macOS remain unverified.
