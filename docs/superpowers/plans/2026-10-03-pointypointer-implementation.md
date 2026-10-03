# PointyPointer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the portable Windows/macOS presentation helper defined in the approved design, with precise sprite-shaped mouse routing and Windows runtime verification.

**Architecture:** Keep project state, interaction, display calculations, and resource ownership in C. Use compact native surfaces for individual sprites, SDL3/OpenGL for rendering and application events, and a Nuklear settings UI adapted selectively from BongoCat. Validate real input passthrough before building the rest of the application.

**Tech Stack:** C11 application code; Objective-C macOS adapter; CMake/CTest; SDL3; OpenGL; SDL3_image; SDL3_ttf with FreeType and HarfBuzz; PlutoSVG; Expat; yyjson; miniz; Nuklear. Some third-party dependencies require a C++ compiler, but the application core stays C.

**Spec:** [Approved PointyPointer design](../specs/2026-10-03-pointypointer-design.md), committed as `b5176c6`. Read both documents before execution.

## Global Constraints

- "The same eight item lists appear on every display."
- "Placed sprites are temporary and are never saved."
- "Only the visible sprite shape intercepts mouse input."
- "Transparent padding, holes, and gaps between letters pass clicks and scrolling to windows below."
- "The first click after arriving over a transparent region must work correctly."
- "Synthetic click replay is not the normal input-routing mechanism."
- "The core must not include Win32 or Cocoa types."
- "Windows receives the first runtime testing."
- Preserve replacement center, rotation, opacity, and longest unrotated dimension; preserve the new aspect ratio.
- Defaults: 16 by 16 logical-pixel activation region; 10 degrees per wheel notch; 10% proportional resize per notch; approximately 120 ms easing.
- Initial hand: `👉`, requested Aptos family, Noto Color Emoji fallback, 96-logical-pixel font size, zero rotation, full opacity. Monochrome text defaults to black.
- Imports become owned project bytes; embed actual permitted fonts and fallbacks; never depend on original paths at reopen.
- No Live2D, Cubism, audio, saved scenes, per-display lists, network synchronization, or remote content fetching.
- Preserve source attribution. BongoCat runtime/UI source uses AGPL; its model-asset MIT notice does not relicense runtime code.
- Do not modify the reference checkout or unrelated `.codegraph/` content.

## Review Focus

These are easy-to-miss conditions beyond the ordinary happy path. Their owning tasks include tests below.

1. A stationary pointer over a GIF pixel that becomes transparent: the next click/wheel must reach the correct window (Tasks 1, 4, 9).
2. A monitor disconnect or scale change during a drag: end or transfer capture safely and keep the instance reachable (Tasks 2, 3, 9).
3. Mixed text with emoji sequences, missing Aptos, and a restricted font: preserve valid appearance or explain the unresolved resource before saving (Tasks 5, 7).
4. Removing a default item while an instance and drawer reference it: retain instance resources and update only future pulls/current drawer contents (Tasks 6, 9).
5. A read-only destination or interrupted replacement during Save: retain the original file, dirty state, and active scene (Tasks 7, 8).

---

## Execution setup and build decisions

At execution time, use the worktree skill and inspect existing attached worktrees before choosing isolation. The reference folder is ignored by Git and will not automatically exist in a new worktree. Read it at `D:/Dev/PointyPointer/Ref/BongoCat`; copy only the particular attributed source files needed into the implementation tree. Never make the finished build depend on `Ref/`.

Use CMake 3.24 or newer. Define `windows-dev` and `windows-release` configure/build/test presets with Visual Studio 17 2022, x64, Debug/Release respectively. Define `macos-dev` using Ninja and Debug. Choose OpenGL 3.2 core with GLSL 150 for a common Windows/macOS shader baseline; adapt BongoCat's GLSL 330 attributes to this baseline. Request a compatible context explicitly and report initialization failure rather than producing an invisible input-blocking window.

### Dependency pins

The following upstream refs were inspected during planning. Fetch by the immutable revision, not a moving branch. Fetch required submodules at the parent revision's recorded gitlinks. CMake dependency configuration belongs in `cmake/Dependencies.cmake` and is introduced only when a task needs it.

| Dependency | Revision | Use |
| --- | --- | --- |
| SDL, release-3.4.18 | `829a65d769d935c4852f8159e964312c0957260a` | Video/events/tray/dialogs |
| SDL_image, release-3.4.8 | `0891fc30428b518e7d018677bd6fa95c1a50f4c6` | PNG and GIF decoder |
| SDL_ttf, release-3.2.2 | `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b` | Text/fallback rendering, pinned FreeType/HarfBuzz submodules |
| PlutoSVG, v0.0.8 | `fd8a080b3d0bccc41f1ae708d3785e7f08398a58` | Static SVG rasterization, pinned PlutoVG submodule |
| Expat, R_2_7_3 | `2120251ef8e5fd5f398063b6508be27796689bd9` | SVG feature and external-reference validation; source subdirectory `expat` |
| yyjson, reference revision | `ac8f6074e1fbc43ec496aa1404b460d08b55d7a5` | Manifest parsing/writing |
| miniz, 3.1.2 | `77d0dce8627735138c51770d1799a1ef48f2117d` | ZIP packages |
| Nuklear, reference revision | `8109cfbabe04f8705408c5d8ab1a6cd48649ccda` | Settings and drawer UI |
| Noto Emoji, v2.051 | `6202fe7c20dd5e1727a4c3c01604edc176c576da` | `fonts/NotoColorEmoji.ttf` and its font license |

Prefer static libraries, turn off samples and unused codecs, and retain offline source overrides through CMake FetchContent variables. Keep the SDL event, video, tray, and dialog functionality. Disable SDL_image's platform-native image backends so decoder behavior is shared. Enable PNG/GIF only there; SVG uses PlutoSVG. Disable SDL_ttf's separate PlutoSVG backend to avoid a second conflicting dependency tree; this plan's emoji fallback uses CBDT/CBLC.

