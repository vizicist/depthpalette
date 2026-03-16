#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Post-processing filter settings (shared between web server and main loop)
struct PostProcSettings {
    int medianKernel = 7;       // 0=OFF, 3, 5, 7
    bool spatialEnable = false;
    int spatialAlpha = 50;      // 0..100 → 0.0..1.0
    int spatialDelta = 0;       // 0=auto
    int spatialIter = 1;        // 1..5
    bool temporalEnable = false;
    int temporalAlpha = 40;     // 0..100 → 0.0..1.0
    int temporalDelta = 0;      // 0=auto
    int temporalPersistency = 3; // PersistencyMode enum 0..8
    bool speckleEnable = false;
    int speckleRange = 50;      // 0..240
    int speckleDiff = 2;        // disparity integer levels
    int decimationFactor = 1;   // 1..4
    int decimationMode = 0;     // 0=PIXEL_SKIPPING, 1=NON_ZERO_MEDIAN, 2=NON_ZERO_MEAN
    // Stereo algorithm
    bool subpixelEnable = false;
    int subpixelBits = 3;       // 3, 4, or 5
    int disparityShift = 0;     // 0..128
    int lrCheckThreshold = 10;  // 0..128
    // Additional post-processing
    int brightnessFilterMin = 0;    // 0..255
    int brightnessFilterMax = 256;  // 0..256
    bool thresholdFilterEnable = true;
    int thresholdFilterMin = 0;     // mm
    int thresholdFilterMax = 1200;  // mm
    int spatialHoleFillingRadius = 2; // 0..16
    // Camera controls
    int lumaDenoise = 1;        // 0..4
    int antiBanding = 0;        // 0=OFF, 1=50Hz, 2=60Hz, 3=AUTO
    int aeCompensation = 0;     // -9..+9
};

class WebServer {
public:
    WebServer(std::atomic<int>& thresholdMm,
              std::atomic<bool>& thresholdEnabled,
              std::atomic<int>& dilateIterations,
              std::atomic<bool>& blobDetectEnabled,
              std::atomic<int>& maxBlobPixels,
              std::atomic<int>& minBlobPixels,
              std::atomic<int>& fpsTenths,
              std::atomic<int>& confidenceThreshold,
              std::atomic<bool>& extendedDisparity,
              std::atomic<int>& stereoPreset,
              std::atomic<bool>& configDirty,
              std::atomic<bool>& restartRequested,
              std::atomic<int>& monoResolution,
              std::atomic<int>& cameraFps,
              bool colorEnabled = true);
    ~WebServer();

    // Start the HTTP server on a background thread (127.0.0.1:8080).
    void start();

    // Stop the server and join the thread.
    void stop();

    // Update the shared frame buffers (called from the main/camera thread).
    void updateColorFrame(const uint8_t* bgr, int width, int height);
    void updateDepthFrame(const uint8_t* bgr, int width, int height);
    void updateBlobs(const std::string& json);

    // Read current post-processing settings (thread-safe copy).
    PostProcSettings getPostProcSettings();

    // Set device info (call after device creation).
    void setDeviceInfo(const std::string& connectionType, const std::string& deviceName,
                       const std::string& mxId);

    // Load settings from settings.json (call before start()).
    void loadSettings();

    // Save all current settings to settings.json.
    void saveSettings();

private:
    void run();

    std::atomic<int>& thresholdMm_;
    std::atomic<bool>& thresholdEnabled_;
    std::atomic<int>& dilateIterations_;
    std::atomic<bool>& blobDetectEnabled_;
    std::atomic<int>& maxBlobPixels_;
    std::atomic<int>& minBlobPixels_;
    std::atomic<int>& fpsTenths_;
    std::atomic<int>& confidenceThreshold_;
    std::atomic<bool>& extendedDisparity_;
    std::atomic<int>& stereoPreset_;
    std::atomic<bool>& configDirty_;
    std::atomic<bool>& restartRequested_;
    std::atomic<int>& monoResolution_;
    std::atomic<int>& cameraFps_;

    std::mutex frameMtx_;
    std::vector<uint8_t> colorBgr_;
    int colorW_ = 0;
    int colorH_ = 0;
    std::vector<uint8_t> depthBgr_;
    int depthW_ = 0;
    int depthH_ = 0;

    std::string blobsJson_;
    std::condition_variable blobsCv_;
    int blobsSeq_ = 0;

    std::condition_variable depthCv_;
    int depthSeq_ = 0;
    std::condition_variable colorCv_;
    int colorSeq_ = 0;

    std::mutex postProcMtx_;
    PostProcSettings postProc_;

    // Sound / UI settings (persisted to settings.json)
    std::atomic<int> soundMode_{0};
    std::atomic<int> soundKey_{0};
    std::atomic<int> soundDecay_{20};
    std::atomic<int> soundRelease_{5};
    std::atomic<int> soundMoveThresh_{0};
    std::atomic<int> soundVolume_{50};
    std::atomic<int> soundTempo_{120};
    std::atomic<bool> showDepth_{false};
    std::string soundScale_{"chromatic"};   // guarded by postProcMtx_
    std::string soundQuantize_{"0"};        // guarded by postProcMtx_

    // Device info (guarded by postProcMtx_)
    std::string connectionType_;
    std::string deviceName_;
    std::string mxId_;

    bool colorEnabled_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
