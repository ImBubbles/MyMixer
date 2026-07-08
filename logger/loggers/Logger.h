#pragma once
#include <string>
#include "../LogLevel.h"
class Logger {
public:
    virtual void nl() = 0;
    virtual void log(const int level, const std::string& message, const bool nl) = 0;
protected:
    bool prefixNext = true;
    virtual const std::string getLevelPrefix(const int logLevel) {
        switch (logLevel) {
            case DEBUG:
                return "[DEBUG]";
            case WARNING:
                return "[WARNING]";
            case ERROR:
                return "[ERROR]";
            default:
                return "[INFO]";
        };
    }
};