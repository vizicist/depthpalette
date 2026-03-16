#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <depthai/depthai.hpp>

#include "blobdetect.hpp"
#include "blobtracker.hpp"
#include "depthcolor.hpp"
#ifdef VIEWER_LINUX
#include "viewer_linux.hpp"
#else
#include "viewer.hpp"
#endif
#include "webserver.hpp"

// Shared settings — adjustable from the web UI
static std::atomic<int> g_thresholdMm{550};
static std::atomic<bool> g_thresholdEnabled{true};
static std::atomic<int> g_dilateIterations{0};
static std::atomic<bool> g_blobDetectEnabled{true};
static std::atomic<int> g_maxBlobPixels{5000};
static std::atomic<int> g_minBlobPixels{20};
static std::atomic<int> g_fpsTenths{0};  // FPS × 10 (e.g. 145 = 14.5 fps)
static std::atomic<int> g_confidenceThreshold{245};
static std::atomic<bool> g_extendedDisparity{true};
static std::atomic<int> g_stereoPreset{0};  // 0=DEFAULT, 1=FACE, 2=HIGH_DETAIL, 3=ROBOTICS
static std::atomic<bool> g_configDirty{false};
static std::atomic<bool> g_restartRequested{false};
static std::atomic<int> g_monoResolution{2};   // 0=720P, 1=800P, 2=400P, 3=480P
static std::atomic<int> g_cameraFps{30};

static dai::MonoCameraProperties::SensorResolution resolutionFromInt(int val) {
    switch (val) {
        case 0: return dai::MonoCameraProperties::SensorResolution::THE_720_P;
        case 1: return dai::MonoCameraProperties::SensorResolution::THE_800_P;
        case 3: return dai::MonoCameraProperties::SensorResolution::THE_480_P;
        default: return dai::MonoCameraProperties::SensorResolution::THE_400_P;
    }
}

static dai::node::StereoDepth::PresetMode presetFromInt(int val) {
    switch (val) {
        case 1: return dai::node::StereoDepth::PresetMode::FACE;
        case 2: return dai::node::StereoDepth::PresetMode::HIGH_DETAIL;
        case 3: return dai::node::StereoDepth::PresetMode::ROBOTICS;
        default: return dai::node::StereoDepth::PresetMode::DEFAULT;
    }
}

