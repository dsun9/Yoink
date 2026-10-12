# Bitmap-only text retention

Ticket #10 uses this useful-strike/canonical mapping policy for anchors,
Settings and drawer as well as sprites, with cached source-alpha visibility and
shared CPU accounting. The selected installed Segoe or identified Noto fallback
bytes remain the same across consumers. See [preview checks](preview-retention.md)
and [source ownership](../architecture/rendering-content.md).

The bitmap sprite path classifies the glyphs that SDL_ttf actually draws after
shaping and fallback. It inspects each selected glyph's FreeType artwork before
rasterization. A whole item uses this path only when every visible operation is
bitmap artwork. An outline, a supported scalable color glyph, or an unknown
visible operation keeps the normal requested-resolution path. The font's
embedding restrictions and the presence of a bitmap fallback do not select this
policy.

`pp_item_snapshot_bitmap_source` retains one immutable useful-strike raster per
snapshot, shared by snapshot retainers. It preserves canonical layout size and
origin separately from source dimensions and raster origin. For the bundled Noto
right hand at a 96-DIP font size, the canonical layout is 120 by 113 DIP and the
useful source is the original 136 by 128 strike at 109/96 pixels per DIP.
Fractional resizing, rotation, opacity and density changes transform that source
in GL. A position-only update moves the window. Static content and translation
perform no source generation, upload, readback or transform draw.

Source alpha validation still runs during text preparation. The retained useful
source then releases the coverage plane because its direct presentation consumer
needs only premultiplied RGBA. The separate compatibility text frame retains its
coverage contract and charge. A useful source costs exactly `stride * height`
bytes. Its allocation preflight includes temporary RGBA plus coverage within the
remaining combined 512-MiB CPU budget and the existing 256-MiB resource limit.
Every old retained frame stays charged until replacement succeeds. Each renderer
retains one useful RGBA texture within the shared presenter's 256-MiB GPU budget;
candidate overlap is charged before upload. Destruction releases every charge.

The public source regression checks every Noto hand pixel against the selected
FreeType strike, including exact premultiplied edge channels. It verifies a joined
emoji remains one glyph and two hands retain their complete line placement. The
canonical result API shapes those lines with a 128-pixel baseline step, yielding
136 by 256 source pixels and a 120 by 226 canonical layout. It also checks the
original Pointy Hybrid bitmap with a negative bearing and raised top, its padding,
alpha border and transparent interior. Mixed outline and bitmap content rejects
bitmap acquisition and continues to render at the requested resolution.

Run the portable checks after a Release build:

```powershell
cmake --build build/windows-release --config Release --target pointy_text_test pointy_bitmap_text_test pointy_anchor_presenter_test pointy_runtime_test --parallel 4
ctest --test-dir build/windows-release -C Release -R '^(text|text_canonical|text_artwork|text_hybrid|bitmap_text|anchor_presenter|runtime)$' --output-on-failure
```

The production presenter regression exercises a 1536-DIP sprite through
fractional wheel easing, rotation, opacity, translation, static reuse, density
changes and dragging. It checks exact CPU/GPU charges, candidate rejection,
sixteen replacements, zero-opacity capture cleanup and broken-context cleanup.
A failed candidate context while replacing a compatibility presentation preserves
the previous usable snapshot and animation clock, then permits a successful retry.

The native receiver check is opt-in and requires exclusive desktop ownership:

```powershell
cmake --build build/windows-release --config Release --target pointy_bitmap_retained_test --parallel 4
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -BuildDirectory build/windows-release -TestRegex '^bitmap_retained$' -LogPrefix bitmap-retained -ProcessTimeoutMilliseconds 60000
```

It compares displayed pixels against independent bilinear sampling of the
original useful source at zero and fractional rotation, including premultiplied
colors and display opacity. It also checks rectangular input, outside input,
capture, nonactivation, static/translation reuse, wheel transforms and failed
window cleanup. The supervisor records original and actual foreground handles
along with input, cursor and foreground recovery results. A failed color or
recovery assertion fails the command.

The bounded Release native gate passed on Windows at 150% density in 5.80
seconds. It checked 462 samples at zero rotation, including 16 alpha-edge samples,
and 576 samples at 17.25 degrees with half opacity, including 121 partial-alpha
samples. Both comparisons had a maximum RGB difference of one level. The source
stayed 136 by 128 with a 69632-byte GPU charge. Input/capture/nonactivation and
source reuse checks passed. The supervisor restored released input, cursor and
the recorded Codex foreground handle, with original and actual HWND both 525984.

Two earlier developmental runs passed the zero-rotation comparison but failed
rotated capture expectations. The first sampled outside its fixed receiver
backdrop; the second captured an unexplained dark background color and also
failed foreground restoration. Root restored the user-selected Codex app before
the next run. Those failures remain preserved in external evidence. The final
fixture uses a full receiver client backdrop, one bounded bulk capture per pose
and foreground checks around capture. It keeps the original color expectation
and tolerance. No renderer change was made to pass those comparisons.

These tests establish source, ownership and native color/input behavior within
their scenarios. They do not establish the complete 16.7-ms, 50-ms input and
200-ms sharp-publication performance gates or visible compositor cadence.
macOS native execution and physical cross-display/fullscreen behavior remain
unverified. The parent integration performance gate covers the full size/source
matrix separately.
