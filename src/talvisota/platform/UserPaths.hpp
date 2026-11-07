#pragma once
#include <filesystem>
#include <string>
namespace talvisota::platform {
struct UserPaths final {std::filesystem::path configurationDirectory;std::filesystem::path dataDirectory;};
[[nodiscard]] bool ResolveUserPaths(UserPaths& paths,std::string& error);
}