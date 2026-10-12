# Sprite display-edge recovery

[Issue #14](https://github.com/dsun9/Yoink/issues/14) requires a sprite's first
draw to be partly off-screen. A complete first draw followed by an excursion
does not reliably reproduce the persistent cutoff.

`pointy_sprite_exposure_test` creates an independent complete reference above
a white native receiver, retains its settled shared source, and hides that
reference window. It creates a new subject with its first native bounds
crossing the selected display edge. Tagged `SendInput` movements then return
the subject through the native captured drag and presenter path. Desktop
Duplication compares every displayed RGB pixel with the reference, including
visible portions during return, release, repeated partial excursions, and a
complete off-screen placement. The subject receives no rotation, resize, or
content edit. The test checks native bounds, accepted press/release/movement,
nonactivation, and unchanged raster/upload/transform/readback counters.

Capture is diagnostic D3D11 code, linked only into this Windows test executable.
Production rendering remains OpenGL. The capture requires a fresh desktop
image update after movement, rejects pointer-only updates, and fails on an
unavailable capture rather than accepting
GL readback or a stale reference. After release, an unchanged desktop may reuse
the last captured frame. The current helper requires Windows 10's Output5
interface and a single unrotated capture output; physical mixed-monitor and
rotated-output acceptance remain separate work.

Build Release targets and run each supervised group without other desktop tests
or builds running:

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^sprite_exposure_(hand|png)_[0-3]$' -LogPrefix exposure-correctness
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^sprite_exposure_large_(hand|png)_[0-3]$' -LogPrefix exposure-large
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^sprite_exposure_ownership$' -LogPrefix exposure-ownership
```

Eight ordinary cases cover the default installed hand and PNG at all four
edges. Eight large cases also run at 1536 DIP and require an actual drawable
density of 1.5. Their real captured moves record ordinary native movement and
exposure publication separately. Recovery timing includes event handling,
presenter update, native placement, blit, returned swap, and explicit GPU
completion. Each timing group requires at least 20 samples and p95 at or below
16.7 ms. A wrapped production input hook observes tagged input arrival and
requires a maximum wait at or below 50 ms. These timings describe completed
owner work; Desktop Duplication supplies the separate visible-correctness
assertion. They do not establish compositor cadence.

The ownership case uses real GL and checks that a target allocation is charged,
an over-budget candidate preserves accepted native bounds/storage, replacement
overlap contributes to the peak, hiding releases the target, reappearance
allocates it again, and a GL error during exposure removes interception and
releases output ownership. Destroying a native sprite window must delete its
actual output texture through the surviving share owner. A forced failure to
bind both contexts keeps the GPU charge until the group's final context ends.
The receiver starts suspended in a private kill-on-close job before creating
its fullscreen window, so abrupt fixture termination also closes the receiver.

On 2026-10-12 the Release build and all 36 tests outside the desktop suite passed.
The native ownership case passed with verified cursor/input/foreground recovery,
including destroyed-window shared-object cleanup and deferred group accounting.
Desktop Duplication received no initial desktop frame on this host, so the new
visible regression remains unverified. Earlier
diagnostic logs recorded 5,040 missing PNG pixels and 6,275 missing hand pixels;
those logs establish the issue's reproduction, not a pass for this change.
The existing bitmap GDI pixel check also failed with the same white sample in
both this implementation and a probe using the original renderer. That check
cannot supply visible recovery evidence on this host.

The explicit `--timing-only` diagnostic skips desktop capture and labels visible
correctness unverified. It runs the same initial edge placement, captured
movement, GL refresh, GPU completion, zero raster/upload/transform/readback
checks, and tagged input hook. Supervised temporary CTest registrations ran all
eight timing cases on this host at 1536 DIP and actual 150% drawable density.
Each case recorded 25 ordinary movement samples and 24 exposure samples.

| Source | Edge | Ordinary p95 ms | Exposure p95 ms |
| --- | --- | ---: | ---: |
| Default hand | Left | 2.046 | 3.513 |
| Default hand | Top | 1.817 | 3.249 |
| Default hand | Right | 1.972 | 3.235 |
| Default hand | Bottom | 2.002 | 3.080 |
| PNG | Left | 2.016 | 2.851 |
| PNG | Top | 2.035 | 3.076 |
| PNG | Right | 2.028 | 3.047 |
| PNG | Bottom | 2.129 | 3.358 |

All eight timing diagnostics passed. Their worst completed exposure sample was
4.864 ms and their tagged hook arrival maximum was 0 ms at the Windows tick
clock's resolution. The existing complete rendering performance probe also
passed all 96 cases and 1,920 warm samples: required target p95 1.190 ms,
matrix worst p95 8.204 ms, maximum tagged input wait 31 ms, and maximum sharp
settlement 143.890 ms. Its cold draw/completion/GPU counts matched 120/120/120.
Neither diagnostic establishes compositor cadence or the pending desktop image
comparison. Logs are `build/exposure-timing-details.log` and
`build/exposure-rendering-performance-desktop.log`, with supervised input,
cursor, and foreground restoration verified.
