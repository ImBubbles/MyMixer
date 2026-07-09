#include <cctype>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include "UtilString.h"

std::string UtilString::asUppercase(const std::string& str) {
    std::string result;
    for(const char c : str) {
        result += std::toupper(c);
    }
    return result;
}

std::string UtilString::asLowercase(const std::string& str) {
    std::string result;
    for(const char c : str) {
        result += std::tolower(c);
    }
    return result;
}


std::string UtilString::precisionDouble(double db, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << db;
    return oss.str();
}

std::vector<std::string> UtilString::split(const std::string& str, const char regex) {
    std::vector<std::string> result;
    int start = -1;
    for(std::size_t i = 0; i < str.length(); i++) {
        if(str[i] == regex && start != -1) {
            result.push_back(str.substr(start, i - start));
            start = -1;
        } else if(str[i] != regex && start == -1) {
            start = i;
        }
    }
    if(start != -1) {
        result.push_back(str.substr(start));
    }
    return result;
}

std::string UtilString::addrString(const void* ptr) {
    std::stringstream ss;
    ss << ptr;
    return ss.str();
}
std::string UtilString::padString(int w, const std::string &original, char padding) {
    if(original.length() > w) {
        return original;
    }
    std::stringstream ss;
    int totalPadding = w - original.length();
    int left = ((double) totalPadding / 2) + 0.5;
    int right = totalPadding / 2;
    for(int i = 1; i <= left; i++) {
        ss << padding;
    }
    ss << original;
    for(int i = 1; i <= right; i++) {
        ss << padding;
    }
    return ss.str();
}
std::string UtilString::rightString(int w, const std::string &original, char padding) {
    if(original.length() > w) {
        return original;
    }
    std::stringstream ss;
    std::size_t pads = w - original.length();
    for(std::size_t i = 1; i <= pads; i++) {
        ss << padding;
    }
    ss << original;
    return ss.str();
}