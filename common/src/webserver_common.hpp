#pragma once

// Shared C++ utility functions used by both orbbec and luxonis webserver.cpp.
// All functions are inline (header-only) so no separate .cpp is needed.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ---- JPEG encoding helpers (stb_image_write.h must already be included) ----

inline void jpegWriteFunc(void* context, void* data, int size) {
    auto* buf = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    buf->insert(buf->end(), bytes, bytes + size);
}

inline std::vector<uint8_t> encodeJpeg(const uint8_t* bgr, int width, int height, int quality = 80) {
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (int i = 0; i < width * height; i++) {
        rgb[i * 3 + 0] = bgr[i * 3 + 2];  // R <- B
        rgb[i * 3 + 1] = bgr[i * 3 + 1];  // G
        rgb[i * 3 + 2] = bgr[i * 3 + 0];  // B <- R
    }
    std::vector<uint8_t> jpeg;
    jpeg.reserve(static_cast<size_t>(width) * height / 4);
    stbi_write_jpg_to_func(jpegWriteFunc, &jpeg, width, height, 3, rgb.data(), quality);
    return jpeg;
}

// ---- BMP encoding ----

inline std::vector<uint8_t> makeBmp(const uint8_t* bgr, int width, int height) {
    int rowBytes = width * 3;
    int rowPadding = (4 - (rowBytes % 4)) % 4;
    int paddedRow = rowBytes + rowPadding;
    int imageSize = paddedRow * height;
    int fileSize = 54 + imageSize;

    std::vector<uint8_t> bmp(fileSize, 0);

    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &fileSize, 4);
    int dataOffset = 54;
    std::memcpy(&bmp[10], &dataOffset, 4);

    int infoSize = 40;
    std::memcpy(&bmp[14], &infoSize, 4);
    std::memcpy(&bmp[18], &width, 4);
    std::memcpy(&bmp[22], &height, 4);
    uint16_t planes = 1;
    std::memcpy(&bmp[26], &planes, 2);
    uint16_t bpp = 24;
    std::memcpy(&bmp[28], &bpp, 2);
    std::memcpy(&bmp[34], &imageSize, 4);

    uint8_t* dst = bmp.data() + 54;
    for (int y = height - 1; y >= 0; y--) {
        const uint8_t* srcRow = bgr + y * rowBytes;
        std::memcpy(dst, srcRow, rowBytes);
        dst += rowBytes;
        for (int p = 0; p < rowPadding; p++) *dst++ = 0;
    }

    return bmp;
}

// ---- Simple JSON parsers for settings.json ----

inline bool jsonInt(const std::string& text, const std::string& key, int& out) {
    std::string needle = "\"" + key + "\":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    if (pos >= text.size()) return false;
    bool negative = false;
    if (text[pos] == '-') { negative = true; pos++; }
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
    int val = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        val = val * 10 + (text[pos] - '0');
        pos++;
    }
    out = negative ? -val : val;
    return true;
}

inline bool jsonBool(const std::string& text, const std::string& key, bool& out) {
    std::string needle = "\"" + key + "\":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    if (pos >= text.size()) return false;
    if (text.compare(pos, 4, "true") == 0) { out = true; return true; }
    if (text.compare(pos, 5, "false") == 0) { out = false; return true; }
    return false;
}

inline bool jsonString(const std::string& text, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++; // skip opening quote
    std::string result;
    while (pos < text.size() && text[pos] != '"') {
        result += text[pos++];
    }
    out = result;
    return true;
}

// ---- Settings file helpers ----

// Atomically write a JSON string to settings.json (temp + rename).
inline void writeSettingsFile(const std::string& json) {
    std::ofstream tmp("settings.json.tmp");
    if (!tmp) { std::cerr << "Failed to write settings.json.tmp" << std::endl; return; }
    tmp << json;
    tmp.close();
    std::remove("settings.json");
    std::rename("settings.json.tmp", "settings.json");
}

