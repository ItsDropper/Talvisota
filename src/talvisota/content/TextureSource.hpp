#pragma once

#include "talvisota/content/TextureImporter.hpp"

namespace talvisota::content
{
// Dispatches only to implemented decoders. An extension being accepted by the
// content manifest does not imply that a decoder exists for it.
[[nodiscard]] bool LoadTextureSource(const std::filesystem::path& path,
                                     DecodedTexture& output,
                                     std::vector<TextureDiagnostic>& diagnostics);
} // namespace talvisota::content
