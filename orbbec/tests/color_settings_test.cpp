#include "webserver.hpp"
#include <iostream>

int main() {
    const PropertyRange saturation{true, 0, 100, 1, 64, 0};
    const PropertyRange gamma{true, 100, 500, 1, 300, 100};
    DeviceSettings settings;
    if (resolveColorSetting(settings.colorSaturation, saturation) != 64) return 1;
    if (resolveColorSetting(settings.colorGamma, gamma) != 300) return 2;
    // An explicitly saved greyscale setting must remain possible.
    if (resolveColorSetting(0, saturation) != 0) return 3;
    if (resolveColorSetting(80, saturation) != 80) return 4;
    if (resolveColorSetting(900, gamma) != 500) return 5;
    if (resolveColorSetting(-1, {}) != -1) return 6;
    std::cout << "Camera color defaults and explicit overrides passed\n";
}
