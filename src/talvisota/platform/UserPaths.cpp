#include "talvisota/platform/UserPaths.hpp"
#include <cstdlib>
#include <memory>
namespace talvisota::platform {
bool ResolveUserPaths(UserPaths& p,std::string& error) {
#if defined(_WIN32)
    // MSVC 14.4x+ deprecates getenv under /W4; _dupenv_s is the supported replacement.
    char* rootValue=nullptr;std::size_t rootValueLength=0U;
    if(_dupenv_s(&rootValue,&rootValueLength,"LOCALAPPDATA")!=0)rootValue=nullptr;
    const std::unique_ptr<char,void(*)(void*)> rootOwner(rootValue,&std::free);
    const char* root=rootValue;
    if(!root||!*root){error="LOCALAPPDATA is not defined";return false;}
    p.configurationDirectory=std::filesystem::path(root)/"Talvisota"/"config";
    p.dataDirectory=std::filesystem::path(root)/"Talvisota"/"data";
#elif defined(__linux__)
    const char* config=std::getenv("XDG_CONFIG_HOME");const char* state=std::getenv("XDG_STATE_HOME");const char* home=std::getenv("HOME");
    if(config&&*config)p.configurationDirectory=std::filesystem::path(config)/"talvisota";
    else if(home&&*home)p.configurationDirectory=std::filesystem::path(home)/".config"/"talvisota";
    else {error="Neither XDG_CONFIG_HOME nor HOME is defined";return false;}
    if(state&&*state)p.dataDirectory=std::filesystem::path(state)/"talvisota";
    else if(home&&*home)p.dataDirectory=std::filesystem::path(home)/".local"/"state"/"talvisota";
    else {error="Neither XDG_STATE_HOME nor HOME is defined";return false;}
#elif defined(__APPLE__)
    const char* home=std::getenv("HOME");if(!home||!*home){error="HOME is not defined";return false;}
    p.configurationDirectory=std::filesystem::path(home)/"Library"/"Application Support"/"Talvisota"/"config";
    p.dataDirectory=std::filesystem::path(home)/"Library"/"Logs"/"Talvisota";
#else
    error="No user-directory implementation exists for this platform";return false;
#endif
    error.clear();return true;
}
}