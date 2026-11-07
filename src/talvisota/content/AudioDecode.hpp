#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace talvisota::content
{
struct DecodedAudioInfo final
{
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frameCount = 0;
};

// Fully decodes Ogg Vorbis or FLAC source audio in bounded chunks. The source
// remains unchanged; only format and decoded stream integrity are reported.
[[nodiscard]] bool DecodeCompressedAudio(const std::filesystem::path& path,
                                         DecodedAudioInfo& output,
                                         std::string& error);
} // namespace talvisota::content
