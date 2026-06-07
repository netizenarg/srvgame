#include "logging/Logger.hpp"

bool g_logDisabled = false;

using null_sink_mt = null_sink<std::mutex>;

static std::shared_ptr<LogService> g_logService;
std::shared_ptr<spdlog::logger> Logger::logger_;

LogSink::LogSink(std::shared_ptr<LogService> service) : service_(std::move(service)) {}

LogSink::LogSink(std::shared_ptr<asio::ip::tcp::socket> socket) : socket_(std::move(socket)) {}

void LogSink::sink_it_(const spdlog::details::log_msg& msg) {
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    std::string line(formatted.data(), formatted.size());
    if (!line.empty() && line.back() == '\n') line.pop_back();
    if (service_) {
        service_->EnqueueLog(std::move(line));
    } else if (socket_ && socket_->is_open()) {
        line += '\n';
        try {
            std::lock_guard<std::mutex> lock(writeMutex_);
            asio::write(*socket_, asio::buffer(line));
        } catch (const std::exception& err) {
            socket_->close();
        }
    }
}

void LogSink::flush_() {}

LogSession::LogSession(asio::ip::tcp::socket socket,
                                   std::deque<std::string>& queue,
                                   std::mutex& queueMutex,
                                   std::condition_variable& cv)
: socket_(std::move(socket)), queue_(queue), queueMutex_(queueMutex), cv_(cv) {}

void LogSession::Start() {
    doRead();
}

void LogSession::doRead() {
    auto self = shared_from_this();
    asio::async_read_until(socket_, buffer_, '\n',
    [self](std::error_code ec, size_t) {
        if (!ec) {
            std::istream is(&self->buffer_);
            std::string line;
            std::getline(is, line);
            {
                std::lock_guard<std::mutex> lock(self->queueMutex_);
                self->queue_.push_back(std::move(line));
            }
            self->cv_.notify_one();
            self->doRead();
        }
    });
}

LogService::LogService(asio::io_context& io, nlohmann::json config)
    : io_(io),
      config_(config),
      acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(),
                  config.at("port").get<int>())),
      port_(config.at("port").get<int>()),
      logFilePath_(config.at("file").get<std::string>()),
      maxFileSize_(config.at("max_file_size").get<int>()),
      maxFiles_(config.at("max_files").get<int>()),
      consoleEnabled_(config.at("console").get<bool>())
{
    if (!logFilePath_.empty()) {
        size_t lastSlash = logFilePath_.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            std::filesystem::create_directories(logFilePath_.substr(0, lastSlash));
    }
}

LogService::~LogService() { Stop(); }

void LogService::Start() {
    writerThread_ = std::thread(&LogService::writerLoop, this);
    auto self = shared_from_this();
    Logger::SetLogService(self);
    Logger::Initialize(config_);
    //Logger::AddSink(std::make_shared<LogSink>(self));
    acceptLoop();
}

void LogService::Stop() {
    running_ = false;
    acceptor_.close();
    queueCV_.notify_one();
    if (writerThread_.joinable()) writerThread_.join();
    if (logFile_.is_open()) logFile_.close();
}

void LogService::EnqueueLog(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        logQueue_.push_back(line);
    }
    queueCV_.notify_one();
}

void LogService::acceptLoop() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                std::make_shared<LogSession>(std::move(socket),
                    logQueue_, queueMutex_, queueCV_)->Start();
            }
            if (running_) acceptLoop();
        });
}

void LogService::writerLoop() {
    if (!logFilePath_.empty()) {
        logFile_.open(logFilePath_, std::ios::app | std::ios::out);
        if (logFile_.good()) {
            logFile_.seekp(0, std::ios::end);
            currentFileSize_ = logFile_.tellp();
        }
    }

    while (running_ || !logQueue_.empty()) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] { return !logQueue_.empty() || !running_; });
            if (!running_ && logQueue_.empty()) break;
            if (!logQueue_.empty()) {
                line = std::move(logQueue_.front());
                logQueue_.pop_front();
            }
        }
        if (!line.empty()) {
            writeToConsole(line);
            if (logFile_.is_open()) {
                logFile_ << line << std::endl;
                currentFileSize_ += line.size() + 1;
                rotateFileIfNeeded();
            }
        }
    }
}

void LogService::writeToConsole(const std::string& line) {
    if (consoleEnabled_) {
        std::cout << line << std::endl;
    }
}

