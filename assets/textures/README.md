# Texture assets

Talvisota records source textures and their colour-space/channel semantics; generic image decoding and CPU texture processing belong to VantaEngine.

The optional Windows/MSVC `talvisota_asset_validate` command decodes registered texture assets through the engine's Windows Imaging Component (WIC) loader. The supported manifest source extensions for this path are BMP, PNG, JPEG, and TIFF variants. Actual codec availability is provided by Windows WIC.

The optional `talvisota_texture_process` command writes deterministic `.ttex` mip-chain containers. Choose sRGB for base-colour/emissive RGB and linear for normal/ORM/data maps. sRGB RGB is filtered in linear light; alpha is always linear. ORM channel packing is R=occlusion, G=roughness, B=metallic.

Keep source files unchanged, retain manifest provenance, and write processed outputs separately. GPU texture upload remains engine-renderer work and is not performed by the offline processor.
