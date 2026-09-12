#pragma once
#include <cstddef>
#include <cstdint>

struct DepthStats {
    int validPixels = 0;
    int foregroundPixels = 0;
    int nearestMm = 0;
};

inline DepthStats measureDepth(const uint16_t* depth, size_t count, uint16_t threshold) {
    DepthStats result;
    for (size_t i = 0; i < count; ++i) {
        const int mm = depth[i];
        if (mm == 0) continue;
        ++result.validPixels;
        if (mm < threshold) ++result.foregroundPixels;
        if (!result.nearestMm || mm < result.nearestMm) result.nearestMm = mm;
    }
    return result;
}
