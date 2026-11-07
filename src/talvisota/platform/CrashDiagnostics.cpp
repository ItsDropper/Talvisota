#include "talvisota/platform/CrashDiagnostics.hpp"
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
namespace talvisota::platform {
namespace {
std::filesystem::path reportPath;std::mutex reportMutex;
[[noreturn]] void HandleTerminate() noexcept {
    try {
        std::scoped_lock lock(reportMutex);std::ofstream out(reportPath,std::ios::app);
        if(out) {
            out<<"Talvisota terminated unexpectedly.\n";
            if(auto exception=std::current_exception()) {
                try {std::rethrow_exception(exception);}
                catch(const std::exception& e){out<<"Unhandled exception: "<<e.what()<<'\n';}
                catch(...){out<<"Unhandled non-standard exception.\n";}
            } else out<<"Termination requested without an active exception.\n";
            out.flush();
        }
    } catch(...) {}
    std::abort();
}
}
bool InstallCrashDiagnostics(const std::filesystem::path& path,std::string& error) {
    std::error_code ec;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),ec);
    if(ec){error="Cannot create crash-report directory: "+ec.message();return false;}
    reportPath=path;std::set_terminate(HandleTerminate);error.clear();return true;
}
}