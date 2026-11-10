#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace talvisota::test_support
{
class ContentManifestFixture final
{
public:
    ContentManifestFixture(const std::filesystem::path& sourceRoot, const std::string_view label)
        : sourceRoot_(sourceRoot)
    {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto timestamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto nonce = timestamp ^ sequence.fetch_add(1U, std::memory_order_relaxed);
        std::error_code filesystemError;
        const auto temporaryRoot = std::filesystem::temp_directory_path(filesystemError);
        if (filesystemError) {
            error_ = "Could not locate the temporary directory: " + filesystemError.message();
            return;
        }

        manifestDirectory_ = temporaryRoot /
            ("talvisota-content-manifest-" + Sanitize(label) + "-" + std::to_string(nonce));
        std::filesystem::create_directories(manifestDirectory_, filesystemError);
        if (filesystemError) {
            error_ = "Could not create the temporary content-manifest directory: " +
                filesystemError.message();
            return;
        }

        std::ifstream source(sourceRoot_ / "content/catalog.tmanifest", std::ios::binary);
        if (!source) {
            error_ = "Could not open the shipped content manifest.";
            return;
        }
        std::ofstream output(ManifestPath(), std::ios::binary | std::ios::trunc);
        if (!output) {
            error_ = "Could not create the temporary content manifest.";
            return;
        }

        std::string line;
        while (std::getline(source, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            auto fields = SplitTabs(line);
            if (fields.size() == 8U &&
                (fields[1] == "model.environment.scots_pine" ||
                    fields[1] == "model.environment.boulder")) {
                const bool isPine = fields[1] == "model.environment.scots_pine";
                const std::string fallbackModel = isPine
                    ? "assets/models/environment/pine.obj"
                    : "assets/models/environment/boulder.obj";
                const std::string fallbackMaterial = isPine
                    ? "assets/materials/environment/forest.mtl"
                    : "assets/materials/environment/boulder.mtl";

                std::error_code assetError;
                bool generatedModelPresent =
                    std::filesystem::is_regular_file(sourceRoot_ / fields[3], assetError);
                if (assetError == std::errc::no_such_file_or_directory ||
                    assetError == std::errc::not_a_directory) {
                    assetError.clear();
                    generatedModelPresent = false;
                }
                if (assetError) {
                    error_ = "Could not inspect the generated model for headless tests: " +
                        assetError.message();
                    return;
                }

                if (!generatedModelPresent) {
                    if (!std::filesystem::is_regular_file(sourceRoot_ / fallbackModel, assetError) ||
                        assetError) {
                        error_ = "Tracked fallback model is unavailable: " + fallbackModel;
                        return;
                    }
                    if (!std::filesystem::is_regular_file(sourceRoot_ / fallbackMaterial, assetError) ||
                        assetError) {
                        error_ = "Tracked fallback material library is unavailable: " + fallbackMaterial;
                        return;
                    }

                    // Keep the original directory depth so the OBJ's relative mtllib
                    // and texture paths continue to resolve without copying assets.
                    fields[3] = fallbackModel;
                }
            }

            for (std::size_t index = 0U; index < fields.size(); ++index) {
                if (index != 0U)
                    output.put('\t');
                output << fields[index];
            }
            output.put('\n');
        }

        if (!source.eof() && source.fail()) {
            error_ = "An I/O error occurred while preparing the headless test manifest.";
            return;
        }
        output.flush();
        if (!output) {
            error_ = "Could not finish writing the headless test manifest.";
            return;
        }
        output.close();
        if (!output) {
            error_ = "Could not close the headless test manifest cleanly.";
            return;
        }
        ready_ = true;
    }

    ~ContentManifestFixture()
    {
        std::error_code ignored;
        if (!manifestDirectory_.empty())
            std::filesystem::remove_all(manifestDirectory_, ignored);
    }

    ContentManifestFixture(const ContentManifestFixture&) = delete;
    ContentManifestFixture& operator=(const ContentManifestFixture&) = delete;

    [[nodiscard]] bool Ready() const noexcept
    {
        return ready_;
    }

    [[nodiscard]] std::filesystem::path ManifestPath() const
    {
        return manifestDirectory_ / "catalog.tmanifest";
    }

    [[nodiscard]] const std::string& Error() const noexcept
    {
        return error_;
    }

private:
    static std::string Sanitize(const std::string_view label)
    {
        std::string result;
        result.reserve(label.size());
        for (const unsigned char character : label) {
            const bool alphanumeric =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
            result.push_back(alphanumeric ? static_cast<char>(character) : '-');
        }
        return result.empty() ? "test" : result;
    }

    static std::vector<std::string> SplitTabs(const std::string& line)
    {
        std::vector<std::string> fields;
        std::size_t begin = 0U;
        for (;;) {
            const std::size_t separator = line.find('\t', begin);
            if (separator == std::string::npos) {
                fields.push_back(line.substr(begin));
                break;
            }
            fields.push_back(line.substr(begin, separator - begin));
            begin = separator + 1U;
        }
        return fields;
    }

    std::filesystem::path sourceRoot_;
    std::filesystem::path manifestDirectory_;
    std::string error_;
    bool ready_ = false;
};
} // namespace talvisota::test_support
