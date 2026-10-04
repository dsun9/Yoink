# Settings and project commands verification

Verified on Windows on 2026-10-03 using the `windows-dev` preset.

## Automated checks

- Full build completed without compiler warnings or errors.
- `ctest --preset windows-dev --output-on-failure -LE desktop`: 14/14 passed
  in 31.28 seconds, covering commands, model, scene, media, persistence,
  displays, interaction, Windows events, UI events and native dialog queue.
- After the final UI input and popup fixes, focused `ui_events` and
  `dialog_queue` checks passed 2/2 in 0.14 seconds. The hidden actual-renderer
  check also passed at controlled 100%/150% UI scale and narrow sizes.
- `ctest --preset windows-dev -R '^settings_smoke$' --output-on-failure`:
  1/1 passed in 22.60 seconds (22.68 seconds total).
- `designmd lint DESIGN.md`: 0 errors, 0 warnings and one token summary.
  The strict premium audit has zero findings; its web-only source scan does
  not inspect native C implementation or establish accessibility compliance.

The desktop smoke requires a coordinated, explicit native-input interval. It
uses SendInput for app controls and verified owned native control messages for
file chooser filename submission. It checks actual text/name edits, collection
font import and face selection, retained invalid drafts and blocked Save,
Revert, settings-over-sprite click priority, image import, native Save As/Open,
modal Escape/focus return, narrow-window keyboard reachability, and dirty close.
The passing run verified all six cleanup input releases, physical cursor
restoration and original foreground restoration, then removed its isolated
files. Hidden rendering does not acquire foreground or synthesize OS input.

## Captures

The smoke writes local BMP evidence under `build/windows-dev/`. The final five
native captures were inspected:

| Capture | Verified content |
| --- | --- |
| `task8-font-popup.bmp` | Both font collection choices fit inside the window. |
| `task8-settings-100.bmp` | Edited face 1, size and reopened project state. |
| `task8-settings-150.bmp` | Real glyph preview and dirty project presentation. |
| `task8-settings-narrow.bmp` | Settings after resizing to 700×600. |
| `task8-settings-narrow-scrolled.bmp` | Keyboard focus reveals the entire lower Easing field. |

`task8-native-dialog-1.bmp` through `task8-native-dialog-4.bmp` capture only owned
native dialogs for font/image import, Save As and Open. Captures and the CTest
log are generated local artifacts and are overwritten by later runs.

Controlled UI scales were tested on one 3840×2160 physical display at 150%.
This does not establish a physical DPI transition or multiple-monitor behavior.
Nuklear does not provide a UI Automation accessibility tree. Screen-reader,
end-to-end IME, macOS, full runtime scheduler and final tray integration remain
outside this settings-stage verification.
