#include "viewer.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

// Store the viewer pointer in the window's user data so WndProc can access it.
LRESULT CALLBACK ImageViewer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<ImageViewer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            if (self) self->running_ = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool ImageViewer::initialize(const std::string& title, int width, int height) {
    if (hwnd_) return true;

    const char* className = "DepthPaletteViewer";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&wc);

    // Adjust window size so the client area matches the requested dimensions
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowA(
        className, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr),
        this  // passed to WM_CREATE -> CREATESTRUCT::lpCreateParams
    );

    if (!hwnd_) {
        std::cerr << "CreateWindow failed: " << GetLastError() << std::endl;
        return false;
    }

    running_ = true;
    return true;
}

void ImageViewer::blitBgr(const uint8_t* bgr, int imgW, int imgH) {
    HDC hdc = GetDC(hwnd_);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = imgW;
    bmi.bmiHeader.biHeight = imgH;  // positive = bottom-up
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    int clientW = clientRect.right;
    int clientH = clientRect.bottom;

    StretchDIBits(hdc,
                  0, 0, clientW, clientH,   // destination (full client area)
                  0, 0, imgW, imgH,         // source
                  bgr, &bmi,
                  DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(hwnd_, hdc);
}

void ImageViewer::update(const uint8_t* planarRgb, int imgWidth, int imgHeight) {
    if (!hwnd_ || !running_) return;

    int planeSize = imgWidth * imgHeight;

    // Convert planar RGB to packed BGR (bottom-up for Windows DIB)
    bgrBuf_.resize(planeSize * 3);

    const uint8_t* rPlane = planarRgb;
    const uint8_t* gPlane = planarRgb + planeSize;
    const uint8_t* bPlane = planarRgb + 2 * planeSize;

    for (int y = 0; y < imgHeight; y++) {
        int srcRow = y;
        int dstRow = imgHeight - 1 - y;
        for (int x = 0; x < imgWidth; x++) {
            int srcIdx = srcRow * imgWidth + x;
            int dstIdx = (dstRow * imgWidth + x) * 3;
            bgrBuf_[dstIdx + 0] = bPlane[srcIdx];
            bgrBuf_[dstIdx + 1] = gPlane[srcIdx];
            bgrBuf_[dstIdx + 2] = rPlane[srcIdx];
        }
    }

    blitBgr(bgrBuf_.data(), imgWidth, imgHeight);
}

void ImageViewer::updateSideBySide(const uint8_t* leftBgr, int leftW, int leftH,
                                    const uint8_t* rightBgr, int rightW, int rightH) {
    if (!hwnd_ || !running_) return;

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    int clientW = clientRect.right;
    int clientH = clientRect.bottom;

    // Split client area in half
    int halfW = clientW / 2;

    HDC hdc = GetDC(hwnd_);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    // Left image (top-down: negative height)
    bmi.bmiHeader.biWidth = leftW;
    bmi.bmiHeader.biHeight = -leftH;
    StretchDIBits(hdc, 0, 0, halfW, clientH,
                  0, 0, leftW, leftH,
                  leftBgr, &bmi, DIB_RGB_COLORS, SRCCOPY);

    // Right image (top-down: negative height)
    bmi.bmiHeader.biWidth = rightW;
    bmi.bmiHeader.biHeight = -rightH;
    StretchDIBits(hdc, halfW, 0, clientW - halfW, clientH,
                  0, 0, rightW, rightH,
                  rightBgr, &bmi, DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(hwnd_, hdc);
}

bool ImageViewer::isRunning() {
    if (!running_) return false;

    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running_ = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return running_;
}

void ImageViewer::shutdown() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
}

ImageViewer::~ImageViewer() {
    shutdown();
}
