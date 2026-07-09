#include "UtilNumber.h"
#include <cstdlib>
#include <ctime>

void UtilNumber::initRand() { // Always call when using rand()
    if(UtilNumber::randInitialized) {
        return;
    }
    srand(time(0));
    UtilNumber::randInitialized = true;
}

int UtilNumber::randInt(const int min, const int max, int* ptr) {
    initRand();
    int result = min + rand() % (max - min + 1);
    if(ptr != nullptr) {
        *ptr = result;
    }
    return result;
}

int UtilNumber::randInt(const int min, const int max) {
    return UtilNumber::randInt(min, max, nullptr);
}

int UtilNumber::clampWrap(int min, int max, int now) {
    int range = max - min + 1;
    return min + ((now - min) % range + range) % range;
}

int UtilNumber::clampBorder(int min, int max, int now) {
    return now > max ? max : now < min ? min : now;
}