#pragma once

// Ensure NOMINMAX is defined before including Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace disk_recover {

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void init(const std::wstring& logPath = L"") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logPath.empty()) {
            // Default to temp directory
            wchar_t tempPath[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempPath);
            logPath_ = std::wstring(tempPath) + L"disk-recover.log";
        } else {
            logPath_ = logPath;
        }

        // Clear previous log
        file_.open(logPath_, std::ios::out | std::ios::trunc);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void log(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Also output to debug
        OutputDebugStringW(message.c_str());

        // Write to file
        file_.open(logPath_, std::ios::out | std::ios::app);
        if (file_.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &time);

            file_ << std::put_time(&tm, L"[%Y-%m-%d %H:%M:%S] ");
            file_ << message;
            if (!message.empty() && message.back() != L'\n') {
                file_ << L"\n";
            }
            file_.close();
        }
    }

    void log(const char* message) {
        std::wstring wide(message, message + strlen(message));
        log(wide);
    }

    template<typename... Args>
    void logFmt(const wchar_t* fmt, Args... args) {
        wchar_t buf[1024];
        _snwprintf_s(buf, _TRUNCATE, fmt, args...);
        log(std::wstring(buf));
    }

    const std::wstring& getLogPath() const { return logPath_; }

private:
    Logger() = default;
    std::wstring logPath_;
    std::wofstream file_;
    std::mutex mutex_;
};

// Convenience macros
#define LOG_INIT(path) disk_recover::Logger::instance().init(path)
#define LOG_MSG(msg) disk_recover::Logger::instance().log(msg)
#define LOG_FMT(fmt, ...) disk_recover::Logger::instance().logFmt(fmt, __VA_ARGS__)

} // namespace disk_recover
