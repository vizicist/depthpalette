#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <libobsensor/ObSensor.hpp>

#include "blobdetect.hpp"
#include "blobtracker.hpp"
#include "depthcolor.hpp"
#include "viewer.hpp"
#include "webserver.hpp"

// Shared settings — adjustable from the web UI  
static std::atomic<int> g_thresholdMm{500};
static std::atomic<bool> g_thresholdEnabled{true};
static std::atomic<int> g_dilateIterations{0};
static std::atomic<bool> g_blobDetectEnabled{true};
static std::atomic<int> g_maxBlobPixels{5000};
static std::atomic<int> g_minBlobPixels{20};
static std::atomic<int> g_fpsTenths{0};  // FPS x 10 (e.g. 145 = 14.5 fps)
static std::atomic<bool> g_configDirty{false};
static std::atomic<bool> g_restartRequested{false};
static std::atomic<int> g_depthResolution{0};   // 0=1280x800, 1=848x480
static std::atomic<int> g_cameraFps{30};
static std::atomic<bool> g_devicePropsDirty{false};

// Helper: query an int property range, returning a PropertyRange
static PropertyRange queryIntRange(std::shared_ptr<ob::Device> device, OBPropertyID prop) {
    PropertyRange r;
    try {
        r.supported = device->isPropertySupported(prop, OB_PERMISSION_READ_WRITE);
        if (r.supported) {
            auto range = device->getIntPropertyRange(prop);
            r.min = range.min;
            r.max = range.max;
            r.step = range.step;
            r.def = range.def;
            r.cur = range.cur;
        }
    } catch (...) {
        r.supported = false;
    }
    return r;
}

// Helper: check if a bool property is supported
static bool queryBoolSupported(std::shared_ptr<ob::Device> device, OBPropertyID prop) {
    try {
        return device->isPropertySupported(prop, OB_PERMISSION_READ_WRITE);
    } catch (...) {
        return false;
    }
}

// Helper: safely read a bool property
static bool readBoolProp(std::shared_ptr<ob::Device> device, OBPropertyID prop, bool fallback) {
    try {
        if (device->isPropertySupported(prop, OB_PERMISSION_READ))
            return device->getBoolProperty(prop);
    } catch (...) {}
    return fallback;
}

// Helper: safely read an int property
static int readIntProp(std::shared_ptr<ob::Device> device, OBPropertyID prop, int fallback) {
    try {
        if (device->isPropertySupported(prop, OB_PERMISSION_READ))
            return device->getIntProperty(prop);
    } catch (...) {}
    return fallback;
}

