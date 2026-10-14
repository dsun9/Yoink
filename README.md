# Yoink

Yoink is a desktop presentation helper written in C, SDL3 and OpenGL.
Pull PNGs, animated GIFs, static SVGs, text or emoji from eight small positions
around each display. Placed sprites stay above ordinary windows. On Windows,
their grab areas approximate the artwork and let mouse input pass through
sufficiently large transparent holes, padding and rotated corners. They do not take keyboard focus
from the presentation. The same eight item lists are shared by every display.

## Run

Extract the entire unsigned Windows Release ZIP, keep `resources/` beside
`Yoink.exe`, and double-click the executable. Startup is tray-only;
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

First launch gives each list the hand `👉`, installed Segoe UI Emoji artwork,
96 logical-pixel font size, full opacity and zero rotation. The ordinary text
family remains Aptos and is discovered independently. Font lookup checks the
actual family and collection face metadata. If Segoe UI Emoji is unavailable,
the bundled Noto Color Emoji keeps the hand usable and the item preview names
that fallback. Anchor, Settings and drawer previews use the same chosen font
bytes as pulled sprites. Plain text defaults to black.

The renderer uses supported scalable color layers when the selected font has
them. It inspects the actual installed font's capabilities and version; exact
native COLRv1 paint parity is outside the current implementation. Regional
indicator sequences follow the font's coverage and may appear as separate
letters. The application package contains Noto and its OFL, and never includes
installed Segoe UI Emoji or Aptos bytes.

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
item affects future pulls; existing sprites keep their snapshots. Original input
outside sprite and preview grab areas reaches the window below. Windows GIFs
keep one grab area covering artwork from any animation frame, including during
blank frames. Hidden, disabled, wholly empty-source and zero-opacity content
releases its accepted input. Small transparent gaps and space near edges can
remain interactive; this approximation gets coarser at larger displayed sizes.
The [grab-area design](docs/superpowers/specs/2026-10-12-approximate-grab-area.md)
and [Windows validation](docs/testing/sprite-grab-area.md) document the limits.
Cocoa retains rectangular routing and empty-frame hiding until a native follow-up.
Changing projects clears temporary sprites only after a successful load.

Previews and sprites share immutable artwork and cached CPU sources. PNG/GIF
keep native resolution, bitmap text keeps useful strike detail, and scalable
text/SVG prepare the latest size while retaining available artwork. Windows
anchors and sprites draw directly into transparent GL windows; Settings and
drawer retain context-local GL preview textures without normal readback.
Settings content commits restart its own clock. Rename, reorder and pose-only
edits reuse sources; drawer GIF thumbnails intentionally show frame zero.

## Projects and supported content

For sharp large emoji with Twemoji artwork, use the optional font or portable
example described in [Optional scalable emoji](resources/optional-fonts/README.md).
It is an explicit choice; installed Segoe UI Emoji remains the default artwork.

Settings provides eight shared ordered lists, default selection, enable/disable,
text/emoji appearance, permitted font import, and PNG/GIF/SVG import.
Each item has an editable menu label shared by its Settings row and replacement
drawer row. An empty label uses the content description. Text items have an
installed-font dropdown and an import action for portable font files.
Ctrl+N, Ctrl+O, Ctrl+S and Ctrl+Shift+S select New, Open, Save and Save As. Invalid drafts
must be corrected or reverted before project transitions; hiding Settings
retains them. Import and replacement choosers open only after queued edits have
been consumed and validated; cancellation preserves those edits. Dialog results
are applied on the serialized main thread.

A `.pointy` project embeds owned source bytes, full permitted font files and
notices. It does not depend on the original imported paths. The Unicode project
name is portable data, independent of the filename; Save As or renaming the
package does not change that name. Temporary scenes, window positions and
per-display overrides are not saved. Local preferences store only the last
project path and Settings geometry.
Yoink continues to read the existing PointyPointer preference directory and
keeps the installed-app identifiers. Existing `.pointy` files retain their
extension and schema; the rename does not rename project files or migrate
preferences.

