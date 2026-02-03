#pragma once

#if !defined(TEST_ENVIRONMENT)
#include <spdlog/sinks/basic_file_sink.h>
#include "Config.h"

inline void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));

    // Start with trace level so config loading can log, then apply configured level
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);

    // Load config and apply configured log level
    Config::ReadConfigOptions();
    spdlog::set_level(Config::options.logLevel);
    spdlog::info("Log level set to: {}", spdlog::level::to_string_view(Config::options.logLevel));
}
#else
// Test environment - logging is stubbed in TestStubs.h
inline void SetupLog() {}
#endif