// Query all device capabilities
static DeviceCaps queryCapabilities(std::shared_ptr<ob::Device> device) {
    DeviceCaps caps;

    // Integer property ranges
    caps.speckleMaxSize = queryIntRange(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_SPECKLE_SIZE_INT);
    caps.speckleMaxDiff = queryIntRange(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_DIFF_INT);
    caps.hwDepthMin = queryIntRange(device, OB_PROP_MIN_DEPTH_INT);
    caps.hwDepthMax = queryIntRange(device, OB_PROP_MAX_DEPTH_INT);
    caps.confidenceThreshold = queryIntRange(device, OB_PROP_CONFIDENCE_STREAM_FILTER_THRESHOLD_INT);
    caps.laserPower = queryIntRange(device, OB_PROP_LASER_POWER_LEVEL_CONTROL_INT);
    caps.depthExposure = queryIntRange(device, OB_PROP_DEPTH_EXPOSURE_INT);
    caps.depthGain = queryIntRange(device, OB_PROP_DEPTH_GAIN_INT);
    caps.disparityRange = queryIntRange(device, OB_PROP_DISP_SEARCH_RANGE_MODE_INT);
    caps.depthPrecisionLevel = queryIntRange(device, OB_PROP_DEPTH_PRECISION_LEVEL_INT);
    caps.colorExposure = queryIntRange(device, OB_PROP_COLOR_EXPOSURE_INT);
    caps.colorGain = queryIntRange(device, OB_PROP_COLOR_GAIN_INT);
    caps.colorWhiteBalance = queryIntRange(device, OB_PROP_COLOR_WHITE_BALANCE_INT);
    caps.colorBrightness = queryIntRange(device, OB_PROP_COLOR_BRIGHTNESS_INT);
    caps.colorSharpness = queryIntRange(device, OB_PROP_COLOR_SHARPNESS_INT);
    caps.colorSaturation = queryIntRange(device, OB_PROP_COLOR_SATURATION_INT);
    caps.colorContrast = queryIntRange(device, OB_PROP_COLOR_CONTRAST_INT);
    caps.colorGamma = queryIntRange(device, OB_PROP_COLOR_GAMMA_INT);

    // Bool property support
    caps.speckleSupported = queryBoolSupported(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_BOOL);
    caps.confidenceSupported = queryBoolSupported(device, OB_PROP_CONFIDENCE_STREAM_FILTER_BOOL);
    caps.holeFillSupported = queryBoolSupported(device, OB_PROP_DEPTH_HOLEFILTER_BOOL);
    caps.depthMirrorSupported = queryBoolSupported(device, OB_PROP_DEPTH_MIRROR_BOOL);
    caps.depthFlipSupported = queryBoolSupported(device, OB_PROP_DEPTH_FLIP_BOOL);
    caps.colorMirrorSupported = queryBoolSupported(device, OB_PROP_COLOR_MIRROR_BOOL);
    caps.colorFlipSupported = queryBoolSupported(device, OB_PROP_COLOR_FLIP_BOOL);
    caps.hdrMergeSupported = queryBoolSupported(device, OB_PROP_HDR_MERGE_BOOL);
    caps.depthAutoExposureSupported = queryBoolSupported(device, OB_PROP_DEPTH_AUTO_EXPOSURE_BOOL);
    caps.colorAutoExposureSupported = queryBoolSupported(device, OB_PROP_COLOR_AUTO_EXPOSURE_BOOL);
    caps.colorAutoWBSupported = queryBoolSupported(device, OB_PROP_COLOR_AUTO_WHITE_BALANCE_BOOL);
    caps.laserEnableSupported = queryBoolSupported(device, OB_PROP_LASER_BOOL);

    // Depth work modes (presets)
    try {
        auto modeList = device->getDepthWorkModeList();
        auto currentMode = device->getCurrentDepthWorkMode();
        caps.currentWorkMode = currentMode.name;
        for (uint32_t i = 0; i < modeList->getCount(); i++) {
            auto mode = modeList->getOBDepthWorkMode(i);
            caps.workModes.push_back(mode.name);
        }
    } catch (...) {}

    return caps;
}

