#pragma once

#include <string>
#include <spdlog/spdlog.h>

namespace Config {
    struct Options {
        // ===== Logging =====
        spdlog::level::level_enum logLevel = spdlog::level::debug;  // Log verbosity (trace, debug, info, warn, error)

        // ===== Haptics =====
        bool globallyDisableHapticFeedback = false;  // Disable all haptic feedback
    };

    extern Options options;

    // Read all config from INI file (creates default if not found)
    bool ReadConfigOptions();

    // Get path to INI file
    const std::string& GetConfigPath();
}
