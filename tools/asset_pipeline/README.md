# Asset pipeline tools

## Headless content validation

Build the default project and run:

```text
talvisota_content_validate <manifest> <content-root>
```

This validates stable IDs, provenance, required files, dependency edges, typed materials,
worlds, missions, factions, characters, and equipment. It does not decode media or
initialize a graphics device.

## Engine-backed media validation and texture processing

On Windows x64 with MSVC, configure using `-DTALVISOTA_ENABLE_VANTA_ENGINE=ON`. This
enables:

```text
talvisota_asset_validate <manifest> <content-root>
talvisota_texture_process <source-image> <output.ttex> <srgb|linear|normal>
talvisota_content_package <manifest> <content-root> <package-dir>
talvisota_content_package --verify <package-dir>
```

The validator imports OBJ/glTF/GLB models through the pinned VantaEngine revision,
including base64/percent-encoded glTF data-URI images and GLB buffer-view images. Embedded
payloads remain owned by CPU materials and are decoded through WIC without sidecar
extraction. External image sources are decoded through WIC; processed `.ttex` containers
are read with the engine's strict versioned reader. OBJ material libraries and texture
references must resolve inside the content root and be declared as manifest dependencies.

Audio validation fully decodes bounded PCM/IEEE-float WAV, Ogg Vorbis, and FLAC streams.
Ogg Vorbis and FLAC use the vendored stb_vorbis and dr_flac decoders. Encoded size,
duration, channel count, sample rate, complete frame count, and finite decoded samples are
checked. Validation is CPU-only and does not initialize D3D.

The texture processor uses WIC decoding and deterministic CPU mip generation. The
little-endian `TVTTEX01` output omits timestamps, absolute paths, and host-specific
metadata. Use `srgb` for colour maps, `linear` for ORM/roughness/AO and other data
maps, and `normal` for tangent-space normal maps. Normal mode normalizes source
vectors, averages and renormalizes each mip level, and filters alpha linearly.

## Procedural environment PBR maps

The default build runs `talvisota_environment_texture_gen` to create 41 deterministic
1024x1024 BMP maps: albedo, tangent-space normal, roughness, and AO for ten material
families, plus packed snow ORM. The generator checks periodic continuity on both UV
axes for all ten source fields before writing. Packed-snow compression uses independent
integer U/V periods, preserving its anisotropic detail without a wrap seam.
`Talvisota.Environment.PbrMaps` runs the generator's `--verify` path to validate all
41 BMP headers, dimensions, byte counts, pixel variation, and the snow/ice relief
ranges. Generated maps are build products and are recreated before content validation
and packaging.


## OBJ material and normal-map diagnostics

The read-only `obj_material_diagnostics.py` utility audits Wavefront OBJ/MTL material
assignments without importing or modifying the asset. It triangulates polygon faces,
validates positive and negative position/UV/normal indices, resolves MTL sidecars and
normal-map paths within the content root, and reports per-material missing/degenerate
mapping counts, singular texel-density bounds, and anisotropy.

Run it against the shipped interior models from the repository root:

```sh
python3 tools/asset_pipeline/obj_material_diagnostics.py \
  assets/models/interiors/church-interior.obj --content-root . \
  --normal-map-size 1024 \
  --source-frequency bark-normal.bmp=840 \
  --source-frequency granite-normal.bmp=1536 --json

python3 tools/asset_pipeline/obj_material_diagnostics.py \
  assets/models/interiors/farmhouse-interior.obj --content-root . \
  --normal-map-size 1024 \
  --source-frequency bark-normal.bmp=840 \
  --source-frequency granite-normal.bmp=1536 --json
```

The generated environment BMPs are build products and are not required in a clean
source checkout. `--normal-map-size 1024` supplies their known dimensions only when
a referenced map is absent or its header cannot be read; an available BMP/PNG header
takes precedence. The source-frequency values are the highest explicit height-field
octaves in `EnvironmentTextureGenerator.cpp`, expressed in cycles per UV tile. For a
1024-pixel map the conservative base-level Nyquist limit is 512 cycles per UV tile.

Interpret the JSON per material. `texels_per_meter_min_singular` and
`texels_per_meter_max_singular` describe the lower and upper singular values of the
world-to-texture Jacobian; `anisotropy` is their ratio. A zero missing/degenerate
count and near-unit anisotropy indicate that the source mapping is well-conditioned,
not that the material is visually correct. `above_nyquist_warning` means a supplied
procedural source octave exceeds the map's base-level sampling limit. It is a
sampling-risk estimate: it does not measure the generated image spectrum, account
for the complete filtering/mip/render path, or prove the cause of a rendered artifact.
Compare generated-map spectra and controlled runtime captures before changing
production material parameters or UVs.