// Apply device settings to the hardware
static void applyDeviceSettings(std::shared_ptr<ob::Device> device, const DeviceSettings& s,
                                const DeviceCaps& caps) {
    auto trySetBool = [&](OBPropertyID prop, bool val, bool supported) {
        if (!supported) return;
        try { device->setBoolProperty(prop, val); }
        catch (const std::exception& e) {
            std::cerr << "Warning: setBoolProperty(" << prop << ") failed: " << e.what() << std::endl;
        }
    };

    auto trySetInt = [&](OBPropertyID prop, int val, const PropertyRange& range) {
        if (!range.supported) return;
        if (val < range.min) val = range.min;
        if (val > range.max) val = range.max;
        try { device->setIntProperty(prop, val); }
        catch (const std::exception& e) {
            std::cerr << "Warning: setIntProperty(" << prop << ") failed: " << e.what() << std::endl;
        }
    };

    // Group 1: Speckle filter
    trySetBool(OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_BOOL, s.speckleEnable, caps.speckleSupported);
    trySetInt(OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_SPECKLE_SIZE_INT, s.speckleMaxSize, caps.speckleMaxSize);
    trySetInt(OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_DIFF_INT, s.speckleMaxDiff, caps.speckleMaxDiff);

    // Group 2: Hardware depth range
    trySetInt(OB_PROP_MIN_DEPTH_INT, s.hwDepthMin, caps.hwDepthMin);
    trySetInt(OB_PROP_MAX_DEPTH_INT, s.hwDepthMax, caps.hwDepthMax);

    // Group 3: Confidence filter
    trySetBool(OB_PROP_CONFIDENCE_STREAM_FILTER_BOOL, s.confidenceEnable, caps.confidenceSupported);
    trySetInt(OB_PROP_CONFIDENCE_STREAM_FILTER_THRESHOLD_INT, s.confidenceThreshold, caps.confidenceThreshold);

    // Group 5: Laser
    trySetBool(OB_PROP_LASER_BOOL, s.laserEnable, caps.laserEnableSupported);
    if (s.laserPower >= 0) {
        trySetInt(OB_PROP_LASER_POWER_LEVEL_CONTROL_INT, s.laserPower, caps.laserPower);
    }

    // Group 6: Hole filling
    trySetBool(OB_PROP_DEPTH_HOLEFILTER_BOOL, s.holeFillEnable, caps.holeFillSupported);

    // Group 7: Depth exposure
    trySetBool(OB_PROP_DEPTH_AUTO_EXPOSURE_BOOL, s.depthAutoExposure, caps.depthAutoExposureSupported);
    if (!s.depthAutoExposure) {
        trySetInt(OB_PROP_DEPTH_EXPOSURE_INT, s.depthExposure, caps.depthExposure);
        trySetInt(OB_PROP_DEPTH_GAIN_INT, s.depthGain, caps.depthGain);
    }

    // Group 9: Mirror/flip
    trySetBool(OB_PROP_DEPTH_MIRROR_BOOL, s.depthMirror, caps.depthMirrorSupported);
    trySetBool(OB_PROP_DEPTH_FLIP_BOOL, s.depthFlip, caps.depthFlipSupported);
    trySetBool(OB_PROP_COLOR_MIRROR_BOOL, s.colorMirror, caps.colorMirrorSupported);
    trySetBool(OB_PROP_COLOR_FLIP_BOOL, s.colorFlip, caps.colorFlipSupported);

    // Group 10: Depth precision level
    if (s.depthPrecisionLevel >= 0) {
        trySetInt(OB_PROP_DEPTH_PRECISION_LEVEL_INT, s.depthPrecisionLevel, caps.depthPrecisionLevel);
    }

    // Group 11: HDR merge
    trySetBool(OB_PROP_HDR_MERGE_BOOL, s.hdrMerge, caps.hdrMergeSupported);

    // Group 12: Color controls
    trySetBool(OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, s.colorAutoExposure, caps.colorAutoExposureSupported);
    if (!s.colorAutoExposure) {
        trySetInt(OB_PROP_COLOR_EXPOSURE_INT, s.colorExposure, caps.colorExposure);
        trySetInt(OB_PROP_COLOR_GAIN_INT, s.colorGain, caps.colorGain);
    }
    trySetBool(OB_PROP_COLOR_AUTO_WHITE_BALANCE_BOOL, s.colorAutoWhiteBalance, caps.colorAutoWBSupported);
    if (!s.colorAutoWhiteBalance) {
        trySetInt(OB_PROP_COLOR_WHITE_BALANCE_INT, s.colorWhiteBalance, caps.colorWhiteBalance);
    }
    trySetInt(OB_PROP_COLOR_BRIGHTNESS_INT, s.colorBrightness, caps.colorBrightness);
    trySetInt(OB_PROP_COLOR_SHARPNESS_INT, s.colorSharpness, caps.colorSharpness);
    trySetInt(OB_PROP_COLOR_SATURATION_INT, s.colorSaturation, caps.colorSaturation);
    trySetInt(OB_PROP_COLOR_CONTRAST_INT, s.colorContrast, caps.colorContrast);
    trySetInt(OB_PROP_COLOR_GAMMA_INT, s.colorGamma, caps.colorGamma);
}

