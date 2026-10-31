# PS2 source: generate game data from a PS2 disc

The converters can read a PS2 disc (Need for Speed Underground 2, US NTSC-U,
`SLUS_210.65`) instead of an Xbox disc:

1. `tools/extract_ps2.py PATH.iso` extracts the ISO 9660 image into `game/ps2`.
2. `NFSU2_SOURCE=ps2 tools/build.sh` generates the distribution. For an individual
   converter, use `--source ps2` or `--game game/ps2/NFSUNDER`.

`tools/n2source.py` verifies the requested source and writes `source.txt` into

the distribution. A converter using another source stops instead of mixing
files from different discs. Index caches in `.tools/cache` (city objects and
vinyls) are stored separately for each source.

## Differences between Xbox and PS2

The container format is the same: 24-byte ZDIR entries, ZZDATA0-3 and
little-endian bChunk records. The 1,217 shared files have identical hashes.

| Data | PS2 representation | Reader |
|---|---|---|
| GLOBALB tables (career, physics, opponent behavior, traffic and cameras), ROUTES/PATHS, city collision and instances, LANGUAGES | Byte-identical | Shared readers |
| Music (`MUSICSFX.MUS`), GIN engines | Byte-identical | Shared readers |
| Meshes (city, tracks, cars, rims, parts and GPS marker) | VU1 VIF packets (`0x134a00-a02`) | `n2formats.ps2_mesh` |
| Textures | CLUT4/CLUT8 and RGBA in GS PSMT8 swizzle | `n2formats.decode_ps2_texture` |
| NIS characters (skinned meshes) | VIF with f32 positions and UVs; low bytes carry bone indices (x 4) and weights (/255) | `n2formats.ps2_skinned` |
| Showroom environment map | Sphere map (`CARSELECTENVMAP`) | `convert_car.sphere_face` |
| Executable tables (sound, NIS rotations, zones, reverbs, help, drivers and flag girls) | In `SLUS_210.65`, partly constructed by code | `n2exe` + `n2mips` |
| Effects, gear changes, turbo, skids, stitches and AEMS loops | VAG (SPU ADPCM), 24-32 kHz versus 48 kHz on Xbox | `convert_audio.decode_vag`, `encode_xa` |
| NIS audio | EA-XA at 32 kHz (Xbox: 36 kHz) | `nis.n2sp` version 2, with sample rate |
| Movies | `MOVIES/*.MPC` (MPEG-2 512x192 anamorphic video, stereo EA ADPCM R3) | ffmpeg |

### Meshes (`ps2_mesh`)

Each object contains:

- A 16-byte record per batch in `0x134a01`, describing vertices, triangles,
  submesh, texture and material.
- One VIF batch per MSCAL in `0x134a02`.

VU addresses within a batch:

| Address | Contents |
|---|---|
| 0 | Header with vertex count |
| 1 | Flag words; word 1 is the position scale |
| 2-3 | Bounding box |
| 4 | Culling cone |
| 5-28 | Positions |
| 29-44 | UVs |
| 45-76 | Colors |
| 77 onward | Normals |

Component encoding:

- **Positions:** u16 multiplied by the scale (0.0002 for cars, 1/256 for large
  city parts), added to the truncated bounding-box minimum minus 1. Some
  batches use f32. Values are grouped in blocks of four vertices (4 x, 4 y, 4 z).
- **UVs:** value/4096, ordered u0 v0 v1 u1 u2 v2 v3 u3.
- **Normals:** s8/127, using the same blocks as positions.
- **Colors:** 0x80 = 1.0.
- **Strips:** each batch is a strip, restarted by ADC flags (`ps2_adc`).

Compared with Xbox using the same triangle, orientation and texture:

| Mesh | Matching values |
|---|---|
| Cars | 99.94-99.99% |
| Rims | 100% |
| City | 99.5-100% |

Car UVs and normals match 100%; city UVs match 99.1%.

### Textures

- **Resolution:** 1,580 of 1,611 city textures match, 24 have half the resolution
  and 7 are larger. In GLOBALB, 37 of 181 have half the resolution. Car textures
  use 8-bit indices instead of Xbox DXT1.
- **Format:** palettes already use the port's CLUT8 format; no requantization
  is required.
- **Mipmaps:** PS2 includes only level 0; `convert_mips` generates the rest.

## Known gaps: absent from PS2 or not yet read by the port

- **Rachel's speech:** offline conversion reads EA MicroTalk v3 with PCM blocks
  (22,050 Hz), using the decoder pinned in `vendor/utk/PROVENANCE.json`, and
  generates mono EA-XA at 32 kHz for the existing reader. All 857 indices and
  blocks were checked in the isolated October 25 output; playback with converted
  PS2 data and on PSP still needs validation.
- **Exhaust pops (`Sparkchatter_00`):** the PS2 bank has 84 samples per sound;
  this effect becomes silent.
- **ROADNOISE:** PS2 has no slot 4. Slot 3 plays in its place.
- **Prelit city colors:** PS2 clamps about 20% of the brightest vertices to 128;
  Xbox values reach 255.
- **City sections G1, O13, O6 and R12:** present only on Xbox.
- **Lower audio rates:** effects use 24-32 kHz instead of 48 kHz; movies have
  192 lines instead of 480.
- **Button icons:** `XBOX_*` entries are absent from PS2; the port uses its own
  PSP icons.
