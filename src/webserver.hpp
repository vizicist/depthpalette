#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class WebServer {
public:
    WebServer(std::atomic<int>& thresholdMm,
              std::atomic<bool>& blobDetectEnabled,
              std::atomic<int>& maxBlobPixels,
              std::atomic<int>& fpsTenths);
    ~WebServer();

    // Start the HTTP server on a background thread (127.0.0.1:8080).
    void start();

    // Stop the server and join the thread.
    void stop();

    // Update the shared frame buffers (called from the main/camera thread).
    void updateColorFrame(const uint8_t* bgr, int width, int height);
    void updateDepthFrame(const uint8_t* bgr, int width, int height);

private:
    void run();

    // Build a 24-bit BMP from a top-down packed BGR buffer.
    static std::vector<uint8_t> makeBmp(const uint8_t* bgr, int width, int height);

    std::atomic<int>& thresholdMm_;
    std::atomic<bool>& blobDetectEnabled_;
    std::atomic<int>& maxBlobPixels_;
    std::atomic<int>& fpsTenths_;

    std::mutex frameMtx_;
    std::vector<uint8_t> colorBgr_;
    int colorW_ = 0;
    int colorH_ = 0;
    std::vector<uint8_t> depthBgr_;
    int depthW_ = 0;
    int depthH_ = 0;

    std::thread thread_;
    std::atomic<bool> running_{false};
};
