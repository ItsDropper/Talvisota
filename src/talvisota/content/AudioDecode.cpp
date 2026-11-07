#include "talvisota/content/AudioDecode.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4456 4457)
#endif
#define DR_FLAC_IMPLEMENTATION
#include "../../../third_party/dr_flac.h"
#include "../../../third_party/stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace talvisota::content
{
namespace
{
constexpr std::uint64_t MaximumEncodedBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumDurationSeconds = 2ULL * 60ULL * 60ULL;
constexpr std::uint32_t MaximumChannels = 8U;
constexpr std::uint32_t MinimumSampleRate = 8000U;
constexpr std::uint32_t MaximumSampleRate = 384000U;
constexpr std::uint64_t DecodeChunkFrames = 4096ULL;

bool ValidStreamFormat(std::uint32_t sampleRate, std::uint32_t channels) noexcept
{
    return sampleRate >= MinimumSampleRate && sampleRate <= MaximumSampleRate &&
           channels >= 1U && channels <= MaximumChannels;
}

bool SamplesAreFinite(const float* samples, std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
        if (!std::isfinite(samples[index]))
            return false;
    return true;
}

bool DecodeFlac(const std::vector<std::uint8_t>& encoded, DecodedAudioInfo& output,
                std::string& error)
{
    drflac* decoder = drflac_open_memory(encoded.data(), encoded.size(), nullptr);
    if (decoder == nullptr)
    {
        error = "FLAC decoder rejected the source stream or its metadata.";
        return false;
    }

    const std::uint32_t sampleRate = decoder->sampleRate;
    const std::uint32_t channels = decoder->channels;
    if (!ValidStreamFormat(sampleRate, channels))
    {
        drflac_close(decoder);
        error = "FLAC stream has an unsupported sample rate or channel count.";
        return false;
    }

    std::array<float, static_cast<std::size_t>(DecodeChunkFrames * MaximumChannels)> samples{};
    std::uint64_t totalFrames = 0U;
    bool valid = true;
    for (;;)
    {
        const drflac_uint64 framesRead = drflac_read_pcm_frames_f32(
            decoder, DecodeChunkFrames, samples.data());
        if (framesRead == 0U)
            break;
        const std::uint64_t sampleCount = framesRead * channels;
        if (sampleCount > samples.size() ||
            !SamplesAreFinite(samples.data(), static_cast<std::size_t>(sampleCount)))
        {
            error = "FLAC decoder produced invalid or non-finite PCM samples.";
            valid = false;
            break;
        }
        if (totalFrames > sampleRate * MaximumDurationSeconds - framesRead)
        {
            error = "FLAC decoded duration exceeds the two-hour validation limit.";
            valid = false;
            break;
        }
        totalFrames += framesRead;
    }
    const std::uint64_t declaredFrames = decoder->totalPCMFrameCount;
    drflac_close(decoder);

    if (!valid)
        return false;
    if (totalFrames == 0U || (declaredFrames != 0U && totalFrames != declaredFrames))
    {
        error = "FLAC stream ended before its declared PCM frame count or contains no audio.";
        return false;
    }

    output = {sampleRate, channels, totalFrames};
    return true;
}

bool DecodeVorbis(const std::vector<std::uint8_t>& encoded, DecodedAudioInfo& output,
                  std::string& error)
{
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        error = "Ogg Vorbis source exceeds the decoder's addressable stream limit.";
        return false;
    }
    int decoderError = VORBIS__no_error;
    stb_vorbis* decoder = stb_vorbis_open_memory(encoded.data(),
        static_cast<int>(encoded.size()), &decoderError, nullptr);
    if (decoder == nullptr)
    {
        error = "Ogg Vorbis decoder rejected the source stream (error " +
                std::to_string(decoderError) + ").";
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(decoder);
    const std::uint32_t sampleRate = static_cast<std::uint32_t>(info.sample_rate);
    const std::uint32_t channels = static_cast<std::uint32_t>(info.channels);
    if (!ValidStreamFormat(sampleRate, channels))
    {
        stb_vorbis_close(decoder);
        error = "Ogg Vorbis stream has an unsupported sample rate or channel count.";
        return false;
    }

    std::array<float, static_cast<std::size_t>(DecodeChunkFrames * MaximumChannels)> samples{};
    std::uint64_t totalFrames = 0U;
    bool valid = true;
    for (;;)
    {
        const int framesRead = stb_vorbis_get_samples_float_interleaved(
            decoder, static_cast<int>(channels), samples.data(),
            static_cast<int>(DecodeChunkFrames * channels));
        if (framesRead == 0)
            break;
        if (framesRead < 0)
        {
            error = "Ogg Vorbis decoder returned an invalid frame count.";
            valid = false;
            break;
        }
        const std::uint64_t sampleCount = static_cast<std::uint64_t>(framesRead) * channels;
        if (sampleCount > samples.size() ||
            !SamplesAreFinite(samples.data(), static_cast<std::size_t>(sampleCount)))
        {
            error = "Ogg Vorbis decoder produced invalid or non-finite PCM samples.";
            valid = false;
            break;
        }
        if (totalFrames > sampleRate * MaximumDurationSeconds - static_cast<std::uint64_t>(framesRead))
        {
            error = "Ogg Vorbis decoded duration exceeds the two-hour validation limit.";
            valid = false;
            break;
        }
        totalFrames += static_cast<std::uint64_t>(framesRead);
    }
    const int finalError = stb_vorbis_get_error(decoder);
    stb_vorbis_close(decoder);
    if (!valid)
        return false;
    if (finalError != VORBIS__no_error)
    {
        error = "Ogg Vorbis stream ended with decoder error " + std::to_string(finalError) + ".";
        return false;
    }
    if (totalFrames == 0U)
    {
        error = "Ogg Vorbis stream contains no decoded audio frames.";
        return false;
    }

    output = {sampleRate, channels, totalFrames};
    return true;
}
} // namespace

bool DecodeCompressedAudio(const std::filesystem::path& path, DecodedAudioInfo& output,
                           std::string& error)
{
    error.clear();
    std::error_code fileError;
    const std::uint64_t size = std::filesystem::file_size(path, fileError);
    if (fileError || size == 0U || size > MaximumEncodedBytes)
    {
        error = "Compressed audio file is missing, empty, or exceeds the 256 MiB input limit.";
        return false;
    }

    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(encoded.data()),
                              static_cast<std::streamsize>(encoded.size())) ||
        input.peek() != std::char_traits<char>::eof())
    {
        error = "Could not read the complete compressed audio source.";
        return false;
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    DecodedAudioInfo candidate;
    const bool decoded = extension == ".flac"
        ? DecodeFlac(encoded, candidate, error)
        : extension == ".ogg"
            ? DecodeVorbis(encoded, candidate, error)
            : false;
    if (!decoded)
    {
        if (error.empty())
            error = "Compressed audio extension is not supported.";
        return false;
    }
    output = candidate;
    return true;
}
} // namespace talvisota::content
