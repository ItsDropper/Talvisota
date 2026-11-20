#include "talvisota/app/Application.hpp"
#include "talvisota/platform/CrashDiagnostics.hpp"
#include "talvisota/platform/UserPaths.hpp"
#include <algorithm>
#include <iostream>
namespace talvisota::app {
Application::~Application(){Shutdown();}
bool Application::Initialize(const std::filesystem::path& configFile,const std::filesystem::path& dataDirectory,std::string& error) {
    if(initialized_){error="Application is already initialized";return false;}
    if(!session_.BeginInitialization()){error="Application session rejected initialization";return false;}
    if(!platform::InstallCrashDiagnostics(dataDirectory/"crash-report.txt",error)){static_cast<void>(session_.Fail(error));return false;}
    const auto loaded=core::LoadOrCreateConfig(configFile);
    if(!loaded.Succeeded()){error=loaded.error;static_cast<void>(session_.Fail(error));return false;}
    config_=loaded.config;
    if(!logger_.Initialize(dataDirectory/"logs"/"talvisota.log",config_.consoleLogging,config_.minimumLogLevel,error)){static_cast<void>(session_.Fail(error));return false;}
    logger_.Write(core::LogLevel::Info,"Application","Talvisota startup");
    logger_.Write(core::LogLevel::Debug,"Application","Configuration loaded from "+configFile.string());
    if(!session_.FinishInitialization()){error="Application session rejected initialization completion";logger_.Write(core::LogLevel::Critical,"Application",error);static_cast<void>(session_.Fail(error));return false;}
    initialized_=true;error.clear();return true;
}
int Application::Run(const std::vector<std::string>& args) {
    if(std::find(args.begin(),args.end(),"--help")!=args.end()){std::cout<<"Talvisota application shell\nUsage: talvisota [--config <file>] [--help]\n";return 0;}
    std::filesystem::path config;for(std::size_t i=0;i<args.size();++i) {
        if(args[i]=="--config"){if(i+1>=args.size()||args[i+1].empty()){std::cerr<<"Argument error: --config requires a file path\n";return 2;}config=args[++i];}
        else {std::cerr<<"Argument error: unknown argument '"<<args[i]<<"'\n";return 2;}
    }
    platform::UserPaths paths;std::string error;
    if(!platform::ResolveUserPaths(paths,error)){std::cerr<<"Platform initialization failed: "<<error<<'\n';return 1;}
    if(config.empty())config=paths.configurationDirectory/"config.ini";
    if(!Initialize(config,paths.dataDirectory,error)){std::cerr<<"Application initialization failed: "<<error<<'\n';Shutdown();return 1;}
    logger_.Write(core::LogLevel::Info,"Application","Core services initialized; interactive runtime is deferred until engine integration");
    Shutdown();return 0;
}
void Application::Shutdown() noexcept {
    if(session_.Phase()==core::GamePhase::Cold||session_.Phase()==core::GamePhase::Stopped)return;
    if(session_.Phase()!=core::GamePhase::ShuttingDown) {
        if(!session_.BeginShutdown()&&session_.Phase()!=core::GamePhase::Failed)return;
    }
    logger_.Write(core::LogLevel::Info,"Application","Talvisota shutdown");logger_.Flush();
    if(session_.Phase()==core::GamePhase::ShuttingDown)static_cast<void>(session_.FinishShutdown());
    initialized_=false;
}
}