**Color-font build requirement:** SDL_ttf's vendored configuration defaults `FT_DISABLE_PNG` and `FT_DISABLE_ZLIB` to ON. Configure both OFF before adding it, and ensure its FreeType target links the same pinned libpng/zlib dependency targets used by SDL_image. Enable HarfBuzz. A successfully linked application without a correctly rendered colored hand does not pass Task 5.

### Shared conventions

Public headers use `PP` types and `pp_` function names. `bool` results use an optional `PPError *` out parameter with a stable error code and readable UTF-8 message. Opaque objects have explicit create/destroy or retain/release pairs. Functions documented as borrowing data do not take ownership. Platform and GL calls run on the main thread.

Create CTest labels `core`, `media`, `storage`, and `desktop`. Desktop checks create their own visible test windows and run serially; they are not ordinary headless CI tests. A second process provides the underlying-window test target. Use synthesized input only in that test harness to exercise real OS routing, never as application passthrough logic. Restore the prior cursor/foreground state when the test ends.

Common commands, from the implementation checkout:

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev
ctest --preset windows-dev --output-on-failure -LE desktop
ctest --preset windows-dev --output-on-failure -L desktop
```

Test assertions must stay enabled in Release; the small C test helper uses an explicit `CHECK` macro that exits nonzero, not C `assert` compiled out by `NDEBUG`. Each task records the intended failing test before implementation, reruns its relevant tests afterward, and commits only its listed files. A missing symbol is an acceptable first failure for a new interface; an unrelated dependency/configure failure must be fixed before treating the test as evidence.

## File structure

| Paths | Responsibility |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | Build, pinned dependencies, tests, application resources, packaging |
| `include/pointy/` | Plain C interfaces and shared data contracts |
| `src/core/` | Geometry, gestures, easing, project model, scene snapshots |
| `src/display/` | Display layout conversion, anchor rectangles, recovery |
| `src/media/` | Asset ownership, PNG/GIF/SVG, fonts/text, limits |
| `src/render/` | GL loader, FBO rendering, cached presented frames |
| `src/platform/windows/`, `src/platform/macos/` | Native surfaces, input, tray, dialogs, file replacement, font lookup |
| `src/project/` | Manifest, archive, import/export, portable resources |
| `src/ui/` | Adapted Nuklear backend, settings, drawer, common theme |
| `src/app/`, `src/main.c` | Lifecycle, commands, event routing, scheduling |
| `resources/` | Tray icon, bundled font/license and UI assets |
| `tests/core/`, `tests/media/`, `tests/storage/`, `tests/platform/` | Focused tests and the separate desktop receiver |
| `tests/fixtures/`, `docs/testing/` | Small owned test assets and observed acceptance results |

## Task 1: Prove transparent Windows presentation and native routing

**Files — create:** `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `cmake/Tests.cmake`, `cmake/windows.manifest.in`, `src/main.c`, `include/pointy/types.h`, `include/pointy/platform.h`, `include/pointy/render.h`, `src/render/gl_api.c`, `src/render/frame.c`, `src/platform/windows/surface.c`, `src/platform/windows/events.c`, `src/app/overlay_probe.c`, `tests/test.h`, `tests/platform/input_receiver.c`, `tests/platform/test_windows_surface.c`, `docs/testing/overlay-probe.md`.

**Interfaces:**

- `PPVec2 { double x, y; }`, `PPRect { double x, y, w, h; }`, `PPError { int code; char message[512]; }`. Task 1 declares opaque `PPRenderer` and `PPSurface` types in their owning headers.
- `PPFrame` owns top-down, premultiplied RGBA8 pixels, dimensions, stride, and a matching content-alpha plane; `void pp_frame_destroy(PPFrame *frame)` frees them.
- `bool pp_renderer_create(PPRenderer **out, PPError *error)`; `bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest_dip, double angle_deg, double opacity, double pixels_per_dip, PPFrame *out, PPError *error)`; `void pp_renderer_destroy(PPRenderer *renderer)`.
- `bool pp_surface_create(PPSurface **out, PPError *error)`; `bool pp_surface_present(PPSurface *surface, const PPFrame *frame, PPRect desktop_bounds, PPError *error)`; `void pp_surface_destroy(PPSurface *surface)`.
- `bool pp_platform_poll(PPPlatformEvent *out)` supplies a normalized queue. Events contain surface identity, kind, desktop point, button, modifiers, wheel delta, and timestamp in seconds. Task 2 adds gesture-related events.

- [ ] **1. Write `test_windows_surface_routes_alpha`** with a separate receiver process behind a 64-by-64 ring. Assert `receiver.clicks == 1` after clicking its transparent center, `overlay.clicks == 1` after clicking its visible rim, and `foreground_after == foreground_before`. Repeat immediately after pointer arrival, with right-click, and after changing the pixel under a stationary pointer from opaque to transparent. Register as `windows_surface` with label `desktop` and `RUN_SERIAL TRUE`. Task 2 adds wheel/capture assertions before the complete native interaction gate is accepted.
- [ ] **2. Configure and run** `cmake --preset windows-dev`, `cmake --build --preset windows-dev --target pointy_windows_surface_test`, then `ctest --preset windows-dev -R '^windows_surface$' --output-on-failure`. The preimplementation test must fail on absent presentation/routing behavior.
- [ ] **3. Implement the smallest real presenter.** Use a hidden SDL OpenGL context and compact FBOs, then read back only a newly drawn frame for a native layered window. The visible Win32 surface uses `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` and topmost placement with `SWP_NOACTIVATE`. Present using `UpdateLayeredWindow` and correct premultiplied BGRA conversion. Return `MA_NOACTIVATE` on `WM_MOUSEACTIVATE`. Do not set `WS_EX_TRANSPARENT` on an interactive sprite: it disables input for its whole shape. Do not rely on `HTTRANSPARENT` alone for cross-process passthrough.
- [ ] **4. Add probe modes** `PointyPointer.exe --overlay-probe ring` and `--overlay-probe changing-alpha`, exercising the same production presenter. Adapt GL loading and layered presentation from the reference, retaining attribution but removing its source/proxy input assumptions. Destroy a failed surface instead of leaving a blank interceptor.
- [ ] **5. Run the desktop test and both probe modes**, recording actual first-click, focus, and rotated-hole results in `docs/testing/overlay-probe.md`. Success means both processes' event counts match; an image that only looks transparent is insufficient. Record wheel observations for Task 2, which completes the native interaction gate.
- [ ] **6. Commit** the explicit task files with `feat: add transparent Windows overlay presenter`.

