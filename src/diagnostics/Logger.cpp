#include "diagnostics/Logger.h"

namespace flickvr::diagnostics {

void Logger::log(LogLevel level, std::string message) {
    LogEntry entry{std::chrono::system_clock::now(), level, std::move(message)};
    std::function<void(const LogEntry&)> sink;
    {
        std::lock_guard lock(mutex_);
        entries_.push_back(entry);
        while (entries_.size() > capacity_) {
            entries_.pop_front();
        }
        sink = sink_;
    }
    if (sink) {
        try {
            sink(entry);
        } catch (...) {
            // Diagnostics must not change the behaviour of the caller.
        }
    }
}

void Logger::setSink(std::function<void(const LogEntry&)> sink) {
    std::lock_guard lock(mutex_);
    sink_ = std::move(sink);
}

std::vector<LogEntry> Logger::snapshot() const {
    std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

void Logger::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

} // namespace flickvr::diagnostics