The standard-library regression suite is registered as
`Talvisota.AssetPipeline.ObjMaterialDiagnostics` in CTest when Python 3 is available.
It covers parser/geometry boundaries and invokes the CLI against both shipped interior
models so their material totals, mapping quality, and source-frequency classifications
remain checked in the normal Debug/Release test path.


## Generated normal-map spectrum analysis

Use `normal_map_spectrum.py` to measure the sampled red/green tangent components
of generated normal maps. BMP input reads uncompressed 24-bit files without
third-party dependencies. Processed `.ttex` input reads the deterministic
`TVTTEX01` RGBA8 mip-chain container written by the pinned VantaEngine texture
processor. Both paths use a periodic 2D radix-2 FFT and report power fractions in
bands relative to the per-axis Nyquist limit. The square frequency shells use
`max(abs(kx), abs(ky))`, matching the square sampling region rather than incorrectly
treating representable diagonal frequencies as out of band.

Each channel and the combined XY result also contains independent `u_axis` and
`v_axis` projections. A U projection sums power over every V frequency at each
absolute U frequency, and vice versa. Its `total_ac_power` therefore remains equal
to the channel's full spatial variance. The `zero_frequency_fraction` records the
share of AC power whose frequency on that axis is zero; for example, a pattern that
varies only along V appears in the U projection's zero-frequency bin. Positive-axis
frequency bands and near-Nyquist fractions are normalized only over non-zero
frequencies on that axis. This preserves directional information that the square
shell summary intentionally combines.

Analyze the generated base-level maps and record the source-octave fold estimates:

```sh
python3 tools/asset_pipeline/normal_map_spectrum.py \
  assets/textures/environment/bark-normal.bmp \
  --source-frequency bark=840 --json

python3 tools/asset_pipeline/normal_map_spectrum.py \
  assets/textures/environment/granite-normal.bmp \
  --source-frequency granite=1536 --json
```

To measure the actual normal-map mip chain, first use the optional MSVC engine
adapter's texture processor. The `normal` mode decodes and renormalizes the source
vectors, averages unit vectors, and renormalizes every successive mip:

```powershell
.\build-msvc\talvisota_texture_process.exe assets\textures\environment\bark-normal.bmp artifacts\bark-normal.ttex normal
.\build-msvc\talvisota_texture_process.exe assets\textures\environment\granite-normal.bmp artifacts\granite-normal.ttex normal
python tools/asset_pipeline/normal_map_spectrum.py artifacts\bark-normal.ttex --source-frequency bark=840 --json-output artifacts\bark-normal-spectrum.json
python tools/asset_pipeline/normal_map_spectrum.py artifacts\granite-normal.ttex --source-frequency granite=1536 --json-output artifacts\granite-normal-spectrum.json
```

A `.ttex` report includes every mip level and recomputes the source-octave folding
estimate using that level's texel count. Each level also reports decoded XYZ normal
length mean/min/max, RMS deviation from unit length, and the count of samples outside
a 2% tolerance. Levels at least 16x16 receive a full frequency spectrum. Smaller
levels retain mean and AC-RMS statistics but are marked
`insufficient_frequency_resolution`; assigning detailed frequency bands to 8x8
and smaller levels would overstate their spectral resolution. The reader validates
the complete ceil-halved mip chain, RGBA8 format, linear colour-space metadata,
byte counts, terminal 1x1 level and absence of trailing data. It accepts dimensions
up to 2048 and containers up to 64 MiB to bound CPU and memory use. Non-square or
non-power-of-two base maps can be parsed for format validation but are rejected for
the current square FFT analysis.

Compare a candidate processed mip chain against the original at identical dimensions
and mip metadata. Use `--json-output` when the JSON needs to be retained as a CI
artifact; the normal human-readable summary remains on standard output:

```sh
python tools/asset_pipeline/normal_map_spectrum.py artifacts/candidate-normal.ttex --compare artifacts/baseline-normal.ttex --json-output artifacts/normal-map-spectrum-comparison.json
```

