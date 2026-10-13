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
The PNG presentation test also passed after replacing GDI sampling with Desktop
Duplication. It verifies transparent padding, premultiplied source color,
display opacity, captured movement, nonactivation, overlap/UI ordering, and
hidden/zero-opacity/failed-window input cleanup.

The same edge fixture was linked against the original renderer from `1fad8fa`,
with no-op adapters for the new exposure APIs. Every baseline case failed during
the real return drag with persistent missing pixels. All eight fixed ordinary
cases and all eight fixed large cases passed with zero mismatched displayed
pixels during return, after release, repeated partial excursions, and a complete
off-screen placement.

| Source | Edge | Baseline missing pixels at 96 DIP | Fixed mismatched pixels |
| --- | --- | ---: | ---: |
| Default hand | Left | 3,279 | 0 |
| Default hand | Top | 4,090 | 0 |
| Default hand | Right | 2,346 | 0 |
| Default hand | Bottom | 1,699 | 0 |
| PNG | Left | 5,040 | 0 |
| PNG | Top | 4,896 | 0 |
| PNG | Right | 4,968 | 0 |
| PNG | Bottom | 4,752 | 0 |

The fixture sets the requested scene pose before its first update, then asserts
both the accepted pose and native dimensions. This prevents an item default
size from silently replacing the requested target. The large cases realized
1536 DIP at actual 1.5 drawable density, with native/drawable bounds of
2304x2131 pixels for the hand and 2304x1152 for the PNG. Each recorded 25 ordinary
movement samples and 24 exposure samples, alongside the full desktop image
comparisons.

| Source | Edge | Ordinary p95 ms | Exposure p95 ms |
| --- | --- | ---: | ---: |
| Default hand | Left | 2.082 | 3.435 |
| Default hand | Top | 2.119 | 3.349 |
| Default hand | Right | 2.033 | 3.369 |
| Default hand | Bottom | 2.336 | 3.459 |
| PNG | Left | 2.155 | 4.159 |
| PNG | Top | 2.273 | 2.949 |
| PNG | Right | 2.117 | 4.012 |
| PNG | Bottom | 2.426 | 3.419 |

The largest completed exposure sample was 5.421 ms. The maximum tagged hook
arrival wait was 16 ms at the Windows tick clock's resolution. The existing
complete rendering performance probe also passed all 96 cases and 1,920 warm
samples: required target p95 1.190 ms, matrix worst p95 8.204 ms, maximum tagged
input wait 31 ms, and maximum sharp settlement 143.890 ms. Its cold
draw/completion/GPU counts matched 120/120/120.

Use an idle desktop with notification popups closed. The fullscreen white test
receiver stays topmost behind the sprites, but system notification bands can
still cover transparent padding. The test compares every displayed pixel and
does not exclude those regions. Such interference must be cleared before
acceptance. Input, cursor, and foreground restoration were verified for each
supervised run.

The optional `--timing-only` diagnostic skips desktop capture and explicitly
labels visible correctness unverified. Earlier diagnostic timings that inherited
the item default size were invalid and are superseded by the asserted-size
captured results above. Neither timing mode establishes compositor cadence.
Physical mixed-monitor and rotated-output correctness remain separate work.

Evidence logs are `build/exposure-baseline-final-summary.log`,
`build/exposure-standard-final-desktop.log`,
`build/exposure-large-accepted-details.log`,
`build/exposure-png-composition-desktop.log`, and
`build/exposure-rendering-performance-desktop.log`.
