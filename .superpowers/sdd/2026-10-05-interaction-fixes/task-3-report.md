# Task 3 implementation report — shaped color emoji item text

Status: DONE_WITH_CONCERNS (implementation and covering tests pass; native picker/IME selection and general bidi remain limits). Independent review is pending, not claimed here.

## Implementation

Only `item-text` dispatches to the new custom widget. All other settings fields retain Nuklear editing. `PPItemEditor.text` remains the authoritative draft, with original validation/commit/staged action behavior.

- `src/ui/text_layout.{c,h}` owns complete Segoe UI (Helvetica on macOS) and bundled Noto Color Emoji font bytes, a common inspected bitmap-strike shaping size, retained SDL_ttf text for each hard line, copied cluster metadata, unique byte caret stops, CPU row rasters and explicit raster-origin offsets. Blank/trailing rows and spaces have geometry. Successful candidate swaps preserve the prior view on failure. Cached identical text and unchanged font/density/ink do not reshape/raster. Retained CPU frames reserve/release against the app's shared PPAssets raster budget; standalone fixture backends own a private manager.
- `src/ui/edit_state.{c,h}` owns byte caret/anchor, preferred vertical X, scroll, full separate preedit and SDL character-count range, frozen composition replacement range, and 64 undo/redo snapshots. One complete SDL committed payload or paste is one transaction. Actual SDL_ttf cluster stops preserve tested ZWJ/modifier/VS16/flag/accent sequences.
- `src/ui/shaped_edit.{c,h}` owns field drawing, queued copied event payloads, white-tinted GL row textures, selection/caret/marked composition, candidate positioning and error text. Field images translate raster origin while caret remains in layout DIP. Pointer/selection/blink/scroll do not reraster unchanged text. Backend reset/destroy and item external sync/destroy dispose custom state and resources. Own model commits preserve undo/caret; density refresh rebuilds raster while preserving editing state.
- SDL polling preserves existing scalar events for ordinary controls and attaches the complete committed payload to the first scalar, plus continuation markers. Composition retains full text and character start/length. `pp_ui_poll_reset(window_id)` discards a previous retained payload tail on backend reset/destruction.
- Existing text raster API remains a wrapper around a new origin-returning internal variant. The optimized bitmap kernel is untouched. Old missing-glyph placeholder explanation was removed once actual field pixels passed.

The parent clarified that the existing CMake manifest hunk must remain unstaged, while this task's separate source/link wiring hunk should be committed conventionally in CMakeLists.txt. `.codegraph/`, Ref/BongoCat, and unrelated plan remain untouched.

## Original RED before production changes

Added a backend-only hidden field fixture (no app, surface, low-level hook or preview). It draws inactive item-text and scans its strict interior for the hand emoji's yellow/orange RGB signature; no focus/selection/preedit highlight can satisfy it.

Build: VS18 Community `VsDevCmd.bat -arch=x64 -host_arch=x64`, then:

```
C:/Users/Reon/scoop/apps/cmake/current/bin/cmake.exe --build D:/Dev/PointyPointer/cmake-build-debug --target pointy_shaped_editor_test --parallel 4
cmake-build-debug/Debug/pointy_shaped_editor_test.exe
```

Original output, exit1 (`build/editor-original-red.err`):

```
field-only density=1.00 colored pixels=0
...test_shaped_editor.c:34: CHECK(colored > 15) failed
```

Original build log: `build/editor-red-build.log`. Original image retained at `build/interaction-debug-2026-10-05/original-worker-shaped-editor-field.bmp`; parent also saved evidence under `editor-evidence`.

## GREEN and commands

All worker launches owned their process handles, redirected output, used hidden windows and a 60,000ms deadline with owned-tree termination on timeout. Native input/show tests were launched only by the root supervisor, which restored pressed keys, cursor and original foreground. Commands below are the child commands; build command scripts retain the exact compiler setup.