## Task 2: Add transforms, capture, and deterministic gestures

**Files — create:** `include/pointy/interaction.h`, `src/core/geometry.c`, `src/core/interaction.c`, `src/core/easing.c`, `src/platform/windows/input.c`, `tests/core/test_interaction.c`, `tests/platform/test_windows_capture.c`.
**Modify:** `include/pointy/platform.h`, `src/platform/windows/events.c`, `cmake/Tests.cmake`.

**Interfaces:**

- Consumes `PPFrame`, `PPVec2`, `PPPlatformEvent`, and `PPSurface` from Task 1.
- `PPPose` holds desktop center, logical longest dimension, unwrapped angle in degrees, and opacity. `PPGestureConfig` holds system drag/double-click thresholds plus rotation step, resize factor, and easing duration. Define zero-initializable `PPInteraction` and `PPInteractionResult` records in `interaction.h`; their state is per active gesture, not global per renderer.
- `bool pp_content_hit(const PPFrame *source, const PPPose *pose, double desktop_units_per_dip, PPVec2 desktop_point)` uses the inverse transform and alpha sampling consistent with presentation.
- `void pp_interaction_step(PPInteraction *state, const PPPlatformEvent *event, const PPGestureConfig *config, PPInteractionResult *out)` outputs commands: begin/move/end/cancel drag, rotate, resize, delete, open drawer, or none, with the target instance ID.
- `double pp_ease_value(double start, double target, double elapsed_seconds, double duration_seconds)` is a clamped cubic ease-out; retarget from the current displayed value.
- `bool pp_platform_capture(PPSurface *surface, PPError *error)` and `void pp_platform_release_capture(void)` own native capture.

- [ ] **1. Write `test_gestures_and_transforms`** asserting a transformed ring hole misses, its rim hits, a drag starts only past the configured threshold, a drag cannot also be a double-click, one notch targets `angle + 10.0`, and resize up/down factors multiply to `1.0` within tolerance. Assert `pp_ease_value(0, 10, .120, .120) == 10` and that retargeting starts at the currently displayed value. Register `interaction` under `core`.
- [ ] **2. Add desktop capture and wheel cases**: press on visible content while the receiver remains foreground, move outside the sprite window, release, then verify the receiver gets its next click. Force loss of capture and surface destruction during the gesture and assert capture is cleared. Wheel over the visible rim must change only the sprite; wheel over the hole must change only the receiver. Repeat with Windows inactive-window scrolling enabled and disabled, restoring that setting after the test.
- [ ] **3. Run** build targets `pointy_interaction_test` and `pointy_windows_capture_test`, then `ctest --preset windows-dev -R '^(interaction|windows_capture)$' --output-on-failure`; establish the missing-behavior failures.
- [ ] **4. Implement the interfaces** with direct positional dragging, 120 ms easing, reciprocal `pow(1.10, wheel_delta)` resizing, unwrapped rotation, and OS-derived click thresholds. Normalize wheel input and poll the Alt modifier without relying on overlay keyboard focus.
- [ ] **5. Implement selective native input observation without activation.** Windows restricts `SetCapture` for background windows, so the adapter must maintain application-owned drag tracking beyond the sprite using a low-level mouse hook, and prove it with the foreground-preservation test. Only a press accepted by a sprite can begin this ownership. Normalize owned move/release events through the same queue and deduplicate WndProc/hook delivery. The same hook can route wheel input: check the actual topmost native target and current presented alpha, consume only input belonging to the sprite or its active drag, and pass everything else onward. Do not replay input. Hook callbacks do no rendering, allocation, file IO, or waiting; clear ownership on release, cancellation, lost state, and shutdown.
- [ ] **6. Rerun Tasks 1–2 tests**, including inactive-window scrolling enabled and disabled, and record the result. Commit with `feat: implement sprite gestures and precise input routing`.

## Task 3: Map displays and reveal the eight anchors

**Files — create:** `include/pointy/display.h`, `src/display/layout.c`, `src/display/anchors.c`, `src/platform/windows/displays.c`, `src/app/anchors.c`, `tests/core/test_displays.c`, `tests/platform/test_windows_anchors.c`.
**Modify:** `include/pointy/platform.h`, `src/app/overlay_probe.c`, `cmake/Tests.cmake`.

**Interfaces:**

- `PPAnchor` enumerates the eight stable IDs in spec order. `PPDisplay` has an ID, native desktop bounds, desktop units per logical pixel, and drawable pixels per logical pixel. Native desktop units are explicitly distinct from drawable pixels.
- `bool pp_platform_displays(PPDisplay **out, size_t *count, PPError *error)` returns an owned array; `void pp_displays_free(PPDisplay *items)` releases it.
- `PPRect pp_anchor_bounds(const PPDisplay *display, PPAnchor anchor, double size_dip)`; `PPVec2 pp_display_to_local(const PPDisplay *display, PPVec2 desktop)`; `PPVec2 pp_display_to_desktop(const PPDisplay *display, PPVec2 local_dip)`.
- `PPVec2 pp_recover_center(const PPDisplay *displays, size_t count, PPVec2 old_center, double longest_dip)` moves a lost instance onto a remaining display.
- `void pp_anchors_update(PPAnchorRuntime *runtime, const PPDisplay *displays, size_t count, PPVec2 pointer, double now)` owns reveal/fade state, not item definitions.

