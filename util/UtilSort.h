#pragma once
#include <functional>

namespace UtilSort {
    template <typename T, typename Comparator>
    void insertionSort(T* arr, const std::size_t size, Comparator comp) {
        for(std::size_t i = 1; i < size; i++) {
        T key = arr[i];
        std::size_t j = i - 1;
        while(j >=0 && comp(key, arr[j])) { // should prev be shifted forward one
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
        }
    }
    template <typename T, typename Comparator>
    void selectionSort(T* arr, const std::size_t size, Comparator comp) {
        for(std::size_t i = 0; i < size -1; i++) {
            std::size_t swapPos = i;
            for(std::size_t j = i + 1; j < size; j++) {
                if(comp(arr[j], arr[swapPos])) {
                    swapPos = j; // Should key swap with against? Against will be the lowest condition by the end of loop
                }
            }
            if(swapPos != i) {
                T temp = arr[i];
                arr[i] = arr[swapPos];
                arr[swapPos] = temp;
            }
        }
    }
    template <typename T, typename Comparator>
    void bubbleSort(T* arr, const int size, Comparator comp) {
        for(std::size_t i = 0; i < size - 1; i++) {
            for(std::size_t j = 0; j < size - i -1; j++) {
                if(comp(arr[j], arr[j + 1])) { // Should swap current with next?
                    T temp = arr[j];
                    arr[j] = arr[j + 1];
                    arr[j + 1] = temp;
                }
            }
        }
    }

    namespace PreparedSort {
        // At least for now, these are all for numerical sorting

        /*
        ltg = least to greatest
        gtl = greatest to least
        */

        template <typename T>
        void ltgInsertionSort(T* arr, const std::size_t size) {
            insertionSort(arr, size, [](const T& key, const T& prev) { return key < prev; });
        }
        template <typename T>
        void gtlInsertionSort(T* arr, const std::size_t size) {
            insertionSort(arr, size, [](const T& key, const T& prev) { return key > prev; });
        }
        template <typename T>
        void ltgSelectionSort(T* arr, const std::size_t size) {
            selectionSort(arr, size, [](const T& key, const T& against) { return key < against; });
        }
        template <typename T>
        void gtlSelectionSort(T* arr, const std::size_t size) {
            selectionSort(arr, size, [](const T& key, const T& against) { return key > against; });
        }
        template <typename T>
        void ltgBubbleSort(T* arr, const std::size_t size) {
            bubbleSort(arr, size, [](const T& current, const T& next) { return current > next; });
        }
        template <typename T>
        void gtlBubbleSort(T* arr, const std::size_t size) {
            bubbleSort(arr, size, [](const T& current, const T& next) { return current < next; });
        }
    }
};