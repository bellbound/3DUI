#include "Config.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace Config {
    Options options;

    static std::string g_configPath;

    // ===== Default INI content =====
    static constexpr const char* DEFAULT_INI_CONTENT = R"(; 3DUI Configuration
; Delete this file to regenerate with defaults

[General]
; Log level: trace, debug, info, warn, error (default: debug)
; trace = most verbose, shows button presses and all internal state
; debug = detailed logging for troubleshooting
; info = normal operation messages
; warn = warnings only
; error = errors only
logLevel=debug

[Haptics]
; Globally disable all haptic feedback (0=enabled, 1=disabled)
globallyDisableHapticFeedback=0
)";

    // ===== Low-level INI readers using Windows API =====
    static std::string GetConfigOption(const char* section, const char* key) {
        const std::string& configPath = GetConfigPath();
        if (configPath.empty()) return "";

        char buffer[256];
        GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), configPath.c_str());
        return buffer;
    }

    static bool GetConfigOptionBool(const char* section, const char* key, bool* out) {
        std::string data = GetConfigOption(section, key);
        if (data.empty()) return false;
        try {
            int val = std::stoi(data);
            *out = (val != 0);
            return true;
        } catch (...) {
            spdlog::warn("Config: Failed to parse bool for {}/{}", section, key);
            return false;
        }
    }

    static spdlog::level::level_enum ParseLogLevel(const std::string& levelStr) {
        if (levelStr == "trace") return spdlog::level::trace;
        if (levelStr == "debug") return spdlog::level::debug;
        if (levelStr == "info") return spdlog::level::info;
        if (levelStr == "warn" || levelStr == "warning") return spdlog::level::warn;
        if (levelStr == "error" || levelStr == "err") return spdlog::level::err;
        if (levelStr == "critical") return spdlog::level::critical;
        if (levelStr == "off") return spdlog::level::off;
        return spdlog::level::debug;  // default
    }

    static const char* LogLevelToString(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace: return "trace";
            case spdlog::level::debug: return "debug";
            case spdlog::level::info: return "info";
            case spdlog::level::warn: return "warn";
            case spdlog::level::err: return "error";
            case spdlog::level::critical: return "critical";
            case spdlog::level::off: return "off";
            default: return "debug";
        }
    }

    // ===== Create default INI file =====
    static bool CreateDefaultConfigFile(const std::string& path) {
        spdlog::info("Config: Creating default config file at {}", path);

        // Ensure directory exists
        std::filesystem::path filePath(path);
        std::filesystem::path dirPath = filePath.parent_path();

        try {
            if (!std::filesystem::exists(dirPath)) {
                std::filesystem::create_directories(dirPath);
                spdlog::info("Config: Created directory {}", dirPath.string());
            }
        } catch (const std::exception& e) {
            spdlog::error("Config: Failed to create directory: {}", e.what());
            return false;
        }

        // Write default content
        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Config: Failed to create config file at {}", path);
            return false;
        }

        file << DEFAULT_INI_CONTENT;
        file.close();

        spdlog::info("Config: Default config file created successfully");
        return true;
    }

    bool ReadConfigOptions() {
        const std::string& path = GetConfigPath();

        // Check if file exists, create default if not
        if (!std::filesystem::exists(path)) {
            spdlog::info("Config: Config file not found, creating default");
            if (!CreateDefaultConfigFile(path)) {
                spdlog::error("Config: Failed to create default config, using built-in defaults");
            }
        }

        spdlog::info("Config: Reading config from {}", path);

        // Reset options to defaults before reading
        options = Options{};

        // Log level
        std::string logLevelStr = GetConfigOption("General", "logLevel");
        if (!logLevelStr.empty()) {
            options.logLevel = ParseLogLevel(logLevelStr);
            spdlog::info("Config: [General] logLevel = {}", LogLevelToString(options.logLevel));
        } else {
            spdlog::debug("Config: logLevel not found, using default {}", LogLevelToString(options.logLevel));
        }

        // Haptics
        if (!GetConfigOptionBool("Haptics", "globallyDisableHapticFeedback", &options.globallyDisableHapticFeedback)) {
            spdlog::debug("Config: globallyDisableHapticFeedback not found, using default {}",
                options.globallyDisableHapticFeedback ? "true" : "false");
        } else {
            spdlog::info("Config: [Haptics] globallyDisableHapticFeedback = {}",
                options.globallyDisableHapticFeedback ? "true" : "false");
        }

        spdlog::info("Config: Loaded successfully");
        return true;
    }

    const std::string& GetConfigPath() {
        if (g_configPath.empty()) {
            // Build path: <game>/Data/SKSE/Plugins/3DUI.ini
            wchar_t pathBuf[MAX_PATH];
            GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);

            std::filesystem::path gamePath(pathBuf);
            gamePath = gamePath.parent_path();  // Remove exe name
            gamePath /= "Data";
            gamePath /= "SKSE";
            gamePath /= "Plugins";
            gamePath /= "3DUI.ini";

            g_configPath = gamePath.string();
            spdlog::info("Config: Path set to {}", g_configPath);
        }
        return g_configPath;
    }
}
