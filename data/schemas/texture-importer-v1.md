# Texture importer v1 capability note

This document records the implemented source-decoding contract. It is not a claim that every texture extension accepted by the content manifest can be imported.

## Implemented format subset

- Container: Windows BMP file with a DIB header of at least 40 bytes (BITMAPINFOHEADER-compatible).
- Planes: exactly one.
- Pixel formats: 24-bit BGR and 32-bit BGRX.
- Compression: BI_RGB (value 0), uncompressed.
- Orientation: positive height (bottom-up) and negative height (top-down).
- Output: tightly packed, top-left-origin RGBA8.
- Colour: RGB bytes are preserved as encoded; no sRGB-to-linear conversion occurs.
- Alpha: BI_RGB's fourth byte is reserved and is ignored; output alpha is 255.

## Validation and resource limits

The decoder checks the BM signature, DIB bounds, pixel-data offset, signed dimensions, plane count, bit depth, compression, row padding, pixel-array bounds, and non-zero declared file/image sizes. It rejects dimensions whose decoded RGBA8 output would exceed 512 MiB. LoadBmp rejects source files larger than 1 GiB. Failed decoding leaves the caller's output image unchanged and returns a path/field diagnostic.

## Not implemented

This capability does not decode PNG, JPEG, TGA, DDS, or KTX2, does not generate mipmaps, does not serialize a processed runtime texture format, and does not upload GPU resources. The content manifest may recognize additional extensions for future importers; that recognition is not decoder support.
