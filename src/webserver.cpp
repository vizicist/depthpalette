#include "webserver.hpp"

#include <cstring>
#include <iostream>
#include <string>

#include <httplib.h>

// ---- HTML page served at / ----
static const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>DepthPalette</title>
<style>
  body { background: #1a1a2e; color: #eee; font-family: Arial, sans-serif;
         margin: 0; display: flex; flex-direction: column; align-items: center; }
  h1 { margin: 16px 0 8px; }
  .images { display: flex; gap: 8px; margin: 8px 0; }
  .images img { border: 1px solid #444; max-width: 640px; height: auto; transform: scaleX(-1); }
  .images canvas { border: 1px solid #444; max-width: 640px; height: auto; transform: scaleX(-1); }
  .controls { background: #16213e; padding: 16px 24px; border-radius: 8px;
              display: flex; align-items: center; gap: 16px; margin: 8px 0;
              flex-wrap: wrap; }
  .controls label { font-size: 14px; }
  .slider { width: 300px; }
  .val { font-weight: bold; min-width: 70px; display: inline-block; }
  .toggle { display: flex; align-items: center; gap: 8px; }
  .switch { position: relative; width: 44px; height: 24px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider-track { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
                  background: #555; border-radius: 24px; transition: 0.3s; }
  .slider-track:before { position: absolute; content: ""; height: 18px; width: 18px;
                          left: 3px; bottom: 3px; background: white; border-radius: 50%;
                          transition: 0.3s; }
  .switch input:checked + .slider-track { background: #4caf50; }
  .switch input:checked + .slider-track:before { transform: translateX(20px); }
  .sep { width: 1px; height: 32px; background: #444; }
  .fps { font-size: 13px; color: #8f8; font-family: monospace; }
</style>
</head>
<body>
<h1>DepthPalette</h1>
<div class="controls">
  <label>Threshold:
    <input id="threshSlider" class="slider" type="range" min="200" max="2000" step="50" value="550">
  </label>
  <span id="threshVal" class="val">550 mm</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="blobToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Blob Detection</span>
  </div>
  <label>Max blob size:
    <input id="blobSlider" class="slider" type="range" min="100" max="50000" step="100" value="5000">
  </label>
  <span id="blobVal" class="val">5000 px</span>
  <label>Min blob size:
    <input id="minBlobSlider" class="slider" type="range" min="1" max="1000" step="1" value="20">
  </label>
  <span id="minBlobVal" class="val">20 px</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="dotsToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Dots View</span>
  </div>
  <div class="sep"></div>
  <span id="fpsDisplay" class="fps">-- fps</span>
</div>
<div class="images">
  <img id="colorImg" alt="Color">
  <img id="depthImg" alt="Depth (B/W)">
  <canvas id="dotsCanvas" style="display:none"></canvas>
</div>
<script>
  const threshSlider = document.getElementById('threshSlider');
  const threshVal = document.getElementById('threshVal');
  const blobToggle = document.getElementById('blobToggle');
  const blobSlider = document.getElementById('blobSlider');
  const blobVal = document.getElementById('blobVal');
  const minBlobSlider = document.getElementById('minBlobSlider');
  const minBlobVal = document.getElementById('minBlobVal');
  const colorImg = document.getElementById('colorImg');
  const depthImg = document.getElementById('depthImg');
  const dotsToggle = document.getElementById('dotsToggle');
  const dotsCanvas = document.getElementById('dotsCanvas');
  const dotsCtx = dotsCanvas.getContext('2d');

  let dotsMode = false;
  let evtSource = null;

  function refreshImages() {
    const t = Date.now();
    colorImg.src = '/color.bmp?t=' + t;
    if (!dotsMode) depthImg.src = '/frame.bmp?t=' + t;
  }

  function drawDots(j) {
    if (j.w > 0 && j.h > 0) {
      dotsCanvas.width = j.w;
      dotsCanvas.height = j.h;
    }
    dotsCtx.fillStyle = '#000';
    dotsCtx.fillRect(0, 0, dotsCanvas.width, dotsCanvas.height);
    if (j.blobs) {
      for (const b of j.blobs) {
        const r = Math.max(4, Math.min(20, Math.sqrt(b.px) / 2));
        dotsCtx.beginPath();
        dotsCtx.arc(b.cx, b.cy, r, 0, 2 * Math.PI);
        dotsCtx.fillStyle = '#0f0';
        dotsCtx.fill();
      }
    }
  }

  dotsToggle.addEventListener('change', function() {
    dotsMode = dotsToggle.checked;
    if (dotsMode) {
      depthImg.style.display = 'none';
      dotsCanvas.style.display = '';
      evtSource = new EventSource('/events');
      evtSource.onmessage = function(e) {
        drawDots(JSON.parse(e.data));
      };
    } else {
      depthImg.style.display = '';
      dotsCanvas.style.display = 'none';
      if (evtSource) { evtSource.close(); evtSource = null; }
    }
  });

  threshSlider.addEventListener('input', function() {
    const v = threshSlider.value;
    threshVal.textContent = v + ' mm';
    fetch('/threshold?value=' + v);
  });

  blobToggle.addEventListener('change', function() {
    fetch('/blobdetect?enabled=' + (blobToggle.checked ? '1' : '0'));
  });

  blobSlider.addEventListener('input', function() {
    const v = blobSlider.value;
    blobVal.textContent = v + ' px';
    fetch('/blobdetect?maxsize=' + v);
  });

  minBlobSlider.addEventListener('input', function() {
    const v = minBlobSlider.value;
    minBlobVal.textContent = v + ' px';
    fetch('/blobdetect?minsize=' + v);
  });

  // Fetch current settings on load
  fetch('/threshold')
    .then(r => r.json())
    .then(j => { threshSlider.value = j.threshold; threshVal.textContent = j.threshold + ' mm'; });

  fetch('/blobdetect')
    .then(r => r.json())
    .then(j => {
      blobToggle.checked = j.enabled;
      blobSlider.value = j.maxsize;
      blobVal.textContent = j.maxsize + ' px';
      minBlobSlider.value = j.minsize;
      minBlobVal.textContent = j.minsize + ' px';
    });

  const fpsDisplay = document.getElementById('fpsDisplay');
  function refreshFps() {
    fetch('/fps')
      .then(r => r.json())
      .then(j => { fpsDisplay.textContent = j.fps.toFixed(1) + ' fps'; });
  }

  setInterval(refreshImages, 200);
  setInterval(refreshFps, 1000);
  refreshImages();
  refreshFps();
</script>
</body>
</html>
)HTML";

// ---- WebServer implementation ----

WebServer::WebServer(std::atomic<int>& thresholdMm,
                     std::atomic<bool>& blobDetectEnabled,
                     std::atomic<int>& maxBlobPixels,
                     std::atomic<int>& minBlobPixels,
                     std::atomic<int>& fpsTenths,
                     bool colorEnabled)
    : thresholdMm_(thresholdMm)
    , blobDetectEnabled_(blobDetectEnabled)
    , maxBlobPixels_(maxBlobPixels)
    , minBlobPixels_(minBlobPixels)
    , fpsTenths_(fpsTenths)
    , colorEnabled_(colorEnabled) {}

WebServer::~WebServer() {
    stop();
}

void WebServer::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&WebServer::run, this);
}

void WebServer::stop() {
    running_ = false;
    blobsCv_.notify_all();  // wake any blocked SSE handlers
    if (thread_.joinable()) thread_.join();
}

void WebServer::updateColorFrame(const uint8_t* bgr, int width, int height) {
    std::lock_guard<std::mutex> lock(frameMtx_);
    size_t sz = static_cast<size_t>(width) * height * 3;
    colorBgr_.resize(sz);
    std::memcpy(colorBgr_.data(), bgr, sz);
    colorW_ = width;
    colorH_ = height;
}

void WebServer::updateDepthFrame(const uint8_t* bgr, int width, int height) {
    std::lock_guard<std::mutex> lock(frameMtx_);
    size_t sz = static_cast<size_t>(width) * height * 3;
    depthBgr_.resize(sz);
    std::memcpy(depthBgr_.data(), bgr, sz);
    depthW_ = width;
    depthH_ = height;
}

void WebServer::updateBlobs(const std::string& json) {
    {
        std::lock_guard<std::mutex> lock(frameMtx_);
        blobsJson_ = json;
        blobsSeq_++;
    }
    blobsCv_.notify_all();
}

std::vector<uint8_t> WebServer::makeBmp(const uint8_t* bgr, int width, int height) {
    // BMP row stride must be a multiple of 4 bytes
    int rowBytes = width * 3;
    int rowPadding = (4 - (rowBytes % 4)) % 4;
    int paddedRow = rowBytes + rowPadding;
    int imageSize = paddedRow * height;
    int fileSize = 54 + imageSize;

    std::vector<uint8_t> bmp(fileSize, 0);

    // -- File header (14 bytes) --
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &fileSize, 4);
    int dataOffset = 54;
    std::memcpy(&bmp[10], &dataOffset, 4);

    // -- Info header (40 bytes) --
    int infoSize = 40;
    std::memcpy(&bmp[14], &infoSize, 4);
    std::memcpy(&bmp[18], &width, 4);
    std::memcpy(&bmp[22], &height, 4);  // positive = bottom-up
    uint16_t planes = 1;
    std::memcpy(&bmp[26], &planes, 2);
    uint16_t bpp = 24;
    std::memcpy(&bmp[28], &bpp, 2);
    // compression = 0 (BI_RGB), already zeroed
    std::memcpy(&bmp[34], &imageSize, 4);

    // -- Pixel data (bottom-up) --
    uint8_t* dst = bmp.data() + 54;
    for (int y = height - 1; y >= 0; y--) {
        const uint8_t* srcRow = bgr + y * rowBytes;
        std::memcpy(dst, srcRow, rowBytes);
        dst += rowBytes;
        // Pad row to 4-byte boundary
        for (int p = 0; p < rowPadding; p++) *dst++ = 0;
    }

    return bmp;
}

void WebServer::run() {
    httplib::Server svr;

    // GET / — HTML page (hide color image if color stream is disabled)
    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string html = kIndexHtml;
        if (!colorEnabled_) {
            // Hide the color image element
            std::string target = "<img id=\"colorImg\" alt=\"Color\">";
            auto pos = html.find(target);
            if (pos != std::string::npos)
                html.replace(pos, target.size(), "");
            // Remove the color refresh line
            std::string colorRefresh = "colorImg.src = '/color.bmp?t=' + t;";
            pos = html.find(colorRefresh);
            if (pos != std::string::npos)
                html.replace(pos, colorRefresh.size(), "");
        }
        res.set_content(html, "text/html");
    });

    // GET /frame.bmp — thresholded depth frame
    svr.Get("/frame.bmp", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<uint8_t> pixels;
        int w, h;
        {
            std::lock_guard<std::mutex> lock(frameMtx_);
            if (depthBgr_.empty()) {
                res.status = 503;
                res.set_content("No frame yet", "text/plain");
                return;
            }
            pixels = depthBgr_;
            w = depthW_;
            h = depthH_;
        }
        auto bmp = makeBmp(pixels.data(), w, h);
        res.set_content(std::string(reinterpret_cast<const char*>(bmp.data()), bmp.size()),
                        "image/bmp");
    });

    // GET /color.bmp — color camera frame
    svr.Get("/color.bmp", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<uint8_t> pixels;
        int w, h;
        {
            std::lock_guard<std::mutex> lock(frameMtx_);
            if (colorBgr_.empty()) {
                res.status = 503;
                res.set_content("No frame yet", "text/plain");
                return;
            }
            pixels = colorBgr_;
            w = colorW_;
            h = colorH_;
        }
        auto bmp = makeBmp(pixels.data(), w, h);
        res.set_content(std::string(reinterpret_cast<const char*>(bmp.data()), bmp.size()),
                        "image/bmp");
    });

    // GET /threshold — get or set threshold
    svr.Get("/threshold", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("value")) {
            int val = std::stoi(req.get_param_value("value"));
            if (val < 100) val = 100;
            if (val > 10000) val = 10000;
            thresholdMm_.store(val);
        }
        int cur = thresholdMm_.load();
        res.set_content("{\"threshold\":" + std::to_string(cur) + "}", "application/json");
    });

    // GET /blobdetect — get or set blob detection settings
    svr.Get("/blobdetect", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("enabled")) {
            blobDetectEnabled_.store(req.get_param_value("enabled") == "1");
        }
        if (req.has_param("maxsize")) {
            int val = std::stoi(req.get_param_value("maxsize"));
            if (val < 10) val = 10;
            if (val > 100000) val = 100000;
            maxBlobPixels_.store(val);
        }
        if (req.has_param("minsize")) {
            int val = std::stoi(req.get_param_value("minsize"));
            if (val < 1) val = 1;
            if (val > 100000) val = 100000;
            minBlobPixels_.store(val);
        }
        bool enabled = blobDetectEnabled_.load();
        int maxsz = maxBlobPixels_.load();
        int minsz = minBlobPixels_.load();
        res.set_content("{\"enabled\":" + std::string(enabled ? "true" : "false") +
                        ",\"maxsize\":" + std::to_string(maxsz) +
                        ",\"minsize\":" + std::to_string(minsz) + "}",
                        "application/json");
    });

    // GET /fps — current processing FPS
    svr.Get("/fps", [this](const httplib::Request&, httplib::Response& res) {
        int tenths = fpsTenths_.load();
        std::string fpsStr = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        res.set_content("{\"fps\":" + fpsStr + "}", "application/json");
    });

    // GET /blobs — current blob positions as JSON
    svr.Get("/blobs", [this](const httplib::Request&, httplib::Response& res) {
        std::string json;
        {
            std::lock_guard<std::mutex> lock(frameMtx_);
            json = blobsJson_;
        }
        if (json.empty()) json = "{\"w\":0,\"h\":0,\"blobs\":[]}";
        res.set_content(json, "application/json");
    });

    // GET /events — SSE stream of blob/cursor updates
    svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t /*offset*/, httplib::DataSink& sink) {
                int lastSeq;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    lastSeq = blobsSeq_;
                }
                // Wait for new data or timeout (5s for keepalive)
                {
                    std::unique_lock<std::mutex> lock(frameMtx_);
                    blobsCv_.wait_for(lock, std::chrono::seconds(5),
                                      [&] { return blobsSeq_ != lastSeq || !running_; });
                }
                if (!running_) return false;
                // Send current blob data as SSE event
                std::string json;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    json = blobsJson_;
                }
                if (json.empty()) json = "{\"w\":0,\"h\":0,\"blobs\":[]}";
                std::string event = "data: " + json + "\n\n";
                sink.write(event.data(), event.size());
                return true;
            });
    });

    std::cout << "Web server listening on http://127.0.0.1:8080" << std::endl;

    // svr.listen blocks — we stop it by calling svr.stop() from the destructor thread.
    // But httplib doesn't have a thread-safe stop easily, so we use listen_after_bind
    // with a polling check.
    svr.set_keep_alive_max_count(4);

    // Use a separate thread to poll running_ and call svr.stop()
    std::thread stopper([this, &svr]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        svr.stop();
    });

    svr.listen("127.0.0.1", 8080);

    stopper.join();
    std::cout << "Web server stopped." << std::endl;
}
