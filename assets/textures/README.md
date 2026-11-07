# Texture assets

Document intended colour space and channel packing for every map. Base colour is not interchangeable with linear data maps.

The first implemented source decoder accepts uncompressed 24-bit BGR and 32-bit BGRX Windows BMP files and emits top-left-origin RGBA8 without colour conversion. Treat RGB as sRGB-encoded source bytes; 32-bit BI_RGB alpha is ignored and output is opaque. This decoder does not yet process PNG, JPEG, TGA, DDS, or KTX2, generate mipmaps, or write a runtime texture container. Keep source files unchanged and retain their manifest provenance.
