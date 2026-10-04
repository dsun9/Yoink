---
version: alpha
colors:
  primary: "#185C83"
  canvas: "#EEF3F7"
  surface: "#FFFFFF"
  ink: "#203547"
  muted: "#526A7B"
  accent: "#185C83"
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
  control: "4px"
  panel: "8px"
components:
  button:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    height: "32px"
  field:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    height: "32px"
  canvas:
    backgroundColor: "{colors.canvas}"
  selection:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.surface}"
  status:
    textColor: "{colors.muted}"
  error:
    textColor: "{colors.danger}"
  focus:
    backgroundColor: "{colors.primary}"
---

# PointyPointer settings

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

Cool paper canvas, white editing surfaces, dark blue ink and restrained blue
selection connect to a presentation screen without imitating slide content.
Red is reserved for errors and removal confirmation. Selection also has a
text marker; dirty state says “Unsaved changes”. Color never carries these alone.

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
Project name / Unsaved changes       New  Open  Save  Save As
+-------------------------+------------------------------------+
| TL        TC        TR  | Selected item / actual preview     |
| CL  display outline CR  | Content, font, size, angle, opacity |
| BL        BC        BR  |                                    |
| Enabled / ordered list | Shared interaction settings        |
| Add / order / default  |                                    |
+-------------------------+------------------------------------+
Persistent outcome or diagnostic                 Settings / Quit
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

Four-unit control corners and eight-unit panel corners; 1-unit borders and a
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

Buttons are verb labels with hover, pressed, disabled and keyboard focus states.
No field change commits during IME composition. Edits affect future pulls; the
preview reflects valid current edits. Save means a successful package write.
Closing settings keeps in-memory edits. New/Open/Quit guard unsaved changes.
The editable project name is portable and independent of the selected filename.
New projects begin as “Untitled project”; Save As changes only the file path.
An untouched new project says “Not saved to a file”. Unsupported control-atlas
characters use visible placeholders with nearby U+ codepoints and an explicit
statement that stored text is unchanged. Text previews retain real Unicode,
color emoji, and actual fallback diagnostics beside the preview.

This single settings workflow does not need a separate UX-CONTRACT document.
There are no browser routes, remote APIs, tables, authentication, Japan-market
flows, or theme picker. Browser CSS/ARIA audits do not establish native desktop
accessibility. Nuklear has no UI Automation accessibility tree; keyboard and
visible-focus coverage must be reported separately from screen-reader support.

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