- [ ] **1. Write `test_anchor_layout_and_recovery`** using displays at `(0,0,1920,1080)` with scale 1 and `(-2560,0,2560,1440)` with scale 1.5. Assert all eight targets exist per display, target width is `16 * desktop_units_per_dip`, middle targets are centered, round-trip coordinate conversion is exact within tolerance, and recovery lands inside a remaining display. Include portrait layout and shared edges.
- [ ] **2. Run** target `pointy_displays_test`, then `ctest --preset windows-dev -R '^displays$' --output-on-failure`, and observe the expected missing-layout failure.
- [ ] **3. Implement Win32 monitor enumeration and conversions** with PerMonitorV2 awareness. Keep raw desktop coordinates at the platform boundary; do not apply one global DPI multiplier. When the active display changes during a drag, preserve the logical size and grabbed point.
- [ ] **4. Implement reveal state with a procedural probe sprite.** Detect pointer approach through native cursor position without drawing fullscreen capture windows. Show the small target and inward preview promptly. Maintain reveal while the pointer is in either region, fade out in 120 ms after leaving, and cancel creation below the system drag threshold. Hide capture targets for disabled/empty anchors once Task 6 supplies the project model.
- [ ] **5. Add and run `windows_anchors` desktop checks** for ordinary clicks outside the 16-by-16 target, transition from target to preview, and disconnect/scale events during a drag. An actual second-display test is recorded separately from simulated display events; do not claim hardware coverage from simulation.
- [ ] **6. Commit** with `feat: add shared anchors across displays`.

## Task 4: Load bounded PNG, GIF, and SVG assets

**Files — create:** `include/pointy/assets.h`, `src/media/assets.c`, `src/media/image.c`, `src/media/gif.c`, `src/media/svg.c`, `src/media/svg_validate.c`, `src/media/limits.h`, `src/core/sha256.c`, `tests/media/test_images.c`, `tests/media/test_svg.c`, `tests/fixtures/media/`.
**Modify:** `cmake/Dependencies.cmake`, `cmake/Tests.cmake`.

**Interfaces:**

- `PPAssetId` is a SHA-256 digest. `PPBytes` is borrowed `const uint8_t *data` plus `size_t size`; `PPAsset` is a retained resource with owned source bytes and decoded/raster caches.
- `bool pp_assets_create(PPAssets **out, PPError *error)`; `void pp_assets_destroy(PPAssets *assets)`; `bool pp_asset_import(PPAssets *assets, PPBytes bytes, PPAssetKind kind, PPAssetId *out, PPError *error)`.
- `PPAsset *pp_asset_retain(PPAssets *assets, PPAssetId id)`; `void pp_asset_release(PPAsset *asset)`; `PPBytes pp_asset_source(const PPAsset *asset)`.
- `bool pp_asset_frame(PPAsset *asset, double elapsed_seconds, int raster_width, int raster_height, const PPFrame **out, double *next_change_seconds, PPError *error)` returns a borrowed cached frame. Callers retain the asset while using it. `PPAssetKind` initially includes PNG, GIF, SVG, and FONT.

- [ ] **1. Write `test_image_frames_and_limits`**: alpha PNG padding remains zero; a disposal-method-2/3 GIF produces expected composited frame pixels; two instance clocks select different frames; a finite-loop GIF stops on its last frame; repeated identical bytes return the same asset ID. Reject dimensions over 8192, decoded resource bytes over 256 MiB, and more than 4096 frames before uncontrolled allocations. Clamp zero GIF delay to 10 ms and test it. Use tiny committed, owned fixtures with a provenance note.
- [ ] **2. Write `test_svg_validation`**: paths, shapes, groups, transforms, gradients, and local `use` references render; scripts, DTD/entities, external references, unsupported text/filter/mask/clip features, cyclic references, and unsupported painting attributes produce an explicit import error. Harmless metadata is ignored. Verify scale changes rerasterize sharply rather than stretching one low-resolution image.
- [ ] **3. Build and run** `pointy_images_test` and `pointy_svg_test`; use `ctest --preset windows-dev -R '^(images|svg)$' --output-on-failure` to establish failures.
- [ ] **4. Implement PNG/GIF using SDL_image's IO and streaming animation-decoder APIs**, including decoder metadata for loop count. Preflight dimensions/frame metadata, bound allocations, and retain composited frames within the budget. Limit total retained decoded/raster CPU data to 512 MiB, evict unused caches first, and report a resource-limit error if active content cannot fit. Test that several individually valid assets cannot bypass the total budget. Do not blindly decode an unbounded animation before checking its size. Use the same frame's alpha for hit routing and presentation.
- [ ] **5. Implement SVG preflight with Expat and rasterization with PlutoSVG.** Accept only rendering features actually implemented by the pinned renderer; its recognized `clipPath` tag is not a guarantee of implemented clipping. Maintain an explicit supported feature list, reject unsupported rendering semantics, disable external entities, cap XML nesting at 128 and local-reference expansion at 256. Retain original bytes. Normalize all decoder output to the Task 1 frame convention and use the shared SHA-256 implementation, with reference attribution if adapted.
- [ ] **6. Run media tests and the changing-alpha desktop probe with an actual GIF.** Commit with `feat: load portable image and animation assets`.

## Task 5: Render portable text and color emoji

**Files — create:** `include/pointy/text.h`, `src/media/fonts.c`, `src/media/text.c`, `src/platform/windows/fonts.c`, `tests/media/test_text.c`, `tests/fixtures/fonts/`, `resources/fonts/`, `resources/licenses/`.
**Modify:** `include/pointy/platform.h`, `cmake/Dependencies.cmake`, `cmake/Tests.cmake`.

**Interfaces:**

- `PPFontInfo` contains family/style, face index, embeddability, and coverage diagnostics. `PPTextStyle` contains requested family, primary/fallback asset IDs and face indices, logical font size, and RGBA text color. `PPBlob` is owned `uint8_t *data` plus `size_t size`. `PPTextDiagnostics` reports fallback runs, missing glyphs, and missing requested families; define both records in the owning headers.
- `bool pp_font_inspect(PPBytes font, int face_index, PPFontInfo *out, PPError *error)`; `bool pp_platform_find_font(const char *family, PPBlob *out, PPError *error)` returns owned font bytes; `void pp_blob_destroy(PPBlob *blob)` frees them.
- `bool pp_text_render(PPAssets *assets, const char *utf8, const PPTextStyle *style, double pixels_per_dip, PPFrame *out, PPTextDiagnostics *diagnostics, PPError *error)` reports actual fallback use and missing glyphs. Font memory stays alive while SDL_ttf borrows it.

