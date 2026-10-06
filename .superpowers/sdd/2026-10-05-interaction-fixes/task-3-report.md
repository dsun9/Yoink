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

## Review repair round 1 (2026-10-06)

Fix base: `cb73385509e7eb4e1ed7d5b7ffeb4797b667a198`. Read independent `task-3-review.md`; repaired all three Important findings in one focused wave. Native/key/raster algorithms are unchanged. Review approval is not claimed.

### Fresh RED before repair

Added actual fresh-draft/budget failure fixtures to `pointy_shaped_editor_test`, with narrow compile-time seams only around SDL_GL_MakeCurrent (inject failure), glDeleteTextures (record current-context ownership), and layout create (count attempts). Real fonts, rasters, GL windows/textures, editing queues and PPAssets reservations are used. The failure modes can be run individually.

Built existing production with the new fixture using `build/interaction-debug-2026-10-05/build-editor.cmd` (VS18 environment, actual CLion Ninja Debug). Build exit0: `build/editor-fix1-red-build.log`. Each child used an owned60s hidden-process bound and stopped at its first assertion:

- `cmake-build-debug/Debug/pointy_shaped_editor_test.exe --failure initial`: exit1, `CHECK(draw(NULL)) failed`, at recovery from an initially oversized1300-W draft (`editor-fix1-initial-red.err`). This differs from the old valid-AB-then-oversized fixture.
- Same executable `--failure context`: exit1, `CHECK(foreign_deletes==0) failed` with another real backend's GL context current and owner MakeCurrent denied (`editor-fix1-context-red.err`).
- Same executable `--failure density`: exit1, `CHECK(pp_ui_shaped_view(ui)==old && error_drawn) failed` after real shared raster budget denial at150% (`editor-fix1-density-red.err`). `error_drawn` inspects emitted Nuklear text commands in the actual field, not just an error variable.

### Repairs and caller contracts

1. Rendering is no longer a precondition for draining draft events. A NULL layout still permits Ctrl+A/clipboard/undo/replacement and composition transactions; geometry-dependent keys/pointer/scroll wait for usable layout. A changed draft can create a successful layout in the same event drain. Exact draft bytes remain authoritative. Fresh oversized and initially budget-denied fixtures both replace their drafts and recover; budget-denied replacements are retained before budget release.
2. `pp_ui_shaped_reset` now returns bool. It binds the owning GL context before any mutation; failure preserves all IDs, CPU layout, queued events and edit state. `pp_ui_backend_reset` propagates a render error before resetting Nuklear/focus/input. `pp_item_editor_sync` releases its newly acquired candidate snapshot and retains the existing editor plus a visible content error on failure. Final teardown calls separate `pp_ui_shaped_destroy`: if binding fails it releases CPU ownership without issuing GL deletes; the backend's eventual owning-context destruction reclaims GPU resources. Tests verify backend-reset error propagation/focus preservation, zero foreign-context deletes, the foreign atlas remaining valid, successful reset retry and final teardown CPU budget release under denied binding.
3. Configuration failures have separate persistent diagnostics from old-layout string updates. One attempted draft copy, target density and shared raster occupancy form the failure signature; repeated unchanged/caret/blink frames do not reload fonts or reraster the failed configuration. Draft/density/occupancy change permits retry. Budget release triggers exactly one successful rebuild; reverting to the already retained density clears a no-longer-applicable configuration error. The signature owns at most one draft-sized string and is freed on replacement/reset/destruction. The old raster/layout and caret state remain usable during failed density replacement.

### GREEN validation

- Debug product/covering targets rebuilt via `build-editor-cover.cmd`, exit0 (`editor-fix1-final-build.log`); final fixture build exit0 (`editor-fix1-final-test-build.log`). Actual `cmake-build-debug/Debug/PointyPointer.exe` is rebuilt.
- `ctest --test-dir cmake-build-debug -C Debug -R "^(shaped_editor|ui_events|ui_density)$" --output-on-failure --stop-on-failure`: **3/3 pass**,4.09s (`build/editor-fix1-debug-final.log`). Default shaped_editor includes all three new failure fixtures alongside prior pixels/editing tests.
- Release rebuilt via `build-editor-release.cmd`, exit0 (`build/editor-fix1-release-build.log`).
- `ctest --test-dir build/windows-release -C Release -R "^(shaped_editor|ui_events|ui_density)$" --output-on-failure --stop-on-failure`: **3/3 pass**,2.45s (`build/editor-fix1-release-final.log`).
- Retained Settings/actions covering result is recorded below after completion; no additional native input or broad desktop suite run for this wave.

### Self-review and deferred minor items

Reviewed reset callers, terminal vs reusable destruction, candidate snapshot reference release, NULL-layout event paths, configuration diagnostic lifetime and retry signature ownership. Failure signatures do not use caret/blink/selection as invalidators. Tests observe actual emitted error commands and distinct GL context resources. Existing bitmap kernel and raster-origin implementation are unchanged.

The review's minor origin-test criticism is valid: the algebraic identity is documentation, not independent origin evidence. An independently measured overhang/image/caret fixture is deferred rather than expanding this focused failure-path repair; current field pixel/density and native caret evidence remain as previously bounded. Existing intentional Release `/UNDEBUG` override warnings are unchanged. Native picker/IME and macOS limitations from the original report remain.

Final retained integration: `cmake-build-debug/Debug/pointy_settings_smoke.exe --render-only` exited0 under the60s owner bound (`build/editor-fix1-settings.log/err`). Actual hidden GL/Nuklear staged imports/validation/cancellation/targets, text and invalid drafts + close/reopen/selection, Save/New shortcut/button actions, and100/150%/narrow rendering all pass.
