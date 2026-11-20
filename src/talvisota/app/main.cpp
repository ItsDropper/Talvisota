#include "talvisota/app/Application.hpp"
#include <exception>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char* argv[]) {
    try {
        std::vector<std::string> args;args.reserve(static_cast<std::size_t>(argc>1?argc-1:0));
        for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
        talvisota::app::Application application;return application.Run(args);
    } catch(const std::exception& error){std::cerr<<"Fatal application error: "<<error.what()<<'\n';return 1;}
      catch(...){std::cerr<<"Fatal application error: unknown exception\n";return 1;}
}