1. Final Debug build: `build/interaction-debug-2026-10-05/build-editor-cover.cmd`, targets PointyPointer, shaped_editor, ui_events, ui_density, settings_smoke, lifecycle, text (`pointy_*_test` names). Final changed targets rebuilt in `build/editor-i10-build.log`, exit0. Actual CLion `cmake-build-debug/Debug/PointyPointer.exe` rebuilt.
2. `ctest --test-dir cmake-build-debug -C Debug -R "^(shaped_editor|ui_events|ui_density|text)$" --output-on-failure --stop-on-failure`: **4/4 pass**, 7.17s (`build/editor-debug-green-i10.log`). Field-only color counts **121 at100%,273 at150%**. Five A+emoji+B fixtures each have4stops; accent has3. Covers exact click/left/right/shift/delete/backspace/replacement/clipboard boundary UTF8, complete-payload atomic undo/redo, empty/space/trailing-space/newline/CRLF rows, preferred-X, caret scroll, full preedit cancel/commit/character ranges, reset payload disposal, copied-generation/raster-origin mapping, budget failure old-view retention/release, oversized retained draft recovery and focus-loss queued committed text.
3. Release build: `build/interaction-debug-2026-10-05/build-editor-release.cmd`, same targets against `build/windows-release --config Release --parallel 4`, exit0 (`build/editor-release-build.log`). Existing dependency configuration warnings and intentional /UNDEBUG override warnings remain; no source build failures.
4. `ctest --test-dir build/windows-release -C Release -R "^(shaped_editor|ui_events|ui_density|text)$" --output-on-failure --stop-on-failure`: **4/4 pass**,5.41s (`build/editor-release-green.log`).
5. `cmake-build-debug/Debug/pointy_settings_smoke.exe --render-only`: exit0 (`build/editor-settings-final.log/err`). Actual hidden GL/Nuklear staged imports, validation, cancellation/targets, retained invalid draft and text + close/reopen/selection, Save/New shortcuts and buttons all pass, as do100/150% and narrow layouts.
6. Updated `pointy_performance_test` built through `build-editor-warm.cmd`; `cmake-build-debug/Debug/pointy_performance_test.exe --drawer-warm`: exit0 (`build/editor-warm-final.log/err`), PASS runtime warm cache first/repeated opens, changed instances, reset input/focus/modal state, density rebuild and teardown.
7. Root-supervised native `pointy_shaped_editor_test.exe --native-input`: corrected Debug attempt exit0 (`build/editor-native-input-debug2-desktop.log/stderr`). **OS SendInput UTF16 -> SDL -> actual field** verifies A+woman-technologist+B bytes,4stops, physical Home/Right/ShiftRight/Backspace/CtrlZ, selection/delete/undo. Root restored input/cursor/foreground and confirmed clean process audit. Image: `cmake-build-debug/shaped-editor-native-input.bmp`.
8. Root-supervised `--native-observe`: exit0 after20s, actual show/render/focus, no native committed/preedit payloads observed (`build/editor-native-observe-desktop.log/stderr`). **Not picker or native IME evidence.**
9. Root-supervised `ctest -R ^lifecycle$` Debug passed1/1 in17.26s (`build/editor-cover-lifecycle-debug-desktop.log`); run preceded final poll-reset helper, whose new behavior is covered by updated ui_events/shaped reset/warm test. Parent plans final integrated lifecycle after later tasks.

## Preserved intermediate failures and fixes

- `editor-i1-build`: undefined `nk_white`; replaced with explicit white RGBA.
- `editor-i4-build`: PPAssets missing header; fixed direct include.
- `editor-settings-i5`: ordinary size edit/staged action failed because widget called nk_edit_unfocus unconditionally. Restricting it to focused item-text restored original normal field behavior; hidden staged actions then passed i6 and final.
- `editor-cover-i5`: old atlas numeric texture ID was reused by new row upload, making `!glIsTexture(old_id)` invalid after the frame. Final test retains changed atlas identity, correct body pixels, exact preview density factor and queries any reused old ID's dimensions against actual current row frames; it must no longer hold an atlas.
- `editor-cover-green.log` (despite filename, **FAIL**): querying Nuklear atlas.tex_width/height after atlas_end was invalid; pinned Nuklear resets these fields. Final query checks actual GL dimensions/error plus ownership above.
- `editor-debug-green-final.log` (despite filename, **FAIL**): adding pp_ui_begin before pp_settings_draw preempted settings.c's previous_scale comparison and prevented preview_dirty. Removed extra begin; original exact preview factor assertion preserved and final normal production flow passes.
- `editor-native-input-debug-desktop`: native Home/Right failed after correct Unicode bytes/4stops, because fixture omitted extended-key/scancode descriptors and represented numpad equivalents. Corrected native fixture with KEYEVENTF_EXTENDEDKEY and MapVirtualKey; root-supervised Debug2 passed. No production arrow workaround added.

## Self-review and limitations

- Shaping boundaries implement a tested LTR emoji visual-boundary policy, not full UAX29/bidi/typographic ligature caret support. No prefix reshaping or ad hoc ZWJ-only segmentation. Whole hard lines retain shaping context; no soft wrapping added.
- Native picker selection and installed-IME composition confirmation were not exercised; deterministic full SDL payload/range/cancel/commit tests pass. No installed IME settings changed. macOS branch builds/runtime were not available for validation.
- Native-input fixture tests OS Unicode keyboard input, not a Windows emoji-picker selection. Undo atomicity is defined per SDL payload, while individual OS Unicode messages can arrive as separate payloads.
- Clipboard fixture now compiles real edit_state.c with only SDL clipboard boundary functions redirected to owned test storage, proving exact bytes without replacing user clipboard formats. Earlier i3/i5 fixtures used the actual clipboard and may have left the flag fixture there; prior clipboard contents were not captured and cannot be reconstructed. Root was informed immediately on finding this.
- CPU retained rasters use shared budget; font bytes/GPU textures/transient renderer scratch retain existing exclusions. Font-density or text failure leaves prior CPU layout, keeps draft and displays renderer error; Ctrl+A/replacement/undo remain usable for an oversized draft.
- Snapshot undo is bounded to64 transactions; field draft capacity remains the pre-existing item buffer capacity.
- No push, PR, branch change, unrelated cleanup, or independent review claimed. All post-implementation work was sequential; no helpers spawned.

## Changed paths

CMakeLists.txt (only source/link hunk), cmake/Tests.cmake; include/pointy/platform.h and ui.h; src/media/text_raster.c/h; src/ui/backend.c/h,events.c,item_editor.c; new src/ui/text_layout.c/h,edit_state.c/h,shaped_edit.c/h; tests/platform/test_shaped_editor.c,new density/event assertions; this report.
