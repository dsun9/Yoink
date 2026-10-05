# Windows acceptance

Task 11 was validated on Windows 11 Pro build 26200, Ryzen 7 7800X3D
(8 cores / 16 logical processors), RTX 5090 driver 32.0.16.1062, and one
3840×2160 display at 150%. No second physical display or Mac is available.

## Reproduction

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev --parallel 6
ctest --preset windows-dev -LE desktop --output-on-failure
cmake --preset windows-release
cmake --build --preset windows-release --parallel 6
ctest --preset windows-release -LE desktop --output-on-failure
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Debug
cpack --config build/windows-release/CPackConfig.cmake -C Release
```

Native tests synthesize mouse/keyboard input, use their owned receiver windows,
stop on first failure, and release input and verify cursor/foreground restoration.
The wrapper has a 180-second outer timeout and kills only its owned test tree.
Focused diagnostics can select `-TestRegex '^sprite_flow$'` and a distinct
`-LogPrefix`; preserve original failed-run evidence instead of overwriting logs.
No test restarts or terminates the user's Explorer process.

## Current evidence

- Final amended non-desktop Debug: **22/22**, 54.07 s, exit 0.
- Final amended non-desktop Release: **22/22**, 30.67 s, exit 0. Test targets explicitly
  undefine NDEBUG; assert-based operations/checks remain enabled. MSVC reports
  the intentional `/DNDEBUG` → `/UNDEBUG` override warning.
- Final amended Debug desktop: **6/6**, 53.63 s, exit 0 (`task11-final4`).
  Sprite flow, Settings, GIF, native surface, capture and anchors all pass.
  The wrapper verified released input and restored original cursor/foreground;
  no owned application/test processes remained afterward.
- Packaged Release: extracted into a unique temporary directory and run from
  `C:\`, `--smoke-test` exited 0. Normal Windows `UseShellExecute` launch found
  the owned SDL tray and hidden renderer, with no visible Settings or console,
  preserved foreground focus, and exited 0 through the registered tray Quit
  callback. This is an automated shell launch, not a physical double-click.
  No input was synthesized. PE subsystem is Windows GUI; imports are OS DLLs
  only, with no external MSVC or third-party runtime DLL requirement.
- ZIP inventory includes the executable, Noto font/OFL, original icon, README,
  project/acceptance documentation and full dependency licenses. No Aptos or
  Segoe font is included. An initial CPack component error omitted the executable;
  the install rule was corrected and the extracted checks use the fixed ZIP.
- Lifecycle covers actual tray-only construction, retained Settings/invalid
  drafts, blocked invalid Quit, dirty Quit Cancel, saved Unicode-name restore,
  missing/corrupt-project fallback without overwrite, and failure after a native
  hidden GL window is allocated. Teardown leaves no SDL windows or capture.
- Owned tray reconstruction deletes only the application's notification icon,
  delivers TaskbarCreated to its owned SDL_TRAY window, and verifies the shell
  icon exists again. This is simulated shell notification handling, not an
  observed real Explorer restart.
- Scheduler checks static infinity, closed-Settings pointer availability,
  60 Hz scheduled ticks with the exact 120 ms easing endpoint, exact GIF timing,
  and an actual exhausted finite-loop GIF with no next-frame deadline.
- Initial native attempts found an Explorer #32768 popup menu over the owned
  receiver at the ring's alpha-zero hole. The creator of that menu is unknown.
  A bounded test preflight cancels only actual active native menu modes through
  their menu owner, then verifies the receiver before injecting input. One
  diagnostic's inner cursor restoration failed; its outer wrapper corrected and
  verified cursor, input and foreground. Other inner/outer restorations passed.
- Subsequent native Settings runs exposed a real keyboard-focus defect: a
  scrolled, clipped anchor control claimed the toolbar New click, corrupting
  the focus origin saved for the confirmation. Event/ID tracing proved this;
  there were no late pointer/key events. A failing normalized regression and
  narrow backend fix now require active, input-enabled, visible panel hit
  eligibility. Hidden scrolled New/Escape and retained-field workflows pass.
  Focus restoration happens at the next frame boundary, so the desktop fixture
  waits for the actual state (bounded 1 s), without injecting extra keys.

Original logs are retained as `build/task11-*.log`. Mac build/runtime and
physical mixed-monitor/Retina/Spaces results are not inferred from these checks.
The startup harness initially needed Unicode P/Invoke and UIntPtr corrections.
Its generic posted thread `WM_QUIT` was consumed by SDL's message pump; the
successful check invokes the actual owned tray Quit callback using the pinned
SDL fresh-process menu ID. Production code uses registered entry callbacks and
does not assume that ID. Failed harness runs terminated only their owned process.

## Measured application loop

### Failed Settings recovery amendment

Review found that a retained failed Settings renderer prevented both reopening
Settings and tray Quit. Settings/ Quit now attempt one draw using the retained
backend and editor; success preserves pending drafts and the original failure
diagnostic. If rendering fails again, a native dialog offers Retry Settings,
Cancel, and explicit Quit without saving. The discard choice clearly includes
both unsaved project changes and pending editor drafts. Save project and quit
is offered only when input and drafts are validated and no file chooser is open;
the existing Save As success/cancel callback still drains while rendering fails.

The lifecycle fixture injects actual hidden Settings GL context failure and
controls only the native dialog choices/results. It covers clean/dirty projects,
invalid and queued valid drafts, repeated failure, Cancel/Retry retention, explicit
discard, validated Save and Save As success/cancel. The native recovery dialog
itself was **not interactively exercised**. The six-test native pass above predates
this amendment; pointer routing and ordinary rendering code did not change.
Focused lifecycle, UI events/density, hidden drawer and command checks pass in
Debug **5/5, 48.59 s** and Release **5/5, 29.33 s**. The full hidden Settings
retained-field batch also passes. Original failing lifecycle regression is retained
as `task11-r1-red.log`; subsequent checks use `task11-r1-*` logs.

### Loop measurement conditions

Release `pointy_lifecycle_test.exe --benchmark` uses the same lifecycle iteration
as the application: native/SDL events, dialog polling, tray queue, runtime,
real display enumeration and pointer query, scheduler and wait. A fixture
translates returned display bounds outside the physical desktop, so native
surfaces and actual GL transforms/uploads run offscreen. Settings stays closed;
there is no visible-compositor/fullscreen performance claim. The only production
call substitutions are counted native presentation, controlled GL failure and
that geometry translation; ordinary benchmark GL/native calls are real.

Each case warms up for 2 seconds, then measures approximately 5 seconds.
A render means one successful pp_surface_present call. Static/GIF cases have ten
independent sprites sharing one asset; PNG/GIF sources are 3×1 pixels, shown at
48 logical-pixel longest side (72×24 drawable pixels at the host's 150%). The
GIF alternates two frames every 100 ms and loops indefinitely. Empty scenes
have no placed sprites. This is one observed machine/run, not a universal target.

| Scene | Interval s | CPU seconds | One-logical CPU % | Total-machine CPU % | Loop iterations | Presents |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Empty | 4.995988 | .015625 | .313 | .020 | 287 | 0 |
| Ten static PNGs | 5.000281 | .015625 | .312 | .020 | 288 | 0 |
| Ten small GIFs | 5.000221 | .328125 | 6.562 | .410 | 300 | 500 |

CPU comes from GetProcessTimes kernel+user time. 100% in the first percentage
column means one logical processor fully busy; the machine column divides by
16. The accounting quantum observed is 15.625 ms, so low idle figures have
limited resolution. The earlier preliminary run recorded zero empty/static CPU
increments, which means below the accounting quantum, not proof of zero work.
The final table is the isolated full-loop measurement, with no concurrent GL
suite/build. Ordinary desktop/background activity was not suppressed. Earlier
presenter-only measurements are retained separately and are not app idle figures.

## Follow-up on physical hardware

On a second physical display with a different scale, arrange one display at a
negative origin. Put a slide deck or input receiver behind the overlays. Pull
PNG/GIF/SVG, multiline text and emoji from every anchor, including the shared
edge; confirm eight identical lists, inward previews, preserved grab offset and
smooth cross-display capture. Rotate/resize during a drag, release in transparent
padding, and confirm the next underlying click/wheel goes through. Replace via
the drawer and verify center/angle/opacity/longest side and new aspect ratio.
Move Settings between scales while retaining an invalid draft and keyboard focus.
Disconnect the display holding a sprite and verify recovery onto a remaining
screen. Repeat above an ordinary/fullscreen presentation without keyboard focus
moving to sprites. These physical traversal/disconnect/fullscreen cases are
**NOT RUN** here; simulated geometry/density tests are separate evidence.

For portable projects, import all media plus permitted font bytes, save under a
Unicode project name, move/delete the original source directory and rename the
`.pointy` package. Reopen from its new location and confirm content and the
original internal project name survive. The automated store/command checks cover
this source-byte ownership and name/filename separation; a complete physical
two-display presentation walk-through remains unavailable.
