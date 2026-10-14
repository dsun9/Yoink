---
version: alpha
colors:
  primary: "#267AC0"
  canvas: "#F0F4F9"
  surface: "#FFFFFF"
  field: "#F5F8FC"
  border: "#DFE7F0"
  ink: "#182230"
  muted: "#627387"
  accent: "#267AC0"
  selection: "#E7F2FC"
  accentHover: "#1E6AAA"
  accentPressed: "#18578F"
  danger: "#A42B34"
typography:
  heading:
    fontFamily: "Segoe UI Semibold, sans-serif"
    fontSize: "21px"
  body:
    fontFamily: "Segoe UI, sans-serif"
    fontSize: "16px"
  utility:
    fontFamily: "Segoe UI, sans-serif"
    fontSize: "14px"
spacing:
  compact: "6px"
  control: "12px"
  panel: "18px"
rounded:
  control: "8px"
  panel: "12px"
components:
  button:
    backgroundColor: "{colors.field}"
    textColor: "{colors.ink}"
    height: "36px"
  field:
    backgroundColor: "{colors.field}"
    textColor: "{colors.ink}"
    height: "36px"
  canvas:
    backgroundColor: "{colors.canvas}"
  selection:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accentHover}"
  status:
    textColor: "{colors.muted}"
  error:
    textColor: "{colors.danger}"
  focus:
    backgroundColor: "{colors.primary}"
---

# Yoink settings

## Menu redesign direction agreed on 2026-10-13

The [approved redesign specification](docs/superpowers/specs/2026-10-13-yoink-menu-redesign.md)
is authoritative for the new menus and public rename. It also records the
unresolved Undo behavior separately from the approved redesign.

The shared runtime theme uses BongoCat's pale surfaces, rounded controls,
clear section hierarchy and compact switches, adapted to a blue Yoink
palette. The user approved these four decisions:

- Blue is the principal accent and selection color.
- Settings separates Items and Interaction into distinct pages, with a shared
  project toolbar and project status. Items keeps the eight-position selector,
  anchor list, actual content preview and item editor together.
- The replacement drawer uses whole-row selection with an integrated thumbnail
  and readable label. Selecting a row immediately replaces the sprite.
- The first revision focuses on the light theme.

The user subsequently approved the responsive Items/Interaction layout and
feedback presentation. The drawer must remain compact, with a maximum of
340 by 360 logical units and a smaller height for short lists. The detailed
design now includes these requirements:

- The product name is Yoink. Application branding, executable/bundle names,
  tray and window titles, current launch instructions and distribution names
  adopt that name. Existing `.pointy` projects and remembered preferences remain
  compatible.
- The project toolbar aligns the app name, visible Preset label, name field and
  action buttons vertically. New, Open, Save and Save as use equal-width icon buttons
  with tooltips on hover and keyboard focus. Unsaved changes appear as an
  asterisk beside the displayed project name, outside the stored name.
- The eight-position selector is a regular three-by-three grid with a monitor
  icon in the unused center. Eight directional arrow labels keep the picker
  compact. The selected position's full name remains visible below it, and
  each button exposes its full position name on hover and keyboard focus.
  Buttons do not overlap the screen diagram or one another.
- Items reorder by dragging, with a visible drag handle and insertion position.
  The list retains a keyboard reorder shortcut. Default and Delete actions
  appear on the hovered or keyboard-focused row, with space reserved so labels
  do not move when actions appear. The current default retains a visible star
  indicator. The only persistent list actions are Add text and Add image.
- Dropdowns draw a proper downward chevron inside the trigger.
- The replacement drawer has a labeled Close button. Whole-row choices retain
  integrated thumbnails, keyboard focus, immediate selection and dismissal.
- Wide Settings windows pair the position/list section with the item editor.
  Small windows stack those sections and wrap the project toolbar; focus reveals
  the active control in the scroll area.
- Routine font coverage information appears under Font details. Actionable
  warnings and errors remain visible.
- Tab switches consume the outgoing page's pending edits before changing pages.
  Invalid drafts survive navigation, the affected tab has an error indicator,
  and project actions continue to check errors on both pages.
- Undo remains a requested follow-up with an unresolved scope. The existing
  Revert fields action discards invalid drafts and reloads the current project
  values. It does not undo committed project edits.

