#pragma once

namespace UtilNumber {
    static bool randInitialized = false;
    void initRand();
    int randInt(const int min, const int max, int* ptr);
    int randInt(const int min, const int max);
    int clampWrap(int min, int max, int now);
    int clampBorder(int min, int max, int now);
}