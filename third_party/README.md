# Vendored audio decoders

- `dr_flac.h` v0.13.4 from [dr_libs](https://github.com/mackron/dr_libs), distributed under the public-domain or MIT-0 terms stated in the source file.
- `stb_vorbis.c` v1.22 from [stb](https://github.com/nothings/stb), public domain (license statement is included at the end of the source file).

These decoders are used only by the offline content validator to decode Ogg Vorbis and FLAC source assets. The original compressed source files are preserved unchanged in content packages.
