# Shared preview verification

Ticket [#10](https://github.com/dsun9/Yoink/issues/10) moves actual anchor,
Settings and drawer consumers onto retained content. The portable presenter
and runtime checks use their agreed native/render boundaries with production
content preparation. They cover equal-consumer sharing, latest-target queue
pressure, independent clocks, immutable prepared pulls/replacement, artwork
identity versus metadata/pose, pending removal cancellation, hidden obsolete
sources, rejected-entry retry and aggregate CPU/GPU overlap rollback.

Release assertions stay enabled. Pure selection must exclude every actual SDL
video/GL/window/input path, irrespective of a test's `core` label. In particular
`lifecycle`, `shaped_editor`, `ui_density` and `drawer_hidden` have `desktop`
labels, `RUN_SERIAL` and bounded timeouts. `performance_worker_isolation` is
headless: its sole dispatcher, one private build/completion, owner TLS, worker
WM_NULL wake and complete reservation cleanup checks do not create video/GL.

Native runs use the existing receiver and supervisor exclusively:

```powershell
cmake --build --preset windows-release --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -BuildDirectory build/windows-release -Configuration Release -TestRegex '^(ui_density|ui_preview|scalable_quality|drawer_hidden|direct_gl|png_retained)$' -LogPrefix retained-consumers
```

The supervised six-test run passed: Settings density/clock 1.08 s, preview
alpha/rejection/context 1.66 s, scalable quality 0.72 s, PNG 7.23 s, direct GL
8.08 s and drawer 4.59 s, total 24.36 s. Saved and actual foreground HWND 67688,
cursor 1637,1311 and released input matched; owned processes/contexts were gone.
These figures describe bounded correctness checks, not rendering acceptance
timings. Subsequent final checks are recorded in the ticket handoff.

`ui_preview` uses actual GL/Nuklear at density 1.25 with fractional drawable
placement and independent bilinear sampling of a half-transparent red SVG.
The original 83-byte failure used an incorrect unquantized Nuklear command
mapping and remains fixture evidence. With the corrected integer logical
command geometry, the old straight-FBO path failed at 30.183 color bytes. Keeping
premultiplied color through the final sample passed at 0.589 over 47 translucent
samples, under the unchanged two-byte gate. Readback is diagnostic-only; normal
preview metrics require zero readbacks.

Full-budget tests retain a usable 96x32 accepted output and its exact 12,300-byte
charge when the same item changes artwork. A rejected different selection has
no texture/dimensions/accepted snapshot, preventing previous-item pixels. Retry
after pressure removal succeeds; unchanged updates add no upload/draw. A scoped
one-shot final-context-restoration fault first failed by deleting the accepted
output. The corrected transaction preserves it, restores actual GL state and
the owning context, rejects an unrelated context before mutation and returns
to zero content GPU bytes after release.

Settings checks run its real hidden editor: rename and opacity edits preserve
clock/source work; an actual text commit restarts its clock while preserving
focus and undo. Density 1->2->1 preserves invalid drafts, caret, control ID,
selection and geometry; closure releases the preview consumer. Drawer checks
run actual frame-zero GIF sampling, source/upload/draw reuse across reorder,
stable focused item, removed/unused-slot cleanup, exact GPU refunds and prepared
replacement with preserved 337-DIP/17.25-degree/0.63-opacity pose and a restarted
instance clock. Long-list scroll and negative-origin density checks remain.

The quality helper observes through the sole content dispatcher. Six source
fixtures at zero/17.25 degrees compare prepared versus fresh RGBA/coverage
byte-for-byte; the independent normal channel error is at most one under the
two-byte gate, with the existing tightly bounded float-edge policy unchanged.
Color-only compatibility output also equals explicit RGBA export byte-for-byte,
has no coverage plane and performs exactly one readback. This proves the shared
GL/Cocoa compatibility code, not native Cocoa runtime behavior.

The first density GREEN failed an obsolete atlas-name fixture and then timed
out; those failures remain distinct. Bounded stage runs and an owned debugger
captured main holding the CRT exit lock while joining a worker that registered
a HarfBuzz finalizer. The test harness now cleans up before normal/failed exit,
not from a worker-joining atexit callback. No speculative production lifecycle
fix was adopted. Diagnostic timeout/stack/dump artifacts remain external.

The earlier 22 state-dependent white first-use observations remain unresolved
history. A clean unchanged PNG/direct-GL baseline and the six-test run passed;
no production remedy or established cause explains those earlier failures.
Hosted Mac builds/headless checks and Windows shared-code tests do not prove
native Mac input, Retina, Spaces or compositor behavior. Physical mixed displays,
fullscreen and visible compositor cadence remain separate unverified cases.
The full 96-case 16.7 ms warm, 50 ms input and 200 ms sharp acceptance remains
ticket #11; this ticket does not weaken or accept those gates.

The premium static audit scans web source formats and cannot execute native
Nuklear controls or establish an accessibility tree. Native keyboard/focus,
diagnostics and retained preview checks provide the relevant behavioral
evidence; browser CSS/ARIA and screen-reader acceptance are not claimed.

## Final ticket check

The final all-target Release build passed. The audited headless selection passed
28/28 in 39.07 s, plus five event/input/dialog/launcher boundary checks in 2.01 s.
Its first run failed the old scene expectation that a scaled export replaces
canonical pixels. The corrected worked charge keeps immutable canonical plus
export bytes and checks the original pixel pointer; the complete suite passed.
After removing redundant visibility scans, runtime/runtime_svg/presenter passed
3/3 in 6.36 s. A zero-opacity 384-DIP source request first failed by admitting
work while a sibling job was held; the corrected path preserves that sibling's
pending source and accepted input without starting hidden work.

The final supervised desktop selection passed all 16 tests in 107.02 s:
shaped_editor, lifecycle, ui_density, ui_preview, bitmap_retained,
scalable_quality, png_retained, direct_gl, sprite_flow, drawer_hidden,
surface_failure, settings_smoke, windows_gif, windows_surface, windows_capture
and windows_anchors. Native hidden surface_failure is now correctly labeled
desktop/serialized/bounded rather than selected as pure merely for its core label.
The drawer epoch RED failed at remaining consumer/texture after New/Discard.
GREEN releases obsolete slots and all their GPU bytes while preserving an
independent old GIF frame-zero source. Hidden Settings epochs also preserve an
independent source's owned pixels. All alpha/context/rollback assertions stayed
unchanged. Exact saved/actual HWND 67688, cursor 1637,1311 and released input
recovered, with no owned processes/contexts left.

Current Settings 100%/150%, focused narrow layout and drawer captures were
visually inspected: established controls, checkerboard/source color, fallback
diagnostic, scrolling and keyboard outline remain. Strict premium audit returned
zero findings; DESIGN lint returned zero errors/warnings. Native C/Nuklear is
outside the audit script's web-source scanner, so those static results do not
replace the native observations. The first audit attempt hit unavailable Windows
Python/npx aliases; the bundled Python and pnpm runtime reruns succeeded.
