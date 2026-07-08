#include "logger/Log.h"
#include "Application.h"

int main(const int argc, char *argv[]) {
    Log::LOG_FILTER = LogLevel::DEBUG;
    Log::defaultLogger();
    Log::info("Initializing application");
    Application app(argc, argv);
    return 0;
}