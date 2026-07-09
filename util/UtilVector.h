#pragma once
#include "vector"

namespace UtilVector {

    template<typename T>
    bool contains(const std::vector<T>& v, const T& against) {
        for(T e : v) {
            if(e == against) {
                return true;
            }
        }
        return false;
    }
    template<typename T>
    bool containsPrim(const T*& v, const std::size_t size, const T& against) {
        for(std::size_t i = 0; i < size; ++i) {
            if(v[i] == against) {
                return true;
            }
        }
        return false;
    }
    template<typename T>
    std::size_t binarySearch(const std::vector<T>& v, const T& element) {
        std::size_t left = 0;
        std::size_t right = v.size();
        while (left < right) {
            std::size_t mid = left + (right - left) / 2;
            if (v[mid] == element) {
                return mid;
            }
            else if (v[mid] < element) {
                left = mid + 1;
            }
            else {
                right = mid;
            }
        }
        return v.size(); // not found
    }
    template<typename T>
    std::size_t normalSearch(const std::vector<T>& v, const T& element) {
        for(std::size_t i = 0; i < v.size(); ++i) {
            if(v[i] == element) {
                return i;
            }
        }
        return v.size();
    }
    template<typename T>
    bool removeIndex(std::vector<T>& v, const std::size_t index) {
        if(index >= v.size() || index < 0) {
            return false;
        }
        v.erase(v.begin() + index);
        return true;
    }
    template<typename T>
    bool removeElement(std::vector<T>& v, const T& element) {
        std::size_t index = normalSearch(v, element);
        if(index == v.size()) {
            return false;
        }
        removeIndex(v, index);
        return true;
    }


}