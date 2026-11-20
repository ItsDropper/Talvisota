#pragma once
#include "talvisota/core/Config.hpp"
#include "talvisota/core/GameSession.hpp"
#include "talvisota/core/Logger.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace talvisota::app {
class Application final {
public:
    Application()=default;Application(const Application&)=delete;Application& operator=(const Application&)=delete;~Application();
    [[nodiscard]] int Run(const std::vector<std::string>& arguments);
    [[nodiscard]] bool Initialize(const std::filesystem::path& configFile,const std::filesystem::path& dataDirectory,std::string& error);
    void Shutdown() noexcept;
private:
    core::Logger logger_;core::ApplicationConfig config_;core::GameSession session_;bool initialized_=false;
};
}