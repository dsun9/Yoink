# Complete item label checks

Issue #30 follows PR #28 at `48b171b4b4db931b09266a62af17af1b808392c8`.

The Settings row previously derived its label into 128 bytes. The shared
tooltip then copied at most 255 bytes and drew a single line at a fixed
36-unit height. Either limit could hide the distinguishing suffix. The first
regression failed on insufficient tooltip height for 80 uppercase W
characters followed by END_MARKER at 640 by 480. A second regression failed
because overflow had no Full label disclosure.

The backend now owns the complete derived label. Row fitting changes a
separate copy. Help and the disclosure wrap by measured utility-font width
at complete UTF-8 codepoints. Help stays within 12-unit window margins and
uses at most 200 logical units of height. Overflow help gives the instruction
"Select item, then Full label to read all text." The selected item's
disclosure uses four measured lines per page and keyboard-accessible
Previous/Next controls. Its state stays in the editor and resets when content
or selection changes.

`ui_events` checks preserved pending input and focus, independently owned
tooltip text beyond 255 bytes, popup/modal suppression, measured whole-rune
wrapping and actual readable drawing-command text within its rectangles.
It does not assert drawing-call counts.

`settings_labels` runs the actual hidden Settings, Nuklear, font and GL
renderer. It checks:

- Hover and focus expose the 80-W label's END_MARKER at ordinary 1040 by 780,
  minimum 640 by 480 and narrow 700 by 600 logical sizes, at controlled
  100%, 150% and 200% window scales.
- Labels with a common 140-byte prefix retain their different suffixes.
  Covered multibyte letters remain Unicode, whitespace collapses, and an
  unsupported pointing-hand glyph remains U+1F449 in the derived label.
  Original multiline Unicode content remains unchanged.
- A 2113-byte derived label has an explicit overflow instruction. Keyboard
  paging reaches TAIL_COMPLETE without gaps or split codepoints, and Previous
  returns to the preceding page. Project revision and selected preview remain
  unchanged. Pointer help also preserves queued editing with its original
  owner and does not change selection or the default item.
- The shared help renderer fits complete text near right/bottom window edges
  in all nine controlled size/scale layouts, drawing above the anchor when
  there is insufficient space below it.

Fresh BMP captures are saved in the selected build directory with the prefix
`issue30-label-`. Inspected all 18 row focus/hover captures, nine edge captures
and three overflow captures, including the final page. END_MARKER and
TAIL_COMPLETE are readable within their window bounds. The tests use
normalized events and hidden windows, without injecting OS desktop input.

Reproduction on Windows:

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev --parallel 6
cmake --build build/windows-dev --config Release --target pointy_performance_test --parallel 6
ctest --preset windows-dev -R '^(settings_labels|ui_events|ui_density|lifecycle|drawer_hidden)$' --output-on-failure
ctest --preset windows-dev -LE desktop --output-on-failure
build/windows-dev/Debug/pointy_settings_smoke.exe --render-only
cmake --preset windows-release
cmake --build --preset windows-release --parallel 6
ctest --preset windows-release -LE desktop --output-on-failure
```

`settings_labels` has a 90-second CTest timeout. Complete hidden Settings
acceptance should run under a 240-second process bound; the ordinary native
`settings_smoke` timeout remains unchanged. Local runs retain
`issue-30-*.log` output, including the original failing regressions.

Measured Windows results on 2026-10-13:

| Check | Result |
| --- | --- |
| Complete Debug and Release builds | Passed |
| Relevant Debug checks | 5/5, 70.31 seconds, including labels, UI events, density, lifecycle and hidden drawer |
| Complete hidden Settings renderer | Passed under the 240-second process bound |
| Debug non-desktop suite | 38/39 in 174.18 seconds initially; the missing Release helper was then built and the remaining check passed 1/1 in 7.90 seconds |
| Release non-desktop suite | 39/39, 57.50 seconds, including labels and UI events |
| Additional Release checks | Density, lifecycle and hidden drawer passed 3/3 in 33.50 seconds |
| Independent Standards and Spec reviews | No implementation findings |

The initial Debug suite failure was `performance_runner`: its existing
supervisor regression requires `Release/pointy_performance_test.exe` inside
the selected build tree. The fresh Debug tree lacked that helper. Building
the required Release target in that tree resolved the setup failure, and
the unchanged test passed. Original failing output remains in
`issue-30-debug-suite.log`; recovery is in
`issue-30-debug-setup-recovery.log`. The reproduction command above includes
that setup step. Release compilation retains the expected test assertion
override warnings, and configure retains the upstream miniz deprecation.

macOS runtime, physical monitor transitions, real IME interaction and native
accessibility providers are outside the measured coverage of these checks.