- [ ] **1. Write `test_text_fallback_and_embedding`** using an OFL fixture font that lacks U+1F449 and the pinned Noto font. Assert `render("A👉")` reports fallback for the hand, contains colored pixels, retains transparent gaps, and honors a monochrome text color. Include combining marks, explicit newline, skin tone, variation selector, and a ZWJ emoji sequence. Resize the result and check logical bounds and alpha instead of platform-fragile whole-image hashes.
- [ ] **2. Add embedding cases** for editable/installable, restricted, print/preview-only, no-subsetting, invalid face index, corrupt font, missing Aptos, and whitespace-only content. Empty/whitespace-only items cannot be a draggable default because they have no visible grab target; show a validation error. Use a synthetic metadata reader fixture for restriction flags rather than modifying a restricted commercial font.
- [ ] **3. Run** `pointy_text_test`, then `ctest --preset windows-dev -R '^text$' --output-on-failure`; the initial fallback/embedding failures must be observed.
- [ ] **4. Implement memory-backed fonts with `TTF_OpenFontIO` and fallback with `TTF_AddFallbackFont`.** Enable FreeType PNG support and HarfBuzz as specified above. Preserve whole fallback clusters; do not split ZWJ sequences by code point. Inspect font restrictions through FreeType and preserve full font bytes, face index, and license notices. Retain color emoji's colors while tinting monochrome glyphs.
- [ ] **5. Implement Aptos lookup without relying on this user's cache path.** Check registered installed fonts and supported Office font locations, then allow explicit font import. Missing Aptos must still allow the all-emoji default using Noto. Bundle only the pinned Noto font/license; do not copy local Aptos into application resources. Render through the shared pipeline on both platforms.
- [ ] **6. Run all media tests and show the default hand in the overlay probe.** Verify color, edges, font fallback diagnostic, and scaling. Commit with `feat: add embedded text and emoji rendering`.

## Task 6: Define the project model and independent scene instances

**Files — create:** `include/pointy/model.h`, `include/pointy/scene.h`, `src/core/project.c`, `src/core/items.c`, `src/core/scene.c`, `tests/core/test_model.c`, `tests/core/test_scene.c`.
**Modify:** `src/app/anchors.c`, `cmake/Tests.cmake`.

**Interfaces:**

- `PPProject` owns eight `PPAnchorList` records, item definitions, resource references, and interaction settings. `PPItem` has a stable `uint64_t` ID, content kind, image/font references or Unicode text, and appearance defaults. `PPScene` owns retained immutable item snapshots plus per-instance pose, origin anchor, and animation start time.
- `bool pp_project_default(PPAssets *assets, PPProject **out, PPError *error)`; `bool pp_project_validate(const PPProject *project, PPError *error)`; `void pp_project_destroy(PPProject *project)`.
- `bool pp_project_add_item(PPProject *project, PPAnchor anchor, const PPItem *item, uint64_t *out_id, PPError *error)`; `bool pp_project_remove_item(PPProject *project, PPAnchor anchor, uint64_t item_id, PPError *error)`; `bool pp_project_set_default(PPProject *project, PPAnchor anchor, uint64_t item_id, PPError *error)`; `bool pp_project_reorder_item(PPProject *project, PPAnchor anchor, uint64_t item_id, size_t index, PPError *error)`; `bool pp_project_edit_item(PPProject *project, uint64_t item_id, const PPItem *replacement, PPError *error)`.
- `bool pp_scene_create(PPScene **out, PPError *error)`; `void pp_scene_destroy(PPScene *scene)`; `bool pp_scene_spawn(PPScene *scene, const PPProject *project, PPAnchor anchor, PPVec2 center, double now, uint64_t *out_id, PPError *error)`; `bool pp_scene_replace(PPScene *scene, uint64_t instance_id, const PPItem *replacement, double now, PPError *error)`; `void pp_scene_delete(PPScene *scene, uint64_t instance_id)`; `void pp_scene_clear(PPScene *scene)`.

- [ ] **1. Write `test_project_defaults`** asserting exactly eight enabled lists, each defaulting to `👉`, requested Aptos, Noto fallback, font size `96.0`, angle `0.0`, opacity `1.0`, and the agreed interaction constants. The first added item becomes default; removing a default selects the first remaining item; reorder preserves its ID; empty/disabled anchors expose no target.
- [ ] **2. Write `test_scene_replacement_and_snapshot`**: replace a 200-by-100 image at center `(300,400)`, angle 35, opacity .6 with portrait content. Assert center, angle, opacity, and longest dimension 200 are unchanged, aspect is preserved, default ID is unchanged, origin anchor is unchanged, and animation restarts. Edit/remove the old item while the instance exists and assert its resource remains valid until deletion.
- [ ] **3. Run** `pointy_model_test` and `pointy_scene_test`, then `ctest --preset windows-dev -R '^(model|scene)$' --output-on-failure`, observing the missing behavior.
- [ ] **4. Implement immutable scene snapshots and retained resources.** Reject nonfinite numbers, invalid references, and out-of-range indices. Support opacity from 0 to 1, with zero-opacity instances noninteractive. Bound image longest side and logical font size to 1–4096; apply the same bound to scroll targets so repeated input cannot allocate unbounded surfaces. Larger raster demands must also respect Task 4 budgets.
- [ ] **5. Connect anchor availability and preview content to the actual shared project.** All displays borrow the same list definitions. Rerun model/scene/display tests. Commit with `feat: model shared projects and temporary sprite instances`.

## Task 7: Save and open complete `.pointy` packages

**Files — create:** `include/pointy/project_store.h`, `src/project/manifest.c`, `src/project/archive.c`, `src/project/store.c`, `src/platform/windows/files.c`, `tests/storage/test_project_store.c`, `tests/storage/test_archive_validation.c`, `tests/fixtures/projects/`, `docs/project-format.md`.
**Modify:** `include/pointy/platform.h`, `cmake/Dependencies.cmake`, `cmake/Tests.cmake`, `.gitignore` (allow `docs/project-format.md`).

**Interfaces:**

