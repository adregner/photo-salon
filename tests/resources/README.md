# Test images

Tiny fixtures for `test_extra_formats`, which exercises the HEIF and JPEG 2000
decoders. All of them are 64×48 and were generated from a synthetic gradient,
so they stay in the low hundreds of bytes.

| File | How it was made |
|---|---|
| `gradient.heic` | `heif-enc -o gradient.heic gradient.png` (libheif 1.17, x265) |
| `fujifilm-exif.heic` | `heif-enc -o fujifilm-exif.heic Fujifilm_FinePix_E500.jpg` — re-encodes an exif-py sample so the HEIF carries a real camera EXIF block |
| `gradient.jpf` | `opj_compress -i gradient.png -o gradient.jp2 -r 20`, renamed — `.jpf` is the JPEG 2000 Part 2 extension and shares the JP2 signature box |
| `gradient.j2k` | `opj_compress -i gradient.png -o gradient.j2k -r 20` — raw codestream, no JP2 container |
| `gray.jp2` | `opj_compress -i gray.png -o gray.jp2 -r 10` — single-component (grayscale) image |

The `gradient.png` source is `R = (x·4) mod 256`, `G = (y·5) mod 256`, `B = 128`;
`gray.png` is `V = (x·4) mod 256`. Both encoders are lossy, so tests compare
pixels with a tolerance.
