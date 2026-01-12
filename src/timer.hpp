#pragma once
#include <chrono>

struct Timer {
    using clock = std::chrono::steady_clock;
    clock::time_point start = clock::now();

    void reset() { start = clock::now(); }

    double seconds() const {
        auto end = clock::now();
        std::chrono::duration<double> diff = end - start;
        return diff.count();
    }
};