The shared theme, toolbar, page navigation, position picker and item rows below
describe the current implementation. Reorder, editor and replacement-row
changes have their own follow-up tickets. The visual reference is `Ref/BongoCat/src/ui/theme/` and
its preferences widgets.

## Overview

A presenter's preparation desk: choose a screen edge, arrange the objects to
pull from it, and see their actual appearance. This is a native desktop tool
with English controls and Unicode item content. The signature is the exact
eight-position selector surrounding a display outline. It makes the spatial
relationship visible before the presenter returns to their slides.

The approved behavior lives in sections 2, 6 and 7 of
[the product design](docs/superpowers/specs/2026-10-03-pointypointer-design.md).
There was no prior settings screen or token system. BongoCat supplies attributed
backend and form patterns, not its visual identity or domain pages.

## Colors

The pale canvas is #F0F4F9, white surfaces contain the toolbar, and fields use
#F5F8FC with #DFE7F0 borders. Text is #182230, secondary text is #627387,
and #267AC0 is the blue action and focus color. Selection uses #E7F2FC with
the darker blue #1E6AAA text, which has 5:1 contrast on the selection fill.
Red #A42B34 identifies errors and removal confirmation. The separate
asterisk beside the project name marks unsaved edits, and its tooltip explains
the state. An untouched new project has no asterisk and reports that it has
not been saved to a file.

**Runtime owner:** `src/ui/theme.c` is the only runtime palette and geometry
token definition. It adapts these normative values into Nuklear styles. Shared
controls consume that theme; feature files do not define another palette.
Hover and focus derive from the accent; disabled text derives from muted ink.
Scrollbars use the same canvas, muted and accent roles throughout every group.

## Typography

Segoe UI is the body face; Segoe UI Semibold provides small section headings.
Utility text shares the body family at 14 DIP. If system font lookup fails,
Nuklear's bundled ProggyClean keeps the UI usable and the diagnostic names it.
User content previews use the application's shaping/font-fallback pipeline,
including colored emoji, independent of the control-label atlas.

## Layout

Default window: 1040 by 780 logical units. Minimum: 640 by 480.
The project bar and status stay above an internally scrolling content area.
Wide layout pairs a 300-unit anchor/list column with a flexible editor. Below
860 units the same sections stack in a single scroll owner. The ordered list
has a bounded inner scroller so long lists do not bury editor controls.

```text
Yoink   Project   [Project name] *   New  Open  Save  Save as
Items                 Interaction
+-------------------------+------------------------------------+
| TL        TC        TR  | Selected item / actual preview     |
| CL  display outline CR  | Content, font, size, angle, opacity |
| BL        BC        BR  |                                    |
| Enabled / ordered list |                                    |
| Add / order / default  |                                    |
+-------------------------+------------------------------------+
Persistent outcome or diagnostic
Project by Bowen / GitHub           Revert fields / Close settings / Quit
```

The prebuild critique removed decorative display numbers and a separate
navigation rail: neither corresponds to the eight unordered spatial anchors.
The screen outline is the one expressive element; fields use familiar labels.
No decorative animation is used. Preview GIF animation reflects actual content.

## Elevation & Depth

Flat editing groups with a thin border. The preview has a subtle checkerboard
for transparency. Native order is sprites, settings, then owned file dialogs.
The same order governs actual input targeting. Unsaved confirmation is an
app-owned modal within settings; underlying controls are inert while it is open.

## Shapes

Eight-unit control corners and twelve-unit panel corners; 1-unit borders and a
2-unit visible keyboard focus outline. Labels and status wrap instead of
requiring hover to discover full information.

## Components

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Select/Listbox | `src/ui/backend.c` | Shared focus/control helpers | Spatial selector, ordered list, font collection popup | ui_events; settings_smoke keyboard and popup |
| Form | `src/ui/item_editor.c` | Shared edit helpers and PPCommand validation | Text, numeric, color, project name | commands; settings_smoke invalid draft and IME guard |
| Scrollbar | `src/ui/theme.c` | Shared Nuklear scrollbar style | Outer content and bounded ordered list | Narrow scrolled screenshot; keyboard reachability |
| Feedback | `src/ui/project_actions.c` | Persistent status and field errors | Success, failure, fallback diagnostics | commands; inspected screenshots |
| CRUD | `src/app/commands.c` | Portable project and revision | Explicit default, transactional save/open | commands; settings_smoke native import/save/open |
| Dialog | `src/ui/project_actions.c` | Save/Discard/Cancel policy | App confirmation; required OS file chooser | settings_smoke Escape, focus, actual file chooser |

