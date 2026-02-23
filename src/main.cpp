#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <depthai/depthai.hpp>

#include "blobdetect.hpp"
#include "depthcolor.hpp"
#include "viewer.hpp"
#include "webserver.hpp"

// Shared settings — adjustable from the web UI
static std::atomic<int> g_thresholdMm{1500};
static std::atomic<bool> g_blobDetectEnabled{false};
static std::atomic<int> g_maxBlobPixels{5000};
static std::atomic<int> g_fpsTenths{0};  // FPS × 10 (e.g. 145 = 14.5 fps)

dai::Pipeline createPipeline() {
    dai::Pipeline pipeline;

    // ---- Stereo depth ----
    auto monoLeft = pipeline.create<dai::node::MonoCamera>();
    auto monoRight = pipeline.create<dai::node::MonoCamera>();
    auto stereo = pipeline.create<dai::node::StereoDepth>();
    auto xoutDepth = pipeline.create<dai::node::XLinkOut>();

    monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
    monoLeft->setCamera("left");
    monoLeft->setFps(15);
    monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
    monoRight->setCamera("right");
    monoRight->setFps(15);

    stereo->setDefaultProfilePreset(dai::node::StereoDepth::PresetMode::DEFAULT);
    stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_7x7);
    stereo->setLeftRightCheck(true);
    stereo->setSubpixel(false);
    stereo->setExtendedDisparity(false);

    monoLeft->out.link(stereo->left);
    monoRight->out.link(stereo->right);

    xoutDepth->setStreamName("depth");
    stereo->depth.link(xoutDepth->input);

    // ---- Color camera ----
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

    return pipeline;
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    bool showWindow = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--window") == 0 || std::strcmp(argv[i], "-w") == 0) {
            showWindow = true;
        }
    }

    std::cout << "Creating pipeline..." << std::endl;
    auto pipeline = createPipeline();

    std::cout << "Connecting to OAK-D..." << std::endl;
    dai::Device device(pipeline);

    auto depthQueue = device.getOutputQueue("depth", 4, false);
    auto colorQueue = device.getOutputQueue("color", 4, false);

    // Wait for first frames
    std::cout << "Waiting for first frames..." << std::endl;
    auto firstDepth = depthQueue->get<dai::ImgFrame>();
    auto firstColor = colorQueue->get<dai::ImgFrame>();

    int colorW = firstColor->getWidth();
    int colorH = firstColor->getHeight();
    int depthW = firstDepth->getWidth();
    int depthH = firstDepth->getHeight();
    std::cout << "Color: " << colorW << "x" << colorH
              << "  Depth: " << depthW << "x" << depthH << std::endl;

    // Optional Win32 viewer window
    ImageViewer viewer;
    if (showWindow) {
        int windowW = colorW + depthW;
        int windowH = std::max(colorH, depthH);
        if (!viewer.initialize("DepthPalette", windowW, windowH)) {
            std::cerr << "Failed to create viewer window" << std::endl;
            return 1;
        }
    }

    // Start web server
    WebServer webServer(g_thresholdMm, g_blobDetectEnabled, g_maxBlobPixels, g_fpsTenths);
    webServer.start();

    // Reusable buffers for BGR conversion
    std::vector<uint8_t> colorBgr(colorW * colorH * 3);
    std::vector<uint8_t> depthBgr(depthW * depthH * 3);

    // Process first frames
    {
        const auto& colorData = firstColor->getData();
        planarRgbToPackedBgr(colorData.data(), colorW, colorH, colorBgr.data());

        const auto& depthData = firstDepth->getData();
        const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());
        depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(),
                            static_cast<uint16_t>(g_thresholdMm.load()));

        if (g_blobDetectEnabled.load())
            detectAndDrawBlobs(depthBgr.data(), depthW, depthH, g_maxBlobPixels.load());

        if (showWindow)
            viewer.updateSideBySide(colorBgr.data(), colorW, colorH,
                                    depthBgr.data(), depthW, depthH);

        webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
        webServer.updateDepthFrame(depthBgr.data(), depthW, depthH);
    }

    std::cout << "Streaming... " << (showWindow ? "Close the window or press Ctrl+C to exit."
                                                : "Press Ctrl+C to exit.") << std::endl;
    std::cout << "Web UI: http://127.0.0.1:8080" << std::endl;

    // Keep latest frame from each stream (they arrive independently)
    auto latestDepth = firstDepth;
    auto latestColor = firstColor;

    // Running condition: if window is shown, run until it's closed; otherwise run forever
    auto isRunning = [&]() {
        if (showWindow) return viewer.isRunning();
        return true;
    };

    int frameCount = 0;
    int fpsFrames = 0;
    auto fpsStart = std::chrono::steady_clock::now();

    while (isRunning()) {
        // Grab whatever is available — don't require both simultaneously
        bool gotNew = false;
        if (auto f = depthQueue->tryGet<dai::ImgFrame>()) { latestDepth = f; gotNew = true; }
        if (auto f = colorQueue->tryGet<dai::ImgFrame>()) { latestColor = f; gotNew = true; }

        if (!gotNew) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const auto& colorData = latestColor->getData();
        planarRgbToPackedBgr(colorData.data(), colorW, colorH, colorBgr.data());

        const auto& depthData = latestDepth->getData();
        const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());
        depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(),
                            static_cast<uint16_t>(g_thresholdMm.load()));

        if (g_blobDetectEnabled.load())
            detectAndDrawBlobs(depthBgr.data(), depthW, depthH, g_maxBlobPixels.load());

        if (showWindow)
            viewer.updateSideBySide(colorBgr.data(), colorW, colorH,
                                    depthBgr.data(), depthW, depthH);

        // Update web server frame buffers
        webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
        webServer.updateDepthFrame(depthBgr.data(), depthW, depthH);

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
    webServer.stop();
    if (showWindow) viewer.shutdown();
    return 0;
}
