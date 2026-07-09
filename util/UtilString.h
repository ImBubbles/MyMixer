#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <functional>

namespace UtilString {
    std::string asUppercase(const std::string&);
    std::string asLowercase(const std::string&);
    std::string precisionDouble(double db, int precision);
    std::vector<std::string> split(const std::string& str, const char regex);
    std::string addrString(const void*);
    std::string padString(int width, const std::string& original, char padding); // center string if possible
    std::string rightString(int width, const std::string& original, char padding); // move string to right
    template <typename T>
    std::string arrAsToString(T* arr, int n) {
        std::stringstream ss;
        for(int i = 0; i < n; i++) {
            ss << std::to_string(arr[i]);
            if(i != n - 1) {
                ss << ", ";
            }
        }
        return ss.str();
    }
    template <typename T>
    std::string vectorAsString(std::vector<T> arr) {
        std::stringstream ss;
        int n = arr.size();
        for(int i = 0; i < n; i++) {
            ss << arr[i];
            if(i != n - 1) {
                ss << ", ";
            }
        }
        return ss.str();
    }
    template <typename T>
    std::string vectorAsStringFunc(std::vector<T> arr, std::function<std::string(T& obj)> toString) {
        std::stringstream ss;
        int n = arr.size();
        for(int i = 0; i < n; i++) {
            ss << toString(arr[i]);
            if(i != n - 1) {
                ss << ", ";
            }
        }
        return ss.str();
    }
};