Project actions use equal-width 44 by 36 logical-unit drawn icon buttons.
The visible Preset label, name field, dirty marker and actions form one centered
group, capped at 760 logical units. The header adds balanced vertical space
above and below the group. The label and name field use compact six-unit spacing;
the action group uses the same spacing beside the field on wide windows. Actions
share the project-name field's vertical alignment and wrap into a centered second
row below 780 units of content width. Save uses the blue
primary style with a contrasting inset focus marker. Hover, pressed, disabled
and keyboard focus states come from the
shared backend. Full action names and shortcuts appear on pointer hover or
keyboard focus. Keyboard focus takes precedence over a stationary pointer;
moving the pointer restores hover help. Tooltips draw in Nuklear's overlay and
cannot change text-input ownership. The asterisk is display decoration outside
the editable value; project name and filename remain independent.

Items and Interaction use compact tabs aligned to the left of a shared baseline.
The selected tab has a pale blue fill and blue lower edge; inactive tabs blend
into the canvas. Keyboard focus adds to the lower indicator and never draws a
top or side border. The existing error markers and input ordering apply to both
tabs.

The white footer places Revert fields, Close settings and Quit in a compact
group on the right. On the left, Project by Bowen appears above the complete
`https://github.com/dsun9/Yoink` link. The link supports pointer and keyboard
activation through the shared control registry and opens the system browser.

`src/ui/icons.c` draws the shared action, direction, monitor, star, deletion,
close and drag icons. `pp_ui_icon_button`, `pp_ui_switch` and `pp_ui_tooltip`
in `src/ui/backend.c` reuse the existing control identities and input boundary.
Dropdown labels reserve space for the drawn downward chevron. Shared field
rows are 36 logical units; section headings keep the system semibold font and
utility text uses 14 units. Empty shared errors reserve no row, and errors use
the separate danger token. Density scales the existing native font atlas,
hit targets and drawing together.

The position picker uses equal cells around a noninteractive monitor. Full
position names appear on hover and keyboard focus; the selected name remains
below the grid. Its Enabled switch changes that anchor list alone. Item rows
are 44 logical units high, with a 32-unit actual-content thumbnail, a derived
label and reserved Default/Delete button space. Text labels collapse whitespace
and identify unsupported control-atlas glyphs by codepoint; the editor and
thumbnail retain the original Unicode. Image labels use their stable item ID
and never add a persisted naming field. Long labels shorten with an ellipsis
and expose their complete derived label on hover/focus. Help wraps at UTF-8
codepoint boundaries using the utility drawing font and stays within the
window. When the label exceeds the bounded help area, an instruction points
to the selected item's Full label disclosure. Its Previous and Next controls
reveal every page of the same derived label without changing item content.

The filled star identifies the default continuously. Row selection changes
the item being edited; Default and Delete retain that row's stable identity
without selecting it. The confirmation keeps the intended anchor/item/project
generation through validation and cancellation. Add text and Add image remain
below the bounded list. Move up/down remain until the reorder follow-up.
Rows remain in keyboard traversal when their group starts below the outer
viewport, and focus reveals them through both scroll owners. Their thumbnails
reuse retained, context-local preview consumers, sample GIF frame zero and
release unused textures after the queued frame completes.

No field change commits during IME composition. Edits affect future pulls; the
preview reflects valid current edits. Save means a successful package write.
Closing settings keeps in-memory edits. New/Open/Quit guard unsaved changes.
The editable project name is portable and independent of the selected filename.
New projects begin as “Untitled project”; Save As changes only the file path.
An untouched new project says “Not saved to a file”. Unsupported control-atlas
characters use visible placeholders with nearby U+ codepoints and an explicit
statement that stored text is unchanged. Text previews retain real Unicode,
color emoji, and actual fallback diagnostics beside the preview.

Content previews retain the same immutable artwork and canonical layout used
by sprites, including the installed Segoe choice or named Noto fallback.
Artwork commits restart the Settings preview clock; rename, reorder and
opacity/rotation edits preserve it and reuse prepared sources. A pending sharp
target keeps accepted artwork visible. Failed same-item preparation preserves
the accepted preview; selecting a different item clears previous-item pixels
before preparing the new one. Closing Settings releases preview work/textures
while preserving drafts and keyboard focus for reopening.

