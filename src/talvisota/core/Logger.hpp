#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
namespace talvisota::core {
enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical };
[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;
class Logger final {
public:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger();
    [[nodiscard]] bool Initialize(const std::filesystem::path& file, bool console,
                                  LogLevel minimum, std::string& error);
    void Write(LogLevel level, std::string_view subsystem, std::string_view message) noexcept;
    void Flush() noexcept;
private:
    std::mutex mutex_;
    std::ofstream file_;
    bool console_ = true;
    LogLevel minimum_ = LogLevel::Info;
};
}