void LogService::rotateFileIfNeeded() {
    if (maxFileSize_ > 0 && currentFileSize_ >= maxFileSize_) {
        logFile_.close();
        if (maxFiles_ > 1) {
            for (int i = static_cast<int>(maxFiles_) - 1; i > 0; --i) {
                auto oldName = logFilePath_ + "." + std::to_string(i);
                auto newName = logFilePath_ + "." + std::to_string(i + 1);
                if (std::filesystem::exists(oldName)) {
                    std::filesystem::rename(oldName, newName);
                }
            }
            if (std::filesystem::exists(logFilePath_)) {
                std::filesystem::rename(logFilePath_, logFilePath_ + ".1");
            }
        }
        logFile_.open(logFilePath_, std::ios::out | std::ios::trunc);
        currentFileSize_ = 0;
    }
}


void Logger::Flush() {
    if (logger_) {
        logger_->flush();
    }
}

void Logger::Initialize(nlohmann::json config) {
    SetupLogger("WorkerMain", config);
}

void Logger::InitializeDefaults() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger_ = std::make_shared<spdlog::logger>("WorkerMain", console_sink);
    logger_->set_level(spdlog::level::info);
    spdlog::set_default_logger(logger_);
    logger_->info("Early logger initialized with default settings");
}

void Logger::InitializeWithWorkerId(int workerId, nlohmann::json config) {
    std::string loggerName = "Worker" + std::to_string(workerId);
    SetupLogger(loggerName, config);
}

void Logger::SetLogService(std::shared_ptr<LogService> svc) {
    g_logService = svc;
}

void Logger::SetupLogger(const std::string& loggerName, nlohmann::json config) {
    if (g_logDisabled) {
        auto sink = std::make_shared<null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>(loggerName, sink);
        logger_->set_level(spdlog::level::off);
        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        return;
    }
    std::vector<spdlog::sink_ptr> sinks;
    std::string filepath = config.at("file").get<std::string>();
    std::string logLevelStr = config.at("level").get<std::string>();
    spdlog::level::level_enum logLevel = spdlog::level::from_str(logLevelStr);
    std::string pattern = config.at("pattern").get<std::string>();
    if (pattern.empty()) {pattern = "[%Y-%m-%d %H:%M:%S.%e] [%P] [%^%l%$] [%n] %v";}
    //if (sinks.empty() && !g_logService) {
    if (config.at("console").get<bool>()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern(pattern);
        sinks.push_back(console_sink);
    }
    if (logger_) {
        spdlog::drop(loggerName);
    }
    logger_ = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
    if (g_logService) {
        logger_->sinks().push_back(std::make_shared<LogSink>(g_logService));
    }
    logger_->set_pattern(pattern);
    logger_->set_level(logLevel);
    logger_->flush_on(spdlog::level::err);
    logger_->set_error_handler([](const std::string& msg) {
        std::cerr << "[LOGGER ERROR] " << msg << std::endl;
    });
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
    logger_->trace("Logger initialized for '{}' with level: {}", loggerName, logLevelStr);
    logger_->trace("Console output: {}", config.at("console").get<bool>());
    if (!filepath.empty()) {
        logger_->debug("File logging to: {}", filepath);
    }
}

void Logger::AddSink(spdlog::sink_ptr sink) {
    if (logger_) {
        logger_->sinks().push_back(sink);
    }
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(const std::string& name) {
    if (!logger_) {
        static auto dummy_logger = []{
            auto log = std::make_shared<spdlog::logger>("dummy", std::make_shared<null_sink_mt>());
            log->set_level(spdlog::level::off);
            return log;
        }();
        return dummy_logger;
    }
    if (name == "WorkerMain" || name.empty()) {
        return logger_;
    }
    auto named_logger = spdlog::get(name);
    if (!named_logger) {
        named_logger = logger_->clone(name);
        spdlog::register_logger(named_logger);
    }
    return named_logger;
}

// template<typename... Args> void Logger::Trace(const std::string& fmt, Args... args) {
//     GetLogger()->trace(fmt, args...);
// }
//
// template<typename... Args> void Logger::Debug(const std::string& fmt, Args... args) {
//     GetLogger()->debug(fmt, args...);
// }
//
// template<typename... Args> void Logger::Info(const std::string& fmt, Args... args) {
//     GetLogger()->info(fmt, args...);
// }
//
// template<typename... Args> void Logger::Warn(const std::string& fmt, Args... args) {
//     GetLogger()->warn(fmt, args...);
// }
//
// template<typename... Args> void Logger::Error(const std::string& fmt, Args... args) {
//     GetLogger()->error(fmt, args...);
// }
//
// template<typename... Args> void Logger::Critical(const std::string& fmt, Args... args) {
//     GetLogger()->critical(fmt, args...);
// }
