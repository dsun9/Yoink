# Transparent OpenGL native feasibility

This is the bounded native gate for [issue #4](https://github.com/dsun9/Yoink/issues/4), part of [the rendering specification](https://github.com/dsun9/Yoink/issues/1). The diagnostic remains opt-in. [Ticket #5](retained-png.md) adopts the validated Windows path for placed PNG sprites. Other content still uses the CPU-frame presenter until its implementation tickets.

The Windows gate passed on the machine below in three consecutive final Release runs on 2026-10-11. This permits the next Windows adoption step on this host. It does not establish the 1536 DIP performance budget, visible compositor frame cadence, other GPU configurations, physical mixed-display behavior, or macOS acceptance.

## Machine and backend

| Field | Observation |
| --- | --- |
| OS | Windows 11 Pro, 10.0.26200, build 26200 |
| CPU | AMD Ryzen 7 7800X3D, 8 cores, 16 logical processors |
| GPU | NVIDIA GeForce RTX 5090 |
| Windows GPU driver | 32.0.16.1062 |
| GL vendor / renderer | NVIDIA Corporation / NVIDIA GeForce RTX 5090/PCIe/SSE2 |
| GL context | 3.2.0 NVIDIA 610.62, core profile |
| SDL backend | windows, SDL 3.4.18 |
| Pinned SDL source | `829a65d769d935c4852f8159e964312c0957260a`, verified against the borrowed dependency source checkout |
| Build | MSVC x64 Release, isolated `codex/issue-4-direct-gl` worktree |
| Diagnostic geometry | 64 by 64 native desktop pixels, 90 by 90 at 37 degrees, drawable pixels matched to the native client rectangle |
| Source | Premultiplied RGBA ring with transparent interior, padding and a partially transparent patch; no source coverage plane |

The SDL revision string reports `SDL-3.4.18-HEAD-HASH-NOTFOUND` when the configured dependency source directory is reused. The source Git revision above establishes the actual pin.

SDL creates the transparent, borderless, nonfocusable, always-on-top OpenGL window. Its pinned Windows backend supplies normal OS compositor transparency. The application adds its existing input adapter and native nonactivating placement. It adds no DirectComposition, D3D, or other application GPU composition backend.

## Repeat the gate

Run from a configured repository checkout with exclusive desktop input use. The existing launcher bounds the child process tree, releases input and restores the cursor and foreground window. The CTest test has a 60-second timeout. A missing observation or a failed assertion exits nonzero.

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target pointy_direct_gl_test pointy_windows_surface_test pointy_windows_capture_test
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -TestRegex '^direct_gl$' -LogPrefix direct-gl-repeat1
```

Repeat the final command with `direct-gl-repeat2` and `direct-gl-repeat3`. Preserve `build/windows-release/Testing/Temporary/LastTest.log` after each run for the detailed observations. The launcher also writes `build/<LogPrefix>-desktop.log` and its stderr companion. Run the CPU contract regression with `-TestRegex '^(windows_surface|windows_capture)$'` and its own log prefix.

The fixture starts the existing separate native receiver, puts the GL window over its client area, injects OS input and examines normalized production events. Visible alpha is checked separately by sampling compositor-visible screen RGB through GDI after drawing, GPU completion and native pumping. These screen samples do not use GL framebuffer readback. Settings and drawer priority checks use native SDL fixture windows registered through the production platform APIs; this is not a full application Settings or drawer UI smoke test.

## Final observations

| Required observation | Result |
| --- | --- |
| Transparent interior and padding over white receiver | RGB 255,255,255 |
| Opaque red rim | RGB 255,0,0 |
| Premultiplied 128/255 red patch over white | RGB 255,127,127 |
| Whole-sprite opacity 0.5, red rim | RGB 255,128,128; interior remains white |
| Rotated transparent interior and empty corner | Both RGB 255,255,255 |
| First clicks at interior, padding, rim and rotated corner | Sprite receives each accepted down and release; receiver receives none |
| Click outside native rectangle | Receiver receives the original click |
| Wheel at transparent interior / outside rectangle | Sprite / receiver receives it respectively |
| Accepted left press | Only its owning surface can acquire capture |
| Captured move and release outside rectangle | Owning sprite receives trajectory and release; underlying keyboard foreground stays on receiver |
| Zero opacity, hide, failed placement, destruction during capture | Capture cannot be reacquired; next stationary-pointer click reaches receiver |
| Visible right gesture and release after hide, zero opacity, failure or destruction | Matching release is consumed; receiver gets no orphan release/context menu; its next original right gesture works |
| Overlapping sprite manipulation | Raising a sprite changes the top target; redraw and movement retain that order |
| Registered Settings and drawer fixtures | New/raised sprites stay below both native UI windows |
| Receiver brought above sprites | Original click reaches receiver despite matching sprite geometry |
| Stationary pointer during content revision | Next click remains a rectangular sprite target; composited center changes to green |
| Translation alone | Same HWND, zero extra transform draws, source uploads or readbacks |
| Repeated upload of same content ID/revision | Zero extra uploads |
| Changed content revision | One new semantic source upload |
| Canonical square layout with 2:1 cached texel raster | Window remains 64 by 64; independent red/green screen samples prove raster mapping fills the canonical bounds |

The final three supervised test durations were 8.24, 8.48 and 8.29 seconds. Each returned success and reported `SUITE RESTORE input=released cursor=restored original-window-valid=yes foreground=restored`. A process audit found no remaining diagnostic or receiver processes. The previous full instrumented run also passed in 8.19 seconds.

One earlier development run failed the foreground assertion after captured movement and release. That run did not record the unexpected foreground HWND. No cause has been established. Adding the HWND/title failure diagnostic changed no presentation or input behavior, and the subsequent full run plus three repeats passed. The strict foreground assertion remains active; this earlier failure is not counted as a successful gate sample.

The native CPU compatibility tests `windows_surface` and `windows_capture` passed 2/2 under the supervisor. They preserve the earlier CPU-frame contract, including transformed color/coverage, top-down premultiplied RGBA, capture and cleanup. The `surface_failure` and `text` regressions also passed 2/2. The application and existing production-source performance probe both built in Release with the new shared APIs. The first API test failed to link with seven missing direct APIs before implementation. A later smoke run exposed SDL applying its pending 1 by 1 hidden-window size on show; recording the size through SDL before native placement fixed that failure.

## APIs for adoption

`pp_surface_create_gl` creates an opt-in native surface. `pp_surface_window_id` exposes its SDL window ID. Create a renderer with `pp_renderer_create_for_window` and retain the source through `pp_renderer_upload_source` using a stable nonzero content ID and an explicit revision. Equal ID, revision and dimensions reuse the source texture; changed pixels require a changed revision. Same-sized updates reuse texture storage.

Place or move the native rectangle with `pp_surface_place_gl`. Pass `interactive=false` for zero opacity; this hides the window and cancels its accepted press and capture. Then `pp_renderer_draw` uses the existing transform shader with the canonical unrotated layout size, pose and drawable density. It returns no transformed CPU frame and allocates no transformed coverage plane. Placement failure hides the interceptor. Renderer or upload failure requires the presenter to call `pp_surface_hide` immediately before reporting the failure. Destroy the renderer before its owning surface.

`pp_renderer_metrics` exposes semantic source uploads, transform draws and readback counts for both renderer modes. `pp_renderer_complete` waits for submitted work in that renderer's context. Completion does not establish that the compositor displayed the frame. `pp_renderer_transform` remains the separate CPU color/coverage operation for existing consumers and explicit export; it rejects a direct renderer to prevent one mode from invalidating another mode's retained source identity.

Each direct renderer retains one RGBA source texture. The adopting presenter still needs source identity, resource-budget reservations, maximum GPU retention and failure isolation. It should move the existing surface for translation and avoid calling draw/upload on unchanged static content. Scalable raster replacement must pass its canonical layout independently from disposable texel dimensions.

## Compatibility and macOS

If the direct native gate fails on another supported configuration, leave direct adoption disabled there. The existing OpenGL transform plus layered CPU-frame presentation is a compatibility candidate. Its rectangular-input behavior and complete timing require their own validation before it can satisfy the new specification. CPU readback/native copy timings must not be reported as a successful direct result.

The shared headers and CPU renderer contract remain available to macOS. The macOS adapter explicitly rejects the experimental direct surface with an unverified-backend error and preserves its current compatibility surface. No Mac compiler, native alpha, capture, ordering, fullscreen or mixed-display run was available for this ticket. macOS build and runtime remain unverified.
