#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class ImageViewer {
public:
    ImageViewer() = default;
    ~ImageViewer();

    // Create the viewer window. Returns false on failure.
    bool initialize(const std::string& title = "DepthPalette", int width = 640, int height = 360);

    // Display a planar RGB frame (R plane, G plane, B plane — depthai format).
    void update(const uint8_t* planarRgb, int imgWidth, int imgHeight);

    // Display two packed BGR images (top-down) side by side.
    void updateSideBySide(const uint8_t* leftBgr, int leftW, int leftH,
                          const uint8_t* rightBgr, int rightW, int rightH);

    // Pump window messages. Returns false when the window is closed.
    bool isRunning();

    // Destroy the window.
    void shutdown();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Blit a packed BGR buffer (bottom-up) to the window client area.
    void blitBgr(const uint8_t* bgr, int imgW, int imgH);

    HWND hwnd_ = nullptr;
    bool running_ = false;

    // Reusable pixel buffers
    std::vector<uint8_t> bgrBuf_;
    std::vector<uint8_t> compositeBuf_;
};