`src/app/content_sources.c` owns shared CPU cache entries and completion dispatch;
`src/ui/preview.c` owns consumer cancellation and context-local GPU publication.
Premultiplied artwork stays premultiplied through filtering and converts at the
final Nuklear sample, preserving the existing control blend contract. UI source
and output textures never cross independent contexts. The adapter restores the
owning frame's GL state on success and failure. Source/target overlap is charged
before publication; false preserves the usable output and its charge.

This single settings workflow does not need a separate UX-CONTRACT document.
There are no browser routes, remote APIs, tables, authentication, Japan-market
flows, or theme picker. Browser CSS/ARIA audits do not establish native desktop
accessibility. Nuklear has no UI Automation accessibility tree; keyboard and
visible-focus coverage must be reported separately from screen-reader support.

## Selected-item editor

The selected item uses a 108-unit actual-content preview on the shared pale
checkerboard, followed by Content and Appearance sections. The preview fits
the content's width and height independently so multiline and wide text use
the available space. It retains the existing prepared sources, animation
clock, context-local textures and exact GPU charges.

Text keeps shaped Unicode and emoji editing, permitted font import and a
direct collection-face popup. Font details holds routine coverage, the
configured emoji fallback and the numeric collection index. Missing requested
fonts, missing glyphs and fallback associated with those problems stay visible
beside the content with a correction action. Complete configured fallback is
routine information. Image items keep Replace image and their size, angle and
opacity controls.

Wide editor columns pair size with color, then angle with opacity. Narrow
Settings stacks those fields and uses the existing focus-revealing scroll.
Appearance errors appear directly after their field or paired group; empty
errors allocate no row. Invalid entered values remain drafts and do not dirty
the project until a valid edit commits. Tabs, close/reopen, chooser cancellation
and Revert fields retain the shared validation and recovery behavior.

Font details changes visibility only after the outgoing controls consume and
validate their complete input batch. The numeric editor releases its active
field before the layout changes, and focus returns to the disclosure. An
invalid collection index and its font error remain attached to the item when
the section is collapsed. A click that cancels composition or dismisses a
popup cannot also activate the disclosure in that frame.
Queued field edits also finish before Tab changes keyboard focus, including
Shift+Tab followed by disclosure activation in the same input batch.

## Replacement drawer

The native drawer reuses `src/ui/backend.c` controls and `src/ui/theme.c` tokens.
Its only operation is replacement of one temporary sprite. The originating
position's current list supplies the choices; project defaults and dirty state
stay unchanged. This is a non-modal selector with a bounded internal scroller,
keyboard focus outline, Escape dismissal, and a persistent Close button.

The compact borderless window is at most 340 by 360 logical units and is clamped
to the current display. Actual content thumbnails make images and emoji
identifiable when the control font cannot render their labels. The empty state
points to Settings and keeps the existing sprite. Display scale changes rebuild
the drawer at the new scale while keeping its focused item.

Thumbnails use fixed-fit retained sources and deliberately sample GIF frame zero.
Reorder transfers visible slots by stable item ID; scrolling, removal and closure
release unused slots without disturbing another preview or sprite consumer.
Replacement borrows the prepared item snapshot, preserves center/rotation/
opacity/longest side, adopts its aspect ratio and restarts only that instance's
clock. [Preview verification](docs/testing/preview-retention.md) records native
alpha, rejection, focus, density and lifecycle checks and their limits.

Drawer and settings windows stay above sprites. Closing by selection or Escape
restores the preceding foreground window when the drawer still owns focus.
An outside click reaches its original target and preserves that target's new
focus. `tests/platform/test_sprite_flow.c` covers hidden rendering, keyboard
scroll, scales and the coordinated native focus/input flow. Nuklear's existing
screen-reader limitation also applies to this drawer.

## Do's and Don'ts

- Keep the eight spatial positions exact and selection separate from default.
- Use the real text/image pipeline for content previews and font diagnostics.
- Keep actionable errors near their field and retain the entered value.
- Keep ownership in shared theme/backend/commands; avoid copied local variants.
- Do not add dashboard cards, marketing copy, decorative gradients or web stacks.
- Do not claim physical DPI, screen-reader, macOS or runtime coverage from a static audit.
