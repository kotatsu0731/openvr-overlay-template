#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace flickvr::diagnostics {
enum class LogLevel { Debug, Info, Warning, Error };
struct LogEntry { std::chrono::system_clock::time_point time; LogLevel level; std::string message; };
class Logger {
public:
    explicit Logger(std::size_t capacity = 256) : capacity_(capacity ? capacity : 1) {}
    void log(LogLevel level, std::string message);
    void debug(std::string message) { log(LogLevel::Debug, std::move(message)); }
    void info(std::string message) { log(LogLevel::Info, std::move(message)); }
    void warning(std::string message) { log(LogLevel::Warning, std::move(message)); }
    void error(std::string message) { log(LogLevel::Error, std::move(message)); }
    // The sink is invoked after the entry has been committed and after the
    // logger mutex has been released.  This allows observers to safely take a
    // snapshot (or detach the sink) from inside their callback.
    void setSink(std::function<void(const LogEntry&)> sink);
    std::vector<LogEntry> snapshot() const;
    void clear();
private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::function<void(const LogEntry&)> sink_;
};
} // namespace flickvr::diagnostics
