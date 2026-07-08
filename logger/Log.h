#pragma once
#include <string>
#include <memory>
#include "LogLevel.h"
#include "loggers/CLogger.h"

class Log {

	// CLASS VARS
public:
	static int LOG_FILTER;
protected:
	static std::unique_ptr<Logger> logger;
	// CLASS METHODS
public:

	Log() = delete;

	static void nl(); // newLine
	static void nl(const int level); // newLine if level
	static void log(const int level, const std::string& message, const bool nl);
	static void log(const int level, const std::string& message) {
		log(level, message, true);
	}
	static void debug(const std::string& message) {
		log(LogLevel::DEBUG, message);
	}
	static void info(const std::string& message) {
		log(LogLevel::INFO, message);
	}
	static void warning(const std::string& message) {
		log(LogLevel::WARNING, message);
	}
	static void error(const std::string& message) {
		log(LogLevel::ERROR, message);
	}
	static void setLogger(std::unique_ptr<Logger> ref) {
		logger = std::move(ref);
	}
	static void defaultLogger();

};