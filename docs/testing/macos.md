# macOS adapter acceptance

Task 10 was implemented and source-reviewed on Windows on 2026-10-04. **No
macOS configure, Objective-C compilation, link, or native runtime test has run.**
The Windows regression results below do not establish Mac parity. In particular,
tiny nonzero alpha, immediate first-click selection, changing-frame hit selection,
background capture/wheel delivery, Retina and Spaces remain acceptance gates.

## Build and automated acceptance on a Mac

Use macOS 11 or newer, Xcode command-line tools, CMake 3.24+ and Ninja. Keep the
pinned dependencies and generated attributed patches. From the repository root:

```sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev -LE desktop --output-on-failure
ctest --preset macos-dev -R '^macos_surface$' -V --output-on-failure
```

All four commands are **NOT RUN** on the implementation host (Windows, no Apple
SDK/runtime). Core/media tests are registered on Apple, including the portable
presenter, runtime and UI-coordinate tests. The existing Windows-only storage
and command test fixtures are not registered on Apple; this is an explicit
coverage gap, not a storage pass. macOS file replacement and dialog behavior
also need the manual acceptance below.

The native test uses the executable itself in a separate `--receiver` process.
That process owns a normal Cocoa window and counts the original mouse-down and
scroll events delivered by WindowServer. The parent injects CGEvents only as a
test driver. Production has no replay or event injection. Arrival and first
down are adjacent with no hover wait. The test fails at the first assertion.
It checks:

- Ring hole versus visible rim, left/right clicks and wheel, including a rim
  rendered with alpha **1/255**.
- Shared OpenGL rotation, transparent outer corner and an actual opaque sample
  selected from the rendered frame.
- A stationary pointer while a visible pixel becomes transparent, then becomes
  visible again; the next click and wheel must go to the correct process.
- Native background focus preservation, accepted-press capture, drag/release
  outside the surface, and wheel during that drag.
- Silent cancellation of one surface without cancelling another owner, plus
  notified cancellation on hide.
- Drawable density versus 64-point desktop bounds using the actual primary
  screen density; settings/drawer priority and conditional prior-focus restore.

The test requires the invoking terminal/test process to have Accessibility
permission to **post test mouse events**. It calls `CGPreflightPostEventAccess`,
does not request permission automatically, and exits nonzero with NOT RUN when
posting is unavailable. This is distinct from production: the adapter requests
no Accessibility permission, installs no global keyboard monitor and observes
only outside mouse-down events for drawer dismissal.

Do not use the pointer while the bounded test runs. Its receiver records the
original foreground application/cursor, releases buttons and restores/verifies
both on normal or assertion exit. The receiver watches parent liveness and a
60-second deadline, kills a stuck parent, then restores input. CTest has a
75-second outer timeout. A missing original application or failed restoration
is a failure, never a reported successful restore. Expected normal duration is
under 15 seconds; permission prompts are not part of that interval.

## Opt-in fullscreen Space and physical display checks

The fullscreen test changes Spaces, so invoke it explicitly after ordinary
native acceptance succeeds:

```sh
./build/macos-dev/pointy_macos_surface_test --spaces
```

This is also **NOT RUN** here. It asks the receiver to enter native fullscreen,
waits at most eight seconds for the fullscreen state, and tests original rim
and hole clicks with the auxiliary overlay in that Space. The same 60-second
receiver watchdog and restoration apply; closing the receiver removes its
temporary fullscreen window. Default CTest output explicitly says the Space
transition was not run. Neither run automates Mission Control or changes its
preferences.

Run once on a physical Retina display and once on a standard-density display.
Record `sw_vers`, `uname -m`, architecture printed by the test, density, console
output, permissions actually required and restoration result. A scale-1 run
prints NOT RUN for Retina. On two physical displays, place one left/above the
primary, drag a sprite across the boundary, wheel/resize during the drag, change
display scale, then disconnect the destination display while dragging. Verify
correct size/sharpness, cursor grab offset, safe release and reachable recovery.
Repeat with the receiver fullscreen on each display and switch Spaces with
Mission Control. These physical cases remain manual and unverified.

