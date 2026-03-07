#include "viewer_linux.hpp"
#include <iostream>

// Stub implementation for Linux — the viewer window is Windows-only.
// The web UI is the primary interface on Linux.

ImageViewer::~ImageViewer() {}

bool ImageViewer::initialize(const std::string& title, int width, int height) {
    std::cerr << "Warning: --window not supported on Linux. Use the web UI instead." << std::endl;
    return false;
}

void ImageViewer::update(const uint8_t*, int, int) {}
void ImageViewer::updateSingle(const uint8_t*, int, int) {}
void ImageViewer::updateSideBySide(const uint8_t*, int, int, const uint8_t*, int, int) {}
bool ImageViewer::isRunning() { return false; }
void ImageViewer::shutdown() {}
