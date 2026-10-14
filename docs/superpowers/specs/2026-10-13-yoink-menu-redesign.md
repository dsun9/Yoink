# Yoink Settings and replacement drawer redesign

Date: 2026-10-13.

Tracked by [issue #20](https://github.com/dsun9/Yoink/issues/20).

Status: visual design approved. The user confirmed the testing boundaries below.
The approved menu redesign and public rename are ready for implementation.
Undo is a requested follow-up with an unresolved behavior contract, described
under Further notes; it is not authorization to add project-wide history.

## Problem statement

Preparing items in Settings takes too much space and scrolling. Project
actions fill the window width, the position picker is crowded, and item actions
compete with the content. Common controls have weak visual hierarchy. The
replacement drawer separates thumbnails from their buttons and spends much of
its compact window on instructions and unused space.

The user wants a lighter, more deliberate native interface inspired by
BongoCat's Settings, with blue as the main accent. The application should be
named Yoink throughout its public interface and distribution.

## Solution

Keep the native application and its existing item, anchor list and sprite
behavior. Apply pale surfaces, rounded controls, blue selection, consistent
typography and visible interaction states across Settings and the replacement
drawer. Use one shared theme and shared control behavior.

Settings has Items and Interaction tabs beneath a compact project toolbar.
Items pairs the position picker and ordered list with the actual item preview
and editor. Interaction groups the global activation, rotation, resize and
easing controls. Small windows stack the Items sections and wrap controls.

The toolbar aligns the Yoink name, project label, editable project name and
equal-width icon buttons for New, Open, Save and Save as. A separate display
asterisk beside the project name indicates unsaved changes. Tooltips identify
icons on pointer hover and keyboard focus.

The position picker uses eight directional arrow labels around a monitor icon.
Items reorder by dragging. Default and Delete buttons appear within the hovered
or keyboard-focused item row. Add text and Add image are the only persistent
list actions. A filled star identifies the default item even when its row is
not hovered.

The replacement drawer presents integrated thumbnail-and-label rows with a
single selection target per choice and a proper labeled Close button. It fits
short lists and stays within its existing 340 by 360 logical-unit maximum.

## User stories

1. As a presenter, I want the application to be named Yoink in its windows,
   tray and distribution, so that its identity is consistent.
2. As an existing user, I want my saved projects and remembered Settings
   geometry to remain available after the rename, so that I can continue my work.
3. As a presenter, I want pale surfaces and a consistent blue accent in both
   menus, so that selection and important actions are easy to recognize.
4. As a presenter, I want aligned toolbar controls, so that project actions are
   easy to scan without taking up the whole window.
5. As a presenter, I want equally sized New, Open, Save and Save as icon
   buttons, so that the toolbar stays compact and orderly.
6. As a keyboard user, I want each icon's purpose available on focus, so that
   I can identify its action without a mouse.
7. As a presenter, I want an asterisk beside the project name when changes are
   unsaved, so that I can see save state without a separate status sentence.
8. As a presenter, I want the unsaved asterisk kept out of the stored project
   name and filename, so that saving preserves the name I entered.
9. As a presenter, I want Items and Interaction on separate tabs, so that the
   selected item's controls are not buried below global settings.
10. As a presenter, I want the Items page to keep the position picker, anchor
    list, preview and editor together, so that their relationship stays clear.
11. As a presenter, I want the eight positions represented by arrows in their
    spatial arrangement, so that I can choose an edge or corner quickly.
12. As a presenter, I want the selected position's full name displayed below
    the picker, so that I can confirm which anchor list I am editing.
13. As a presenter, I want the position's enabled state shown as a compact
    switch, so that I can change it without a long action button.
14. As a presenter, I want item rows to show real thumbnails and useful labels,
    so that I can distinguish text, emoji and images.
15. As a presenter, I want clicking an item to select it for editing without
    changing the default item, so that previewing and choosing future pulls stay
    separate operations.
16. As a presenter, I want to drag an item to reorder its anchor list, so that
    I can arrange choices directly.
17. As a presenter, I want an insertion marker and scrolling during a drag
    near list edges, so that I can place items accurately in long lists.
18. As a keyboard user, I want a reorder shortcut, so that moving an item does
    not require pointer dragging.
19. As a presenter, I want Default and Delete actions on the item I hover,
    so that list management stays close to its target.
20. As a keyboard user, I want those same row actions available on focus,
    so that I can change the default or delete an item without hovering.
21. As a presenter, I want a persistent star on the default item, so that I
    can identify what future pulls will create.
22. As a presenter, I want Add text and Add image beneath the list, so that
    creating entries remains easy to find.
23. As a presenter, I want a compact preview of the actual selected content,
    so that I can inspect its appearance while keeping fields visible.
24. As a presenter, I want clearly grouped content and appearance fields,
    so that I can edit text, font, size, color, rotation and opacity efficiently.
25. As a presenter, I want dropdowns with proper down-arrow indicators,
    so that I can recognize available choices.
26. As a presenter, I want routine font information under Font details,
    so that diagnostics do not dominate ordinary editing.
27. As a presenter, I want actionable font warnings and field errors to stay
    visible, so that I know what needs correction.
28. As a presenter, I want Interaction labels and descriptions aligned with
    their controls, so that I can understand global gesture settings.
29. As a presenter, I want changing tabs to retain my drafts and selected item,
    so that navigation does not erase work.
30. As a presenter, I want a tab with invalid fields marked and saving blocked
    until correction, so that hidden errors do not become saved data.
31. As a presenter, I want an edit followed immediately by a tab switch or Save
    to be processed in order, so that the latest input is not lost.
32. As a presenter using a small window or scaled display, I want controls to
    reflow and focused fields to scroll into view, so that they remain reachable.
33. As a presenter, I want a compact replacement drawer with integrated rows,
    so that I can choose replacement content quickly.
34. As a presenter, I want selecting a drawer row to replace only that sprite
    and close the drawer, so that I can return to the presentation immediately.
35. As a presenter, I want a real Close button plus Escape and outside-click
    dismissal, so that closing the drawer is predictable.
36. As a presenter, I want replacement to preserve the sprite's position,
    rotation, opacity and longest side, so that it retains its placement.
37. As a presenter, I want an empty drawer to explain how to add choices
    without removing the current sprite, so that an empty list is recoverable.
38. As a presenter, I want closing and reopening Settings to retain my drafts,
    so that temporarily returning to my slides does not discard input.
39. As a presenter, I want Undo behavior to be defined accurately,
    so that an Undo label promises an action the application actually supports.

## Implementation decisions

### Native UI and visual system

- Keep C, SDL3, OpenGL and Nuklear. Work through the existing theme, shared UI
  backend, Settings orchestration, item editor, project actions and drawer
  modules. BongoCat is a visual reference, not a runtime dependency.
- Apply the blue variant to the light theme. Target pale canvas `#F0F4F9`,
  white surfaces, field fill `#F5F8FC`, border `#DFE7F0`, text `#182230`,
  muted text `#627387`, blue accent `#267AC0` and selection tint `#E7F2FC`.
  Retain a separate error/removal color. Keep text contrast readable in normal,
  selected, disabled and error states.
- Keep the existing system UI font family and actual content-rendering pipeline.
  Use stronger section headings, compact secondary text, roughly 36-unit field
  heights, 8-unit control corners and 12-unit section corners. These are logical
  units and must scale through the existing density handling.
- Icon buttons are proper controls with hover, pressed, disabled and visible
  keyboard-focus states. Draw consistent icons and chevrons rather than using
  letters as substitutes. Full action names appear in hover/focus tooltips.
- Keep one runtime theme owner. Update the design documentation to describe
  the resulting theme rather than leaving its old palette authoritative.

### Project toolbar and Settings navigation

- The 2026-10-13 layout follow-up centers the visible Preset label, editable
  name, dirty marker and action icons as one bounded group, with balanced
  vertical padding and compact six-unit gaps around the name field.
  Narrow windows keep the wrapped action row centered below the name row.
  Items and Interaction use compact tab styling instead of full-width buttons;
  their keyboard focus cue is a lower indicator with no top or side border.
  Footer actions form a compact right-aligned group; the left side credits
  Project by Bowen and links to `https://github.com/dsun9/Yoink`.
- New, Open, Save and Save as have equal-width icon buttons, approximately
  44 logical units wide, aligned vertically with the project-name field. The
  asterisk is display decoration and never an edit to portable project data.
- Project name remains independent of the filename. Existing save-state rules
  remain authoritative: an untouched new project may be unsaved to a file
  without having edits. Preserve this distinction in its tooltip/status feedback.
- Retain project shortcuts, save/open dialogs and Save/Discard/Cancel guards.
  Project controls remain available on both tabs and continue checking all errors.
- Use Items and Interaction tabs. Items includes the eight-position picker,
  enabled switch, anchor list, selected-item preview, content and appearance.
  Interaction includes activation region, rotation step, resize step and easing.
- Preserve the current default Settings size and 640 by 480 logical-unit minimum.
  Use two columns when they fit and stack them when narrow. Wrap the toolbar,
  pair fields only where their labels and inputs fit, and keep controls reachable
  by scrolling. Replace hardcoded oversized column heights with content-based
  layout. Long anchor lists retain a bounded list scroller.
- Consume the outgoing page's pending input before applying a tab change. A
  hidden page keeps its draft buffers and validation errors. Mark a tab containing
  errors and provide a clear route back to those fields from global feedback.
- Suspend hidden editor hit testing and input routing without clearing text,
  caret or undo state. Hidden controls do not receive keyboard or pointer input
  and are absent from the current focus order. File choosers, composition,
  dropdown popups and app modals retain their existing interaction guards.
- Process project, selection and list mutations after the current input batch
  is consumed and validated. An invalid field continues to block mutations that
  can change the editor binding or save data, even if that field is on another tab.

### Positions and item lists

- Use a regular three-by-three picker: northwest, north, northeast; west,
  noninteractive monitor icon, east; southwest, south, southeast. Arrows point
  toward their represented screen positions. Full position names are available
  on hover/focus, and the selected name is always visible below the grid.
- Preserve eight shared anchor lists across displays and the distinction between
  item selection and the default item. The enabled switch changes the selected
  anchor list's enabled state, not its selection or default.
- Reuse retained thumbnails and actual content for row identity. Text labels
  derive from the text where practical; image labels remain predictable without
  requiring new persisted names. Preserve full Unicode content in the editor
  and preview when the control-label font lacks glyphs.
- Reorder by dragging an item or its handle. A click without sufficient motion
  continues to select the item. Show an insertion marker, scroll near viewport
  edges and commit one reorder on a valid drop within the same anchor list.
  Escape or an invalid/outside drop cancels without changing order. Retain stable
  item identity, default identity and selected-item drafts during reorder.
- Provide a documented keyboard reorder shortcut, such as Alt+Up/Down, while the
  item row is focused. It changes order through the same command boundary as a
  pointer drop and keeps focus on that item.
- Default and Delete buttons appear on row hover and keyboard focus. Reserve
  their space so labels do not jump. Pointer movement into either button keeps
  the row's actions visible. Buttons affect the row they belong to and do not
  also trigger the row selection or start a drag.
- Show the current default's filled star continuously. Choosing Default sets
  future pulls; choosing a row to edit does not. Delete retains the existing
  confirmation and validation policy.
- Remove persistent Up, Down, Default and Remove controls. Keep Add text and
  Add image below the list. Add image uses the existing supported-media chooser
  and retains PNG, GIF and SVG support.

### Editor and feedback

- Present compact actual-content preview, Content and Appearance sections.
  Pair size/color and angle/opacity when width permits. Image items keep their
  applicable controls and image-replacement chooser. Font import and collection
  face selection remain available for text items.
- Preserve real text/emoji shaping, permitted-font import, fallback handling,
  preview GIF clocks and retained content ownership. The redraw is not a change
  to image sampling, content budgets, sprite grab areas or render scheduling.
- Use a subtle transparency checkerboard. Collapse routine coverage details
  under Font details; keep fallback, missing-glyph, unsupported-content and
  validation warnings visible when actionable. Empty errors consume no row.
- Keep errors next to the affected field or group and preserve invalid entered
  values. Distinguish project unsaved state from an invalid draft that has not
  reached the project model. Save must never silently discard that draft.
- Preserve closing/reopening drafts, composition guards, focus-revealing scroll,
  error recovery and the ability to recover from invalid fields. The existing
  draft-reset behavior must remain accurately named until the Undo follow-up is
  decided; relabeling that reset as Undo does not satisfy the requested behavior.

### Replacement drawer

- Whole-row choices combine a thumbnail, useful label and optional type caption.
  Clicking the thumbnail or label has the same result. Preserve keyboard selection
  and focus-revealing scroll using stable item identities.
- Size the drawer to its header, actual choices and feedback for short lists,
  with a maximum of 340 by 360 logical units. Clamp to the current display and
  use a bounded list scroller for overflow. Eliminate reserved empty error space.
- Provide a proper labeled Close button aligned with the header. Preserve Close,
  Escape, outside-click and focus-loss dismissal and immediate close on selection.
- Choices come from the originating anchor's current list. Replacement changes
  only that temporary sprite, keeps its center, rotation, opacity and longest
  side, adopts the new aspect ratio and restarts that instance's animation.
  GIF thumbnails continue to show frame zero.
- Keep drawer/settings stacking above sprites and existing focus restoration.
  An outside click reaches its original target and retains that target's focus.
  Display-density rebuilding preserves the focused item. Empty or obsolete lists
  keep the existing sprite and provide useful feedback with Close still available.

### Public rename and compatibility

- Rename public app identity to Yoink in Settings, tray, native dialog labels,
  command help, public diagnostics, executable/bundle outputs, package/archive
  names, CI artifact references and current launch documentation.
- Keep existing `.pointy` packages and schema compatible. Continue reading the
  existing preference location so remembered project and window geometry survive.
  A preference migration, if later introduced, must be explicit and tested.
- Internal C prefixes, library names, native identifiers, historical validation
  records, fixture metadata and copyright notices are outside the public rename.
  Preserve installed-app identity unless a separately justified migration is made.

## Testing decisions

The user confirmed these boundaries on 2026-10-13. Prefer the existing native
Settings and drawer integration harnesses as the primary boundary. Assert visible
control behavior, project results, drafts, focus and saved files, rather than the
internal layout algorithm. Add lower-level checks only where the integration
boundary cannot demonstrate an input or persistence guarantee.

- Extend the existing Settings smoke and hidden actual-renderer cases. Verify
  both tabs, icon actions and tooltips, dirty asterisk, position arrows, enabled
  switch, dropdowns, hover/focus row actions, drag reorder, keyboard reorder,
  Font details and visible warning/error states.
- Exercise input batches containing an edit followed by tab change, Save,
  selection, Delete or a reorder drop before the next draw. Check the resulting
  content, draft/error retention and blocked transitions. Cover a hidden-page
  invalid draft, return to its fields and subsequent correction or draft reset.
- Prove that hidden item-text controls cannot intercept clicks at their former
  bounds or receive text events, while returning to Items retains the draft,
  caret, selection and existing field-edit undo state.
- Extend the current drawer flow and hidden-rendering cases for whole-row hits,
  real Close, compact short-list sizing, empty/long lists, keyboard scrolling,
  density transitions and unchanged replacement/focus semantics.
- Reuse command and persistence tests to verify item/default identity, order,
  canceled drops, targeted row actions, saved list order and project-name
  roundtrips without an injected asterisk. Verify an existing `.pointy` fixture
  and stored preferences still work under Yoink branding.
- Reuse shared UI-event and density tests for navigation, composition, clipped
  focus, popup dismissal, error retention and correctly scaled hit targets.
  Use current stable control IDs or action semantics; avoid tests that assert
  the internal number of drawing calls.
- Inspect captures of Items, Interaction and drawer at ordinary window sizes,
  640 by 480 and a representative narrow size, plus controlled 100%, 150% and
  200% scale. Include long labels, multiline/Unicode text, image content, row
  hover/focus, tooltips, open dropdowns and visible errors. Look for clipping,
  overlap, inconsistent alignment and inaccessible actions.
- Build and launch the renamed outputs and inspect distribution contents and
  current executable/bundle references. Use the existing isolated smoke launch
  to confirm startup, resources and Settings entry still work.
- Native SDL/GL checks remain native tests even when their windows are hidden.
  Keep desktop-label handling, bounded timeouts and serial execution. Actual
  pointer/keyboard acceptance uses the existing receiver and desktop supervisor,
  with verified input release and cursor/foreground restoration.
- Report only measured platform coverage. Static theme scans and controlled
  density captures do not establish physical monitor transitions, macOS runtime
  behavior or screen-reader support. Nuklear currently lacks a native accessibility
  tree; keyboard reachability and visible focus must be verified separately.

## Out of scope

- Dark mode or System/Light/Dark appearance preferences.
- BongoCat branding, domain pages or new runtime dependencies.
- Project-format or extension changes, automatic renaming of users' project
  files, or a rewrite of internal symbol and native identifier naming.
- Changes to sprite gestures, grab areas, content budgets, rendering algorithms,
  snapshots or animation policy beyond preserving them during the new UI flows.
- New persisted item-name fields, search, cross-anchor dragging or sorting modes.
- A new accessibility provider or unverified cross-platform acceptance claims.
- Project-wide Undo/Redo history until its scope is explicitly settled. This
  spec does not treat the mockup's Undo label as approval for such a subsystem.

## Further notes

### Undo follow-up

The user requested Undo in place of Revert fields. The visual label is approved;
field-only versus project-wide behavior was asked but not answered. The existing
Revert fields action only reloads drafts from the current model and clears invalid
draft errors. Valid committed changes remain in the model. Existing text controls
have local edit undo, but there is no project-level command history.

Project-wide Undo with Redo was recommended during design discussion, not
approved. A future contract would need to define edit grouping, restoration of
removed items and defaults, history lifetime on New/Open, saved-state identity,
and bounded retention of content/resources. Keep this as a separate scope
decision. Complete the approved redesign without adding a misleading Undo action
or removing the existing way to recover invalid drafts. If implementation is
expected to include the footer Undo request, that portion requires clarification.

### Reference artifacts

The approved Settings and drawer mockups are stored under
`docs/superpowers/specs/assets/yoink-menu-redesign/`. The Items capture includes a hovered second
row to demonstrate Default and Delete. The Interaction capture shares the same
toolbar and theme. They are design references with illustrative content, not
screenshots of an implemented feature or pixel-exact layout constraints.

Use the root glossary for item, anchor list, default item and replacement drawer.
Preserve the bounded sprite-grab-area decision and its existing acceptance gates.
The redesign-specific requirements supersede the old menu layout and theme
description for this work; older validation records remain historical evidence.
