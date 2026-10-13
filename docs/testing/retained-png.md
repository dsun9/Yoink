# Retained PNG presentation

[Ticket #5](https://github.com/dsun9/Yoink/issues/5) adopts the Windows direct transparent OpenGL path after the [native feasibility gate](direct-gl-feasibility.md) passed. Placed PNG sprites retain their original decoded resolution and one RGBA texture. Ticket #10 extends retained source policies to anchors, Settings and drawer; see [shared preview checks](preview-retention.md) and [ownership](../architecture/rendering-content.md).

## Ownership and reusable presenter seam

`PPSourcePresentation` in `src/app/presenter_internal.h` carries a borrowed source frame only when content changes, an immutable content identity, a source revision, canonical logical layout, and source visibility. PNG uses the immutable snapshot revision as content identity and source revision 1. The visibility scan runs once when that source is acquired. A source with any alpha keeps its full rectangle interactive, including transparent holes; a completely transparent source hides the surface and releases accepted input even at opacity 1.

`pp_sprite_present` separates PNG source acquisition from `pp_retained_present`, which owns texture upload, native placement and draw decisions. Static updates perform no work. Translation moves the existing native surface without raster generation, uploads or draws. Size, rotation, opacity and drawable-density changes reuse the texture and update shader parameters. The direct path returns no transformed CPU color or coverage frame, performs no readback and copies no native bitmap. Canonical layout controls bounds independently from source texel dimensions. Later text work must add an independent canonical source rectangle to preserve raster origin and baseline; PNG occupies the full canonical rectangle.

`PPRetainedPresentation` owns the renderer, retained immutable snapshot, source key, GPU charge, accepted pose, bounds, density and visibility. Release the renderer before its native surface. GIF can supply a selected-frame revision alongside snapshot identity without changing placement. Scalable source work can supply a replacement packet after its generation/revision checks; source requests and asynchronous publication remain separate later work. Contexts currently remain independent, so textures are per sprite. Shared immutable GPU entries are not implemented.

`pp_anchor_presenter_renderer_metrics` and `pp_runtime_renderer_metrics` sum the shared compatibility renderer, active sprite/anchor renderers and retired direct renderers. `gpu_resources` accessors report the app aggregate retained bytes, peak bytes and limit. Native/UI content sources and UI output targets share 256 MiB, including old/candidate overlap. Charges release on their consumer's replacement/deletion/closure; a live sibling keeps shared storage charged once. Control-label atlases, SDL/driver contexts, drawable backing and compositor storage remain separate. Existing 8192-axis presentation and CPU raster/cache limits remain in force.

A preflight size/budget rejection or transactional allocation rejection restores the exact previous snapshot, pose and animation start through `pp_scene_restore`. A broken GL upload/context, draw or native surface immediately hides and removes that interceptor. Other usable sprites continue. Matching right-button release after deletion remains owned and consumed. Surface cancellation is scoped to the affected sprite.

## Deterministic checks

The first translation test failed on the old path because dragging performed another CPU transform. Presenter and runtime boundary tests now verify static/translation reuse, native PNG resolution, cached appearance changes, independent canonical layout/drawable density, the grabbed point across simulated scale changes, display removal, stationary immutable replacement, empty-source visibility, capture cleanup, scene rollback and project-generation cleanup. Four 4096 by 4096 sources fill the 256 MiB GPU limit; a fifth source and an old/candidate overlap are rejected without changing the usable scene. Deletion permits a later candidate reservation, and clear releases every charge. Injected allocation and context/upload failures distinguish usable rollback from failed interception cleanup.

These tests replace OS/GL boundaries but exercise real assets, snapshots, scene, presenter and runtime. They do not replace native routing evidence.

## Native observation on 2026-10-11

The machine/backend are the Windows 11 / Ryzen 7 7800X3D / RTX 5090 / SDL 3.4.18 / NVIDIA GL 3.2 host recorded in the feasibility document. The new `png_retained` test uses the production retained presenter, actual SDL GL windows, original native injected input and a separate receiver process. At the host's 1.5 drawable density, the 3 by 1 PNG remains a 12-byte source texture while its native window is 144 by 48 at 96 DIP.

Screen samples through the compositor were:

| Sample over white receiver | Observed and expected RGB |
| --- | --- |
| Transparent padding | 255, 255, 255 |
| Premultiplied partial-alpha texel | 255, 191, 159 |
| Same texel at opacity 0.5 | 255, 223, 207 |
| Empty rotated corner | 255, 255, 255 |
| Immutable blue replacement | 0, 0, 255 |

The test passed first clicks inside a transparent hole and rotated corner, original outside clicks, same-HWND static/translation reuse with unchanged source/draw/upload counts, captured movement/release outside bounds, cached wheel resize/rotation/opacity, hidden and zero-opacity cleanup, completely transparent replacement at opacity 1, Settings/drawer priority, overlapping sprites, matching right-release drain after deletion, external native-window destruction cleanup and keyboard nonactivation. Its aggregate final renderer counts were 5 uploads, 21 draws, zero readbacks, GPU peak 140 bytes and zero remaining bytes.

The PNG test passed twice in 6.58 and 6.48 seconds. The final supervised suite passed `png_retained`, `direct_gl`, `windows_surface` and `windows_capture` 4/4 (34.68 seconds). Every completed supervisor run reported released input, restored cursor and restored original foreground. One initial PNG run failed its first partial-alpha screen sample, observing white instead of the expected color. No cause was established. Adding origin/visibility/draw diagnostics changed no renderer or input behavior; the two subsequent runs passed. The first failure remains a failed observation rather than a successful gate sample. Development logs are retained under `build/png-retained-*`; the tracked recipe and measurements here are the durable evidence.

Run the opt-in native checks exclusively:

```powershell
cmake --build --preset windows-release --target pointy_png_retained_test pointy_direct_gl_test pointy_windows_surface_test pointy_windows_capture_test --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^(png_retained|direct_gl|windows_surface|windows_capture)$' -LogPrefix png-retained
```

## Replacement GPU rejection regression

[Issue #16](https://github.com/dsun9/Yoink/issues/16) covers a source-changing
replacement whose source fits the remaining GPU budget but whose output does
not. The native `png_retained` regression replaces the 3 by 1 alpha source
with the 8 by 4 blue source at 96 DIP. At 150% drawable density, the accepted
output is 144 by 48 and the replacement needs 144 by 72. The candidate source
needs 128 bytes. The test sets the presenter's limit to its accepted charge
plus 128, allowing that source while rejecting overlapping output storage.

On reviewed PR #15 commit `3afd21e`, the regression failed because the scene
lost its sole sprite. With both reservations checked before source publication,
the same sprite retains its snapshot, pose, animation start, native surface and
bounds, source identity, output storage and GPU charge. A fresh Desktop
Duplication image compares every displayed RGB pixel with the accepted image.
The receiver repaints to request a fresh desktop frame without redrawing the
sprite. The test also checks usable rectangular input, unchanged upload/draw
counts on rejection, successful retry after restoring the budget, and accepted
replacement ownership. Existing native GL errors still remove the failed
sprite and release interception and GPU charges.

The failing baseline and passing replacement run on 2026-10-12 both verified
released input, restored cursor and restored original foreground through the
desktop supervisor. The Release build and all 36 non-desktop tests passed.
All 33 desktop tests also passed, run serially in supervised groups, including
the new regression, native ownership, all four edges for hand and PNG at both
96 and 1536 DIP, and native failure/input cleanup. The full sweep found an
existing GIF source-only charge assertion that also failed on `3afd21e`.
Its updated checks include retained outputs, deriving their size from native
window bounds and retaining the shared-source and final zero-charge checks.
Both standards and specification reviews found no actionable issues.
Run the native regression with the supervised `png_retained` command above.

## Driver allocation rejection regression

[Issue #17](https://github.com/dsun9/Yoink/issues/17) extends replacement rollback
past application budget reservations. `replacement_output_exhaustion` permits
both reservations and verifies real 8 by 4 candidate source storage before
injecting `GL_OUT_OF_MEMORY` specifically for the 144 by 72 candidate output.
The accepted source is the 3 by 1 alpha PNG at 96 DIP and actual 1.5 drawable
density. On reviewed commit `d560f826`, the regression failed because its only
scene and presenter sprite disappeared. The baseline supervisor verified
released input and restored cursor and foreground.

Preparation now keeps accepted source ownership until both allocations succeed,
and keeps accepted output until publication succeeds. Allocation exhaustion is
a protective memory rejection. The native test checks the same sprite and
surface, snapshot, pose, animation start, source identity, native bounds, every
displayed RGB pixel through fresh Desktop Duplication, usable rectangular input,
exact accepted GPU charge, and deletion of both rejected texture names. It then
forces an accepted-source redraw and successfully retries the replacement after
removing the injected failure. Candidate overlap contributes to the peak charge.
`replacement_source_exhaustion` applies the same checks when source allocation
fails, while `png_retained` retains application budget rollback coverage.

The full Release build and all 36 non-desktop tests passed on 2026-10-12.
All 35 desktop tests passed across supervised groups, including both allocation
regressions, ownership, PNG presentation, native failure/input cleanup and all
four edges for hand and PNG at both 96 and 1536 DIP. An initial `sprite_flow`
run failed to locate its Settings cancel-transition control; the reviewed
baseline and isolated fixed-branch rerun passed. The original failure evidence
is retained, and no fixture or product change was made for that transient
failure. Every supervised run verified input, cursor and foreground restoration.
Standards and specification reviews each reported zero actionable findings.
The regression concerns OpenGL storage and native presentation; its desktop
capture helper does not change the application's OpenGL backend. macOS native
execution remains unverified.

```powershell
cmake --build --preset windows-release --parallel 6
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^(replacement_output_exhaustion|replacement_source_exhaustion|png_retained|sprite_exposure_ownership)$' -LogPrefix issue17-replacement
```

Evidence is retained in `build/issue17-red-desktop.log`,
`build/issue17-green-desktop.log`, `build/issue17-exhaustion-desktop.log`,
`build/issue17-non-desktop.log`, `build/issue17-edges-desktop.log`,
`build/issue17-edges-details.log`, `build/issue17-native-rest-desktop.log`,
`build/issue17-flow-baseline-desktop.log`, and
`build/issue17-native-remaining-desktop.log`.

## Scoped timing and limits

The [performance matrix](rendering-performance.md) now measures PNG through `pp_sprite_present` and aggregates its retained renderer. Each timer query is created, resolved and deleted in its owning context. The PNG-only diagnostic completed 16/16 cases, 320/320 warm GPU observations and tagged native input observations in all 16 cases. Every case reported zero raster builds, one cold source upload, 23 transform draws across cold/warmup/varying-pose samples and zero readbacks. Worst complete-path p95 was 16.851 ms and maximum tagged hook-arrival wait was 32 ms. GL completion waits include driver/swap pacing; these are synthetic poses in offscreen windows, with visible compositor timing unobserved.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Performance -PngRetained -Configuration Release -LogPrefix png-only-matrix
```

This command identifies a 16-case PNG diagnostic and explicitly reports `overall_spec_gate=not-evaluated`. The unchanged full acceptance command still failed: at the unmigrated default emoji 1536 DIP / 1.5-density case, input wait reached 141 ms and triggered protective stop. Fourteen of 96 cases and 280 of 1920 samples completed; required Segoe timings and sharp samples were incomplete. Ticket #11 must establish the complete default-Segoe budget and actual on-display gesture/worker/compositor observations. Neither the scoped PNG timing nor native color samples establish sustained visible cadence, fullscreen behavior or physical mixed-display acceptance.

macOS keeps the GL transform/CPU-frame compatibility path. PNG uses a rectangular native shape there, with zero-opacity/empty-source hiding represented in the shared presenter. The direct Mac API remains explicitly unsupported. No macOS native transparency, capture, order, mixed-display or runtime performance result was obtained for this ticket; hosted compile/headless validation is distinct from runtime acceptance.