- `bool pp_project_load(const char *utf8_path, PPAssets *assets, PPProject **out, PPError *error)` returns a separate complete candidate; leaves `*out` null on failure.
- `bool pp_project_save(const PPProject *project, const char *utf8_path, PPError *error)` writes a temporary package and atomically replaces the target only after successful close/flush.
- `bool pp_platform_replace_file(const char *temporary_path, const char *target_path, PPError *error)` and UTF-8 file-open helpers in `platform.h` use Windows wide-path APIs or macOS file APIs.
- `PPProjectStoreHooks` is a test-only injectable IO seam for write/flush/replace failure; the normal API uses native defaults.

- [ ] **1. Write `test_portable_round_trip`**: build eight lists sharing one image and two fonts, save, move the original source fixtures, reopen using a new asset store, and assert appearance/default/order equality. Inspect the ZIP: one resource per digest, no absolute paths, no scene/window/display state. Include non-ASCII project paths and a font collection face index.
- [ ] **2. Write rejection tests** for future schema version, duplicate IDs/ZIP names, `../` and absolute paths, symlink entries, missing resources, hash mismatch, invalid face index, restricted-font metadata, and oversized central-directory declarations. Reinspect actual font bytes at load; never trust the manifest's embedding flags.
- [ ] **3. Write `test_atomic_save_failure`**: seed a valid file; inject write, flush, and replace failures; assert its SHA-256 is unchanged and it still reopens. Test an unwritable destination and clean up only temporary files created by that save operation.
- [ ] **4. Run** targets `pointy_store_test` and `pointy_archive_test`, followed by `ctest --preset windows-dev -L storage --output-on-failure`, establishing failures.
- [ ] **5. Implement schema version 1 using yyjson and miniz.** Archive limits: 256 MiB compressed file, 512 MiB total declared/extracted bytes, 64 MiB per source resource, 2 MiB manifest, 4096 entries, 1024 bytes per entry name. Use checked arithmetic and bounded extraction callbacks; do not extract to paths. Validate manifest references and actual decoded resources before returning a candidate. Schema stores canonical text font size and image longest-side size, not competing size fields.
- [ ] **6. Implement same-directory temporary save and native replacement**, releasing all file handles before replace. Save permitted full fonts and notices; the default emoji-only item can retain requested-family metadata without embedding absent Aptos when every used glyph resolves to embedded Noto. Other unresolved glyphs/fonts prevent a successful portable save. Document this and the limits in `docs/project-format.md`.
- [ ] **7. Rerun storage and font tests.** Commit with `feat: save self-contained PointyPointer projects`.

## Task 8: Build settings and project commands

**Files — create:** `include/pointy/app.h`, `include/pointy/ui.h`, `src/app/commands.c`, `src/app/local_preferences.c`, `src/ui/backend.c`, `src/ui/nuklear_config.h`, `src/ui/theme.c`, `src/ui/settings.c`, `src/ui/item_editor.c`, `src/ui/project_actions.c`, `src/platform/windows/dialogs.c`, `tests/core/test_commands.c`, `tests/platform/test_settings_smoke.c`, `docs/third-party-notices.md`.
**Modify:** `src/main.c`, `cmake/Dependencies.cmake`, `cmake/Tests.cmake`, `.gitignore` (allow `docs/third-party-notices.md`).

**Interfaces:**

- `PPApp` owns active project, scene, asset store, selected project path, dirty state, and UI state. `PPCommand` is a tagged command for project actions, item mutations, enable/default/reorder actions, and shared interaction settings. Define opaque `PPSettings` in `ui.h`; define `PPDialogKind` for OpenProject, SaveProject, ImportImage, and ImportFont, with `PPDialogCallback(void *userdata, const char *utf8_path, const PPError *error)` borrowing its result during the callback. Null path and null error mean cancellation.
- `bool pp_app_command(PPApp *app, const PPCommand *command, PPError *error)` is the single mutation entrypoint; UI handlers call it rather than mutating shared internals.
- `bool pp_settings_create(PPSettings **out, PPApp *app, PPError *error)`; `void pp_settings_event(PPSettings *settings, const PPPlatformEvent *event)`; `void pp_settings_draw(PPSettings *settings)`; `void pp_settings_destroy(PPSettings *settings)`.
- `void pp_platform_file_dialog(PPDialogKind kind, const char *suggested_path, PPDialogCallback callback, void *userdata)` reports a chosen UTF-8 path, cancellation, or error; marshal asynchronous callbacks onto the application event queue.

- [ ] **1. Write `test_project_commands`**: edits mark dirty, unchanged values do not; make-default is explicit; Save clears dirty only on success; Save As cancellation leaves state untouched. New/Open/Quit use Save/Discard/Cancel; failed Save cancels the destructive transition. Successful Open clears the scene, failed Open preserves it. Closing settings preserves dirty changes and keeps the application alive.
- [ ] **2. Run** `pointy_commands_test` and `ctest --preset windows-dev -R '^commands$' --output-on-failure`, observing failures.
- [ ] **3. Implement command handling and local preferences.** Remember only successfully opened/saved project paths plus settings geometry in the SDL preference directory. Untouched built-in defaults are clean. A missing last project initializes defaults with an accessible diagnostic and never overwrites the old path/file automatically.
- [ ] **4. Adapt the reference's Nuklear backend and form widgets** into the exact files above, removing model-specific pages and global single-context assumptions. Use one explicit backend instance per UI window. Record original paths/revision and licenses in `docs/third-party-notices.md`. Apply the frontend design skills at execution for this UI while preserving the approved eight-position layout and scope.
- [ ] **5. Build settings controls** for project name/actions/dirty indicator, eight-position selector, enable toggle, ordered list, add/remove/reorder/default, live preview, type-specific content/font/color, size/angle/opacity, and shared interaction settings. Dialog filters accept only supported formats. Show field-specific validation and actual fallback information. Keep settings and its dialogs above the sprite group while open. Use the text pipeline for Unicode item previews so the Nuklear font atlas does not silently replace emoji.
- [ ] **6. Run command tests and the `settings_smoke` desktop check**, covering actual editing, an import, Save As, reopening, window resize, and closing settings while dirty. Capture a settings screenshot for visual inspection and confirm text/controls fit at 100% and 150% scaling. Commit with `feat: add project settings and editing UI`.

