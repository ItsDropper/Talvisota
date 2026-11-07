#include "talvisota/core/Config.hpp"
#include <fstream>
#include <string>
#include <system_error>

namespace talvisota::core {
namespace {

bool ParseLevel(const std::string& value, LogLevel& out) {
    if (value == "trace") {
        out = LogLevel::Trace;
    } else if (value == "debug") {
        out = LogLevel::Debug;
    } else if (value == "info") {
        out = LogLevel::Info;
    } else if (value == "warning") {
        out = LogLevel::Warning;
    } else if (value == "error") {
        out = LogLevel::Error;
    } else if (value == "critical") {
        out = LogLevel::Critical;
    } else {
        return false;
    }

    return true;
}

bool WriteDefaults(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (ec) {
        error = "Cannot create configuration directory: " + ec.message();
        return false;
    }

    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            error = "Cannot create configuration file '" + tmp + "'";
            return false;
        }
        out << "# Talvisota configuration\nconfig_version=1\nlog_level=info\nconsole_logging=true\n";
        out.flush();
        if (!out) {
            error = "Failed writing configuration file '" + tmp + "'";
            return false;
        }
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        error = "Cannot publish configuration file: " + ec.message();
        return false;
    }

    error.clear();
    return true;
}

} // namespace

ConfigLoadResult LoadOrCreateConfig(const std::filesystem::path& path) {
    ConfigLoadResult result;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        result.error = "Cannot inspect configuration file: " + ec.message();
        return result;
    }
    if (!exists) {
        static_cast<void>(WriteDefaults(path, result.error));
        return result;
    }

    std::ifstream in(path);
    if (!in) {
        result.error = "Cannot open configuration file '" + path.string() + "'";
        return result;
    }

    bool version = false;
    bool level = false;
    bool console = false;
    std::string line;
    std::size_t n = 0;
    while (std::getline(in, line)) {
        ++n;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        const auto eq = line.find('=', first);
        if (eq == std::string::npos || eq == first) {
            result.error = "Invalid configuration syntax at line " + std::to_string(n);
            return result;
        }

        const auto keyEnd = line.find_last_not_of(" \t", eq - 1);
        const auto valueStart = line.find_first_not_of(" \t", eq + 1);
        const auto valueEnd = line.find_last_not_of(" \t\r");
        if (keyEnd == std::string::npos || keyEnd < first) {
            result.error = "Empty key at line " + std::to_string(n);
            return result;
        }

        const auto key = line.substr(first, keyEnd - first + 1);
        const auto value = valueStart == std::string::npos
            ? std::string{}
            : line.substr(valueStart, valueEnd - valueStart + 1);

        if (key == "config_version") {
            if (version || value != "1") {
                result.error = "Unsupported or duplicate config_version at line " + std::to_string(n);
                return result;
            }
            version = true;
        } else if (key == "log_level") {
            if (level || !ParseLevel(value, result.config.minimumLogLevel)) {
                result.error = "Invalid or duplicate log_level at line " + std::to_string(n);
                return result;
            }
            level = true;
        } else if (key == "console_logging") {
            if (console || (value != "true" && value != "false")) {
                result.error = "Invalid or duplicate console_logging at line " + std::to_string(n);
                return result;
            }
            result.config.consoleLogging = value == "true";
            console = true;
        } else {
            result.error = "Unknown configuration key '" + key + "' at line " + std::to_string(n);
            return result;
        }
    }

    if (!in.eof()) {
        result.error = "Failed reading configuration file '" + path.string() + "'";
    } else if (!version) {
        result.error = "Configuration file is missing config_version";
    }

    return result;
}

} // namespace talvisota::core
