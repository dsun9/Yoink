# PointyPointer design

Date: 2026-10-03

Status: written specification approved on 2026-10-03.

## 1. Purpose and success criteria

PointyPointer is a desktop presentation helper. It lets a presenter pull visual
objects from eight positions on any display, place them above the presentation,
and manipulate them without taking keyboard focus from the presentation.

The application starts in the system tray. Outside settings, the item drawer,
hover previews, and placed sprites, it is visually unobtrusive. The same eight
item lists appear on every display. A portable project carries those lists,
their defaults, and their assets and fonts to another computer. Placed sprites
are temporary and are never saved.

Success requires all of the following:

- PNG, animated GIF, static SVG, text, and emoji can be dragged from an anchor.
- Placed sprites remain above ordinary application windows and move smoothly.
- Windows sprite grab areas approximate the silhouette with bounded precision,
  preserving visible artwork and passing input through sufficiently large holes,
  padding and rotated corners. Animation input uses a stable union of frames.
- Dragging between displays works with different display scales and desktop
  arrangements, including displays with negative desktop coordinates.
- Opening a saved project works after the original imported files are moved.
- The application uses C, SDL3, OpenGL, and CMake, with small native platform
  adapters for Windows and macOS. Windows receives the first runtime testing.

The local `Ref/BongoCat` project supplies architecture and reusable settings UI
code. Live2D, Cubism, model animation controllers, and audio are outside scope.

## 2. Agreed product behavior

### 2.1 Startup and tray

- Start without showing settings or any placed sprites.
- Create a tray icon. Its context menu contains Settings and Quit.
- Reopen the last successfully opened or saved project if it remains available.
- On first launch, initialize eight lists, each containing the default pointing
  hand text `👉`, with Aptos selected and the emoji fallback described below.
  Initial appearance is a 96-logical-pixel font size, zero rotation, and full
  opacity. Monochrome text defaults to black; the emoji keeps its own colors.
- A missing or invalid last project leaves the application running with the
  built-in configuration and a clear diagnostic accessible from settings. It
  must not overwrite the missing or invalid project.
- Closing settings keeps the application running. Quit ends the application
  and discards the temporary scene after handling unsaved project edits.

Local application preferences store the last project path and settings window
geometry. These machine-specific values are not part of the portable project.

### 2.2 Eight anchors on each display

The anchors are top-left, top-center, top-right, center-left, center-right,
bottom-left, bottom-center, and bottom-right. Each display has all eight, using
one shared set of list definitions. There are no per-display item overrides.

Activation regions initially measure 16 by 16 logical pixels. They sit inside
the physical display bounds at each anchor; middle anchors are centered along
the corresponding edge. The previews extend inward so they remain visible.
Regions exist on shared edges between adjacent displays as well as outer edges.

Entering a region reveals a faint preview of its default item. There is no
mandatory dwell delay. Leaving the region and preview without dragging fades
the preview away. Moving from the activation region onto the preview keeps it
available. A disabled anchor or an empty list has no activation target.

The small activation region deliberately accepts input to start a pull. On
Windows, the preview uses the same approximate grab area as a placed sprite;
input outside that area and the activation target reaches its original target.
Hidden, disabled, zero-opacity and wholly empty-source previews cancel only
their own accepted input and stop interception. A GIF preview with a nonempty
animation union stays interactive through blank frames. Cocoa keeps its
rectangular preview behavior pending a native follow-up.

### 2.3 Creating and manipulating instances

| Input | Result |
| --- | --- |
| Left-drag an anchor preview | Create one instance of the anchor's default item after crossing the system drag threshold. |
| Left-drag a placed sprite | Move the instance directly with the pointer. |
| Wheel over a sprite | Rotate around its center, initially 10 degrees per wheel notch, with about 120 ms of easing. |
| Alt+wheel over a sprite | Resize proportionally, initially 10% per notch, with about 120 ms of easing. |
| Right-click a sprite | Delete that instance immediately. |
| Double-click a sprite | Open the item drawer for the anchor that originally created it. |
| Escape or an outside click while the drawer is open | Close the drawer without changing the instance. |

Dragging preserves the location grabbed within the sprite, so the object does
not jump to put its center under the mouse. Grabbing the small anchor target
uses the nearest corresponding point on the preview. The default preview is
available again for subsequent pulls, and each pull creates a separate object.

Dragging follows the pointer without positional easing. Rotation and resizing
use elapsed-time animation toward target values. Further wheel input updates
the target immediately, without queueing a chain of animations. Fractional wheel
input is accumulated proportionally. Equal upward and downward resize input
uses reciprocal factors, avoiding accumulated size drift.

