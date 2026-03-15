#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Turbo colormap LUT (256 entries, RGB)
// Approximation of the Turbo colormap: blue -> cyan -> green -> yellow -> red
inline void turboRgb(uint8_t idx, uint8_t& r, uint8_t& g, uint8_t& b) {
    // Piecewise linear approximation of the turbo colormap
    float t = idx / 255.0f;

    // Red channel
    float rf;
    if (t < 0.35f) rf = 0.0f;
    else if (t < 0.65f) rf = (t - 0.35f) / 0.30f;
    else rf = 1.0f;

    // Green channel
    float gf;
    if (t < 0.15f) gf = 0.0f;
    else if (t < 0.35f) gf = (t - 0.15f) / 0.20f;
    else if (t < 0.65f) gf = 1.0f;
    else if (t < 0.85f) gf = 1.0f - (t - 0.65f) / 0.20f;
    else gf = 0.0f;

    // Blue channel
    float bf;
    if (t < 0.35f) bf = 0.5f + t / 0.35f * 0.5f;
    else if (t < 0.65f) bf = 1.0f - (t - 0.35f) / 0.30f;
    else bf = 0.0f;

    r = static_cast<uint8_t>(rf * 255.0f);
    g = static_cast<uint8_t>(gf * 255.0f);
    b = static_cast<uint8_t>(bf * 255.0f);
}

// Convert a raw uint16 depth image (millimeters) to packed BGR for display.
// outBgr must be pre-allocated to width * height * 3 bytes.
// Depth 0 (invalid) -> black. Values are clamped to [0, maxDepthMm].
inline void depthToColorBgr(const uint16_t* depthMm, int width, int height,
                             uint8_t* outBgr, uint16_t maxDepthMm = 10000) {
    float scale = 255.0f / maxDepthMm;

    for (int i = 0; i < width * height; i++) {
        uint16_t d = depthMm[i];
        int outIdx = i * 3;

        if (d == 0) {
            outBgr[outIdx + 0] = 0;
            outBgr[outIdx + 1] = 0;
            outBgr[outIdx + 2] = 0;
        } else {
            uint8_t idx = static_cast<uint8_t>(std::min(d * scale, 255.0f));
            uint8_t r, g, b;
            turboRgb(idx, r, g, b);
            outBgr[outIdx + 0] = b;  // BGR order for Windows
            outBgr[outIdx + 1] = g;
            outBgr[outIdx + 2] = r;
        }
    }
}

// Threshold depth to black/white. Closer than thresholdMm -> black, farther -> white.
// Depth 0 (invalid/no data) -> white.
inline void depthToThresholdBgr(const uint16_t* depthMm, int width, int height,
                                 uint8_t* outBgr, uint16_t thresholdMm) {
    for (int i = 0; i < width * height; i++) {
        uint16_t d = depthMm[i];
        uint8_t val = (d > 0 && d < thresholdMm) ? 0 : 255;
        int outIdx = i * 3;
        outBgr[outIdx + 0] = val;
        outBgr[outIdx + 1] = val;
        outBgr[outIdx + 2] = val;
    }
}

// Dilate the binary (black=foreground, white=background) BGR image in-place.
// Uses a 3x3 square structuring element. Each iteration expands black regions
// by one pixel in all 8 directions. Useful for connecting nearby blobs.
inline void dilateBinaryBgr(uint8_t* bgr, int width, int height, int iterations) {
    if (iterations <= 0) return;
    int total = width * height;
    std::vector<uint8_t> temp(total);

    for (int iter = 0; iter < iterations; iter++) {
        // Extract single channel (all channels identical for binary image)
        for (int i = 0; i < total; i++) temp[i] = bgr[i * 3];

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                if (temp[idx] == 0) continue;  // already foreground, skip

                // Check 3x3 neighborhood for any black (foreground) pixel
                bool hasBlack = false;
                for (int dy = -1; dy <= 1 && !hasBlack; dy++) {
                    int ny = y + dy;
                    if (ny < 0 || ny >= height) continue;
                    for (int dx = -1; dx <= 1 && !hasBlack; dx++) {
                        int nx = x + dx;
                        if (nx < 0 || nx >= width) continue;
                        if (temp[ny * width + nx] == 0) hasBlack = true;
                    }
                }
                if (hasBlack) {
                    int bi = idx * 3;
                    bgr[bi] = bgr[bi + 1] = bgr[bi + 2] = 0;
                }
            }
        }
    }
}

// Convert packed RGB to packed BGR (swap R and B channels).
// Used by Orbbec cameras which output packed RGB frames.
// outBgr must be pre-allocated to width * height * 3 bytes.
inline void packedRgbToPackedBgr(const uint8_t* rgb, int width, int height,
                                  uint8_t* outBgr) {
    int total = width * height;
    for (int i = 0; i < total; i++) {
        int idx = i * 3;
        outBgr[idx + 0] = rgb[idx + 2];  // B <- R
        outBgr[idx + 1] = rgb[idx + 1];  // G
        outBgr[idx + 2] = rgb[idx + 0];  // R <- B
    }
}

// Convert planar RGB (depthai format) to packed BGR (Windows format).
// Used by Luxonis cameras which output planar RGB frames.
// outBgr must be pre-allocated to width * height * 3 bytes.
inline void planarRgbToPackedBgr(const uint8_t* planarRgb, int width, int height,
                                  uint8_t* outBgr) {
    int planeSize = width * height;
    const uint8_t* rPlane = planarRgb;
    const uint8_t* gPlane = planarRgb + planeSize;
    const uint8_t* bPlane = planarRgb + 2 * planeSize;

    for (int i = 0; i < planeSize; i++) {
        int outIdx = i * 3;
        outBgr[outIdx + 0] = bPlane[i];
        outBgr[outIdx + 1] = gPlane[i];
        outBgr[outIdx + 2] = rPlane[i];
    }
}
