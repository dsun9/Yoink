# PointyPointer

PointyPointer is a desktop presentation helper written in C, SDL3 and OpenGL.
Pull PNGs, animated GIFs, static SVGs, text or emoji from eight small positions
around each display. Placed sprites stay above ordinary windows and accept
mouse input only on their visible content. They do not take keyboard focus
from the presentation. The same eight item lists are shared by every display.

## Run

Extract the entire unsigned Windows Release ZIP, keep `resources/` beside
`PointyPointer.exe`, and double-click the executable. Startup is tray-only;
open **Settings** or **Quit** from the tray menu. There is no console or settings
window on ordinary startup. The last successfully saved/opened project is
restored. A missing or corrupt project falls back to the default pointing hand
and leaves a diagnostic in Settings, without overwriting the original file.
Closing Settings preserves its drafts and keeps the application running.
Quit offers Save, Discard or Cancel for unsaved project changes.
If Settings rendering fails, reopening Settings or choosing Quit retries the
retained editor. A persistent failure offers native Retry/Cancel/shutdown choices
without silently losing drafts; saving is offered only when pending input and
fields have been validated.

First launch gives each list the hand `👉`, requested Aptos family, Noto Color
Emoji fallback, 96 logical-pixel font size, full opacity and zero rotation.
Aptos is discovered locally if installed; the distribution bundles only the
permitted Noto emoji font and its OFL. Plain text defaults to black.

| Gesture | Result |
| --- | --- |
| Approach one of eight 16×16 logical-pixel edge/corner regions | Reveal a faint inward preview |
| Left-drag a preview | Create a temporary sprite, preserving the grabbed point |
| Left-drag a sprite | Move it directly |
| Wheel | Rotate by 10° per notch with 120 ms easing |
| Alt+wheel | Resize by 10% per notch, with reciprocal downward steps |
| Right-click | Delete the sprite |
| Double-click | Open its originating list in the replacement drawer |
| Escape or outside click in drawer | Close the drawer |

Replacement keeps the instance's center, rotation, opacity and longest side,
uses the new content's aspect ratio, and restarts a GIF. Editing/removing a list
item affects future pulls; existing sprites keep their snapshots. Transparent
padding, holes and letter gaps pass the original click and wheel to the window
below. Changing projects clears temporary sprites only after a successful load.

## Projects and supported content

Settings provides eight shared ordered lists, default selection, enable/disable,
text/emoji appearance, permitted font import, and PNG/GIF/SVG import. Ctrl+N,
Ctrl+O, Ctrl+S and Ctrl+Shift+S select New, Open, Save and Save As. Invalid drafts
must be corrected or reverted before project transitions; hiding Settings
retains them. Dialog results are applied on the serialized main thread.

A `.pointy` project embeds owned source bytes, full permitted font files and
notices. It does not depend on the original imported paths. The Unicode project
name is portable data, independent of the filename; Save As or renaming the
package does not change that name. Temporary scenes, window positions and
per-display overrides are not saved. Local preferences store only the last
project path and Settings geometry.

PNG alpha and composited GIF disposal/finite looping are supported. Static SVG
supports paths, basic shapes, groups, transforms, `viewBox`, local `use`, solid
fills/strokes and linear/radial gradients in the validated renderer subset.
Unsupported features are rejected with diagnostics: scripts, external resources,
CSS stylesheets, SVG animation, filters, masks, clipping, SVG text and embedded
images. Convert unsupported SVGs to PNG before import.

Media is limited to 8192 pixels per axis, 4096 GIF frames, 256 MiB decoded per
resource and 512 MiB of retained CPU raster data across assets, text and native
presentation copies. GPU allocations and source/decoder scratch are separate.
An import accepts at most 256 MiB of source bytes; portable packages are stricter:
64 MiB per source, 512 MiB total declared/extracted data, 2 MiB manifest, and
4096 ZIP entries/items. Project names are 1–255 UTF-8 bytes. Font embedding must
be installable/editable for every face; unavailable families or missing glyphs
can block portable saving. See [project format](docs/project-format.md) for the
complete schema and validation rules.

## Build and check

Windows requires x64 Visual Studio 2022 C/C++ tools, Windows SDK, Git and CMake
3.24 or newer. Dependencies are pinned and fetched at configure time. No reference
checkout is a build dependency. The static CRT is selected consistently, including
SDL and Expat. From a PowerShell terminal in this repository:

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev --parallel
ctest --preset windows-dev -LE desktop --output-on-failure
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release -LE desktop --output-on-failure
cpack --config build/windows-release/CPackConfig.cmake -C Release
```

`PointyPointer.exe --smoke-test` starts the real tray/runtime with isolated local
preferences, loads the default resource and exits after approximately one second
of event-loop operation. Automated callers should impose an outer timeout for
native driver/startup failures. `--settings` explicitly opens Settings at startup.
The older bounded `--overlay-probe MODE --duration-ms N` diagnostics remain.

Desktop tests move the pointer/type keys. On an available interactive Windows
desktop run `powershell -NoProfile -ExecutionPolicy Bypass -File
tests/platform/run_acceptance_desktop.ps1`; it runs all desktop tests once,
stops on failure and uses a 180-second outer timeout plus verified restoration.
Do not run desktop tests concurrently. Release test assertions remain enabled.

On macOS 11+, install Xcode command-line tools, Git, CMake and Ninja, then:

```sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev -L headless --output-on-failure
cpack --config build/macos-dev/CPackConfig.cmake -C Debug
```

The bundle is `build/macos-dev/Debug/PointyPointer.app`. SDL's bundle base path
resolves `Contents/Resources`; the application loads its `resources/` child.
The Windows ZIP and Mac bundle are unsigned. Signing/notarization is outside
this build. CI is configured to build Windows and macOS and run **headless**
core/media/storage tests, separately from real pointer-routing acceptance.

## Coverage and licensing

Windows is the first validated platform. The available host has one 3840×2160
display at 150%; simulated geometry/density checks do not establish physical
mixed-monitor behavior. Physical two-display traversal/disconnect/fullscreen
acceptance remains not run. No Mac, Apple SDK or Mac runtime was available:
Mac configure/compile/link, tiny-alpha/first-click routing, capture/wheel,
Retina traversal and Spaces remain unverified. See the concrete evidence and
follow-up instructions in [Windows testing](docs/testing/windows.md) and
[macOS testing](docs/testing/macos.md).

PointyPointer uses AGPL-3.0-or-later adapted BongoCat runtime/UI code. Preserve
[LICENSE](LICENSE), [third-party notices](docs/third-party-notices.md), the packaged
license texts and corresponding source when redistributing. The Noto font is
under SIL OFL. No Aptos or Segoe UI font is shipped in the application package.