PNG alpha and composited GIF disposal/finite looping are supported. Static SVG
supports paths, basic shapes, groups, transforms, `viewBox`, local `use`, solid
fills/strokes and linear/radial gradients in the validated renderer subset.
Unsupported features are rejected with diagnostics: scripts, external resources,
CSS stylesheets, SVG animation, filters, masks, clipping, SVG text and embedded
images. Convert unsupported SVGs to PNG before import.

Media is limited to 8192 pixels per axis, 4096 GIF frames, 256 MiB decoded per
resource and 512 MiB of retained CPU raster data across assets, text and native
presentation copies, shared preview sources, and pending candidates. Worker
candidates have a 64 MiB cap; immutable captures have 32 MiB per job and 64 MiB
total caps, with one running and one waiting job. Worker scratch has a separate
384 MiB policy. App-owned native/UI content source and UI output textures share
a 256 MiB GPU budget, including old/candidate overlap. Driver/compositor memory
and the control-label atlas are outside these content budgets. See
[rendering ownership](docs/architecture/rendering-content.md).
An import accepts at most 256 MiB of source bytes; portable packages are stricter:
64 MiB per source, 512 MiB total declared/extracted data, 2 MiB manifest, and
4096 ZIP entries/items. Project names are 1–255 UTF-8 bytes. Font embedding must
be installable/editable for every face; unavailable families or missing glyphs
can block portable saving. When Aptos is absent, text fully covered by embedded
permitted Segoe UI Emoji or Noto Color Emoji remains portable, including multiple or multiline
emoji. See [project format](docs/project-format.md) for the
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

`Yoink.exe --smoke-test` starts the real tray/runtime with isolated local
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

The bundle is `build/macos-dev/Debug/Yoink.app`. SDL's bundle base path
resolves `Contents/Resources`; the application loads its `resources/` child.
The archives are named `Yoink-0.1.0-Windows-AMD64.zip` on Windows x64 and
`Yoink-0.1.0-Darwin-<architecture>.zip` on macOS. Keep the complete resources
and licenses from the archive with the application. The internal CMake target
remains `PointyPointer`, so existing `--target PointyPointer` commands build
the renamed executable or bundle.
The Windows ZIP and Mac bundle are unsigned. Signing/notarization is outside
this build. CI is configured to build Windows and macOS and run **headless**
core/media/storage tests, separately from real pointer-routing acceptance.

Pushing a tag triggers both Release builds and their headless tests. After both
jobs succeed, CI attaches the Windows ZIP and unsigned macOS application ZIP to
the GitHub release for that tag. If no release exists, CI creates one with
generated release notes. Re-running the workflow replaces assets with the same
filenames. Branch pushes and pull requests continue to upload build artifacts.
For example, after committing and pushing the workflow changes:

```sh
git tag v0.1.0
git push origin v0.1.0
```

## Coverage and licensing

The Yoink output rename has been built and smoke-tested on Windows. The macOS
bundle name and resource paths are updated, but the renamed bundle has not been
built or run on this Windows host.

Windows is the first validated platform. The available host has one 3840×2160
display at 150%; simulated geometry/density checks do not establish physical
mixed-monitor behavior. Physical two-display traversal/disconnect/fullscreen
acceptance remains not run. Hosted Mac builds and headless checks have passed
for the integration; native Mac runtime, first-click routing, capture/wheel,
Retina traversal and Spaces remain unverified. Cocoa uses the shared source
policy with a color-only compatibility readback. The complete 16.7 ms warm,
50 ms input and 200 ms sharp gates remain ticket #11 acceptance work. See
[preview verification](docs/testing/preview-retention.md), the concrete evidence and
follow-up instructions in [Windows testing](docs/testing/windows.md) and
[macOS testing](docs/testing/macos.md).

Yoink uses AGPL-3.0-or-later adapted BongoCat runtime/UI code. Preserve
[LICENSE](LICENSE), [third-party notices](docs/third-party-notices.md), the packaged
license texts and corresponding source when redistributing. The Noto font is
under SIL OFL. No Aptos, Segoe UI or Segoe UI Emoji font is shipped in the application package.
