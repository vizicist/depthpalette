#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Minimal post-processing settings for Orbbec (filters will be added later)
struct PostProcSettings {
    bool thresholdFilterEnable = true;
    int thresholdFilterMin = 0;     // mm
    int thresholdFilterMax = 1200;  // mm
};

// All Orbbec device properties controllable from the web UI
struct DeviceSettings {
    // Group 1: On-device speckle removal
    bool speckleEnable = false;
    int speckleMaxSize = 200;
    int speckleMaxDiff = 64;

    // Group 2: Hardware depth range (mm)
    int hwDepthMin = 100;
    int hwDepthMax = 10000;

    // Group 3: Confidence filter
    bool confidenceEnable = true;
    int confidenceThreshold = 15;

    // Group 4: Depth work mode / preset (name-based, restart required)
    std::string depthWorkMode;  // empty = don't change

    // Group 5: Laser
    bool laserEnable = true;
    int laserPower = -1;  // -1 = default

    // Group 6: Hole filling
    bool holeFillEnable = false;

    // Group 7: Depth exposure
    bool depthAutoExposure = true;
    int depthExposure = 8330;  // us
    int depthGain = 16;

    // Group 8: Disparity search range (restart required)
    int disparityRange = 128;  // 128 or 256

    // Group 9: Mirror/flip
    bool depthMirror = false;
    bool depthFlip = false;
    bool colorMirror = false;
    bool colorFlip = false;

    // Group 10: Depth precision level
    int depthPrecisionLevel = -1;  // -1 = don't set

    // Group 11: HDR merge
    bool hdrMerge = false;

    // Group 12: Color controls
    bool colorAutoExposure = true;
    int colorExposure = 156;
    int colorGain = 0;
    bool colorAutoWhiteBalance = true;
    int colorWhiteBalance = 4600;
    int colorBrightness = 0;
    int colorSharpness = 0;
    int colorSaturation = 0;
    int colorContrast = 0;
    int colorGamma = 0;
};

// Range info for a single integer property
struct PropertyRange {
    bool supported = false;
    int min = 0;
    int max = 0;
    int step = 1;
    int def = 0;
    int cur = 0;
};

// Capabilities queried from the device at startup
struct DeviceCaps {
    // Integer property ranges
    PropertyRange speckleMaxSize;
    PropertyRange speckleMaxDiff;
    PropertyRange hwDepthMin;
    PropertyRange hwDepthMax;
    PropertyRange confidenceThreshold;
    PropertyRange laserPower;
    PropertyRange depthExposure;
    PropertyRange depthGain;
    PropertyRange disparityRange;
    PropertyRange depthPrecisionLevel;
    PropertyRange colorExposure;
    PropertyRange colorGain;
    PropertyRange colorWhiteBalance;
    PropertyRange colorBrightness;
    PropertyRange colorSharpness;
    PropertyRange colorSaturation;
    PropertyRange colorContrast;
    PropertyRange colorGamma;

    // Bool property support flags
    bool speckleSupported = false;
    bool confidenceSupported = false;
    bool holeFillSupported = false;
    bool depthMirrorSupported = false;
    bool depthFlipSupported = false;
    bool colorMirrorSupported = false;
    bool colorFlipSupported = false;
    bool hdrMergeSupported = false;
    bool depthAutoExposureSupported = false;
    bool colorAutoExposureSupported = false;
    bool colorAutoWBSupported = false;
    bool laserEnableSupported = false;

    // Available depth work modes (presets)
    std::vector<std::string> workModes;
    std::string currentWorkMode;
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
              std::atomic<bool>& configDirty,
              std::atomic<bool>& restartRequested,
              std::atomic<int>& depthResolution,
              std::atomic<int>& cameraFps,
              std::atomic<bool>& devicePropsDirty,
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

    // Device settings access (thread-safe).
    DeviceSettings getDeviceSettings();
    void setDeviceCaps(const DeviceCaps& caps);

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
    std::atomic<bool>& configDirty_;
    std::atomic<bool>& restartRequested_;
    std::atomic<int>& depthResolution_;
    std::atomic<int>& cameraFps_;
    std::atomic<bool>& devicePropsDirty_;

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

    std::mutex devSettingsMtx_;
    DeviceSettings devSettings_;
    DeviceCaps devCaps_;

    // Sound / UI settings (persisted to settings.json)
    std::atomic<int> soundMode_{0};
    std::atomic<int> soundKey_{0};
    std::atomic<int> soundDecay_{20};
    std::atomic<int> soundRelease_{5};
    std::atomic<int> soundMoveThresh_{0};
    std::atomic<int> soundVolume_{50};
    std::atomic<int> soundTempo_{120};
    std::atomic<bool> showDepth_{false};
    std::string soundScale_{"chromatic"};   // guarded by devSettingsMtx_
    std::string soundQuantize_{"0"};        // guarded by devSettingsMtx_

    bool colorEnabled_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
