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
#include "viewer.hpp"
#include "webserver.hpp"

// Shared settings — adjustable from the web UI
static std::atomic<int> g_thresholdMm{550};
static std::atomic<bool> g_blobDetectEnabled{true};
static std::atomic<int> g_maxBlobPixels{5000};
static std::atomic<int> g_minBlobPixels{20};
static std::atomic<int> g_fpsTenths{0};  // FPS × 10 (e.g. 145 = 14.5 fps)

dai::Pipeline createPipeline(bool enableColor) {
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

    std::cout << "Creating pipeline..." << (showColor ? " (with color)" : " (depth only)") << std::endl;
    auto pipeline = createPipeline(showColor);

    std::cout << "Connecting to OAK-D..." << std::endl;
    dai::Device device(pipeline);

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

    // Optional Win32 viewer window
    ImageViewer viewer;
    if (showWindow) {
        int windowW = showColor ? colorW + depthW : depthW;
        int windowH = showColor ? std::max(colorH, depthH) : depthH;
        if (!viewer.initialize("DepthPalette", windowW, windowH)) {
            std::cerr << "Failed to create viewer window" << std::endl;
            return 1;
        }
    }

    // Start web server (optional)
    WebServer webServer(g_thresholdMm, g_blobDetectEnabled, g_maxBlobPixels, g_minBlobPixels, g_fpsTenths, showColor);
    if (showWeb) webServer.start();

    // Reusable buffers for BGR conversion
    std::vector<uint8_t> colorBgr;
    if (showColor) colorBgr.resize(colorW * colorH * 3);
    std::vector<uint8_t> depthBgr(depthW * depthH * 3);

    auto programStart = std::chrono::steady_clock::now();

    // Process first frames
    {
        if (showColor) {
            const auto& colorData = firstColor->getData();
            planarRgbToPackedBgr(colorData.data(), colorW, colorH, colorBgr.data());
            if (showWeb) webServer.updateColorFrame(colorBgr.data(), colorW, colorH);
        }

        const auto& depthData = firstDepth->getData();
        const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());
        depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(),
                            static_cast<uint16_t>(g_thresholdMm.load()));

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
            depthToThresholdBgr(depthPixels, depthW, depthH, depthBgr.data(),
                                static_cast<uint16_t>(g_thresholdMm.load()));

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
    if (showWeb) webServer.stop();
    if (showWindow) viewer.shutdown();
    return 0;
}
