#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ImageViewer {
public:
    ImageViewer() = default;
    ~ImageViewer();

    bool initialize(const std::string& title = "DepthPalette", int width = 640, int height = 360);
    void update(const uint8_t* planarRgb, int imgWidth, int imgHeight);
    void updateSingle(const uint8_t* bgr, int imgW, int imgH);
    void updateSideBySide(const uint8_t* leftBgr, int leftW, int leftH,
                          const uint8_t* rightBgr, int rightW, int rightH);
    bool isInitialized() const { return false; }
    bool isRunning();
    void shutdown();
};
