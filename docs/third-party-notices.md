# Third-party notices

## Adapted settings backend

PointyPointer adapts backend, input and form patterns from
[BongoCat](https://github.com/vladelaina/BongoCat), revision
`3f39cf8c47b408ed7976d73ce9c318f59fb66528`:

- `src/ui/backend/ui_backend.c`, `ui_backend.h`, `ui_input.c`,
  `nuklear_config.h`, and `nuklear_impl.c`: Nuklear setup, OpenGL vertex
  conversion, clipboard and normalized input patterns.
- `src/ui/preferences/preferences_widgets.c` and
  `preferences_widgets_compound.c`: labeled form/widget patterns.

Copyright BongoCat contributors. Licensed under GNU Affero General Public
License version 3 or later; the full license is in the repository root
`LICENSE`. Adapted files are `src/ui/backend.c`, `events.c`,
`nuklear_config.h`, `settings.c`, `item_editor.c`, and `project_actions.c`.
The OpenGL loader/shader setup in `src/render/gl_api.c` also adapts BongoCat
`src/render/gl_api.c`, under the same AGPL-3.0-or-later terms.
The adaptation removes global context lookup, uses one backend per window,
and replaces BongoCat-specific settings with PointyPointer commands.
The reference checkout is not a build dependency.

## Nuklear and embedded support code

[Nuklear](https://github.com/Immediate-Mode-UI/Nuklear), revision
`8109cfbabe04f8705408c5d8ab1a6cd48649ccda`, is used under its MIT option.
Copyright (c) 2017 Micha Mettke and contributors. Its embedded stb text edit,
TrueType and rectangle packing code is by Sean Barrett (public domain or MIT).
The embedded fallback ProggyClean font is Copyright (c) 2004, 2005
Tristan Grimmer, under MIT. These notices and the license are retained in
the upstream `nuklear.h`; the common MIT permission follows:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Fonts and dependency inventory

Segoe UI and Segoe UI Semibold are read from the Windows installation for
settings controls, and are not bundled. Aptos is looked up on the host for
ordinary text and is not bundled. Installed Segoe UI Emoji supplies default
emoji artwork and is not bundled. Its permitted complete bytes are owned
privately by projects, with selected-font metadata and original license tables
retained. Document embedding does not authorize application redistribution.
The bundled Noto Color Emoji font is the missing-Segoe fallback; its
copyright and SIL Open Font License are retained in `resources/licenses/NotoEmoji-OFL.txt`.
Portable projects embed that notice when they embed the font.

The optional `resources/optional-fonts/Twemoji.Mozilla-v0.7.0.ttf` contains
unmodified Mozilla Twemoji COLR0 font bytes (Twemoji 14 artwork). Twemoji visual
artwork by Twitter and other contributors is licensed CC BY 4.0. Mozilla's
font build code is Apache-2.0. Attribution, immutable source revision, font
digest, upstream notice and full CC BY 4.0 legal text are retained in
`resources/licenses/Twemoji-Mozilla-NOTICE.txt`; the upstream notice and legal
text are also supplied separately. The portable Twemoji example embeds this
complete notice and the complete font. Installed Segoe UI Emoji remains the
default, with bundled Noto when Segoe is unavailable.

Dependency revisions are pinned in `cmake/Dependencies.cmake`. Upstream
source license files remain in each fetched source tree. The distribution
assembly must retain those license texts along with this document:

| Dependency | License source in upstream tree |
|---|---|
| SDL3, SDL_image, SDL_ttf | `LICENSE.txt` (zlib) |
| FreeType | `docs/FTL.TXT` and `docs/GPLv2.TXT` (dual license; FTL option) |
| HarfBuzz | `COPYING` (MIT-style notices) |
| HarfBuzz Universal Shaping Engine data | `src/ms-use/COPYING` (Microsoft MIT) |
| PlutoSVG, PlutoVG | `LICENSE` (MIT) |
| libpng | `LICENSE` (libpng license) |
| zlib | `LICENSE` (zlib license) |
| Expat | `expat/COPYING` (MIT-style) |
| yyjson | `LICENSE` (MIT; Copyright 2020 YaoYuan) |
| miniz | `LICENSE` (MIT; RAD Game Tools, Valve, Rich Geldreich, Tenacious Software) |

Imported user media and fonts retain their own licenses. Font import checks
the embedding permissions of every face in a collection. It preserves full
font bytes, including internal license metadata, rather than subsetting.

## Distribution and altered dependency sources

The Release distribution includes the full license texts in `licenses/`, the
application AGPL in `LICENSE`, and Noto's complete OFL alongside its font in
`resources/licenses/NotoEmoji-OFL.txt`. It ships Noto Color Emoji v2.051 and
does not redistribute Aptos, Segoe UI, Segoe UI Emoji, or any other installed proprietary font.
The application icon is original geometric artwork authored for PointyPointer.

This software uses FreeType under its FreeType License. Portions of this
software are copyright © The FreeType Project (www.freetype.org). All rights
reserved. HarfBuzz and its Microsoft shaping data retain both upstream notices.

PointyPointer builds explicitly altered, hash-checked copies of SDL_image's
GIF decoder and SDL_ttf's shaping source. `cmake/PatchSDLImage.cmake` changes
disposal-3 snapshots/restores to copy transparent pixels exactly.
`cmake/PatchSDLTTF.cmake` preserves complete fallback clusters, including default
ignorables and the final cluster boundary. It also measures COLR0 glyphs using
FreeType's existing color-layer compositor, because an empty base outline does
not describe the visible color layers. Upstream source trees are untouched;
the generated files preserve their original notices and mark our changes.

The SVG numeric validator in `src/media/svg_validate.c` adapts PlutoSVG's
`parse_float`; its complete MIT permission/copyright remains in that source
and the distribution's `licenses/PlutoSVG.txt`.

SDL_image's GIF decoder additionally incorporates XPaint code with this notice:

Copyright 1990, 1991, 1993 David Koblas. Copyright 1996 Torsten Martinsen.
Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting documentation.
This software is provided "as is" without express or implied warranty.

Binary redistribution must accompany the complete corresponding PointyPointer
source, including build scripts and pinned dependency/patch information, under
the application AGPL terms. This local unsigned ZIP is a build artifact; no
public distribution, signing, notarization, or source offer has been published.