Also move an open Settings window from density 1 to 2 and back while retaining
an invalid item draft and focused edit field. Text and the item preview should
refresh sharply without changing the draft, selection, focus or window size.
The `ui_density` core test controls only SDL's density query and exercises the
real hidden Settings/backend, fonts and GL preview in both directions; it is
not physical Mac traversal evidence.

## Additional native follow-up

Open Settings, import PNG/GIF/SVG and a permitted local font through Cocoa
dialogs, cancel each dialog, and verify settings/drawer remain above sprites
without stealing focus after an outside click. Confirm local Aptos discovery
when Office provides it and the bundled Noto fallback when Aptos is absent.
Save/reopen a project with Unicode paths, overwrite an existing file, and try an
unwritable destination; the old file and dirty state must survive rejection.
Exercise tray Settings and Quit after Task 11 connects the portable lifecycle.
The Task 10 tray hook is present but the application does not yet call it.

## Mechanism and evidence limits

Each overlay is an owned `NSPanel` subclass with the nonactivating style, a
transparent visual-only Cocoa view, and a public SDL wrapper using
`SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER` and the corresponding view
property. Frames are drawn as premultiplied RGBA CGImages into native point
bounds. The adapter commits the view and flushes the window on each frame;
there is no polling timer for routing. The panel rejects SDL's whole-window
`ignoresMouseEvents` samples so they cannot replace the native pixel alpha with
a stale hover result. WindowServer's current transparent-window selection is
the candidate authority. Tiny-alpha behavior and event/compositor timing are
still unverified and must pass the separate receiver before parity is claimed.

SDL keeps an ARGB shape copy and the CGImage provider owns a separate RGBA copy:
the platform reports **8 bytes per raster pixel**, reserved in the shared
budget before allocation. CGImage borrows its provider memory. Old and candidate
copies overlap until commit. Shared transform RGBA+coverage uses an additional
temporary 5 bytes/pixel reservation. Native Cocoa backing/WindowServer and GPU
memory remain outside the existing retained CPU budget. Destruction clears
input, unregisters the SDL wrapper before releasing its externally owned panel
and view, and releases the CGImage/provider.

Source references reviewed (documentation supports the API choice, not a pass):

- [Apple native window selection](https://developer.apple.com/documentation/appkit/nswindow/windownumber(at:belowwindowwithwindownumber:))
  uses mouse-down hit-testing and skips transparent points.
- [Nonactivating panel](https://developer.apple.com/documentation/appkit/nswindow/stylemask-swift.struct/nonactivatingpanel)
  requires NSPanel or a subclass.
- [Global event monitor](https://developer.apple.com/documentation/appkit/nsevent/addglobalmonitorforevents(matching:handler:))
  observes original outside events asynchronously; it cannot reroute them.
- [Fullscreen auxiliary behavior](https://developer.apple.com/documentation/appkit/nswindow/collectionbehavior-swift.struct/fullscreenauxiliary).
- Pinned SDL `SDL_cocoawindow.m`, `SDL_cocoashape.m`, `SDL_cocoamouse.m`,
  `SDL_video.c`, and [SDL_SetWindowShape](https://wiki.libsdl.org/SDL3/SDL_SetWindowShape).

Windows verification: Debug 19/19 non-desktop tests; Release 9/9 covering
checks; the final native Release batch 5/5 in 30.91 seconds with verified input,
cursor and foreground restoration. A hidden renderer run passed its batched
editor cases, 100%/150% layouts and narrow layout. The two-plane reservation test
passed separately in Debug and Release. All original outputs are retained as
`build/task10-*.log`; no Mac result is inferred from these Windows runs.

Final shared review added two further hidden regressions after that native
batch: persisted Settings size must use window units, and a density-only
transition must rebuild drawer resources while preserving focus. Both first
failed and then passed in Debug, with Release covering verification recorded in
the task report. Native input/activation code did not change after the batch;
on Windows drawable and desktop scales remain equal.