## Task 9: Integrate sprite lifecycle and the replacement drawer

**Files — create:** `include/pointy/runtime.h`, `src/app/runtime.c`, `src/app/sprites.c`, `src/ui/drawer.c`, `tests/core/test_runtime.c`, `tests/platform/test_sprite_flow.c`.
**Modify:** `src/app/anchors.c`, `src/app/commands.c`, `src/main.c`, `cmake/Tests.cmake`.

**Interfaces:**

- `bool pp_runtime_create(PPRuntime **out, PPApp *app, PPError *error)`; `void pp_runtime_event(PPRuntime *runtime, const PPPlatformEvent *event)`; `void pp_runtime_tick(PPRuntime *runtime, double now)`; `void pp_runtime_destroy(PPRuntime *runtime)`.
- `bool pp_drawer_open(PPDrawer **out, PPApp *app, uint64_t instance_id, PPError *error)`; `void pp_drawer_close(PPDrawer *drawer)`; drawer selection emits the existing scene-replace operation and never a project-default edit.
- Consumes Task 2 interaction results, Task 3 coordinates, Task 4/5 frames, and Task 6 scene snapshots. `PPRuntime` maps stable instance IDs to surfaces; the scene itself owns no native handles.

- [ ] **1. Write `test_runtime_ownership`**: drag preview creates exactly one instance beyond the threshold; subsequent pulls use the same configured default; replace preserves the four agreed properties; right-click releases resources/surface/capture; an empty originating list opens an empty drawer; deleting a selected item does not invalidate an open instance/drawer. Unknown stale instance IDs are ignored safely.
- [ ] **2. Add `sprite_flow` desktop cases** for drag, cross-window capture, wheel, Alt+wheel, double-click without motion, drawer selection, Escape/outside dismissal, and topmost overlap. Assert transparent areas reach the lower receiver and the presentation focus returns after dismissing a keyboard-focused drawer. Include a stationary pointer over a GIF changing to transparent and monitor removal during drag.
- [ ] **3. Build and run** `pointy_runtime_test` and `pointy_sprite_flow_test`, then `ctest --preset windows-dev -R '^(runtime|sprite_flow)$' --output-on-failure` to establish integration failures.
- [ ] **4. Implement the runtime mapping and UI.** Clamp drawer geometry to the current display, maintain grab offsets across scaling, synchronize frame/shape changes before accepting the next input, and keep preview activation regions limited to their approved area. Raise the interacted sprite only within the application's sprite ordering. If decorative shadows are added, present them in a separate wholly pass-through surface; do not merge their alpha into an interactive layered window.
- [ ] **5. Rerun Tasks 1–3 and 9 desktop checks** because their input path is now integrated; inspect the real default hand and mixed content in the running app. Commit with `feat: connect sprite interactions and replacement drawer`.

## Task 10: Implement macOS platform parity

**Files — create:** `src/platform/macos/surface.m`, `src/platform/macos/events.m`, `src/platform/macos/input.m`, `src/platform/macos/displays.m`, `src/platform/macos/fonts.m`, `src/platform/macos/files.m`, `src/platform/macos/dialogs.m`, `src/platform/macos/tray.m`, `cmake/Info.plist.in`, `tests/platform/test_macos_surface.m`, `docs/testing/macos.md`.
**Modify:** `CMakeLists.txt`, `cmake/Tests.cmake`, `CMakePresets.json`.

**Interfaces:** Implement the same platform/surface functions from Tasks 1–8 with no changes to core call sites. Declare opaque `PPTray` and produce `bool pp_platform_tray_create(PPTray **out, PPApp *app, PPError *error)` and `void pp_platform_tray_destroy(PPTray *tray)` for Task 11's portable tray wrapper. Add only private Cocoa helpers and native state. Enable Objective-C conditionally under `APPLE`.

- [ ] **1. Port the separate-process receiver and alpha/capture/focus cases** to `macos_surface`. Reuse the ring/changing-frame assertions from Task 1, plus Retina display conversion and Space/fullscreen placement. Register the test only on macOS with label `desktop`.
- [ ] **2. Run the initial test on a Mac when available**, using `cmake --preset macos-dev`, `cmake --build --preset macos-dev`, and `ctest --preset macos-dev -R '^macos_surface$' --output-on-failure`. Without a Mac, record these as not run and continue implementing; a Windows cross-check is not equivalent evidence.
- [ ] **3. Implement compact nonactivating Cocoa surfaces** using transparent SDL windows/native handles and a visual-only Cocoa image view for the shared rendered frame. Apply the current alpha shape through supported SDL/Cocoa mechanisms. Account for the pinned SDL Cocoa backend updating `ignoresMouseEvents` from shape samples: test immediate clicks and a stationary pointer during alpha changes explicitly, and refresh native routing on both pointer and frame changes. Do not declare the strict first-click guarantee satisfied by a timer-only heuristic; if the Mac test exposes a limitation, record it and resolve it in this adapter before claiming parity.
- [ ] **4. Implement native input, coordinates, private font discovery, dialogs, atomic file replacement, and menu bar tray behavior.** Use an accessory application policy, nonactivating overlay levels, explicit top-left desktop conversion, Retina raster scaling, and fullscreen auxiliary behavior where supported. Settings remains an ordinary focusable window. Do not request global keyboard-monitoring permissions for functionality that only needs pointer state.
- [ ] **5. Run shared and desktop tests on a Mac when available.** Record build architecture, macOS version, Retina/Space results, and any permissions actually required. Keep missing runtime verification explicit. Commit with `feat: add macOS desktop platform adapter`.

## Task 11: Finish startup, scheduling, packaging, and acceptance

