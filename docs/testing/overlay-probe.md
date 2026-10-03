# Windows overlay foundation observations

Observed on 2026-10-03: Windows 10.0.26200, MSVC 19.44, one 2560 × 1440 display,
NVIDIA RTX 5090. SDL revision `829a65d769d935c4852f8159e964312c0957260a`.

## Reproduce the desktop routing test

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev --target pointy_windows_surface_test PointyPointer
ctest --preset windows-dev -R '^windows_surface$' --output-on-failure -V
```

The test starts a separate `pointy_input_receiver.exe` process with its own
visible window. The overlay sits above its client area. Test input uses
`SetCursorPos` and `SendInput`, so Windows chooses the recipient. Production
presentation does not replay clicks or toggle transparency on pointer arrival.

| Check | Actual result |
| --- | --- |
| First left-click immediately after arriving at 64 × 64 ring center | Receiver left count 1; overlay left count 0 |
| First left-click immediately after arriving at visible rim | Receiver left count 1; overlay left count 1 |
| Right-click center, then rim, with immediate pointer arrival | Receiver right count 1; overlay right count 1 |
| Rim becomes transparent under stationary pointer; next left-click, without moving pointer | Receiver left count 2; overlay left count 1 |
| Ring transformed by GL through 37 degrees; click rotated hole | Receiver left count 3; overlay left count 1; output 90 × 90; center content alpha 0 |
| Foreground before/after click cases | Same receiver HWND throughout; overlay never became foreground |
| Invalid subsequent presentation | Native overlay removed; next click at its former rim reaches receiver, left count 4 |
| Wheel at rotated hole | Receiver wheel count 1; overlay wheel count 0 |

The last wheel result is an observation. Task 2 must add the wheel/capture
assertions before the complete native interaction gate is accepted.

The GL fixture also checks top-down row order, RGBA channels, premultiplied
opacity, content alpha independent of opacity, and rejection of zero requested
size. On this GPU, half-opacity opaque green/blue reads back as 127; the test
accepts 127 or 128 for the halfway UNORM conversion and requires color and alpha
to match. The partially transparent red reads back as exactly `(64,0,0,64)`.
This tolerance follows the fixed-point conversion rules in the
[OpenGL 3.2 core specification, section 2.1.5](https://registry.khronos.org/OpenGL/specs/gl/glspec32.core.pdf).

## Probe modes

```powershell
.\build\windows-dev\Debug\PointyPointer.exe --overlay-probe ring
.\build\windows-dev\Debug\PointyPointer.exe --overlay-probe changing-alpha
```

Both modes use the production SDL/OpenGL transformer and native presenter.
They place a 37-degree rotated ring at desktop pixel `(220,220)`.
`changing-alpha` alternates each second between the ring and a completely
transparent frame. Ctrl+C stops an interactive run. `--duration-ms N` provides
a bounded smoke run.

Actual bounded runs:

```text
PointyPointer.exe --overlay-probe ring --duration-ms 2000
Overlay probe at (220,220), rotated 37 degrees. Ctrl+C ends the probe.
Probe ring completed: overlay left=0 right=0 wheel=0

PointyPointer.exe --overlay-probe changing-alpha --duration-ms 3000
Overlay probe at (220,220), rotated 37 degrees. Ctrl+C ends the probe.
Probe changing-alpha completed: overlay left=0 right=0 wheel=0
```

Those two runs verify initialization, GL drawing, native presentation, timed
alpha updates, and clean shutdown. No clicks were injected during those separate
probe runs. The counted first-click, foreground, and rotated-hole results above
come from the desktop routing test using the same production presenter.
No screenshot or manual visual acceptance is claimed.

## Presentation mechanism and attribution

The visible surface uses `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`,
topmost placement with `SWP_NOACTIVATE`, and `UpdateLayeredWindow` with a
top-down premultiplied BGRA DIB. `WM_MOUSEACTIVATE` returns `MA_NOACTIVATE`.
Zero-alpha pixels use the native layered-window routing documented by
[Microsoft's Window Features reference](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows).

The GL loading/shader setup and layered DIB presentation selectively adapt
BongoCat's `src/render/gl_api.c` and
`src/platform/windows/windows_layered.c`, under AGPL-3.0-or-later. Attribution
appears in the adapted source files; the repository retains its AGPL license.
The build fetches pinned SDL directly and has no dependency on `Ref/`.

macOS, additional monitors, wheel gestures, and capture are later-task work and
have no runtime acceptance claim here.
