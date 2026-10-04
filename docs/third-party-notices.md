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
default text and is not bundled. The bundled Noto Color Emoji font's
copyright and SIL Open Font License are retained in `resources/licenses/NotoEmoji-OFL.txt`.
Portable projects embed that notice when they embed the font.

Dependency revisions are pinned in `cmake/Dependencies.cmake`. Upstream
source license files remain in each fetched source tree. The distribution
assembly must retain those license texts along with this document:

| Dependency | License source in upstream tree |
|---|---|
| SDL3, SDL_image, SDL_ttf | `LICENSE.txt` (zlib) |
| FreeType | `docs/FTL.TXT` and `docs/GPLv2.TXT` (dual license; FTL option) |
| HarfBuzz | `COPYING` (MIT-style notices) |
| PlutoSVG, PlutoVG | `LICENSE` (MIT) |
| libpng | `LICENSE` (libpng license) |
| zlib | `LICENSE` (zlib license) |
| Expat | `expat/COPYING` (MIT-style) |
| yyjson | `LICENSE` (MIT; Copyright 2020 YaoYuan) |
| miniz | `LICENSE` (MIT; RAD Game Tools, Valve, Rich Geldreich, Tenacious Software) |

Imported user media and fonts retain their own licenses. Font import checks
the embedding permissions of every face in a collection. It preserves full
font bytes, including internal license metadata, rather than subsetting.
