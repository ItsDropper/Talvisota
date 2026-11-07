#pragma once
#include <filesystem>
#include <string>
namespace talvisota::platform {
[[nodiscard]] bool InstallCrashDiagnostics(const std::filesystem::path& reportPath,std::string& error);
}