// Build the shared portion of the settings JSON (without leading { or trailing }).
// Caller wraps this in { ... platform-specific fields ... }.
inline std::string saveSharedSettingsJson(
    int thresholdMm, bool thresholdEnabled, int dilateIterations,
    bool blobDetectEnabled, int maxBlobPixels, int minBlobPixels, int cameraFps,
    int soundMode, int soundKey, const std::string& soundScale,
    int soundDecay, int soundRelease, int soundMoveThresh,
    const std::string& soundQuantize, int soundVolume, int soundTempo, bool showDepth)
{
    return
        std::string("  \"thresholdMm\": ") + std::to_string(thresholdMm) + ",\n"
        "  \"thresholdEnabled\": " + (thresholdEnabled ? "true" : "false") + ",\n"
        "  \"dilateIterations\": " + std::to_string(dilateIterations) + ",\n"
        "  \"blobDetectEnabled\": " + (blobDetectEnabled ? "true" : "false") + ",\n"
        "  \"maxBlobPixels\": " + std::to_string(maxBlobPixels) + ",\n"
        "  \"minBlobPixels\": " + std::to_string(minBlobPixels) + ",\n"
        "  \"cameraFps\": " + std::to_string(cameraFps) + ",\n"
        "  \"soundMode\": " + std::to_string(soundMode) + ",\n"
        "  \"soundKey\": " + std::to_string(soundKey) + ",\n"
        "  \"soundScale\": \"" + soundScale + "\",\n"
        "  \"soundDecay\": " + std::to_string(soundDecay) + ",\n"
        "  \"soundRelease\": " + std::to_string(soundRelease) + ",\n"
        "  \"soundMoveThresh\": " + std::to_string(soundMoveThresh) + ",\n"
        "  \"soundQuantize\": \"" + soundQuantize + "\",\n"
        "  \"soundVolume\": " + std::to_string(soundVolume) + ",\n"
        "  \"soundTempo\": " + std::to_string(soundTempo) + ",\n"
        "  \"showDepth\": " + (showDepth ? "true" : "false");
}

// Parse the shared portion of settings from a JSON text string.
inline void loadSharedSettings(const std::string& text,
    std::atomic<int>& thresholdMm, std::atomic<bool>& thresholdEnabled,
    std::atomic<int>& dilateIterations, std::atomic<bool>& blobDetectEnabled,
    std::atomic<int>& maxBlobPixels, std::atomic<int>& minBlobPixels,
    std::atomic<int>& cameraFps,
    std::atomic<int>& soundMode, std::atomic<int>& soundKey,
    std::atomic<int>& soundDecay, std::atomic<int>& soundRelease,
    std::atomic<int>& soundMoveThresh,
    std::atomic<int>& soundVolume, std::atomic<int>& soundTempo,
    std::atomic<bool>& showDepth)
{
    int iv; bool bv;
    if (jsonInt(text, "thresholdMm", iv)) thresholdMm.store(iv);
    if (jsonBool(text, "thresholdEnabled", bv)) thresholdEnabled.store(bv);
    if (jsonInt(text, "dilateIterations", iv)) dilateIterations.store(iv);
    if (jsonBool(text, "blobDetectEnabled", bv)) blobDetectEnabled.store(bv);
    if (jsonInt(text, "maxBlobPixels", iv)) maxBlobPixels.store(iv);
    if (jsonInt(text, "minBlobPixels", iv)) minBlobPixels.store(iv);
    if (jsonInt(text, "cameraFps", iv)) cameraFps.store(iv);
    if (jsonInt(text, "soundMode", iv)) soundMode.store(iv);
    if (jsonInt(text, "soundKey", iv)) soundKey.store(iv);
    if (jsonInt(text, "soundDecay", iv)) soundDecay.store(iv);
    if (jsonInt(text, "soundRelease", iv)) soundRelease.store(iv);
    if (jsonInt(text, "soundMoveThresh", iv)) soundMoveThresh.store(iv);
    if (jsonInt(text, "soundVolume", iv)) soundVolume.store(iv);
    if (jsonInt(text, "soundTempo", iv)) soundTempo.store(iv);
    if (jsonBool(text, "showDepth", bv)) showDepth.store(bv);
    // Note: soundScale_ and soundQuantize_ (strings) are loaded by the caller
    // under its own mutex, since the mutex differs between platforms.
}
