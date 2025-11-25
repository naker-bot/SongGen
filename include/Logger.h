#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <functional>
#include <mutex>

/**
 * Logger - Globaler Logger mit optionalem GUI-Callback
 */
class Logger {
public:
    using LogCallback = std::function<void(const std::string&)>;
    
    static Logger& instance() {
        static Logger instance;
        return instance;
    }
    
    void setCallback(LogCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = callback;
    }
    
    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (callback_) {
            callback_(message);
        }
    }
    
    // Convenience-Methoden
    static void info(const std::string& msg) { instance().log("ℹ️ " + msg); }
    static void success(const std::string& msg) { instance().log("✅ " + msg); }
    static void error(const std::string& msg) { instance().log("❌ " + msg); }
    static void warning(const std::string& msg) { instance().log("⚠️ " + msg); }
    static void progress(const std::string& msg) { instance().log("🎵 " + msg); }
    static void debug(const std::string& msg) { instance().log("🔍 " + msg); }
    
private:
    Logger() = default;
    LogCallback callback_;
    std::mutex mutex_;
};

#endif // LOGGER_H