dai::Pipeline createPipeline(bool enableColor, dai::node::StereoDepth::PresetMode preset,
                             int confidenceThreshold, bool extendedDisparity,
                             const PostProcSettings& pp,
                             dai::MonoCameraProperties::SensorResolution monoRes, int fps) {
    dai::Pipeline pipeline;

    // ---- Stereo depth ----
    auto monoLeft = pipeline.create<dai::node::MonoCamera>();
    auto monoRight = pipeline.create<dai::node::MonoCamera>();
    auto stereo = pipeline.create<dai::node::StereoDepth>();
    auto xoutDepth = pipeline.create<dai::node::XLinkOut>();

    monoLeft->setResolution(monoRes);
    monoLeft->setCamera("left");
    monoLeft->setFps(fps);
    monoRight->setResolution(monoRes);
    monoRight->setCamera("right");
    monoRight->setFps(fps);

    stereo->setDefaultProfilePreset(preset);
    stereo->setLeftRightCheck(true);
    stereo->setSubpixel(pp.subpixelEnable);
    if (pp.subpixelEnable)
        stereo->setSubpixelFractionalBits(pp.subpixelBits);
    stereo->setExtendedDisparity(extendedDisparity);

    // Apply all settings via initialConfig (before device creation, like official example)
    {
        auto config = stereo->initialConfig.get();
        config.costMatching.confidenceThreshold = static_cast<uint8_t>(confidenceThreshold);
        config.algorithmControl.disparityShift = pp.disparityShift;
        config.algorithmControl.leftRightCheckThreshold = pp.lrCheckThreshold;

        switch (pp.medianKernel) {
            case 3:  config.postProcessing.median = dai::MedianFilter::KERNEL_3x3; break;
            case 5:  config.postProcessing.median = dai::MedianFilter::KERNEL_5x5; break;
            case 7:  config.postProcessing.median = dai::MedianFilter::KERNEL_7x7; break;
            default: config.postProcessing.median = dai::MedianFilter::MEDIAN_OFF; break;
        }

        config.postProcessing.spatialFilter.enable = pp.spatialEnable;
        config.postProcessing.spatialFilter.alpha = pp.spatialAlpha / 100.0f;
        config.postProcessing.spatialFilter.delta = pp.spatialDelta;
        config.postProcessing.spatialFilter.numIterations = pp.spatialIter;

        config.postProcessing.temporalFilter.enable = pp.temporalEnable;
        config.postProcessing.temporalFilter.alpha = pp.temporalAlpha / 100.0f;
        config.postProcessing.temporalFilter.delta = pp.temporalDelta;
        config.postProcessing.temporalFilter.persistencyMode =
            static_cast<dai::RawStereoDepthConfig::PostProcessing::TemporalFilter::PersistencyMode>(pp.temporalPersistency);

        config.postProcessing.speckleFilter.enable = pp.speckleEnable;
        config.postProcessing.speckleFilter.speckleRange = pp.speckleRange;
        config.postProcessing.speckleFilter.differenceThreshold = pp.speckleDiff;

        config.postProcessing.decimationFilter.decimationFactor = pp.decimationFactor;
        config.postProcessing.decimationFilter.decimationMode =
            static_cast<dai::RawStereoDepthConfig::PostProcessing::DecimationFilter::DecimationMode>(pp.decimationMode);

        config.postProcessing.brightnessFilter.minBrightness = pp.brightnessFilterMin;
        config.postProcessing.brightnessFilter.maxBrightness = pp.brightnessFilterMax;
        config.postProcessing.thresholdFilter.minRange = pp.thresholdFilterEnable ? pp.thresholdFilterMin : 0;
        config.postProcessing.thresholdFilter.maxRange = pp.thresholdFilterEnable ? pp.thresholdFilterMax : 65535;
        config.postProcessing.spatialFilter.holeFillingRadius = pp.spatialHoleFillingRadius;

        stereo->initialConfig.set(config);
    }

    // Camera controls (applied to both mono cameras)
    {
        auto setupCamCtrl = [&](auto& cam) {
            cam->initialControl.setLumaDenoise(pp.lumaDenoise);
            cam->initialControl.setAutoExposureCompensation(pp.aeCompensation);
            switch (pp.antiBanding) {
                case 1: cam->initialControl.setAntiBandingMode(dai::CameraControl::AntiBandingMode::MAINS_50_HZ); break;
                case 2: cam->initialControl.setAntiBandingMode(dai::CameraControl::AntiBandingMode::MAINS_60_HZ); break;
                case 3: cam->initialControl.setAntiBandingMode(dai::CameraControl::AntiBandingMode::AUTO); break;
                default: cam->initialControl.setAntiBandingMode(dai::CameraControl::AntiBandingMode::OFF); break;
            }
        };
        setupCamCtrl(monoLeft);
        setupCamCtrl(monoRight);
    }

    monoLeft->out.link(stereo->left);
    monoRight->out.link(stereo->right);

    xoutDepth->setStreamName("depth");
    stereo->depth.link(xoutDepth->input);

    // ---- Color camera (optional) ----
    if (enableColor) {
        auto colorCam = pipeline.create<dai::node::ColorCamera>();
        auto xoutColor = pipeline.create<dai::node::XLinkOut>();

        colorCam->setCamera("color");
        colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
        colorCam->setPreviewSize(640, 360);
        colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);
        colorCam->setInterleaved(false);
        colorCam->setFps(15);

        xoutColor->setStreamName("color");
        colorCam->preview.link(xoutColor->input);
    }

    return pipeline;
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
                        g_fpsTenths, g_confidenceThreshold, g_extendedDisparity, g_stereoPreset,
                        g_configDirty, g_restartRequested, g_monoResolution, g_cameraFps,
                        showColor);
    webServer.loadSettings();
    if (showWeb) webServer.start();

    auto programStart = std::chrono::steady_clock::now();

    // Outer loop: recreate pipeline when preset changes require restart
    while (true) {
        auto preset = presetFromInt(g_stereoPreset.load());
        auto pp = webServer.getPostProcSettings();
        int confThreshold = g_confidenceThreshold.load();
        bool extDisp = g_extendedDisparity.load();
        auto monoRes = resolutionFromInt(g_monoResolution.load());
        int camFps = g_cameraFps.load();
        g_restartRequested.store(false);
        g_configDirty.store(false);

        std::cout << "Creating pipeline..." << (showColor ? " (with color)" : " (depth only)")
                  << " preset=" << g_stereoPreset.load()
                  << " res=" << g_monoResolution.load()
                  << " fps=" << camFps << std::endl;

        try {

        auto pipeline = createPipeline(showColor, preset, confThreshold, extDisp, pp, monoRes, camFps);

        std::cout << "Connecting to OAK-D..." << std::endl;
        dai::Device device(pipeline);

        // Query and display device info
        {
            std::string usbStr;
            switch (device.getUsbSpeed()) {
                case dai::UsbSpeed::HIGH:       usbStr = "USB2"; break;
                case dai::UsbSpeed::SUPER:      usbStr = "USB3"; break;
                case dai::UsbSpeed::SUPER_PLUS: usbStr = "USB3.1"; break;
                default:                        usbStr = "USB"; break;
            }
            std::string devName = device.getDeviceName();
            std::string mxId = device.getMxId();
            std::cout << "Device: " << devName << " (" << usbStr << ") MxId: " << mxId << std::endl;
            webServer.setDeviceInfo(usbStr, devName, mxId);
        }

        auto depthQueue = device.getOutputQueue("depth", 4, false);

        // Wait for first depth frame
        std::cout << "Waiting for first frames..." << std::endl;
        auto firstDepth = depthQueue->get<dai::ImgFrame>();

        int depthW = firstDepth->getWidth();
        int depthH = firstDepth->getHeight();
        std::cout << "Depth: " << depthW << "x" << depthH << std::endl;

        // Color stream (optional)
        int colorW = 0, colorH = 0;
        std::shared_ptr<dai::ImgFrame> firstColor;
        std::shared_ptr<dai::DataOutputQueue> colorQueue;
        if (showColor) {
            colorQueue = device.getOutputQueue("color", 4, false);
            firstColor = colorQueue->get<dai::ImgFrame>();
            colorW = firstColor->getWidth();
            colorH = firstColor->getHeight();
            std::cout << "Color: " << colorW << "x" << colorH << std::endl;
        }

        if (showWindow && !viewer.isInitialized()) {
            int windowW = showColor ? colorW + depthW : depthW;
            int windowH = showColor ? std::max(colorH, depthH) : depthH;
            if (!viewer.initialize("DepthPalette", windowW, windowH)) {
                std::cerr << "Failed to create viewer window" << std::endl;
                if (showWeb) webServer.stop();
                return 1;
            }
        }

        // Reusable buffers for BGR conversion
        std::vector<uint8_t> colorBgr;
        if (showColor) colorBgr.resize(colorW * colorH * 3);
        std::vector<uint8_t> depthBgr(depthW * depthH * 3);

        // Process first frames
        {
            if (showColor) {
                const auto& colorData = firstColor->getData();
                planarRgbToPackedBgr(colorData.data(), colorW, colorH, colorBgr.data());
                if (showWeb) webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
            }

            const auto& depthData = firstDepth->getData();
            const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());
            {
                uint16_t thr = g_thresholdEnabled.load()
                    ? static_cast<uint16_t>(g_thresholdMm.load()) : uint16_t(65535);
                depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(), thr);
            }
            dilateBinaryBgr(depthBgr.data(), depthW, depthH, g_dilateIterations.load());

            if (g_blobDetectEnabled.load())
                detectAndDrawBlobs(depthBgr.data(), depthW, depthH,
                                   g_maxBlobPixels.load(), depthPixels,
                                   g_minBlobPixels.load());

            if (showWindow) {
                if (showColor)
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

        // Keep latest frame from each stream (they arrive independently)
        auto latestDepth = firstDepth;
        auto latestColor = firstColor;

        // Running condition: if window is shown, run until it's closed; otherwise run forever
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
            // Grab whatever is available — only reprocess streams that actually updated
            bool gotNewDepth = false;
            bool gotNewColor = false;
            if (auto f = depthQueue->tryGet<dai::ImgFrame>()) { latestDepth = f; gotNewDepth = true; }
            if (showColor) {
                if (auto f = colorQueue->tryGet<dai::ImgFrame>()) { latestColor = f; gotNewColor = true; }
            }

            if (!gotNewDepth && !gotNewColor) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // Only reprocess color when the color frame actually changed
            if (gotNewColor) {
                const auto& colorData = latestColor->getData();
                planarRgbToPackedBgr(colorData.data(), colorW, colorH, colorBgr.data());
                if (showWeb) webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
            }

            // Only reprocess depth when the depth frame actually changed
            if (gotNewDepth) {
                const auto& depthData = latestDepth->getData();
                const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());
                {
                    uint16_t thr = g_thresholdEnabled.load()
                        ? static_cast<uint16_t>(g_thresholdMm.load()) : uint16_t(65535);
                    depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(), thr);
                }
                dilateBinaryBgr(depthBgr.data(), depthW, depthH, g_dilateIterations.load());

                {
                    auto now2 = std::chrono::steady_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now2 - programStart).count();

                    if (g_blobDetectEnabled.load()) {
                        auto blobs = detectAndDrawBlobs(depthBgr.data(), depthW, depthH,
                                                        g_maxBlobPixels.load(), depthPixels,
                                                        g_minBlobPixels.load());

                        // Update persistent blob tracker (prints start/moved/end messages)
                        tracker.update(blobs, frameCount, static_cast<long long>(ms));

                        // Send tracked blob positions to web server as JSON
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
                        // Blob detection off — end any active tracked blobs
                        tracker.update({}, frameCount, static_cast<long long>(ms));

                        std::printf("[%6d %7lldms]\n", frameCount, static_cast<long long>(ms));
                        if (showWeb) {
                            webServer.updateBlobs("{\"w\":" + std::to_string(depthW) +
                                                  ",\"h\":" + std::to_string(depthH) +
                                                  ",\"blobs\":[]}");
                        }
                    }
                }

                if (showWeb) webServer.updateDepthFrame(depthBgr.data(), depthW, depthH);
            }

            if (showWindow) {
                if (showColor)
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

        std::cout << "Done (" << frameCount << " frames)." << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Pipeline error: " << e.what() << std::endl;
            std::cerr << "Retrying in 3 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // If restart was requested (preset change), loop back; otherwise exit
        if (!g_restartRequested.load() && !g_configDirty.load()) break;
        std::cout << "Restarting pipeline with new settings..." << std::endl;
        // Brief pause to batch rapid UI changes
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    } // end outer restart loop

    if (showWeb) webServer.stop();
    if (showWindow) viewer.shutdown();
    return 0;
}
