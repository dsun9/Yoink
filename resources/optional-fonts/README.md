# Optional scalable emoji

`Twemoji.Mozilla-v0.7.0.ttf` is an optional color font with scalable COLR0
artwork. It changes the emoji design to Twemoji 14. Installed Segoe UI Emoji is
the default, with bundled Noto when Segoe is unavailable,
and opening an existing project retains its saved font bytes and appearance.

## Open the example

1. Open PointyPointer Settings and choose **Open** (Ctrl+O).
2. Choose `resources/examples/twemoji-example.pointy` beside the application.
3. Select the **Top-left** list. It contains a hand, skin-tone hand, heart,
   woman technologist, US flag and keycap, all using **Twemoji Mozilla** as
   their primary font with the existing Noto fallback.
4. Use **Save As** (Ctrl+Shift+S) to keep an editable copy in your own folder.

The example embeds both complete fonts and their complete notices. Its saved
Unicode strings and rendered pixels were checked after loading into a fresh
asset manager. It does not need the original font paths after saving.

## Import into a text item

1. Select a text/emoji item in Settings.
2. Choose **Import font (TTF / OTF / TTC / OTC)**.
3. Select `resources/optional-fonts/Twemoji.Mozilla-v0.7.0.ttf` and use face 0.
4. Confirm that the requested family is **Twemoji Mozilla**, then save.

Import selects the font as the item's primary font. The font is best suited
to emoji strings; it does not provide general Latin text coverage. The
portable example already has the attribution stored inside the project.
The generic font importer does not automatically read adjacent license files;
retain `resources/licenses/Twemoji-Mozilla-NOTICE.txt` with projects made by
raw font import when sharing them.

## Quality and cost

Scalable outlines retain edge detail when enlarged. This font has different
artwork and older coverage than bundled Noto Color Emoji v2.051; unsupported
sequences may use the item's retained emoji fallback or display separate/missing glyphs. Noto's existing
bitmap font still has its original source-resolution limit.

On the measured Windows CLion Debug build, a 1536 DIP hand at 150% density
took 134 ms to generate Twemoji's source and 18–21 ms for its final OpenGL
transform at 0°/27°. Noto took 56 ms plus 13–19 ms in the same harness.
These are individual measured samples, not frame-rate guarantees. A separate
mixed Latin/emoji layout with a scalable emoji fallback took 498 ms at a
1536 DIP font size and hit the probe's 250 ms stop threshold. That case uses
font size rather than the hand's longest-side size, so it is not a paired
speed comparison.

## Source and attribution

The full, unmodified font is Mozilla Twemoji COLR v0.7.0, from source revision
`c0460fdb2654dee584e75c4913bfa9fd2f873338`:
https://github.com/mozilla/twemoji-colr/releases/tag/v0.7.0

Size: 1,474,284 bytes. SHA-256:
`6d90152ee0d29e82fe2a87793af5aa4b7ad13e6538360889e141e81ed299ee8e`.

Twemoji artwork by Twitter and other contributors is CC BY 4.0. Mozilla's
font build code is Apache-2.0. Complete attribution, upstream notice and
CC BY 4.0 legal text are in `resources/licenses/Twemoji-Mozilla-NOTICE.txt`.
The original upstream notice and the full legal text are also supplied as
`Twemoji-Mozilla-Upstream-LICENSE.md` and `Twemoji-CC-BY-4.0.txt`.

The application build and CPack distribution copy the whole `resources/`
directory, including this font, these notices and the portable example.
