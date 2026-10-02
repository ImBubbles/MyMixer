#include "logger/Log.h"
#include "Application.h"

#include <QApplication>

#include <string>
#include <vector>

int main(const int argc, char *argv[]) {
    Log::LOG_FILTER = LogLevel::DEBUG;
    Log::defaultLogger();
    Log::info("Initializing application");

    bool noUi = false;
    std::vector<char*> qtArguments;
    qtArguments.push_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--no-ui") {
            noUi = true;
        } else {
            qtArguments.push_back(argv[index]);
        }
    }

    if (noUi) {
        Application application;
        return application.runHeadless();
    }

    int qtArgumentCount = static_cast<int>(qtArguments.size());
    QApplication qtApplication(qtArgumentCount, qtArguments.data());
    Application application;
    return application.runGui(qtApplication);
}