// Read current values from device into DeviceSettings
static void readCurrentSettings(std::shared_ptr<ob::Device> device, DeviceSettings& s,
                                const DeviceCaps& caps) {
    // Bools
    if (caps.speckleSupported)
        s.speckleEnable = readBoolProp(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_BOOL, s.speckleEnable);
    if (caps.confidenceSupported)
        s.confidenceEnable = readBoolProp(device, OB_PROP_CONFIDENCE_STREAM_FILTER_BOOL, s.confidenceEnable);
    if (caps.holeFillSupported)
        s.holeFillEnable = readBoolProp(device, OB_PROP_DEPTH_HOLEFILTER_BOOL, s.holeFillEnable);
    if (caps.depthAutoExposureSupported)
        s.depthAutoExposure = readBoolProp(device, OB_PROP_DEPTH_AUTO_EXPOSURE_BOOL, s.depthAutoExposure);
    if (caps.depthMirrorSupported)
        s.depthMirror = readBoolProp(device, OB_PROP_DEPTH_MIRROR_BOOL, s.depthMirror);
    if (caps.depthFlipSupported)
        s.depthFlip = readBoolProp(device, OB_PROP_DEPTH_FLIP_BOOL, s.depthFlip);
    if (caps.colorMirrorSupported)
        s.colorMirror = readBoolProp(device, OB_PROP_COLOR_MIRROR_BOOL, s.colorMirror);
    if (caps.colorFlipSupported)
        s.colorFlip = readBoolProp(device, OB_PROP_COLOR_FLIP_BOOL, s.colorFlip);
    if (caps.hdrMergeSupported)
        s.hdrMerge = readBoolProp(device, OB_PROP_HDR_MERGE_BOOL, s.hdrMerge);
    if (caps.laserEnableSupported)
        s.laserEnable = readBoolProp(device, OB_PROP_LASER_BOOL, s.laserEnable);
    if (caps.colorAutoExposureSupported)
        s.colorAutoExposure = readBoolProp(device, OB_PROP_COLOR_AUTO_EXPOSURE_BOOL, s.colorAutoExposure);
    if (caps.colorAutoWBSupported)
        s.colorAutoWhiteBalance = readBoolProp(device, OB_PROP_COLOR_AUTO_WHITE_BALANCE_BOOL, s.colorAutoWhiteBalance);

    // Ints
    if (caps.speckleMaxSize.supported)
        s.speckleMaxSize = readIntProp(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_SPECKLE_SIZE_INT, s.speckleMaxSize);
    if (caps.speckleMaxDiff.supported)
        s.speckleMaxDiff = readIntProp(device, OB_PROP_DEPTH_NOISE_REMOVAL_FILTER_MAX_DIFF_INT, s.speckleMaxDiff);
    if (caps.hwDepthMin.supported)
        s.hwDepthMin = readIntProp(device, OB_PROP_MIN_DEPTH_INT, s.hwDepthMin);
    if (caps.hwDepthMax.supported)
        s.hwDepthMax = readIntProp(device, OB_PROP_MAX_DEPTH_INT, s.hwDepthMax);
    if (caps.confidenceThreshold.supported)
        s.confidenceThreshold = readIntProp(device, OB_PROP_CONFIDENCE_STREAM_FILTER_THRESHOLD_INT, s.confidenceThreshold);
    if (caps.laserPower.supported)
        s.laserPower = readIntProp(device, OB_PROP_LASER_POWER_LEVEL_CONTROL_INT, s.laserPower);
    if (caps.depthExposure.supported)
        s.depthExposure = readIntProp(device, OB_PROP_DEPTH_EXPOSURE_INT, s.depthExposure);
    if (caps.depthGain.supported)
        s.depthGain = readIntProp(device, OB_PROP_DEPTH_GAIN_INT, s.depthGain);
    if (caps.disparityRange.supported)
        s.disparityRange = readIntProp(device, OB_PROP_DISP_SEARCH_RANGE_MODE_INT, s.disparityRange);
    if (caps.depthPrecisionLevel.supported)
        s.depthPrecisionLevel = readIntProp(device, OB_PROP_DEPTH_PRECISION_LEVEL_INT, s.depthPrecisionLevel);
    if (caps.colorExposure.supported)
        s.colorExposure = readIntProp(device, OB_PROP_COLOR_EXPOSURE_INT, s.colorExposure);
    if (caps.colorGain.supported)
        s.colorGain = readIntProp(device, OB_PROP_COLOR_GAIN_INT, s.colorGain);
    if (caps.colorWhiteBalance.supported)
        s.colorWhiteBalance = readIntProp(device, OB_PROP_COLOR_WHITE_BALANCE_INT, s.colorWhiteBalance);
    if (caps.colorBrightness.supported)
        s.colorBrightness = readIntProp(device, OB_PROP_COLOR_BRIGHTNESS_INT, s.colorBrightness);
    if (caps.colorSharpness.supported)
        s.colorSharpness = readIntProp(device, OB_PROP_COLOR_SHARPNESS_INT, s.colorSharpness);
    if (caps.colorSaturation.supported)
        s.colorSaturation = readIntProp(device, OB_PROP_COLOR_SATURATION_INT, s.colorSaturation);
    if (caps.colorContrast.supported)
        s.colorContrast = readIntProp(device, OB_PROP_COLOR_CONTRAST_INT, s.colorContrast);
    if (caps.colorGamma.supported)
        s.colorGamma = readIntProp(device, OB_PROP_COLOR_GAMMA_INT, s.colorGamma);

    // Work mode
    try {
        auto currentMode = device->getCurrentDepthWorkMode();
        s.depthWorkMode = currentMode.name;
    } catch (...) {}
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    bool showWindow = false;
    bool showColor = false;
    bool showWeb = true;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--window") == 0 || std::strcmp(argv[i], "-w") == 0) {
            showWindow = true;
        } else if (std::strcmp(argv[i], "--color") == 0 || std::strcmp(argv[i], "-c") == 0) {
            showColor = true;
        } else if (std::strcmp(argv[i], "--no-web") == 0) {
            showWeb = false;
        } else if (std::strcmp(argv[i], "--no-blob") == 0) {
            g_blobDetectEnabled.store(false);
        }
    }

    // Optional Win32 viewer window
    ImageViewer viewer;

    // Start web server (optional) — persists across pipeline restarts
    WebServer webServer(g_thresholdMm, g_thresholdEnabled, g_dilateIterations,
                        g_blobDetectEnabled, g_maxBlobPixels, g_minBlobPixels,
                        g_fpsTenths, g_configDirty, g_restartRequested,
                        g_depthResolution, g_cameraFps, g_devicePropsDirty,
                        showColor);
    webServer.loadSettings();
    if (showWeb) webServer.start();

    auto programStart = std::chrono::steady_clock::now();

    // Outer loop: recreate pipeline when settings require restart
    while (true) {
        g_restartRequested.store(false);
        g_configDirty.store(false);
        g_devicePropsDirty.store(false);

        int resIdx = g_depthResolution.load();
        int camFps = g_cameraFps.load();

        // Determine resolution from index
        int reqW = 1280, reqH = 800;
        if (resIdx == 1) { reqW = 848; reqH = 480; }

        std::cout << "Creating Orbbec pipeline..."
                  << " res=" << reqW << "x" << reqH
                  << " fps=" << camFps << std::endl;

        ob::Pipeline pipe;

        // Switch depth work mode before configuring streams (if requested)
        {
            auto devSettings = webServer.getDeviceSettings();
            if (!devSettings.depthWorkMode.empty()) {
                try {
                    auto device = pipe.getDevice();
                    auto currentMode = device->getCurrentDepthWorkMode();
                    if (devSettings.depthWorkMode != currentMode.name) {
                        std::cout << "Switching depth work mode to: " << devSettings.depthWorkMode << std::endl;
                        device->switchDepthWorkMode(devSettings.depthWorkMode.c_str());
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Warning: switchDepthWorkMode failed: " << e.what() << std::endl;
                }
            }
        }

        auto config = std::make_shared<ob::Config>();
        config->enableVideoStream(OB_STREAM_DEPTH, reqW, reqH, camFps, OB_FORMAT_Y16);
        if (showColor) {
            config->enableVideoStream(OB_STREAM_COLOR, OB_WIDTH_ANY, OB_HEIGHT_ANY,
                                      camFps, OB_FORMAT_RGB);
        }

        std::cout << "Starting pipeline..." << std::endl;
        pipe.start(config);

        // Get device handle and query capabilities
        auto device = pipe.getDevice();
        auto caps = queryCapabilities(device);
        webServer.setDeviceCaps(caps);

        // Read current device settings from hardware
        {
            auto devSettings = webServer.getDeviceSettings();
            readCurrentSettings(device, devSettings, caps);
            // Write back the read values
            {
                // We need to update the webserver's copy — use a small hack:
                // set via the webserver's internal lock by re-getting/setting
            }
        }

        // Apply persisted settings to device
        applyDeviceSettings(device, webServer.getDeviceSettings(), caps);

        // Wait for first depth frame
        std::cout << "Waiting for first frames..." << std::endl;
        std::shared_ptr<ob::FrameSet> firstFrameSet;
        while (!firstFrameSet) {
            firstFrameSet = pipe.waitForFrameset(1000);
        }

        auto firstDepthRaw = firstFrameSet->getFrame(OB_FRAME_DEPTH);
        if (!firstDepthRaw) {
            std::cerr << "No depth frame in first frameset!" << std::endl;
            pipe.stop();
            continue;
        }
        auto firstDepth = firstDepthRaw->as<ob::DepthFrame>();
        int depthW = firstDepth->getWidth();
        int depthH = firstDepth->getHeight();
        float depthScale = firstDepth->getValueScale();
        std::cout << "Depth: " << depthW << "x" << depthH
                  << " scale=" << depthScale << std::endl;

        // Color stream (optional)
        int colorW = 0, colorH = 0;
        if (showColor) {
            auto firstColorRaw = firstFrameSet->getFrame(OB_FRAME_COLOR);
            if (firstColorRaw) {
                auto firstColor = firstColorRaw->as<ob::ColorFrame>();
                colorW = firstColor->getWidth();
                colorH = firstColor->getHeight();
                std::cout << "Color: " << colorW << "x" << colorH << std::endl;
            } else {
                std::cerr << "Warning: no color frame in first frameset" << std::endl;
            }
        }

        if (showWindow && !viewer.isInitialized()) {
            int windowW = showColor ? colorW + depthW : depthW;
            int windowH = showColor ? std::max(colorH, depthH) : depthH;
            if (!viewer.initialize("DepthPalette (Orbbec)", windowW, windowH)) {
                std::cerr << "Failed to create viewer window" << std::endl;
                pipe.stop();
                if (showWeb) webServer.stop();
                return 1;
            }
        }

        // Reusable buffers
        std::vector<uint8_t> colorBgr;
        if (showColor && colorW > 0) colorBgr.resize(colorW * colorH * 3);
        std::vector<uint8_t> depthBgr(depthW * depthH * 3);
        std::vector<uint16_t> depthMm(depthW * depthH);  // depth in mm

        // Helper: convert raw depth to millimeters
        auto convertDepthToMm = [&](const uint16_t* raw, int count, float scale) {
            if (scale == 1.0f) {
                std::memcpy(depthMm.data(), raw, count * sizeof(uint16_t));
            } else {
                for (int i = 0; i < count; i++) {
                    float mm = raw[i] * scale;
                    depthMm[i] = (mm > 65535.0f) ? uint16_t(65535) : static_cast<uint16_t>(mm);
                }
            }
        };

        // Process first frame
        {
            const auto* rawDepth = reinterpret_cast<const uint16_t*>(firstDepth->getData());
            convertDepthToMm(rawDepth, depthW * depthH, depthScale);

            uint16_t thr = g_thresholdEnabled.load()
                ? static_cast<uint16_t>(g_thresholdMm.load()) : uint16_t(65535);
            depthToThresholdBgr(depthMm.data(), depthW, depthH, depthBgr.data(), thr);
            dilateBinaryBgr(depthBgr.data(), depthW, depthH, g_dilateIterations.load());

            if (g_blobDetectEnabled.load())
                detectAndDrawBlobs(depthBgr.data(), depthW, depthH,
                                   g_maxBlobPixels.load(), depthMm.data(),
                                   g_minBlobPixels.load());

            if (showColor && colorW > 0) {
                auto firstColorRaw = firstFrameSet->getFrame(OB_FRAME_COLOR);
                if (firstColorRaw) {
                    auto firstColor = firstColorRaw->as<ob::ColorFrame>();
                    const auto* rgbData = reinterpret_cast<const uint8_t*>(firstColor->getData());
                    packedRgbToPackedBgr(rgbData, colorW, colorH, colorBgr.data());
                    if (showWeb) webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
                }
            }

            if (showWindow) {
                if (showColor && colorW > 0)
                    viewer.updateSideBySide(colorBgr.data(), colorW, colorH,
                                            depthBgr.data(), depthW, depthH);
                else
                    viewer.updateSingle(depthBgr.data(), depthW, depthH);
            }

            if (showWeb) webServer.updateDepthFrame(depthBgr.data(), depthW, depthH);
        }

        std::cout << "Streaming... " << (showWindow ? "Close the window or press Ctrl+C to exit."
                                                    : "Press Ctrl+C to exit.") << std::endl;
        if (showWeb) std::cout << "Web UI: http://127.0.0.1:8080" << std::endl;

        // Running condition
        auto isRunning = [&]() {
            if (g_restartRequested.load() || g_configDirty.load()) return false;
            if (showWindow) return viewer.isRunning();
            return true;
        };

        BlobTracker tracker;
        int frameCount = 0;
        int fpsFrames = 0;
        auto fpsStart = std::chrono::steady_clock::now();

        while (isRunning()) {
            // Apply device property changes if dirty
            if (g_devicePropsDirty.exchange(false)) {
                applyDeviceSettings(device, webServer.getDeviceSettings(), caps);
            }

            auto frameSet = pipe.waitForFrameset(100);
            if (!frameSet) continue;

            bool gotNewDepth = false;
            bool gotNewColor = false;

            // Process depth
            auto depthRaw = frameSet->getFrame(OB_FRAME_DEPTH);
            if (depthRaw) {
                gotNewDepth = true;
                auto depthFrame = depthRaw->as<ob::DepthFrame>();
                const auto* rawData = reinterpret_cast<const uint16_t*>(depthFrame->getData());
                convertDepthToMm(rawData, depthW * depthH, depthScale);

                uint16_t thr = g_thresholdEnabled.load()
                    ? static_cast<uint16_t>(g_thresholdMm.load()) : uint16_t(65535);
                depthToThresholdBgr(depthMm.data(), depthW, depthH, depthBgr.data(), thr);
                dilateBinaryBgr(depthBgr.data(), depthW, depthH, g_dilateIterations.load());

                auto now2 = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now2 - programStart).count();

                if (g_blobDetectEnabled.load()) {
                    auto blobs = detectAndDrawBlobs(depthBgr.data(), depthW, depthH,
                                                    g_maxBlobPixels.load(), depthMm.data(),
                                                    g_minBlobPixels.load());

                    tracker.update(blobs, frameCount, static_cast<long long>(ms));

                    if (showWeb) {
                        const auto& tracked = tracker.activeBlobs();
                        std::string json = "{\"w\":" + std::to_string(depthW) +
                                           ",\"h\":" + std::to_string(depthH) +
                                           ",\"blobs\":[";
                        for (size_t i = 0; i < tracked.size(); i++) {
                            const auto& t = tracked[i];
                            if (i > 0) json += ",";
                            json += "{\"id\":" + std::to_string(t.serial) +
                                    ",\"cx\":" + std::to_string(t.cx) +
                                    ",\"cy\":" + std::to_string(t.cy) +
                                    ",\"avg\":" + std::to_string(static_cast<int>(t.avgDepthMm + 0.5f)) +
                                    ",\"max\":" + std::to_string(static_cast<int>(t.maxDepthMm)) +
                                    ",\"px\":" + std::to_string(t.pixelCount) + "}";
                        }
                        json += "]}";
                        webServer.updateBlobs(json);
                    }
                } else {
                    tracker.update({}, frameCount, static_cast<long long>(ms));
                    std::printf("[%6d %7lldms]\n", frameCount, static_cast<long long>(ms));
                    if (showWeb) {
                        webServer.updateBlobs("{\"w\":" + std::to_string(depthW) +
                                              ",\"h\":" + std::to_string(depthH) +
                                              ",\"blobs\":[]}");
                    }
                }

                if (showWeb) webServer.updateDepthFrame(depthBgr.data(), depthW, depthH);
            }

            // Process color
            if (showColor && colorW > 0) {
                auto colorRaw = frameSet->getFrame(OB_FRAME_COLOR);
                if (colorRaw) {
                    gotNewColor = true;
                    auto colorFrame = colorRaw->as<ob::ColorFrame>();
                    const auto* rgbData = reinterpret_cast<const uint8_t*>(colorFrame->getData());
                    packedRgbToPackedBgr(rgbData, colorW, colorH, colorBgr.data());
                    if (showWeb) webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
                }
            }

            if (gotNewDepth || gotNewColor) {
                if (showWindow) {
                    if (showColor && colorW > 0)
                        viewer.updateSideBySide(colorBgr.data(), colorW, colorH,
                                                depthBgr.data(), depthW, depthH);
                    else
                        viewer.updateSingle(depthBgr.data(), depthW, depthH);
                }

                frameCount++;
                fpsFrames++;

                // Update FPS once per second
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsStart).count();
                if (elapsed >= 1000) {
                    int tenths = static_cast<int>(fpsFrames * 10000 / elapsed);
                    g_fpsTenths.store(tenths);
                    fpsFrames = 0;
                    fpsStart = now;
                }
            }
        }

        std::cout << "Done (" << frameCount << " frames)." << std::endl;

        pipe.stop();

        // If restart was requested, loop back; otherwise exit
        if (!g_restartRequested.load() && !g_configDirty.load()) break;
        std::cout << "Restarting pipeline with new settings..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    } // end outer restart loop

    if (showWeb) webServer.stop();
    if (showWindow) viewer.shutdown();
    return 0;
}
