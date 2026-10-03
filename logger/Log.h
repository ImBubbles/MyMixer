#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "LogLevel.h"
#include "loggers/CLogger.h"

class Log {

	// CLASS VARS
public:
	static int LOG_FILTER;
protected:
	static std::unique_ptr<Logger> logger;

	struct PendingEntry {
		int level;
		std::string message;
		bool newline;
	};
	static std::mutex asyncMutex;
	static std::vector<PendingEntry> asyncQueue;
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

	// Non-blocking: safe to call from the realtime PipeWire thread; silently drops the message if contended.
	static void logAsync(const int level, const std::string& message);
	// Must be called from a non-realtime thread (e.g. the UI thread); flushes any queued async messages.
	static void drainAsync();

};