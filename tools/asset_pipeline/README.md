# Asset pipeline tools

## Texture processing

Build the tool with the normal CMake build. Run:

```text
talvisota_texture_process <source.bmp|source.tga> <output.ttex> <srgb|linear>
```

The source decoder supports uncompressed 24-bit BGR and 32-bit BGRX BI_RGB BMP, plus true-colour TGA type 2 (raw) and type 10 (RLE), 24-bit BGR and 32-bit BGRA. TGA orientation flags are normalized. The decoder emits top-left-origin RGBA8; BMP's fourth BI_RGB byte is ignored while TGA alpha is preserved when declared.

Choose `srgb` for base-colour/emissive RGB and `linear` for normal/ORM/data maps. The processor creates a full mip chain. sRGB RGB is filtered in linear light; alpha is always linear. Output is a deterministic little-endian `.ttex` container with magic `TVTTEX01`, versioned header, explicit colour-space metadata, and tightly packed RGBA8 mip levels. No timestamps, absolute paths, or host-endian data are written. The writer stages output and replaces the destination only after a complete successful write.

The content validator decodes manifest-registered BMP and TGA files. Manifest-recognized PNG/JPEG/DDS/KTX2 remain unsupported until their decoders are implemented. Model import and runtime GPU upload are separate pipeline stages and are not implied by texture processing.
