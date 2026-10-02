#include "Application.h"

#include <QApplication>

#include <chrono>
#include <csignal>
#include <thread>

#include "config/Config.h"
#include "environment/Environment.h"
#include "logger/Log.h"
#include "ui/MainWindow.h"

namespace {
volatile std::sig_atomic_t shutdownRequested = 0;

void requestShutdown(int) {
    shutdownRequested = 1;
}
}

Application::Application() {
    Log::info("Application started");
    Environment::setupFileEnvironment();

    try {
        const std::string configJson = Environment::readFile(Environment::settings.config);
        const Config config = ConfigHandler::fromJson(configJson);
        if (!ConfigHandler::loadConfig(&pipeWireContext, config)) {
            Log::error("Failed to load configuration");
        }
    } catch (const std::exception& error) {
        Log::error("Failed to read startup configuration: " + std::string(error.what()));
    }
}

Application::~Application() = default;

int Application::runGui(QApplication& application) {
    application.setQuitOnLastWindowClosed(false);
    MainWindow window(pipeWireContext);
    window.show();
    return application.exec();
}

int Application::runHeadless() {
    shutdownRequested = 0;
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
    Log::info("Running without UI; press Ctrl-C to exit");

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}