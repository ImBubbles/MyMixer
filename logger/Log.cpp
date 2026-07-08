#include "Log.h"
#include "loggers/CLogger.h"
#include <memory>

int Log::LOG_FILTER = 0;

void Log::nl() {
	if(Log::logger == nullptr) {
		return;
	}
	Log::logger->nl();
}
void Log::nl(const int level) {
	if(level < Log::LOG_FILTER) {
		return;
	}
	if(Log::logger == nullptr) {
		return;
	}
	nl();
}
void Log::log(const int level, const std::string& message, const bool nl) {
	if (level < Log::LOG_FILTER)
		return;
	if (Log::logger == nullptr) {
		return;
	}
	Log::logger->log(level, message, nl);
}

void Log::defaultLogger() {
	Log::setLogger(std::make_unique<CLogger>());
}

std::unique_ptr<Logger> Log::logger = nullptr;