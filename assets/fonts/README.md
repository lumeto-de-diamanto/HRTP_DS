# Round-results serif

`results_serif.bdf` is the fixed bitmap master for **HRTP Results Serif**, a
Libre Bodoni Bold derivative. Other screens still use the selected `assets/font.bmp`.

* Source: [Libre Bodoni](https://github.com/googlefonts/Libre-Bodoni), by
  Pablo Impallari, Rodrigo Fuenzalida and the Libre Bodoni Project Authors.
* Input: Google Fonts `ofl/librebodoni/LibreBodoni[wght].ttf`, SHA-256
  `0d543665bd869819ffd7d46608804e245497ba835960832d44be89e561b0c717`.
* Weight 700, FreeType light auto-hinting and monochrome rasterization.
  Width/height are set **before** hinting; no rasterized pixels are stretched.
* Numerals: 24 x 21 pixels per em, centered in fixed 16 x 16 cells. All ten
  digits are 16 pixels high; ink widths are 11--15 pixels (the 1 stays narrower).
  One blank column prevents adjacent wide digits from joining. Fixed advances
  preserve rolling and right alignment.
* Labels: 20 x 20 pixels per em, HarfBuzz default shaping/kerning and normal
  proportional advances. Each complete string is precomposed, left-aligned
  in16-pixel-high strips, except the prompt's 24-row strip, which preserves
  the descender in “Key” at the same 20×20 ppem. Padding is only at the end
  and below the bitmap, never between letters.

The BDF contains ten ASCII numerals plus eight private-use strip glyphs:

| Encoding | Text |
| --- | --- |
| U+E000 | STAGE |
| U+E001 | COMPLETE |
| U+E002 | Time |
| U+E003 | Max Combo |
| U+E004 | Bomb/Life |
| U+E005 | Clear |
| U+E006 | Total |
| U+E007 | Hit A or B Key |

The BDF is the build input on purpose: no system font, installed FreeType,
HarfBuzz, network access or rasterizer-version-dependent output is needed to
build the game. `tools/results_serif_to_h.py` validates and packs its exact rows
and strip dimensions, padding 24-row data to two rows of 16×16 cells. It also
exports actual ink bounds for pixel-accurate centering. Runtime uses the existing
BG0 tiles and zipper compositor, not a font engine.

The results refinement centers the combined STAGE/number line and COMPLETE
independently on their visible ink; COMPLETE moves down exactly one pixel.
The full prompt is centered and keeps its bottom ink row at 190. Numerals,
other label pixels, tally positions and animation cadence are unchanged.

Copyright 2012 The Libre Bodoni Project Authors. The bitmap derivative remains
under the [SIL Open Font License 1.1](../licenses/LibreBodoni-OFL.txt).
The full notice is also included in the ROM at `licenses/LibreBodoni-OFL.txt`.
