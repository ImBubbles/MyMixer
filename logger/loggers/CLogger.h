#pragma once
#include <iostream>
#include "Logger.h"

namespace ANSI {
	const std::string RESET = "\033[0m";
	const std::string RED = "\033[31m";
	const std::string GREEN = "\033[32m";
	const std::string YELLOW = "\033[33m";
	const std::string BLUE = "\033[34m";
	const std::string MAGENTA = "\033[35m";
	const std::string CYAN = "\033[36m";
	const std::string WHITE = "\033[37m";
	const std::string BOLD = "\033[1m";
}

class CLogger : public Logger {
    void nl() override {
        std::cout << ANSI::RESET << std::endl;
        prefixNext = true;
    }
    void log(const int level, const std::string& message, const bool nl) override {
        if(prefixNext) {
            std::cout << getLevelPrefix(level) << " ";
        }
        std::cout << message;
        prefixNext = false;
        if(nl) {
            this->nl();
        }
    }
    const std::string getLevelPrefix(const int logLevel) override {
        switch (logLevel) {
            case DEBUG:
                return ANSI::CYAN + "[DEBUG]";
            case WARNING:
                return ANSI::YELLOW + "[WARNING]";
            case ERROR:
                return ANSI::RED + "[ERROR]";
            default:
                return ANSI::RESET + "[INFO]";

        }
    }
};