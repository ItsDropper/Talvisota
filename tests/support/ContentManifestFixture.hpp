#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
namespace talvisota::test_support {
class ContentManifestFixture final {
public:
    ContentManifestFixture(const std::filesystem::path& sourceRoot,std::string_view label):sourceRoot_(sourceRoot) {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto nonce=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())^sequence.fetch_add(1U,std::memory_order_relaxed);
        const auto token=std::to_string(nonce);std::error_code ec;
        const auto temp=std::filesystem::temp_directory_path(ec);
        if(ec){error_="Could not locate temporary directory: "+ec.message();return;}
        manifestDirectory_=temp/("talvisota-content-manifest-"+Sanitize(label)+"-"+token);
        fallbackDirectory_=sourceRoot_/"build"/"test-content-fixtures"/token;
        std::filesystem::create_directories(manifestDirectory_,ec);
        if(ec){error_="Could not create temporary manifest directory: "+ec.message();return;}
        std::filesystem::create_directories(fallbackDirectory_,ec);
        if(ec){error_="Could not create test fallback directory: "+ec.message();return;}
        std::ifstream source(sourceRoot_/"content/catalog.tmanifest",std::ios::binary);
        if(!source){error_="Could not open shipped content manifest.";return;}
        std::ofstream output(ManifestPath(),std::ios::binary|std::ios::trunc);
        if(!output){error_="Could not create temporary content manifest.";return;}
        std::string line;
        while(std::getline(source,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            auto fields=SplitTabs(line);
            if(fields.size()==8U&&(fields[1]=="model.environment.scots_pine"||fields[1]=="model.environment.boulder")){
                const std::string fallbackSource=fields[1]=="model.environment.scots_pine"?"assets/models/environment/pine.obj":"assets/models/environment/boulder.obj";
                std::error_code assetError;
                bool present=std::filesystem::is_regular_file(sourceRoot_/fields[3],assetError);
                if(assetError==std::errc::no_such_file_or_directory||assetError==std::errc::not_a_directory){assetError.clear();present=false;}
                if(assetError){error_="Could not inspect generated model: "+assetError.message();return;}
                if(!present){
                    const auto sourceFallback=sourceRoot_/fallbackSource;
                    if(!std::filesystem::is_regular_file(sourceFallback,assetError)||assetError){error_="Tracked fallback model missing: "+sourceFallback.string();return;}
                    const auto name=fields[1]=="model.environment.scots_pine"?"scots_pine_test_fallback.obj":"boulder_test_fallback.obj";
                    const auto copy=fallbackDirectory_/name;
                    std::filesystem::copy_file(sourceFallback,copy,std::filesystem::copy_options::overwrite_existing,assetError);
                    if(assetError){error_="Could not copy tracked fallback: "+assetError.message();return;}
                    fields[3]=copy.lexically_relative(sourceRoot_).generic_string();
                }
            }
            for(std::size_t i=0U;i<fields.size();++i){if(i)output.put('\t');output<<fields[i];}
            output.put('\n');
        }
        if(!source.eof()&&source.fail()){error_="I/O error reading shipped content manifest.";return;}
        output.flush();if(!output){error_="Could not write temporary content manifest.";return;}
        output.close();if(!output){error_="Could not close temporary content manifest.";return;}
        ready_=true;
    }
    ~ContentManifestFixture(){std::error_code ignored;if(!manifestDirectory_.empty())std::filesystem::remove_all(manifestDirectory_,ignored);ignored.clear();if(!fallbackDirectory_.empty())std::filesystem::remove_all(fallbackDirectory_,ignored);}
    ContentManifestFixture(const ContentManifestFixture&)=delete;
    ContentManifestFixture& operator=(const ContentManifestFixture&)=delete;
    [[nodiscard]] bool Ready()const noexcept{return ready_;}
    [[nodiscard]] std::filesystem::path ManifestPath()const{return manifestDirectory_/"catalog.tmanifest";}
    [[nodiscard]] const std::string& Error()const noexcept{return error_;}
private:
    static std::string Sanitize(std::string_view label){std::string out;out.reserve(label.size());for(const unsigned char ch:label)out.push_back(((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9'))?static_cast<char>(ch):'-');return out.empty()?"test":out;}
    static std::vector<std::string> SplitTabs(const std::string& line){std::vector<std::string> out;std::size_t begin=0U;for(;;){const auto sep=line.find('\t',begin);if(sep==std::string::npos){out.push_back(line.substr(begin));break;}out.push_back(line.substr(begin,sep-begin));begin=sep+1U;}return out;}
    std::filesystem::path sourceRoot_,manifestDirectory_,fallbackDirectory_;
    std::string error_;bool ready_=false;
};
} // namespace talvisota::test_support
