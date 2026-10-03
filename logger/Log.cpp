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

void Log::logAsync(const int level, const std::string& message) {
	if (level < Log::LOG_FILTER) {
		return;
	}
	std::unique_lock<std::mutex> lock(asyncMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return; // dropped: never block the realtime caller
	}
	asyncQueue.push_back({level, message, true});
}

void Log::drainAsync() {
	std::vector<PendingEntry> pending;
	{
		std::lock_guard<std::mutex> lock(asyncMutex);
		pending.swap(asyncQueue);
	}
	for (const PendingEntry& entry : pending) {
		Log::log(entry.level, entry.message, entry.newline);
	}
}

std::unique_ptr<Logger> Log::logger = nullptr;
std::mutex Log::asyncMutex;
std::vector<Log::PendingEntry> Log::asyncQueue;