Double-click recognition follows the system timing and movement thresholds.
Crossing the drag threshold cancels a pending click gesture. Opening the drawer
does not accidentally move or create a sprite.

Images preserve aspect ratio. Text changes its rendered font size, and SVGs are
rendered at an appropriate resolution for the new size. Rotation is continuous
across the 0/360-degree boundary. Platform wheel direction is normalized so the
same user action produces consistent behavior.

### 2.4 Replacement and item lifetime

Selecting an item in the drawer replaces the current instance in place:

- Preserve its center, rotation, opacity, and longest unrotated dimension.
- Preserve the replacement content's own aspect ratio; never stretch it to
  match the old rectangle.
- Apply the replacement content, font, and color. Derive its rendering size
  from the preserved displayed dimension.
- Restart replacement GIF content at its first frame.
- Keep the instance's originating anchor.
- Leave that anchor's configured default unchanged.

Editing item defaults affects future pulls only. Existing instances retain a
snapshot of their content and appearance, even when their source item is edited
or removed. Resource references remain valid until those instances are deleted.
The drawer shows the originating anchor's current list. An empty list produces
an empty drawer with a clear explanation; it does not delete the instance.
Adding the first item to an empty list makes it the default. Removing the
default selects the first remaining item, or clears the default if none remain.
Reordering a list preserves its selected default by item ID.

Switching to a successfully loaded project clears the temporary scene and closes
previews and drawers. A failed project load preserves the current project and
scene. No scene restoration or scene persistence is included.

## 3. Approximate sprite mouse routing

Windows placed sprites and anchor previews use conservative source-space region
quadtrees with fixed maximum depth 7. Occupied leaves contain any nonzero source
alpha; uniform blocks merge. Native projection includes linear-filter support
and rounding margins, preserves visible artwork, and passes original input
through sufficiently large transparent holes, padding and rotated corners.
Small gaps and space near visible edges can remain interactive. Source-space
precision is fixed, so this tolerance grows with displayed sprite size.

The final native region has at most 1024 sorted, nonoverlapping rectangles after
rotation. Output coverage uses 64 cells per axis, conservatively coarsening to
32 when necessary. Input rotation uses 0.05-degree steps with an angular envelope
that contains the continuous visual pose. Unchanged final regions reuse the
native publication without entering GDI. The source trees and cached native
rectangle arrays share a separate 8 MiB cache budget including accepted/candidate
overlap. Region geometry
and accepted source ownership publish together; a protective memory rejection
preserves the previous usable presentation.

GIF input uses one union of all full composited frames, with disposal already
resolved. The union remains interactive during wholly transparent frames and
after finite playback stops. An animation with no visible alpha in any frame has
an empty input region. Opacity zero, explicit hiding, presentation failure and
destruction remove interception and cancel capture.

For overlapping sprites, input targets the topmost native grab area at that
point. Clicking or beginning a drag on an instance brings it above other
PointyPointer sprites without activating the presentation overlay as a keyboard
window.

Pass-through means the underlying application receives the original input
normally. Receiving and ignoring events in PointyPointer is insufficient.
Synthetic click replay is not the normal input-routing mechanism. The first
click after arriving inside or outside the grab area must reach its owner correctly.

After a left press starts a drag, maintain mouse capture through movement beyond
the sprite, through transparent pixels, and across displays until release.
Release capture on cancellation, loss of capture, instance destruction, or
shutdown. A blank animation frame does not end the accepted drag. Outside an
active gesture, return immediately to native grab-area routing. The same grab
area governs double-click, right-click deletion and wheel gestures.

Prepare and cache the source quadtree outside input callbacks, without GPU
readback. Translation reuses the window-local region; animation frame selection
reuses its union. Pose geometry, density, canonical mapping and accepted artwork
changes can reproject the shape. The native adapter must provide original input
passthrough outside the grab area, including to other
processes. A core hit query alone does not establish operating-system routing.

