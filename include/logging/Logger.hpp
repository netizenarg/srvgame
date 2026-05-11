#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>
//#include "config/ConfigManager.hpp"
//#include "network/BinaryProtocol.hpp"

template<typename Mutex>
class null_sink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg&) override {}
    void flush_() override {}
};

struct LogSession : std::enable_shared_from_this<LogSession> {
    explicit LogSession(asio::ip::tcp::socket socket,
                           std::deque<std::string>& queue,
                           std::mutex& queueMutex,
                           std::condition_variable& cv);
    void Start();
private:
    asio::ip::tcp::socket socket_;
    asio::streambuf buffer_;
    std::deque<std::string>& queue_;
    std::mutex& queueMutex_;
    std::condition_variable& cv_;
    void doRead();
};

class LogService : public std::enable_shared_from_this<LogService> {
public:
    LogService(asio::io_context& io, nlohmann::json config);
    ~LogService();
    void Start();
    void Stop();
    void EnqueueLog(const std::string& line);
    void AddMasterSink();

private:
    asio::io_context& io_;
    nlohmann::json config_;
    asio::ip::tcp::acceptor acceptor_;
    uint16_t port_;
    std::string logFilePath_;
    size_t maxFileSize_;
    size_t maxFiles_;
    bool consoleEnabled_;

    std::deque<std::string> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::thread writerThread_;
    std::atomic<bool> running_{true};

    std::ofstream logFile_;
    size_t currentFileSize_ = 0;
    void rotateFileIfNeeded();

    void acceptLoop();
    void writerLoop();
    void writeToConsole(const std::string& line);
};

class LogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit LogSink(std::shared_ptr<LogService> service);
    LogSink(std::shared_ptr<asio::ip::tcp::socket> socket);
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override;
private:
    std::shared_ptr<LogService> service_ = nullptr;
    std::shared_ptr<asio::ip::tcp::socket> socket_ = nullptr;
    std::mutex writeMutex_;
};

class Logger {
public:
    static void Initialize(nlohmann::json config={});
    static void InitializeDefaults();
    static void InitializeWithWorkerId(int workerId, nlohmann::json config={});
    static void SetLogService(std::shared_ptr<LogService> svc);
    static std::shared_ptr<spdlog::logger> GetLogger(const std::string& name="WorkerMain");
    static void AddSink(spdlog::sink_ptr sink);

    // template<typename... Args> static void Trace(const std::string& fmt, Args... args);
    // template<typename... Args> static void Debug(const std::string& fmt, Args... args);
    // template<typename... Args> static void Info(const std::string& fmt, Args... args);
    // template<typename... Args> static void Warn(const std::string& fmt, Args... args);
    // template<typename... Args> static void Error(const std::string& fmt, Args... args);
    // template<typename... Args> static void Critical(const std::string& fmt, Args... args);
    template<typename... Args> static void Trace(const std::string& fmt, Args... args) {
        GetLogger()->trace(fmt, args...);
    }

    template<typename... Args> static void Debug(const std::string& fmt, Args... args) {
        GetLogger()->debug(fmt, args...);
    }

    template<typename... Args> static void Info(const std::string& fmt, Args... args) {
        GetLogger()->info(fmt, args...);
    }

    template<typename... Args> static void Warn(const std::string& fmt, Args... args) {
        GetLogger()->warn(fmt, args...);
    }

    template<typename... Args> static void Error(const std::string& fmt, Args... args) {
        GetLogger()->error(fmt, args...);
    }

    template<typename... Args> static void Critical(const std::string& fmt, Args... args) {
        GetLogger()->critical(fmt, args...);
    }

    static void Flush();

private:
    static std::shared_ptr<spdlog::logger> logger_;

    static void SetupLogger(const std::string& loggerName="WorkerMain", nlohmann::json config={});
};
