# Retained PNG presentation

[Ticket #5](https://github.com/dsun9/Yoink/issues/5) adopts the Windows direct transparent OpenGL path after the [native feasibility gate](direct-gl-feasibility.md) passed. Placed PNG sprites retain their original decoded resolution and one RGBA texture. Other content and anchor previews keep the existing CPU-frame path while their rendering tickets are implemented.

## Ownership and reusable presenter seam

`PPSourcePresentation` in `src/app/presenter_internal.h` carries a borrowed source frame only when content changes, an immutable content identity, a source revision, canonical logical layout, and source visibility. PNG uses the immutable snapshot revision as content identity and source revision 1. The visibility scan runs once when that source is acquired. A source with any alpha keeps its full rectangle interactive, including transparent holes; a completely transparent source hides the surface and releases accepted input even at opacity 1.

`pp_sprite_present` separates PNG source acquisition from `pp_retained_present`, which owns texture upload, native placement and draw decisions. Static updates perform no work. Translation moves the existing native surface without raster generation, uploads or draws. Size, rotation, opacity and drawable-density changes reuse the texture and update shader parameters. The direct path returns no transformed CPU color or coverage frame, performs no readback and copies no native bitmap. Canonical layout controls bounds independently from source texel dimensions. Later text work must add an independent canonical source rectangle to preserve raster origin and baseline; PNG occupies the full canonical rectangle.

`PPRetainedPresentation` owns the renderer, retained immutable snapshot, source key, GPU charge, accepted pose, bounds, density and visibility. Release the renderer before its native surface. GIF can supply a selected-frame revision alongside snapshot identity without changing placement. Scalable source work can supply a replacement packet after its generation/revision checks; source requests and asynchronous publication remain separate later work. Contexts currently remain independent, so textures are per sprite. Shared immutable GPU entries are not implemented.

`pp_anchor_presenter_renderer_metrics` and `pp_runtime_renderer_metrics` sum the shared compatibility renderer, active sprite renderers and retired direct renderers. `gpu_resources` accessors report retained bytes, peak bytes and limit. The presenter caps retained source RGBA at 256 MiB. It reserves old plus candidate textures before immutable-source or dimension replacement, commits the new charge only after successful upload, and releases charges on deletion, scene clear and project changes. This budget covers application source texture storage; SDL/driver contexts, drawable backing and compositor storage remain OS-managed. Existing 8192-axis presentation and CPU raster/cache limits remain in force.

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

## Scoped timing and limits

The [performance matrix](rendering-performance.md) now measures PNG through `pp_sprite_present` and aggregates its retained renderer. Each timer query is created, resolved and deleted in its owning context. The PNG-only diagnostic completed 16/16 cases, 320/320 warm GPU observations and tagged native input observations in all 16 cases. Every case reported zero raster builds, one cold source upload, 23 transform draws across cold/warmup/varying-pose samples and zero readbacks. Worst complete-path p95 was 16.851 ms and maximum tagged hook-arrival wait was 32 ms. GL completion waits include driver/swap pacing; these are synthetic poses in offscreen windows, with visible compositor timing unobserved.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Performance -PngRetained -Configuration Release -LogPrefix png-only-matrix
```

This command identifies a 16-case PNG diagnostic and explicitly reports `overall_spec_gate=not-evaluated`. The unchanged full acceptance command still failed: at the unmigrated default emoji 1536 DIP / 1.5-density case, input wait reached 141 ms and triggered protective stop. Fourteen of 96 cases and 280 of 1920 samples completed; required Segoe timings and sharp samples were incomplete. Ticket #11 must establish the complete default-Segoe budget and actual on-display gesture/worker/compositor observations. Neither the scoped PNG timing nor native color samples establish sustained visible cadence, fullscreen behavior or physical mixed-display acceptance.

macOS keeps the GL transform/CPU-frame compatibility path. PNG uses a rectangular native shape there, with zero-opacity/empty-source hiding represented in the shared presenter. The direct Mac API remains explicitly unsupported. No macOS native transparency, capture, order, mixed-display or runtime performance result was obtained for this ticket; hosted compile/headless validation is distinct from runtime acceptance.