**Files — create:** `src/app/lifecycle.c`, `src/app/scheduler.c`, `src/platform/windows/tray.c`, `src/app/tray.c`, `resources/pointypointer.ico`, `cmake/ApplicationResources.cmake`, `cmake/Packaging.cmake`, `tests/core/test_scheduler.c`, `tests/platform/test_lifecycle.c`, `README.md`, `docs/testing/windows.md`, `.github/workflows/build.yml`.
**Modify:** `src/main.c`, `src/app/runtime.c`, `CMakeLists.txt`, `cmake/Tests.cmake`, `src/platform/macos/tray.m`, `docs/third-party-notices.md`.

**Interfaces:**

- `int pp_app_run(int argc, char **argv)` owns initialization and shutdown order.
- `double pp_next_wake(const PPSchedule *schedule, double now)` returns the next pointer poll, GIF frame, easing/fade tick, UI update, or queued-command deadline.
- `bool pp_tray_create(PPTray **out, PPApp *app, PPError *error)`; `void pp_tray_destroy(PPTray *tray)`.
- Implement the Windows versions of Task 10's native tray hooks. `PPSchedule` contains the next deadline for pointer polling, active easing, preview fade, GIF playback, and UI work; absent deadlines are positive infinity.

- [ ] **1. Write `test_scheduler_deadlines`** asserting static sprites have no redraw deadline, active easing finishes at .120 seconds, a stopped finite-loop GIF has no next-frame wake, and pointer polling stays available with settings closed. Use 60 Hz maximum idle pointer polling, 60 Hz active easing/drag repaint scheduling, and exact GIF deadlines; native input events can wake earlier.
- [ ] **2. Add lifecycle checks** for first launch, reopening the last saved project, missing/corrupt last project, settings close, unsaved Quit cancel, and failure midway through surface/GL initialization. Assert no remaining capture or invisible native surfaces after teardown. Add a bounded `--smoke-test` mode that starts tray/runtime, validates the default resource, and exits without user interaction.
- [ ] **3. Run** targets `pointy_scheduler_test` and `pointy_lifecycle_test`, then `ctest --preset windows-dev -R '^(scheduler|lifecycle)$' --output-on-failure`, establishing failures.
- [ ] **4. Integrate the final event loop and tray** with Settings/Quit, native message pumping, single-threaded platform/GL access, queued dialog results, cached static frames, and resource teardown in reverse ownership order. Preserve modal-dialog responsiveness without recursive command mutation. Reinitialize the tray after Explorer restarts. The executable name is `PointyPointer`.
- [ ] **5. Add relocatable Windows ZIP and macOS app-bundle packaging.** Stage the Noto font, icon, and notices with no developer-machine paths. Keep signing/notarization outside this local build task. Add Windows and macOS CI builds with headless core/media/storage tests; do not imply that CI exercises native pointer routing. Document build commands, SVG supported features, project limits, gestures, and verified/unverified platform coverage in the README.
- [ ] **6. Run final Windows validation:** configure/build both Windows presets; run non-desktop CTest in Debug and Release; run the desktop suite once on the final Debug build; run the packaged Release executable's `--smoke-test`. Then manually verify the spec's two-display/mixed-DPI presentation flow with PNG/GIF/SVG/text/emoji, project source-file independence, drawer replacement, and no unwanted keyboard activation. Mark unavailable hardware cases not run, with concrete follow-up steps.
- [ ] **7. Record observed results and limits** in `docs/testing/windows.md` and `docs/testing/macos.md`. Include measured CPU/render cadence for an idle empty scene, static sprites, and ten active small GIFs on the available machine; do not invent a universal performance target. Run `git diff --check`, verify staged scope, and commit with `feat: package PointyPointer and verify presentation workflow`.

## Coverage and completion criteria

| Spec requirement | Owning tasks |
| --- | --- |
| Tray startup, last project, quit/dirty state | 8, 11 |
| Eight identical lists on every display | 3, 6, 9 |
| Small reveal region, inward preview, grab offset | 2, 3, 9 |
| Visible-shape passthrough and no focus theft | 1, 2, 9, 10 |
| Rotation/resize easing and double-click separation | 2, 9 |
| Replacement and snapshot lifetime | 6, 9 |
| PNG, GIF, SVG, text, emoji and private fonts | 4, 5 |
| Portable package, validation, failed-save recovery | 7, 8 |
| Settings and drawer | 8, 9 |
| Mixed DPI and disconnected displays | 3, 9, 10 |
| C architecture, CMake, macOS adapter, packaging | 1, 10, 11 |

Completion requires a runnable Windows artifact and recorded acceptance evidence, not merely passing core unit tests. Missing macOS hardware or second-monitor access must remain explicit in the report. Native routing defects are functional blockers for the affected platform, not cosmetic issues. Follow the selected execution workflow's review and branch-finishing steps after the task checks pass.

## Planning evidence and references

- Local reference: `Ref/BongoCat/src/platform/windows/windows_layered.c`, `windows_borderless.c`, `src/ui/backend/ui_backend.c`, `cmake/Dependencies.cmake`.
- [Windows layered-window alpha routing](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows).
- [Windows background-window capture limitations](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture).
- [SDL window shape and update cost](https://wiki.libsdl.org/SDL3/SDL_SetWindowShape).
- [Pinned SDL Cocoa shape handling](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/cocoa/SDL_cocoawindow.m).
- [SDL_ttf fallback-font API](https://wiki.libsdl.org/SDL3_ttf/TTF_AddFallbackFont) and [vendored font build configuration](https://github.com/libsdl-org/SDL_ttf/blob/release-3.2.2/CMakeLists.txt).
- [SDL_image animation decoder and metadata declarations](https://github.com/libsdl-org/SDL_image/blob/release-3.4.8/include/SDL3_image/SDL_image.h).
- [PlutoSVG source and supported renderer](https://github.com/sammycage/plutosvg/tree/v0.0.8).
- [Pinned Noto font resources](https://github.com/googlefonts/noto-emoji/tree/v2.051/fonts).

## Review and execution handoff

The written specification is approved. This plan is ready for user review;
implementation has not started. Choose native execution in this session or
subagent-driven execution after reviewing the plan. Native execution is the
recommended starting choice because the native surface/input, resource, and
runtime interfaces are closely coupled; maintaining one implementation context
should reduce repeated handoffs. A final independent review still applies under
the selected execution workflow.
