# Settings and project commands verification

Verified on Windows on 2026-10-03 using the `windows-dev` preset.

## Original implementation checks (`ab3083a`)

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

## Event-ordering fix checks

The subsequent review fix defers project shortcuts, project buttons, close
requests and commands that change the editor's selected item until the current
draw has consumed and validated pending field edits. Remove confirmation also
waits for that validation; closing retains invalid drafts for reopening.

- `build/windows-dev/Debug/pointy_settings_smoke.exe --render-only` passes 20
  real settings/Nuklear cases: valid item text or an invalid size, followed by
  Save or New before one draw, through shortcuts or normalized pointer/button
  events. Checks cover the saved package, clean/dirty state, retained invalid
  draft, preserved project generation and unsaved-change confirmation. The
  additional 12 cases cover text or invalid size followed by native close,
  Close settings, anchor selection, item selection, Add text or Remove item.
  They verify close/reopen retention, edits staying on the original item and
  invalid drafts blocking selection/list changes and removal confirmation.
- That same hidden run also passes the existing popup, scale and narrow-layout
  checks. Hidden windows never acquire foreground; no OS input is synthesized.
- Affected app/test targets build without compiler warnings or errors.
  `ctest --preset windows-dev -R '^(ui_events|dialog_queue|commands)$'
  --output-on-failure`: 3/3 passed in 9.30 seconds on the final amended code.

The 22.60-second native pass above predates this event-ordering fix. No native
input run was performed for this amendment; later integrated native gates
remain separate.

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
