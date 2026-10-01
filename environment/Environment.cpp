//
// Created by bubbles on 8/4/25.
//

#include "Environment.h"
//#include "defaults/settings.h"
//#include "defaults/config.h"

int ensureExistence(const std::string& filePath) {
    Log::debug("Ensuring file "+filePath);
    if (std::filesystem::exists(filePath)) {
        return 1;
    }
    std::ofstream outfile(filePath);
    if (outfile.is_open()) {
        outfile.close();
        return 0;
    }
    outfile.close();
    return -1;
}

int ensureExistence(const std::string& filePath, const std::string& defaultValue) {
    const int result = ensureExistence(filePath);
    if (result!=0) {
        return result;
    }
    std::fstream file(filePath);
    file << defaultValue;
    file.close();
    return result;
}

std::string readFile(std::string& filePath) {
    Log::debug("Reading file "+filePath);
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Log::debug("Could not find file: " + filePath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


void setupFileEnvironment() {

    // todo

}