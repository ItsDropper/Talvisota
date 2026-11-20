#include "talvisota/core/Config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
namespace {int failures=0;void Check(bool ok,std::string_view message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/"talvisota-config-tests";std::error_code ec;
    std::filesystem::remove_all(dir,ec);const auto path=dir/"config.ini";
    auto result=talvisota::core::LoadOrCreateConfig(path);
    Check(result.Succeeded(),"missing config creates defaults");Check(std::filesystem::exists(path),"default config is written");
    {std::ofstream out(path,std::ios::trunc);out<<"config_version=1\nlog_level=debug\nconsole_logging=false\n";}
    result=talvisota::core::LoadOrCreateConfig(path);Check(result.Succeeded(),"valid config loads");
    Check(result.config.minimumLogLevel==talvisota::core::LogLevel::Debug,"log level applied");
    Check(!result.config.consoleLogging,"console logging disabled");
    {std::ofstream out(path,std::ios::trunc);out<<"config_version=99\n";}
    Check(!talvisota::core::LoadOrCreateConfig(path).Succeeded(),"unsupported version rejected");
    {std::ofstream out(path,std::ios::trunc);out<<"config_version=1\nunknown_option=true\n";}
    Check(!talvisota::core::LoadOrCreateConfig(path).Succeeded(),"unknown keys rejected");
    std::filesystem::remove_all(dir,ec);if(ec)Check(false,"temporary config directory removed");
    if(failures){std::cerr<<failures<<" assertion(s) failed\n";return EXIT_FAILURE;}
    std::cout<<"All Talvisota configuration tests passed\n";return EXIT_SUCCESS;
}