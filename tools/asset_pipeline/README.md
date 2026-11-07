# Asset pipeline tools

Import, validate, convert, and package source media. Preserve provenance and never overwrite source assets during processing.

## Current importer support

The content library includes a portable Windows BMP decoder for uncompressed 24-bit BGR and 32-bit BGRX (BI_RGB) images with BITMAPINFOHEADER-compatible DIB headers. It normalizes both bottom-up and top-down images into tightly packed, top-left-origin RGBA8 pixels. The source RGB bytes are preserved as sRGB-encoded values; no implicit colour conversion or alpha interpretation is performed. The reserved fourth byte of 32-bit BI_RGB pixels is ignored and output alpha is opaque.

The decoder rejects unsupported compression/bit depths, inconsistent offsets and declared sizes, truncated rows, invalid dimensions, and images exceeding its 512 MiB decoded-pixel safety limit. File loading is bounded to 1 GiB. It is exposed through TextureImporter.hpp and covered by headless regression tests.

The content validator now decodes every manifest-registered .bmp texture during its normal validation pass and reports the number decoded. This is a first real media-decoding capability, not a complete texture pipeline. PNG, JPEG, TGA, DDS, and KTX2 are still manifest-recognized source extensions but do not yet have decoders here. Deterministic processed-file serialization, mip generation, GPU upload, and model import remain unimplemented. Do not treat manifest validation as successful media import.
