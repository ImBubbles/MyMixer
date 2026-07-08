#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include "Logger.h"

class FLogger : public Logger {

private:
    std::ofstream file;
public:

    FLogger(const std::string& fileName) : file(fileName, std::ios::out) {} // will close upon deletion by default
    void nl() override {
        file << std::endl;
        prefixNext = true;
    }
    void log(const int level, const std::string& message, const bool nl) override {
        if(prefixNext) {
            file << getLevelPrefix(level) << " ";
        }
        file << message;
        prefixNext = false;
        if(nl) {
            this->nl();
        }
    }
};