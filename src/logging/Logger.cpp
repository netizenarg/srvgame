#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"

// Static member initialization (no longer inline in header)
std::shared_ptr<spdlog::logger> Logger::logger_;
std::string Logger::configPath_;

void Logger::SetupLogger(const std::string& loggerName) {
    auto& config = ConfigManager::GetInstance();
    std::vector<spdlog::sink_ptr> sinks;

    // Get log level from configuration
    std::string logLevelStr = config.GetLogLevel();
    spdlog::level::level_enum logLevel = spdlog::level::from_str(logLevelStr);

    // Console sink
    if (config.GetConsoleOutput()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(logLevel);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        sinks.push_back(console_sink);
    }

    // File sink
    if (!config.GetLogFilePath().empty()) {
        try {
            // Extract directory from file path
            std::string logFilePath = config.GetLogFilePath();
            size_t lastSlash = logFilePath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                std::string logDir = logFilePath.substr(0, lastSlash);
                // Create directory if it doesn't exist
                std::filesystem::create_directories(logDir);
            }

            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFilePath,
                config.GetMaxLogFileSize() * 1024 * 1024, // Convert MB to bytes
                config.GetMaxLogFiles()
            );
            file_sink->set_level(logLevel);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%P] %v");
            sinks.push_back(file_sink);
        } catch (const std::exception& e) {
            // If file logging fails, fall back to console only
            if (sinks.empty()) {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_level(logLevel);
                console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [FILE_ERROR] %v");
                sinks.push_back(console_sink);
            }
        }
    }

    // If no sinks were created, create a default console sink
    if (sinks.empty()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        sinks.push_back(console_sink);
    }

    // Create or replace logger
    if (logger_) {
        spdlog::drop(loggerName);
    }

    logger_ = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
    logger_->set_level(logLevel);
    logger_->flush_on(spdlog::level::err);
    logger_->set_error_handler([](const std::string& msg) {
        // Write to stderr if logging fails
        std::cerr << "[LOGGER ERROR] " << msg << std::endl;
    });

    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);

    // Log initialization message
    logger_->info("Logger initialized for '{}' with level: {}", loggerName, logLevelStr);
    if (config.GetConsoleOutput()) {
        logger_->debug("Console output: enabled");
    }
    if (!config.GetLogFilePath().empty()) {
        logger_->debug("File logging to: {}", config.GetLogFilePath());
    }
}

void Logger::Initialize(const std::string& configPath) {
    configPath_ = configPath;
    SetupLogger("GameServer");
}

void Logger::InitializeWithWorkerId(int workerId) {
    std::string loggerName = "Worker" + std::to_string(workerId);
    SetupLogger(loggerName);
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(const std::string& name) {
    // If main logger isn't initialized, initialize it with defaults
    if (!logger_) {
        // Try to get configuration first
        try {
            auto& config = ConfigManager::GetInstance();
            if (config.HasKey("logging")) {
                Initialize("");
            } else {
                // No config available, use defaults
                logger_ = spdlog::stdout_color_mt(name);
                logger_->set_level(spdlog::level::info);
                logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
                logger_->warn("Logger initialized with default settings (no config found)");
            }
        } catch (const std::exception& e) {
            // Fallback to basic console logger
            logger_ = spdlog::stdout_color_mt(name);
            logger_->set_level(spdlog::level::info);
            logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
            logger_->warn("Logger fallback initialization: {}", e.what());
        }
    }

    // Return the requested logger
    if (name == "GameServer" || name.empty()) {
        return logger_;
    } else {
        auto named_logger = spdlog::get(name);
        if (!named_logger) {
            // Create a logger with the same sinks as the main logger
            named_logger = logger_->clone(name);
            spdlog::register_logger(named_logger);
        }
        return named_logger;
    }
}
