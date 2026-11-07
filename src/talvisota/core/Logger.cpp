#include "talvisota/core/Logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <system_error>
namespace talvisota::core {
namespace {
std::string Timestamp() {
    const auto now=std::chrono::system_clock::now();
    const auto time=std::chrono::system_clock::to_time_t(now);
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000;
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc,&time);
#else
    gmtime_r(&time,&utc);
#endif
    std::ostringstream out;
    out<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<ms<<'Z';
    return out.str();
}
}
std::string_view ToString(LogLevel level) noexcept {
    switch(level) {
    case LogLevel::Trace:return "TRACE"; case LogLevel::Debug:return "DEBUG";
    case LogLevel::Info:return "INFO"; case LogLevel::Warning:return "WARN";
    case LogLevel::Error:return "ERROR"; case LogLevel::Critical:return "CRITICAL";
    } return "UNKNOWN";
}
Logger::~Logger(){ Flush(); }
bool Logger::Initialize(const std::filesystem::path& path,bool console,LogLevel minimum,std::string& error) {
    std::error_code ec;
    if(!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(),ec);
        if(ec){error="Cannot create log directory: "+ec.message();return false;}
    }
    std::ofstream output(path,std::ios::app);
    if(!output){error="Cannot open log file '"+path.string()+"'";return false;}
    std::scoped_lock lock(mutex_);
    file_=std::move(output); console_=console; minimum_=minimum; error.clear(); return true;
}
void Logger::Write(LogLevel level,std::string_view subsystem,std::string_view message) noexcept {
    if(static_cast<int>(level)<static_cast<int>(minimum_)) return;
    try {
        const std::string line=Timestamp()+" ["+std::string(ToString(level))+"] ["+
            std::string(subsystem)+"] "+std::string(message);
        std::scoped_lock lock(mutex_);
        if(file_.is_open()){file_<<line<<'\n';file_.flush();}
        if(console_){auto& out=level>=LogLevel::Warning?std::cerr:std::cout;out<<line<<'\n';}
    } catch(...) {}
}
void Logger::Flush() noexcept {
    try { std::scoped_lock lock(mutex_); if(file_.is_open()) file_.flush(); } catch(...) {}
}
}