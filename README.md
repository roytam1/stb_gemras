# stb_gemras

Single-file C89 decoder for Digital Research GEM Raster (GEM VDI Bit Image,
`.IMG`) in `stb_image` style. Public domain (MIT alternative, see the header).

## Usage

```c
#define STB_GEMRAS_IMPLEMENTATION
#include "stb_gemras.h"

int w, h, comp;
unsigned char *px = stb_gemras_load("image.img", &w, &h, &comp, 0);
/* ... use px ... */
stb_gemras_free(px);
```

`req_comp` follows the `stb_image` convention: `0` keeps the native count,
`1` gray, `2` gray+alpha, `3` RGB, `4` RGBA. Native output is 1 channel for
monochrome images and 3 channels otherwise. Header-only info probes
(`stb_gemras_info*`) and format sniffers (`stb_gemras_is_*`) never decode
pixels; human-readable errors come from `stb_gemras_failure_reason()`.

Build options: `STB_GEMRAS_NO_STDIO` drops the filename APIs,
`STB_GEMRAS_STATIC` makes everything `static`, and `STB_GEMRAS_MALLOC` /
`STB_GEMRAS_REALLOC` / `STB_GEMRAS_FREE` override allocation. Compiles clean
with `gcc -std=c89 -pedantic -Wall -Wextra -Werror` and as C++.

## Supported variants

- 8-word headers: mono, ST color default for 2/3/4 planes, grayscale otherwise
- 9-word headers, 1-8 planes (mono, grayscale, Ventura 3/4-plane color)
- 25-word headers with Atari ST palette (Hyperpaint and similar)
- XIMG extended headers with direct-index 0-1000 RGB palette
- XIMG 8-plane header-only grayscale, STTT 54-byte, TIMG 28-byte 15/16/24,
  chunky 16/24/32-plane truecolor, Falcon 18-byte R8G8B8 triplets

Everything else (other header sizes, invalid opcodes) fails to load.

Decompression implements the four GEM opcodes — solid run, literal run
(`0x80`, where `0x80 0x00` means 256 bytes), pattern run (`0x00 nn`), and
scanline repeat (`0x00 0x00 0xFF nn`, total count) — with a stream model, so
opcodes may span output rows. Truncated input is zero-padded.

## Verification

All 82 files in `images/` decode; pixel output was diffed against recoil
6.4.5 `recoil2png` (see `tests.txt`). Remaining divergences are deliberate
and match the deark (`gemras.c`) reference instead:

- XIMG 0-1000 palette scaling rounds; recoil truncates (max 1 LSB).
- The Ventura gray/color flag is honored; recoil ignores it.
- ST palette 9/12-bit is content-autodetected; recoil forces it by resolution.
- Stored (not display-doubled) dimensions are reported.
- Truncated input decodes with zero padding instead of failing.

## References

- `gemras.c` — Deark's GEM Raster module, the behavior reference.
- `recoil-6.4.5.tar.gz` — RECOIL's `RECOIL_DecodeStImg`, source of the
  extended-variant coverage (ver 3, chunky, STTT, TIMG, Falcon, stream RLE).
- `abydos-0.2.12.tar.xz` — Abydos has no native raster decoder of its own;
  its `gemvdi` plugin handles only the vector metafile while raster goes
  through its bundled (older) recoil, whose GEM logic matches 6.4.5.
- `images/` — 82 sample files.