The comparison reports candidate-minus-baseline changes in the square-shell
near-Nyquist power, spectral centroid, AC RMS and frequency bands for every
resolvable mip. It also reports U- and V-axis projection deltas, including their
zero-frequency shares, directional near-Nyquist fractions, centroids and bands.
Normal-vector length statistics are compared at every level, including RMS deviation
from unit length and the count of samples outside the 2% tolerance. A
positive near-Nyquist delta means the candidate retains more power in the highest
quarter of the representable per-axis frequency range. Source-frequency folding
assumes an axis-aligned periodic component sampled once per texel; it is a diagnostic
estimate, not a reconstruction of the full multi-axis FBM field. The analysis still
does not model GPU texture filtering, sampler state, material binding, surface
orientation, rasterization or D3D12 sampling, and it cannot prove the cause of a
rendered artifact.

The standard-library suite is registered separately as
`Talvisota.AssetPipeline.NormalMapSpectrum`. When the normal build has generated
`bark-normal.bmp` and `granite-normal.bmp`, the suite analyzes both real 1024x1024
base-level maps; synthetic BMP and `.ttex` fixtures independently verify exact U/V
FFT bins, orientation-preserving projections, Parseval power conservation, per-mip
normalization, malformed-container rejection and comparison semantics.
## Deterministic packages

The package tool validates source definitions and media, stages every present manifest
asset, writes a normalized `content.tmanifest`, and creates `package.tmanifest` with
the catalog digest plus per-asset SHA-256, size, path, source, and licence records.
Before publishing, it reloads the staged catalog, validates dependency and typed
definitions, reimports/decodes packaged media, and checks exact catalog/inventory/file
correspondence. `--verify` performs the same semantic and integrity revalidation.

See [content-package-v1.md](../../data/schemas/content-package-v1.md) for the format
contract and [ASSET_RIGHTS_AUDIT.md](../../docs/production/ASSET_RIGHTS_AUDIT.md) for
the current shipped-media provenance and vendored-code licence audit.


## Reproducible processed environment normal-map reports

The spectrum analyzer can be run directly on an existing `.ttex` file, but the
following orchestration command is the canonical way to measure the exact normal
mip chains produced by the pinned engine texture processor. It invokes the processor
without a shell, checks its exit status and output, analyzes every mip, and writes
two deterministic JSON reports plus a SHA-256 provenance manifest:

```powershell
python tools/asset_pipeline/generate_environment_normal_map_reports.py `
  --texture-processor build-msvc/talvisota_texture_process.exe `
  --source-root assets/textures/environment `
  --output-directory artifacts/normal-map-spectrum
```

The command processes `bark-normal.bmp` (840 source cycles/UV) and
`granite-normal.bmp` (1,536 source cycles/UV) in the engine's `normal` mode.
Both source maps must be regular files, not file-level symbolic links; the generator
and verifier enforce the same input-path contract so a generated artifact is immediately
verifiable. The output directory contains `bark-normal-spectrum.json`,
`granite-normal-spectrum.json`, and
`environment-normal-map-spectrum-manifest.json`. The manifest records source,
processor, processed-container and report SHA-256 digests, dimensions, mip count,
and the source-frequency assumption for each report. It contains no timestamps or
absolute paths, so identical inputs and processor binaries produce identical output.

Reports are staged and validated before publication. A failed processor, missing or
malformed `.ttex` output, incomplete mip inventory, invalid normal-vector metrics,
or source-frequency mismatch returns a non-zero exit code and does not publish a
partial report set. The manifest is removed at the start of a run and written last;
its presence identifies a complete two-map report set. The processed `.ttex` files
are temporary and are not retained in the output directory.


## Verifying generated normal-map report artifacts

After downloading the three-file report artifact, verify it before comparing or
sharing the measurements:

```powershell
python tools/asset_pipeline/verify_environment_normal_map_reports.py artifacts/normal-map-spectrum/environment-normal-map-spectrum-manifest.json --source-root assets/textures/environment
```

The verifier checks the fixed bark/granite report inventory, safe filenames,
report and source SHA-256 values, analyzer provenance, every per-mip spectrum field,
normalized frequency-band distributions, U/V Parseval power conservation,
normal-vector ranges, source-frequency folding metadata, and consistency between
the manifest and each JSON report. Pass `--texture-processor` with the original
executable to verify its recorded binary hash as well.

The manifest is unsigned. These checks detect missing, corrupted, or internally
inconsistent files relative to that manifest; they do not authenticate who
created the artifact. Obtain the manifest through a trusted CI artifact channel.
