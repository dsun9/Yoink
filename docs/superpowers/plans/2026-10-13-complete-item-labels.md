# Complete item labels implementation plan

> Execute this plan inline, with the existing shared UI and hidden Settings renderer as the test boundaries specified in issue #30.

Goal: Show complete derived item labels on hover and focus, with readable bounded help and explicit access to overflow text.

Architecture: Keep label derivation separate from row fitting. Own the complete tooltip source in the backend, wrap by the utility drawing font at UTF-8 boundaries, and add a paged Full label disclosure in the selected item editor when the bounded tooltip cannot show everything. The disclosure uses the same derived label and wrapping as help.

Tech stack: C11, SDL3, OpenGL, Nuklear, CMake and CTest.

Spec: https://github.com/dsun9/Yoink/issues/30, based on PR #28 at 48b171b4b4db931b09266a62af17af1b808392c8.

Constraints: Preserve collapsed whitespace and unsupported-glyph codepoint labels, original Unicode content, actual thumbnails and selected preview. No persisted item names. Help remains an informational overlay and respects popup/modal suppression. Work on codex/issue-30-label-help without altering the original checkout.

## Preserve and wrap labels

- [x] Add a failing hidden renderer regression in tests/platform/test_settings_smoke.c for 80 W characters plus END_MARKER on hover and focus at 640 by 480.
- [x] Run pointy_settings_smoke --labels-only under a bounded timeout and retain the failing output.
- [x] Update src/ui/labels.c and backend.h to allocate the full derived label and share measured UTF-8 wrapping. Keep the existing bounded label API for other controls.
- [x] Update settings.c to fit a separate row copy and supply the complete source to help. Update backend.c to own tooltip text, wrap it with the utility font, and clamp its rectangle within the window.
- [x] Run the regression and shared UI tests.

## Reveal overflow and preserve interaction

- [x] Add a failing hidden regression for labels larger than the bounded help area, requiring a visible instruction and keyboard-accessible Full label disclosure.
- [x] Add the disclosure in item_editor.c with Previous/Next label page buttons and UTF-8 wrapping, keeping its state local to the editor. Reset pages when item content changes.
- [x] Extend test_ui_events.c for full tooltip source, UTF-8 line boundaries, focus/input preservation and popup/modal suppression.
- [x] Extend hidden checks for two labels with a shared prefix beyond 127 bytes, Unicode label policy, preserved item content, unchanged selection/default/preview on hover, and complete overflow paging.

## Validate and review

- [x] Inspect fresh ordinary 1040 by 780, minimum 640 by 480 and narrow 700 by 600 captures at controlled 100%, 150% and 200% scales for hover/focus and window-edge help.
- [x] Build Debug and Release. Run relevant Settings, UI-event and density checks with bounded timeouts, then the full non-desktop suite and complete hidden Settings renderer once at the end.
- [x] Record measured Windows coverage and any platform limitations in docs/testing/complete-item-labels.md.
- [x] Apply the code-review skill against the pinned PR commit and run independent Standards and Spec reviews. No implementation findings remained.

Delivery: Commit only this task's changes to codex/issue-30-label-help and preserve the managed worktree for integration with PR #28.
