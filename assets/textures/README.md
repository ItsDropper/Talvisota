# Texture assets

Document intended colour space and channel packing for every map. Base colour is not interchangeable with linear data maps.

Implemented source decoding:
- BMP: uncompressed 24-bit BGR and 32-bit BGRX BI_RGB; top-down and bottom-up; fourth byte ignored and output opaque.
- TGA: true-colour type 2 raw and type 10 packet-RLE; 24-bit BGR and 32-bit BGRA; all four origin combinations; declared alpha preserved.

Both decode to top-left-origin RGBA8. The offline `talvisota_texture_process` tool generates complete box-filtered mip chains and deterministic `.ttex` files. Select sRGB for base-colour/emissive RGB and linear for normal/ORM/data maps. sRGB RGB channels are filtered in linear light; alpha is linear.

PNG, JPEG, DDS and KTX2 decoding, compressed GPU texture formats, and renderer upload are not implemented. Keep source files unchanged and retain manifest provenance.