This Windows contract covers PNG, GIF, bitmap text, scalable/mixed text and SVG.
Cocoa retains its rectangular compatibility adapter and current empty-frame
hiding/cancellation behavior until a separately validated follow-up. Native
macOS runtime is unverified. See the accepted [issue #18 design](2026-10-12-approximate-grab-area.md)
for the behavior choices and validation evidence.

SDL documents window-shape updates as expensive. The implementation plan must
start by validating the native presentation and input path with transparency,
animation, rotation, and an underlying application before expanding the UI.

## 4. Architecture

### 4.1 Window strategy

Use a compact transparent window per placed sprite, plus transient windows for
hover previews and the drawer and a normal settings window. Share assets and
rendering resources where supported by the selected OpenGL context arrangement.
Do not allocate a new asset copy for every instance.

This follows BongoCat's small-window architecture and limits the areas being
presented. Alternatives considered were one full overlay per display and one
window spanning the virtual desktop. The former complicates cross-display
interaction and large transparency updates; the latter also complicates mixed
display scaling and monitor layout changes.

Sprite windows are borderless, excluded from taskbar or Dock listings, and
configured as topmost without activation. Settings can take normal keyboard
focus. The drawer can receive the keyboard input needed for dismissal, then
restore focus appropriately.

Topmost behavior is subject to the operating system's compositor and window
levels. Validate ordinary presentation windows and supported fullscreen/Space
behavior explicitly. Secure system surfaces and exclusive display modes are
not surfaces the application can promise to cover.

### 4.2 Component responsibilities

| Component | Owns and exposes | Dependencies |
| --- | --- | --- |
| Core model | Projects, eight anchor lists, immutable item snapshots, temporary instances, validation | C standard library and small utility modules |
| Interaction controller | Hover, press/drag/click state, scoped capture, wheel targets, drawer selection, sprite grab-area queries | Core model, display coordinates, cached source grab areas |
| Display manager | Display bounds and scaling, anchor positions, coordinate conversion, disconnect recovery | SDL display events and platform coordinate adapter |
| Asset manager | Immutable source bytes, image/GIF decoding, canonical text/bitmap layout, source alpha and raster accounting | Selected media/font libraries |
| Shared content owner | Consumer lifetimes, artwork/target cache, bounded worker admission and sole completion dispatch | App-owned assets; private immutable worker captures |
| Renderer | OpenGL resources and presentation of content, previews, drawer, settings | SDL3, OpenGL, asset manager |
| Settings UI | Project actions, eight-position selection, item lists, item editor, diagnostics | Nuklear backend adapted from BongoCat, core APIs |
| Project store | Versioned package load/save, resource deduplication, validation, atomic replacement | Archive and JSON libraries, platform file operations |
| Platform layer | Native windows, mouse routing/capture, tray, dialogs, fonts, coordinate conversion | Win32 or Cocoa, SDL native handles |
| Application lifecycle | Startup, event loop, scheduling, active project changes, shutdown | Component interfaces above |

The core must not include Win32 or Cocoa types. Platform operations use opaque
handles and plain C structures. macOS-specific code can use Objective-C behind
that interface. CMake selects the appropriate platform sources.

Use the reference's Nuklear settings backend and general structure selectively.
Do not import its Live2D state model or retain unused model/audio dependencies.
Copied code retains attribution and applicable license notices. The repository
already contains the AGPL license; the reference's MIT notice applies to its
specified model assets, not the general runtime source.

### 4.3 Display coordinates and lifecycle

Use an explicit desktop coordinate abstraction rather than treating native
pixels, SDL window coordinates, and logical UI units as interchangeable.
Conversion is centralized in the display manager and platform adapter. Display
scaling affects raster quality and window geometry; moving an object across
displays preserves its logical displayed size and grabbed point.

Display changes rebuild the anchor windows from the same shared configuration.
If a display disappears, relocate affected sprites onto a remaining display,
keeping them reachable. Close or reposition any drawer on the removed display.
Recompute routing and geometry after scaling or resolution changes.

Only active animation, dragging, easing, and preview fades require continuous
redraw. Static sprites do not need identical frames continuously repainted.
Pointer monitoring remains active while settings are closed, with work limited
to what is necessary to reveal anchors and maintain correct input routing.

## 5. Assets, text, and fonts

PNG preserves alpha. GIF honors frame timing, disposal, transparency, and encoded
loop behavior. Each instance has an independent playback clock while sharing
decoded resources. SVG support covers self-contained static artwork; scripts
and remote resource fetching are not part of an imported sprite. An unsupported
or externally dependent SVG must report the issue instead of silently dropping
essential content.

Text stores Unicode content, font selection, font size, and color. Render through
a shared font pipeline that supports shaping and the chosen color-emoji font on
both platforms. Do not depend on each operating system's default emoji artwork
for portable project appearance. Multiline text retains explicit line breaks.
Color emoji retains its intrinsic colors; the text color applies to monochrome
glyphs.

Aptos remains the requested ordinary text family. Select installed Segoe UI
Emoji independently for default emoji, matching actual family metadata and the
collection face. Inspect each font's scalable outlines, bitmap strikes and
supported color layers separately from its embedding permissions. Use the
scalable color layers supported by the current FreeType pipeline; exact native
COLRv1 paint parity is outside this implementation. If Segoe is unavailable,
use bundled Noto Color Emoji and identify that choice in the item preview.
Anchor, Settings, drawer and sprite content use the same selected font bytes.
Preserve the font's actual sequence coverage, including regional indicators
that can render as separate letters without missing-glyph diagnostics.

Embed the actual permitted font files used by a project, including fallback
fonts. Use embedded fonts privately within the project without installing them
system-wide. Check embedding restrictions and applicable font terms at import;
fonts that cannot support an editable project require choosing another font.
Do not silently produce a supposedly portable project with unresolved fonts.

Application font redistribution is separate from document embedding. Noto's
font license permits the proposed bundled emoji fallback with its notices.
Segoe UI Emoji and Aptos are obtained from the user's available font files and
are never copied into application redistributable resources. Default projects
own complete permitted local bytes privately and attach notices identifying the
actual selected fonts. If Aptos is unavailable, the default hand still renders
through the chosen Segoe or Noto emoji font. Text completely resolved by that
embedded permitted font remains portable. Other unresolved text needs a covering
font before it can be saved. The missing-Segoe Noto choice has its own diagnostic,
separate from an unavailable requested Aptos family. See ADR-0001 and
[the implementation specification](https://github.com/dsun9/Yoink/issues/1).

Previews and sprites share the retained source policies described in
[rendering ownership](../../architecture/rendering-content.md). Each consumer
has its own target and elapsed clock. Content identity includes artwork/style/
font bytes; project name, reorder, item ID and pose do not invalidate sources.
Settings commits restart its preview clock only when artwork changes; drawer
GIF thumbnails sample frame zero. Selection changes clear previous-item pixels,
while failed same-item replacements preserve a usable accepted preview.

Original asset files are retained inside the project. Decoded frames, GPU
textures, and raster caches are disposable runtime data. Hash-based storage
deduplicates identical source files across anchors and instances.

## 6. Portable project format and data flow

### 6.1 Package contents

Use the `.pointy` extension for a ZIP package containing:

- `manifest.json`: schema version, project name, eight anchor definitions,
  ordered item entries, selected defaults, and shared interaction settings.
- `assets/`: source images and SVGs, keyed by content hash.
- `fonts/`: the required font files, keyed by content hash, with face selection
  and embedding metadata recorded in the manifest.
- Applicable resource license notices included with imported/bundled resources.

Each anchor has a stable identifier, enabled flag, ordered item IDs, and a
default item ID or no default when empty. Each item has a stable ID, content
kind, asset references or Unicode text, and appearance defaults. Images use a
logical longest-side size; text uses font size and records font face and color.
Text bounds are derived from layout rather than a second conflicting size
setting. Orientation is an angle; opacity is a normalized value. Validate finite
numeric values and valid references before accepting a project.

Project-level interaction settings include activation-region size, rotation
step, proportional resize step, and easing duration. There are no per-display
item lists, native display identifiers, source file dependencies, window handles,
or scene instances in the package.

### 6.2 Import, edit, save, and open

1. Import reads and validates a source file, acquires its owned bytes, and
   creates the project resource. The project no longer depends on its path.
2. Settings edits update the in-memory project, mark it dirty, and affect future
   pulls. Existing instances retain their snapshots and resource references.
3. Save validates a complete package, writes a temporary file beside the target,
   closes it successfully, then replaces the target using suitable native file
   operations. A failure preserves the previous project file and dirty state.
4. Open reads into a separate candidate project, validates versions, assets,
   fonts, references, and supported content, then swaps it into the application.
   Only a successful swap clears the old temporary scene.

New and Open handle unsaved changes with Save, Discard, and Cancel. Closing
settings keeps unsaved edits in memory. Quit uses the same save handling.
Untouched built-in defaults do not trigger an unnecessary save prompt.

Unknown future format versions produce a clear unsupported-version message.
Malformed archives, unsafe entry paths, duplicate/conflicting entries, excessive
resource sizes, and invalid decoded dimensions are rejected before activation.
The implementation must apply explicit bounded package and decode budgets and
report which limit was exceeded. It must not extract arbitrary archive paths
into user directories or fetch external content while loading a project.

## 7. Settings interface

The settings window exposes New, Open, Save, and Save As, the project name, and
an unsaved-change indicator. A selector arranged around a display outline maps
directly to the eight positions. Selecting a position exposes:

- An enable/disable control.
- Its ordered item list, with add, remove, reorder, and make-default actions.
- An editor for the selected item and a live preview.
- Size, orientation, and opacity controls.
- Unicode text, font, font size, and color controls for text items.
- Font fallback and asset import diagnostics where relevant.

Shared interaction settings are edited once for every display. Defaults and
previews update immediately. Selecting a different item in settings does not
implicitly change the anchor's default; make-default is an explicit action.

The drawer is a compact selector for the originating list, positioned near the
instance and constrained to the current display. It is distinct from settings:
selection replaces the instance and closes the drawer. It does not edit the
project or mark it dirty.

## 8. Error handling and verification

Import failures leave the current item unchanged. Load failures preserve the
active project and scene. Save failures preserve the prior file and report an
actionable error. A renderer or native transparency failure must not leave an
invisible window intercepting the desktop; clean up that surface and report the
failure. Shutdown releases capture, closes windows, and releases resources.

### Core and storage checks

- Canonical transformed bounds, conservative quadtree coverage, transparent
  holes/corners, animation unions, complexity limits and candidate rejection.
- Gesture transitions, double-click/drag separation, and capture cancellation.
- Easing under fractional input and varying frame times.
- Replacement invariants and independent default selection.
- Display conversion and recovery with mixed scaling and negative coordinates.
- Save/load round trips with deduplicated assets and fonts and no scene data.
- Invalid packages, missing resources, future versions, and failed-save recovery.
- GIF disposal/frame timing and Unicode/emoji rendering with embedded fonts.

### Windows runtime acceptance

Verify with real underlying application windows, rather than core tests alone:

- Startup, tray Settings/Quit, and continuing to run after settings closes.
- All eight anchors on at least two displays, including a shared display edge.
- Original first-click/wheel pass-through at sufficiently large padding, holes,
  rotated corners and letter gaps; consistent mouse gestures in the grab area.
  GIF unions retain interception/capture through blank frames; wholly empty
  sources remove input.
- Correct stacking across overlapping sprite grab areas and UI windows.
- Rotation and size changes updating both visual and hit behavior.
- Pointer capture through cross-display dragging, without keyboard-focus theft.
- Mixed DPI, negative monitor coordinates, disconnect/reconnect, and resolution
  changes while the application is running.
- Default hand rendering, PNG, GIF, SVG, multiline text, and emoji fallback.
- Portable project reopening after moving original source files, with embedded
  fonts used privately and consistent content appearance.
- Replacement retaining the agreed transform while subsequent pulls use the
  configured default.
- Topmost behavior over a presentation and responsiveness with multiple sprites.

### macOS verification status

Provide the macOS adapter and CMake configuration from the outset. Run shared
core tests wherever possible. Hosted Mac build/headless checks and native
Mac runtime checks are separate. Windows testing cannot establish macOS runtime
correctness; native runtime remains unverified until executed on a Mac.
Include Cocoa rectangular input/passthrough, non-activation, menu
bar tray behavior, Retina scaling, and fullscreen/Spaces in that verification.

## 9. Scope and implementation handoff

This specification covers one coherent application. It excludes saved scenes,
per-display configurations, Live2D, audio, network synchronization, remote asset
loading, and a general vector or rich-text editor.

The implementation plan should sequence work as follows: validate native
transparent presentation and real input passthrough; establish core interaction
and display handling; add content/font support; implement portable persistence;
adapt settings and drawer UI; complete Windows acceptance and macOS checks when
a Mac is available. Concrete dependency versions and test commands belong in
the implementation plan.

This document's approval authorizes preparation of that implementation plan.
Implementation follows written-plan review and selection of an execution method,
as required by the invoked brainstorming workflow.

## References

- Local architecture and UI reference: `Ref/BongoCat/CMakeLists.txt`,
  `Ref/BongoCat/src/runtime/shell/window_hit.c`, `Ref/BongoCat/src/ui/`, and
  `Ref/BongoCat/src/platform/`.
- [SDL window shape and mouse transparency](https://wiki.libsdl.org/SDL3/SDL_SetWindowShape).
- [Microsoft Aptos font family](https://learn.microsoft.com/en-us/typography/font-list/aptos).
- [Microsoft font embedding and redistribution FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq).
- [Noto Emoji fonts and licensing](https://github.com/googlefonts/noto-emoji).
