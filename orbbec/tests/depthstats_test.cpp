#include "depthstats.hpp"
#include <iostream>

int main() {
    const uint16_t highResolution[]{0, 260, 300, 1000};
    auto stats = measureDepth(highResolution, 4, 250);
    if (stats.validPixels != 3 || stats.foregroundPixels != 0 || stats.nearestMm != 260) return 1;
    stats = measureDepth(highResolution, 4, 500);
    if (stats.foregroundPixels != 2) return 2;
    const uint16_t lowResolution[]{0, 180, 249, 250};
    stats = measureDepth(lowResolution, 4, 250);
    if (stats.foregroundPixels != 2 || stats.nearestMm != 180) return 3;
    const uint16_t invalid[]{0, 0};
    stats = measureDepth(invalid, 2, 250);
    if (stats.validPixels || stats.foregroundPixels || stats.nearestMm) return 4;
    std::cout << "Depth threshold, invalid-frame and nearest-depth checks passed\n";
}
