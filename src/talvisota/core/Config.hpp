#pragma once
#include "talvisota/core/Logger.hpp"
#include <filesystem>
#include <string>
namespace talvisota::core {
struct ApplicationConfig final {
    static constexpr int CurrentVersion=1;
    LogLevel minimumLogLevel=LogLevel::Info;
    bool consoleLogging=true;
};
struct ConfigLoadResult final {
    ApplicationConfig config;
    std::string error;
    [[nodiscard]] bool Succeeded() const noexcept{return error.empty();}
};
[[nodiscard]] ConfigLoadResult LoadOrCreateConfig(const std::filesystem::path& path);
}