#include "talvisota/app/Application.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
namespace {int failures=0;void Check(bool ok,std::string_view message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}}
int main() {
    const auto root=std::filesystem::temp_directory_path()/"talvisota-application-tests";std::error_code ec;
    std::filesystem::remove_all(root,ec);
    for(int cycle=0;cycle<3;++cycle) {
        talvisota::app::Application app;std::string error;const auto base=root/("cycle-"+std::to_string(cycle));
        Check(app.Initialize(base/"config"/"custom.ini",base/"data",error),"application initializes in repeated cycle");
        Check(error.empty(),"successful initialization clears error");app.Shutdown();
    }
    Check(std::filesystem::exists(root/"cycle-0"/"config"/"custom.ini"),"custom config file created");
    Check(std::filesystem::exists(root/"cycle-0"/"data"/"logs"/"talvisota.log"),"log file created");
    Check(!std::filesystem::exists(root/"cycle-0"/"data"/"crash-report.txt"),"normal startup creates no crash report");
    const auto blocker=root/"not-a-directory";{std::ofstream out(blocker);out<<"blocks directory creation";}
    {talvisota::app::Application app;std::string error;
     Check(!app.Initialize(blocker/"config.ini",root/"data-2",error),"directory creation failure reported");
     Check(!error.empty(),"failure has diagnostic");app.Shutdown();}
    std::filesystem::remove_all(root,ec);if(ec)Check(false,"temporary app directory removed");
    if(failures){std::cerr<<failures<<" assertion(s) failed\n";return EXIT_FAILURE;}
    std::cout<<"All Talvisota application tests passed\n";return EXIT_SUCCESS;
}