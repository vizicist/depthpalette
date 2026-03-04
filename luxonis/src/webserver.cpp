#include "webserver.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include <httplib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---- JPEG encoding for MJPEG streaming ----
static void jpegWriteFunc(void* context, void* data, int size) {
    auto* buf = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    buf->insert(buf->end(), bytes, bytes + size);
}

static std::vector<uint8_t> encodeJpeg(const uint8_t* bgr, int width, int height, int quality = 80) {
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

// ---- HTML page served at / ----
static const std::string kIndexHtml = std::string(R"HTML(<!DOCTYPE html>
<html>)HTML") + R"HTML(
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
  .slider { width: 200px; }
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
  select { background: #1a1a2e; color: #eee; border: 1px solid #444; border-radius: 4px;
           padding: 4px 8px; font-size: 14px; }
  .restart-note { font-size: 11px; color: #f80; }
  .help-btn { display: inline-block; width: 16px; height: 16px; border-radius: 50%;
              background: #555; color: #fff; font-size: 11px; text-align: center;
              line-height: 16px; cursor: pointer; margin-left: 4px; font-weight: bold;
              vertical-align: middle; user-select: none; flex-shrink: 0; }
  .help-btn:hover { background: #888; }
  .help-overlay { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%;
                  background: rgba(0,0,0,0.5); z-index: 1000; justify-content: center;
                  align-items: center; }
  .help-overlay.active { display: flex; }
  .help-box { background: #16213e; border: 1px solid #444; border-radius: 8px;
              padding: 16px 20px; max-width: 400px; font-size: 14px; line-height: 1.5; }
  .help-box h3 { margin: 0 0 8px; color: #8cf; }
  .help-box p { margin: 0 0 12px; }
  .help-box button { background: #444; color: #eee; border: none; border-radius: 4px;
                     padding: 6px 16px; cursor: pointer; font-size: 13px; }
  .help-box button:hover { background: #666; }
</style>
</head>
<body>
<div id="helpOverlay" class="help-overlay" onclick="if(event.target===this)closeHelp()">
  <div class="help-box">
    <h3 id="helpTitle"></h3>
    <p id="helpText"></p>
    <button onclick="closeHelp()">Close</button>
  </div>
</div>
<h1>DepthPalette</h1>
<div class="controls">
  <div class="toggle">
    <label class="switch">
      <input id="threshToggle" type="checkbox" checked>
      <span class="slider-track"></span>
    </label>
    <span>Threshold<span class="help-btn" onclick="showHelp('Threshold','Software depth threshold. Pixels beyond this distance are shown as black. Useful for filtering out background objects. Applies per-frame without restart.')">?</span></span>
  </div>
  <label>
    <input id="threshSlider" class="slider" type="range" min="200" max="1200" step="10" value="550">
  </label>
  <span id="threshVal" class="val">550 mm</span>
  <label>Dilate<span class="help-btn" onclick="showHelp('Dilate','Expands black (foreground) regions by N pixels using a 3x3 kernel. Applied after thresholding and before blob detection. Helps connect nearby blobs and fill small gaps. 0 = off. Does not require restart.')">?</span>:
    <input id="dilateSlider" class="slider" type="range" min="0" max="10" step="1" value="0" style="width:100px">
  </label>
  <span id="dilateVal" class="val">0</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="blobToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Blob Detection<span class="help-btn" onclick="showHelp('Blob Detection','Enables connected-component blob detection on the depth image. Detected blobs are outlined and tracked across frames.')">?</span></span>
  </div>
  <label>Max blob size<span class="help-btn" onclick="showHelp('Max Blob Size','Maximum number of pixels a blob can contain. Blobs larger than this are ignored. Helps filter out large surfaces like walls.')">?</span>:
    <input id="blobSlider" class="slider" type="range" min="100" max="50000" step="100" value="5000">
  </label>
  <span id="blobVal" class="val">5000 px</span>
  <label>Min blob size<span class="help-btn" onclick="showHelp('Min Blob Size','Minimum number of pixels a blob must contain. Blobs smaller than this are ignored. Helps filter out noise.')">?</span>:
    <input id="minBlobSlider" class="slider" type="range" min="1" max="1000" step="1" value="20">
  </label>
  <span id="minBlobVal" class="val">20 px</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="dotsToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Dots View<span class="help-btn" onclick="showHelp('Dots View','Replaces the depth image with a simplified dot display showing only tracked blob center positions.')">?</span></span>
  </div>
  <div class="sep"></div>
  <label>Confidence<span class="help-btn" onclick="showHelp('Confidence Threshold','Stereo matching confidence threshold (0-255). Higher values reject more uncertain depth pixels, reducing noise but creating more holes. Default: 245. Requires restart.')">?</span>:
    <input id="confSlider" class="slider" type="range" min="0" max="255" step="1" value="245">
  </label>
  <span id="confVal" class="val">245</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="extDispToggle" type="checkbox" checked>
      <span class="slider-track"></span>
    </label>
    <span>Extended Disparity<span class="help-btn" onclick="showHelp('Extended Disparity','Doubles the disparity range from 0-95 to 0-190, allowing closer objects to be detected. Cannot be used together with subpixel mode. Requires restart.')">?</span></span>
  </div>
  <span id="restartNote" class="restart-note"></span>
  <div class="sep"></div>
  <span id="fpsDisplay" class="fps">-- fps</span>
</div>
)HTML" R"HTML(<div class="controls">
  <label>Preset<span class="help-btn" onclick="showHelp('Stereo Preset','Preconfigured stereo matching profiles. DEFAULT is general-purpose. FACE optimizes for close-range face detection. HIGH_DETAIL preserves fine detail. ROBOTICS optimizes for obstacle avoidance. Requires restart.')">?</span>:
    <select id="presetSelect">
      <option value="0">DEFAULT</option>
      <option value="1">FACE</option>
      <option value="2">HIGH_DETAIL</option>
      <option value="3">ROBOTICS</option>
    </select>
  </label>
  <div class="sep"></div>
  <label>Resolution<span class="help-btn" onclick="showHelp('Resolution','Mono camera sensor resolution. Higher resolutions give more detail but reduce max FPS. 400P (640x400) is fastest. 800P (1280x800) is the native sensor resolution. Requires restart.')">?</span>:
    <select id="resolutionSelect">
      <option value="2">400P</option>
      <option value="3">480P</option>
      <option value="0">720P</option>
      <option value="1">800P</option>
    </select>
  </label>
  <div class="sep"></div>
  <label>Camera FPS<span class="help-btn" onclick="showHelp('Camera FPS','Sensor capture frame rate. Higher values give smoother video but increase USB bandwidth and processing load. Requires restart.')">?</span>:
    <input id="camFpsSlider" class="slider" type="range" min="1" max="60" step="1" value="30">
  </label>
  <span id="camFpsVal" class="val">30 fps</span>
</div>
<div class="controls">
  <div class="toggle">
    <label class="switch">
      <input id="subpixelToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Subpixel<span class="help-btn" onclick="showHelp('Subpixel Mode','Enables subpixel interpolation for finer depth precision. Especially useful for the short-baseline OAK-D-SR at close range. Cannot be used with Extended Disparity. Requires restart.')">?</span></span>
  </div>
  <label>Bits<span class="help-btn" onclick="showHelp('Subpixel Fractional Bits','Number of fractional bits for subpixel depth (3, 4, or 5). More bits = finer depth steps but potentially more noise. Only effective when Subpixel is enabled. Requires restart.')">?</span>:
    <select id="subpixelBits">
      <option value="3" selected>3</option>
      <option value="4">4</option>
      <option value="5">5</option>
    </select>
  </label>
  <div class="sep"></div>
  <label>Disparity Shift<span class="help-btn" onclick="showHelp('Disparity Shift','Shifts the stereo search range to detect closer objects, at the cost of reduced maximum depth range. Increase this to bring the minimum detectable depth closer. 0 = default. Requires restart.')">?</span>:
    <input id="dispShiftSlider" class="slider" type="range" min="0" max="128" step="1" value="0">
  </label>
  <span id="dispShiftVal" class="val">0</span>
  <div class="sep"></div>
  <label>LR Check Threshold<span class="help-btn" onclick="showHelp('Left-Right Check Threshold','Threshold for left-right consistency check (0-128). Lower values are stricter, rejecting more inconsistent depth pixels. Higher values are more permissive. Default: 10. Requires restart.')">?</span>:
    <input id="lrCheckSlider" class="slider" type="range" min="0" max="128" step="1" value="10">
  </label>
  <span id="lrCheckVal" class="val">10</span>
</div>
<div class="controls">
  <label>Luma Denoise<span class="help-btn" onclick="showHelp('Luma Denoise','Denoises the mono camera images before stereo matching (0-4). Higher values reduce noise in the source images, which can improve depth quality. 0 = off, default: 1. Requires restart.')">?</span>:
    <input id="lumaDenoiseSlider" class="slider" type="range" min="0" max="4" step="1" value="1">
  </label>
  <span id="lumaDenoiseVal" class="val">1</span>
  <div class="sep"></div>
  <label>Anti-Banding<span class="help-btn" onclick="showHelp('Anti-Banding Mode','Prevents banding artifacts caused by artificial lighting flicker. Set to match your local mains frequency: 50Hz (Europe/Asia) or 60Hz (Americas). AUTO detects automatically. Requires restart.')">?</span>:
    <select id="antiBandingSelect">
      <option value="0" selected>OFF</option>
      <option value="1">50 Hz</option>
      <option value="2">60 Hz</option>
      <option value="3">AUTO</option>
    </select>
  </label>
  <div class="sep"></div>
  <label>AE Compensation<span class="help-btn" onclick="showHelp('Auto-Exposure Compensation','Adjusts auto-exposure brightness bias (-9 to +9). Positive values brighten the image, negative values darken it. Better exposure improves stereo matching quality. Requires restart.')">?</span>:
    <input id="aeCompSlider" class="slider" type="range" min="-9" max="9" step="1" value="0">
  </label>
  <span id="aeCompVal" class="val">0</span>
  <div class="sep"></div>
  <label>Brightness Filter<span class="help-btn" onclick="showHelp('Brightness Filter','Rejects depth pixels where the source image brightness is outside the min-max range. Dark and overexposed regions produce unreliable depth. Min 0-255, Max 0-256. Requires restart.')">?</span>:
    <input id="brightMinSlider" class="slider" type="range" min="0" max="255" step="1" value="0" style="width:100px">
  </label>
  <span id="brightMinVal" class="val">0</span>
  <span>-</span>
  <input id="brightMaxSlider" class="slider" type="range" min="0" max="256" step="1" value="256" style="width:100px">
  <span id="brightMaxVal" class="val">256</span>
</div>
)HTML" R"HTML(<div class="controls">
  <label>Median<span class="help-btn" onclick="showHelp('Median Filter','Applies a median filter to the depth map to remove salt-and-pepper noise. Larger kernels smooth more but lose fine detail. Requires restart.')">?</span>:
    <select id="medianSelect">
      <option value="0">OFF</option>
      <option value="3">3x3</option>
      <option value="5">5x5</option>
      <option value="7" selected>7x7</option>
    </select>
  </label>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="spatialToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Spatial<span class="help-btn" onclick="showHelp('Spatial Filter','Edge-preserving spatial smoothing filter. Fills holes and smooths depth while preserving edges. Alpha controls smoothing strength, Iter controls passes. Requires restart.')">?</span></span>
  </div>
  <label>Alpha<span class="help-btn" onclick="showHelp('Spatial Alpha','Spatial filter smoothing strength (0.0-1.0). Lower values smooth more aggressively. Higher values preserve more detail. Requires restart.')">?</span>: <input id="spatialAlpha" class="slider" type="range" min="0" max="100" step="1" value="50"></label>
  <span id="spatialAlphaVal" class="val">0.50</span>
  <label>Iter<span class="help-btn" onclick="showHelp('Spatial Iterations','Number of passes the spatial filter makes over the image (1-5). More iterations = more smoothing. Requires restart.')">?</span>: <input id="spatialIter" class="slider" type="range" min="1" max="5" step="1" value="1"></label>
  <span id="spatialIterVal" class="val">1</span>
  <label>Hole Fill<span class="help-btn" onclick="showHelp('Spatial Hole Filling Radius','Radius for filling holes in the depth map during spatial filtering (0-16). Larger values fill bigger gaps. 0 disables hole filling. Default: 2. Requires restart.')">?</span>: <input id="spatialHoleSlider" class="slider" type="range" min="0" max="16" step="1" value="2" style="width:100px"></label>
  <span id="spatialHoleVal" class="val">2</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="temporalToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Temporal<span class="help-btn" onclick="showHelp('Temporal Filter','Smooths depth over time using historical frames. Reduces flickering and fills transient holes. Alpha controls how much history to use. Requires restart.')">?</span></span>
  </div>
  <label>Alpha<span class="help-btn" onclick="showHelp('Temporal Alpha','Temporal filter weight (0.0-1.0). Lower values rely more on historical data (more smoothing over time). Higher values favor the current frame. Requires restart.')">?</span>: <input id="temporalAlpha" class="slider" type="range" min="0" max="100" step="1" value="40"></label>
  <span id="temporalAlphaVal" class="val">0.40</span>
  <label>Persist<span class="help-btn" onclick="showHelp('Temporal Persistency','Controls how aggressively the temporal filter keeps previous depth values when current data is missing. Higher persistency fills more holes but may retain stale data. Requires restart.')">?</span>:
    <select id="temporalPersist">
      <option value="0">OFF</option>
      <option value="1">8/8</option>
      <option value="2">2/3</option>
      <option value="3" selected>2/4</option>
      <option value="4">2/8</option>
      <option value="5">1/2</option>
      <option value="6">1/5</option>
      <option value="7">1/8</option>
      <option value="8">Always</option>
    </select>
  </label>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="speckleToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Speckle<span class="help-btn" onclick="showHelp('Speckle Filter','Removes small isolated depth regions (speckles) that are likely noise. Range controls how far to search for connected pixels. Requires restart.')">?</span></span>
  </div>
  <label>Range<span class="help-btn" onclick="showHelp('Speckle Range','Maximum search distance for connecting speckle pixels (0-240). Larger values merge more distant pixels into the same region. Requires restart.')">?</span>: <input id="speckleRange" class="slider" type="range" min="0" max="240" step="1" value="50"></label>
  <span id="speckleRangeVal" class="val">50</span>
  <div class="sep"></div>
  <label>Decimation<span class="help-btn" onclick="showHelp('Decimation Filter','Downscales the depth map by the given factor (1-4). Reduces resolution but can improve density and processing speed. 1 = off. Requires restart.')">?</span>:
    <select id="decimationSelect">
      <option value="1" selected>1 (off)</option>
      <option value="2">2x</option>
      <option value="3">3x</option>
      <option value="4">4x</option>
    </select>
  </label>
  <label>Mode<span class="help-btn" onclick="showHelp('Decimation Mode','Method used to combine pixels during downscaling. Pixel Skip is fastest. Non-Zero Median/Mean use only valid depth pixels, producing denser output. Requires restart.')">?</span>:
    <select id="decimationMode">
      <option value="0">Pixel Skip</option>
      <option value="1">Median</option>
      <option value="2">Mean</option>
    </select>
  </label>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="depthRangeToggle" type="checkbox" checked>
      <span class="slider-track"></span>
    </label>
    <span>Depth Range<span class="help-btn" onclick="showHelp('Depth Range Filter','Hardware depth range filter. Rejects depth values outside the min-max range (in mm). Runs on the VPU before other processing. More efficient than software thresholding. Requires restart.')">?</span></span>
  </div>
  <input id="threshFilterMinSlider" class="slider" type="range" min="0" max="1200" step="10" value="0" style="width:100px">
  <span id="threshFilterMinVal" class="val">0</span>
  <span>-</span>
  <input id="threshFilterMaxSlider" class="slider" type="range" min="0" max="1200" step="10" value="1200" style="width:100px">
  <span id="threshFilterMaxVal" class="val">1200</span>
  <span>mm</span>
</div>
)HTML" R"HTML(<div class="images">
  <img id="colorImg" src="/color.mjpeg" alt="Color">
  <img id="depthImg" src="/depth.mjpeg" alt="Depth (B/W)">
  <canvas id="dotsCanvas" style="display:none"></canvas>
</div>
<script>
  function showHelp(title, text) {
    document.getElementById('helpTitle').textContent = title;
    document.getElementById('helpText').textContent = text;
    document.getElementById('helpOverlay').classList.add('active');
  }
  function closeHelp() {
    document.getElementById('helpOverlay').classList.remove('active');
  }

  const threshToggle = document.getElementById('threshToggle');
  const threshSlider = document.getElementById('threshSlider');
  const threshVal = document.getElementById('threshVal');
  const dilateSlider = document.getElementById('dilateSlider');
  const dilateVal = document.getElementById('dilateVal');
  const depthRangeToggle = document.getElementById('depthRangeToggle');
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
  const confSlider = document.getElementById('confSlider');
  const confVal = document.getElementById('confVal');
  const extDispToggle = document.getElementById('extDispToggle');
  const presetSelect = document.getElementById('presetSelect');
  const restartNote = document.getElementById('restartNote');
  const resolutionSelect = document.getElementById('resolutionSelect');
  const camFpsSlider = document.getElementById('camFpsSlider');
  const camFpsVal = document.getElementById('camFpsVal');
  const subpixelToggle = document.getElementById('subpixelToggle');
  const subpixelBits = document.getElementById('subpixelBits');
  const dispShiftSlider = document.getElementById('dispShiftSlider');
  const dispShiftVal = document.getElementById('dispShiftVal');
  const lrCheckSlider = document.getElementById('lrCheckSlider');
  const lrCheckVal = document.getElementById('lrCheckVal');
  const lumaDenoiseSlider = document.getElementById('lumaDenoiseSlider');
  const lumaDenoiseVal = document.getElementById('lumaDenoiseVal');
  const antiBandingSelect = document.getElementById('antiBandingSelect');
  const aeCompSlider = document.getElementById('aeCompSlider');
  const aeCompVal = document.getElementById('aeCompVal');
  const brightMinSlider = document.getElementById('brightMinSlider');
  const brightMinVal = document.getElementById('brightMinVal');
  const brightMaxSlider = document.getElementById('brightMaxSlider');
  const brightMaxVal = document.getElementById('brightMaxVal');
  const medianSelect = document.getElementById('medianSelect');
  const spatialToggle = document.getElementById('spatialToggle');
  const spatialAlpha = document.getElementById('spatialAlpha');
  const spatialAlphaVal = document.getElementById('spatialAlphaVal');
  const spatialIter = document.getElementById('spatialIter');
  const spatialIterVal = document.getElementById('spatialIterVal');
  const spatialHoleSlider = document.getElementById('spatialHoleSlider');
  const spatialHoleVal = document.getElementById('spatialHoleVal');
  const temporalToggle = document.getElementById('temporalToggle');
  const temporalAlpha = document.getElementById('temporalAlpha');
  const temporalAlphaVal = document.getElementById('temporalAlphaVal');
  const temporalPersist = document.getElementById('temporalPersist');
  const speckleToggle = document.getElementById('speckleToggle');
  const speckleRange = document.getElementById('speckleRange');
  const speckleRangeVal = document.getElementById('speckleRangeVal');
  const decimationSelect = document.getElementById('decimationSelect');
  const decimationMode = document.getElementById('decimationMode');
  const threshFilterMinSlider = document.getElementById('threshFilterMinSlider');
  const threshFilterMinVal = document.getElementById('threshFilterMinVal');
  const threshFilterMaxSlider = document.getElementById('threshFilterMaxSlider');
  const threshFilterMaxVal = document.getElementById('threshFilterMaxVal');

  let restartTimer = null;
  function showRestart() {
    restartNote.textContent = 'Restarting...';
    if (restartTimer) clearTimeout(restartTimer);
    restartTimer = setTimeout(function() { restartNote.textContent = ''; }, 5000);
  }
  function ppSend(params) { showRestart(); fetch('/postproc?' + params); }

  let dotsMode = false;
  let evtSource = null;

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

  threshToggle.addEventListener('change', function() {
    threshSlider.disabled = !threshToggle.checked;
    fetch('/threshold?enabled=' + (threshToggle.checked ? '1' : '0'));
  });
  threshSlider.addEventListener('input', function() {
    threshVal.textContent = threshSlider.value + ' mm';
    fetch('/threshold?value=' + threshSlider.value);
  });

  dilateSlider.addEventListener('input', function() {
    dilateVal.textContent = dilateSlider.value;
    fetch('/threshold?dilate=' + dilateSlider.value);
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

  confSlider.addEventListener('input', function() {
    confVal.textContent = confSlider.value;
  });
  confSlider.addEventListener('change', function() {
    showRestart();
    fetch('/stereoconfig?confidence=' + confSlider.value);
  });

  extDispToggle.addEventListener('change', function() {
    showRestart();
    fetch('/stereoconfig?extended=' + (extDispToggle.checked ? '1' : '0'));
  });

  presetSelect.addEventListener('change', function() {
    showRestart();
    fetch('/stereoconfig?preset=' + presetSelect.value);
  });

  resolutionSelect.addEventListener('change', function() {
    showRestart();
    fetch('/stereoconfig?resolution=' + resolutionSelect.value);
  });

  camFpsSlider.addEventListener('input', function() {
    camFpsVal.textContent = camFpsSlider.value + ' fps';
  });
  camFpsSlider.addEventListener('change', function() {
    showRestart();
    fetch('/stereoconfig?fps=' + camFpsSlider.value);
  });

  medianSelect.addEventListener('change', function() { ppSend('median=' + medianSelect.value); });
  spatialToggle.addEventListener('change', function() { ppSend('spatialEnable=' + (spatialToggle.checked ? '1' : '0')); });
  spatialAlpha.addEventListener('input', function() {
    spatialAlphaVal.textContent = (spatialAlpha.value / 100).toFixed(2);
  });
  spatialAlpha.addEventListener('change', function() { ppSend('spatialAlpha=' + spatialAlpha.value); });
  spatialIter.addEventListener('input', function() {
    spatialIterVal.textContent = spatialIter.value;
  });
  spatialIter.addEventListener('change', function() { ppSend('spatialIter=' + spatialIter.value); });
  temporalToggle.addEventListener('change', function() { ppSend('temporalEnable=' + (temporalToggle.checked ? '1' : '0')); });
  temporalAlpha.addEventListener('input', function() {
    temporalAlphaVal.textContent = (temporalAlpha.value / 100).toFixed(2);
  });
  temporalAlpha.addEventListener('change', function() { ppSend('temporalAlpha=' + temporalAlpha.value); });
  temporalPersist.addEventListener('change', function() { ppSend('temporalPersist=' + temporalPersist.value); });
  speckleToggle.addEventListener('change', function() { ppSend('speckleEnable=' + (speckleToggle.checked ? '1' : '0')); });
  speckleRange.addEventListener('input', function() {
    speckleRangeVal.textContent = speckleRange.value;
  });
  speckleRange.addEventListener('change', function() { ppSend('speckleRange=' + speckleRange.value); });
  decimationSelect.addEventListener('change', function() { ppSend('decimation=' + decimationSelect.value); });
  decimationMode.addEventListener('change', function() { ppSend('decimationMode=' + decimationMode.value); });

  subpixelToggle.addEventListener('change', function() { ppSend('subpixelEnable=' + (subpixelToggle.checked ? '1' : '0')); });
  subpixelBits.addEventListener('change', function() { ppSend('subpixelBits=' + subpixelBits.value); });
  dispShiftSlider.addEventListener('input', function() { dispShiftVal.textContent = dispShiftSlider.value; });
  dispShiftSlider.addEventListener('change', function() { ppSend('disparityShift=' + dispShiftSlider.value); });
  lrCheckSlider.addEventListener('input', function() { lrCheckVal.textContent = lrCheckSlider.value; });
  lrCheckSlider.addEventListener('change', function() { ppSend('lrCheckThreshold=' + lrCheckSlider.value); });

  lumaDenoiseSlider.addEventListener('input', function() { lumaDenoiseVal.textContent = lumaDenoiseSlider.value; });
  lumaDenoiseSlider.addEventListener('change', function() { ppSend('lumaDenoise=' + lumaDenoiseSlider.value); });
  antiBandingSelect.addEventListener('change', function() { ppSend('antiBanding=' + antiBandingSelect.value); });
  aeCompSlider.addEventListener('input', function() { aeCompVal.textContent = aeCompSlider.value; });
  aeCompSlider.addEventListener('change', function() { ppSend('aeCompensation=' + aeCompSlider.value); });

  brightMinSlider.addEventListener('input', function() { brightMinVal.textContent = brightMinSlider.value; });
  brightMinSlider.addEventListener('change', function() { ppSend('brightnessFilterMin=' + brightMinSlider.value); });
  brightMaxSlider.addEventListener('input', function() { brightMaxVal.textContent = brightMaxSlider.value; });
  brightMaxSlider.addEventListener('change', function() { ppSend('brightnessFilterMax=' + brightMaxSlider.value); });

  spatialHoleSlider.addEventListener('input', function() { spatialHoleVal.textContent = spatialHoleSlider.value; });
  spatialHoleSlider.addEventListener('change', function() { ppSend('spatialHoleFilling=' + spatialHoleSlider.value); });

  depthRangeToggle.addEventListener('change', function() {
    const dis = !depthRangeToggle.checked;
    threshFilterMinSlider.disabled = dis;
    threshFilterMaxSlider.disabled = dis;
    ppSend('thresholdFilterEnable=' + (depthRangeToggle.checked ? '1' : '0'));
  });
  threshFilterMinSlider.addEventListener('input', function() { threshFilterMinVal.textContent = threshFilterMinSlider.value; });
  threshFilterMinSlider.addEventListener('change', function() { ppSend('thresholdFilterMin=' + threshFilterMinSlider.value); });
  threshFilterMaxSlider.addEventListener('input', function() { threshFilterMaxVal.textContent = threshFilterMaxSlider.value; });
  threshFilterMaxSlider.addEventListener('change', function() { ppSend('thresholdFilterMax=' + threshFilterMaxSlider.value); });

  // Fetch current settings on load
  fetch('/threshold')
    .then(r => r.json())
    .then(j => {
      threshToggle.checked = j.enabled;
      threshSlider.value = j.threshold;
      threshSlider.disabled = !j.enabled;
      threshVal.textContent = j.threshold + ' mm';
      dilateSlider.value = j.dilate;
      dilateVal.textContent = j.dilate;
    });

  fetch('/blobdetect')
    .then(r => r.json())
    .then(j => {
      blobToggle.checked = j.enabled;
      blobSlider.value = j.maxsize;
      blobVal.textContent = j.maxsize + ' px';
      minBlobSlider.value = j.minsize;
      minBlobVal.textContent = j.minsize + ' px';
    });

  fetch('/stereoconfig')
    .then(r => r.json())
    .then(j => {
      confSlider.value = j.confidence;
      confVal.textContent = j.confidence;
      extDispToggle.checked = j.extended;
      presetSelect.value = j.preset;
      resolutionSelect.value = j.resolution;
      camFpsSlider.value = j.fps;
      camFpsVal.textContent = j.fps + ' fps';
    });

  fetch('/postproc')
    .then(r => r.json())
    .then(j => {
      medianSelect.value = j.median;
      spatialToggle.checked = j.spatialEnable;
      spatialAlpha.value = j.spatialAlpha;
      spatialAlphaVal.textContent = (j.spatialAlpha / 100).toFixed(2);
      spatialIter.value = j.spatialIter;
      spatialIterVal.textContent = j.spatialIter;
      spatialHoleSlider.value = j.spatialHoleFilling;
      spatialHoleVal.textContent = j.spatialHoleFilling;
      temporalToggle.checked = j.temporalEnable;
      temporalAlpha.value = j.temporalAlpha;
      temporalAlphaVal.textContent = (j.temporalAlpha / 100).toFixed(2);
      temporalPersist.value = j.temporalPersist;
      speckleToggle.checked = j.speckleEnable;
      speckleRange.value = j.speckleRange;
      speckleRangeVal.textContent = j.speckleRange;
      decimationSelect.value = j.decimation;
      decimationMode.value = j.decimationMode;
      subpixelToggle.checked = j.subpixelEnable;
      subpixelBits.value = j.subpixelBits;
      dispShiftSlider.value = j.disparityShift;
      dispShiftVal.textContent = j.disparityShift;
      lrCheckSlider.value = j.lrCheckThreshold;
      lrCheckVal.textContent = j.lrCheckThreshold;
      lumaDenoiseSlider.value = j.lumaDenoise;
      lumaDenoiseVal.textContent = j.lumaDenoise;
      antiBandingSelect.value = j.antiBanding;
      aeCompSlider.value = j.aeCompensation;
      aeCompVal.textContent = j.aeCompensation;
      brightMinSlider.value = j.brightnessFilterMin;
      brightMinVal.textContent = j.brightnessFilterMin;
      brightMaxSlider.value = j.brightnessFilterMax;
      brightMaxVal.textContent = j.brightnessFilterMax;
      threshFilterMinSlider.value = j.thresholdFilterMin;
      threshFilterMinVal.textContent = j.thresholdFilterMin;
      threshFilterMaxSlider.value = j.thresholdFilterMax;
      threshFilterMaxVal.textContent = j.thresholdFilterMax;
      depthRangeToggle.checked = j.thresholdFilterEnable;
      threshFilterMinSlider.disabled = !j.thresholdFilterEnable;
      threshFilterMaxSlider.disabled = !j.thresholdFilterEnable;
    });

  const fpsDisplay = document.getElementById('fpsDisplay');
  function refreshFps() {
    fetch('/fps')
      .then(r => r.json())
      .then(j => { fpsDisplay.textContent = j.fps.toFixed(1) + ' fps'; });
  }

  setInterval(refreshFps, 1000);
  refreshFps();
</script>
</body>
</html>
)HTML";

// ---- WebServer implementation ----

WebServer::WebServer(std::atomic<int>& thresholdMm,
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
                     bool colorEnabled)
    : thresholdMm_(thresholdMm)
    , thresholdEnabled_(thresholdEnabled)
    , dilateIterations_(dilateIterations)
    , blobDetectEnabled_(blobDetectEnabled)
    , maxBlobPixels_(maxBlobPixels)
    , minBlobPixels_(minBlobPixels)
    , fpsTenths_(fpsTenths)
    , confidenceThreshold_(confidenceThreshold)
    , extendedDisparity_(extendedDisparity)
    , stereoPreset_(stereoPreset)
    , configDirty_(configDirty)
    , restartRequested_(restartRequested)
    , monoResolution_(monoResolution)
    , cameraFps_(cameraFps)
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
    blobsCv_.notify_all();   // wake any blocked SSE handlers
    depthCv_.notify_all();   // wake any blocked MJPEG handlers
    colorCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WebServer::updateColorFrame(const uint8_t* bgr, int width, int height) {
    {
        std::lock_guard<std::mutex> lock(frameMtx_);
        size_t sz = static_cast<size_t>(width) * height * 3;
        colorBgr_.resize(sz);
        std::memcpy(colorBgr_.data(), bgr, sz);
        colorW_ = width;
        colorH_ = height;
        colorSeq_++;
    }
    colorCv_.notify_all();
}

void WebServer::updateDepthFrame(const uint8_t* bgr, int width, int height) {
    {
        std::lock_guard<std::mutex> lock(frameMtx_);
        size_t sz = static_cast<size_t>(width) * height * 3;
        depthBgr_.resize(sz);
        std::memcpy(depthBgr_.data(), bgr, sz);
        depthW_ = width;
        depthH_ = height;
        depthSeq_++;
    }
    depthCv_.notify_all();
}

void WebServer::updateBlobs(const std::string& json) {
    {
        std::lock_guard<std::mutex> lock(frameMtx_);
        blobsJson_ = json;
        blobsSeq_++;
    }
    blobsCv_.notify_all();
}

PostProcSettings WebServer::getPostProcSettings() {
    std::lock_guard<std::mutex> lock(postProcMtx_);
    return postProc_;
}

void WebServer::saveSettings() {
    PostProcSettings pp;
    { std::lock_guard<std::mutex> lock(postProcMtx_); pp = postProc_; }

    std::string json =
        std::string("{\n"
        "  \"thresholdMm\": ") + std::to_string(thresholdMm_.load()) + ",\n"
        "  \"thresholdEnabled\": " + (thresholdEnabled_.load() ? "true" : "false") + ",\n"
        "  \"dilateIterations\": " + std::to_string(dilateIterations_.load()) + ",\n"
        "  \"blobDetectEnabled\": " + (blobDetectEnabled_.load() ? "true" : "false") + ",\n"
        "  \"maxBlobPixels\": " + std::to_string(maxBlobPixels_.load()) + ",\n"
        "  \"minBlobPixels\": " + std::to_string(minBlobPixels_.load()) + ",\n"
        "  \"confidenceThreshold\": " + std::to_string(confidenceThreshold_.load()) + ",\n"
        "  \"extendedDisparity\": " + (extendedDisparity_.load() ? "true" : "false") + ",\n"
        "  \"stereoPreset\": " + std::to_string(stereoPreset_.load()) + ",\n"
        "  \"monoResolution\": " + std::to_string(monoResolution_.load()) + ",\n"
        "  \"cameraFps\": " + std::to_string(cameraFps_.load()) + ",\n"
        "  \"medianKernel\": " + std::to_string(pp.medianKernel) + ",\n"
        "  \"spatialEnable\": " + (pp.spatialEnable ? "true" : "false") + ",\n"
        "  \"spatialAlpha\": " + std::to_string(pp.spatialAlpha) + ",\n"
        "  \"spatialDelta\": " + std::to_string(pp.spatialDelta) + ",\n"
        "  \"spatialIter\": " + std::to_string(pp.spatialIter) + ",\n"
        "  \"temporalEnable\": " + (pp.temporalEnable ? "true" : "false") + ",\n"
        "  \"temporalAlpha\": " + std::to_string(pp.temporalAlpha) + ",\n"
        "  \"temporalDelta\": " + std::to_string(pp.temporalDelta) + ",\n"
        "  \"temporalPersistency\": " + std::to_string(pp.temporalPersistency) + ",\n"
        "  \"speckleEnable\": " + (pp.speckleEnable ? "true" : "false") + ",\n"
        "  \"speckleRange\": " + std::to_string(pp.speckleRange) + ",\n"
        "  \"speckleDiff\": " + std::to_string(pp.speckleDiff) + ",\n"
        "  \"decimationFactor\": " + std::to_string(pp.decimationFactor) + ",\n"
        "  \"decimationMode\": " + std::to_string(pp.decimationMode) + ",\n"
        "  \"subpixelEnable\": " + (pp.subpixelEnable ? "true" : "false") + ",\n"
        "  \"subpixelBits\": " + std::to_string(pp.subpixelBits) + ",\n"
        "  \"disparityShift\": " + std::to_string(pp.disparityShift) + ",\n"
        "  \"lrCheckThreshold\": " + std::to_string(pp.lrCheckThreshold) + ",\n"
        "  \"brightnessFilterMin\": " + std::to_string(pp.brightnessFilterMin) + ",\n"
        "  \"brightnessFilterMax\": " + std::to_string(pp.brightnessFilterMax) + ",\n"
        "  \"thresholdFilterEnable\": " + (pp.thresholdFilterEnable ? "true" : "false") + ",\n"
        "  \"thresholdFilterMin\": " + std::to_string(pp.thresholdFilterMin) + ",\n"
        "  \"thresholdFilterMax\": " + std::to_string(pp.thresholdFilterMax) + ",\n"
        "  \"spatialHoleFillingRadius\": " + std::to_string(pp.spatialHoleFillingRadius) + ",\n"
        "  \"lumaDenoise\": " + std::to_string(pp.lumaDenoise) + ",\n"
        "  \"antiBanding\": " + std::to_string(pp.antiBanding) + ",\n"
        "  \"aeCompensation\": " + std::to_string(pp.aeCompensation) + "\n"
        "}\n";

    // Write to temp file then rename for atomic update
    std::ofstream tmp("settings.json.tmp");
    if (!tmp) { std::cerr << "Failed to write settings.json.tmp" << std::endl; return; }
    tmp << json;
    tmp.close();
    std::remove("settings.json");
    std::rename("settings.json.tmp", "settings.json");
}

// Simple helper: find "key": in text and extract the value (int or bool)
static bool jsonInt(const std::string& text, const std::string& key, int& out) {
    std::string needle = "\"" + key + "\":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    if (pos >= text.size()) return false;
    // Check for negative sign
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

static bool jsonBool(const std::string& text, const std::string& key, bool& out) {
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

void WebServer::loadSettings() {
    std::ifstream file("settings.json");
    if (!file) return;  // No file yet — use defaults

    std::string text((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    int iv; bool bv;
    if (jsonInt(text, "thresholdMm", iv)) thresholdMm_.store(iv);
    if (jsonBool(text, "thresholdEnabled", bv)) thresholdEnabled_.store(bv);
    if (jsonInt(text, "dilateIterations", iv)) dilateIterations_.store(iv);
    if (jsonBool(text, "blobDetectEnabled", bv)) blobDetectEnabled_.store(bv);
    if (jsonInt(text, "maxBlobPixels", iv)) maxBlobPixels_.store(iv);
    if (jsonInt(text, "minBlobPixels", iv)) minBlobPixels_.store(iv);
    if (jsonInt(text, "confidenceThreshold", iv)) confidenceThreshold_.store(iv);
    if (jsonBool(text, "extendedDisparity", bv)) extendedDisparity_.store(bv);
    if (jsonInt(text, "stereoPreset", iv)) stereoPreset_.store(iv);
    if (jsonInt(text, "monoResolution", iv)) monoResolution_.store(iv);
    if (jsonInt(text, "cameraFps", iv)) cameraFps_.store(iv);

    {
        std::lock_guard<std::mutex> lock(postProcMtx_);
        if (jsonInt(text, "medianKernel", iv)) postProc_.medianKernel = iv;
        if (jsonBool(text, "spatialEnable", bv)) postProc_.spatialEnable = bv;
        if (jsonInt(text, "spatialAlpha", iv)) postProc_.spatialAlpha = iv;
        if (jsonInt(text, "spatialDelta", iv)) postProc_.spatialDelta = iv;
        if (jsonInt(text, "spatialIter", iv)) postProc_.spatialIter = iv;
        if (jsonBool(text, "temporalEnable", bv)) postProc_.temporalEnable = bv;
        if (jsonInt(text, "temporalAlpha", iv)) postProc_.temporalAlpha = iv;
        if (jsonInt(text, "temporalDelta", iv)) postProc_.temporalDelta = iv;
        if (jsonInt(text, "temporalPersistency", iv)) postProc_.temporalPersistency = iv;
        if (jsonBool(text, "speckleEnable", bv)) postProc_.speckleEnable = bv;
        if (jsonInt(text, "speckleRange", iv)) postProc_.speckleRange = iv;
        if (jsonInt(text, "speckleDiff", iv)) postProc_.speckleDiff = iv;
        if (jsonInt(text, "decimationFactor", iv)) postProc_.decimationFactor = iv;
        if (jsonInt(text, "decimationMode", iv)) postProc_.decimationMode = iv;
        if (jsonBool(text, "subpixelEnable", bv)) postProc_.subpixelEnable = bv;
        if (jsonInt(text, "subpixelBits", iv)) postProc_.subpixelBits = iv;
        if (jsonInt(text, "disparityShift", iv)) postProc_.disparityShift = iv;
        if (jsonInt(text, "lrCheckThreshold", iv)) postProc_.lrCheckThreshold = iv;
        if (jsonInt(text, "brightnessFilterMin", iv)) postProc_.brightnessFilterMin = iv;
        if (jsonInt(text, "brightnessFilterMax", iv)) postProc_.brightnessFilterMax = iv;
        if (jsonBool(text, "thresholdFilterEnable", bv)) postProc_.thresholdFilterEnable = bv;
        if (jsonInt(text, "thresholdFilterMin", iv)) postProc_.thresholdFilterMin = iv;
        if (jsonInt(text, "thresholdFilterMax", iv)) postProc_.thresholdFilterMax = iv;
        if (jsonInt(text, "spatialHoleFillingRadius", iv)) postProc_.spatialHoleFillingRadius = iv;
        if (jsonInt(text, "lumaDenoise", iv)) postProc_.lumaDenoise = iv;
        if (jsonInt(text, "antiBanding", iv)) postProc_.antiBanding = iv;
        if (jsonInt(text, "aeCompensation", iv)) postProc_.aeCompensation = iv;
    }

    std::cout << "Loaded settings from settings.json" << std::endl;
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
            std::string target = "<img id=\"colorImg\" src=\"/color.mjpeg\" alt=\"Color\">";
            auto pos = html.find(target);
            if (pos != std::string::npos)
                html.replace(pos, target.size(), "");
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

    // GET /depth.mjpeg — MJPEG stream of depth frames
    svr.Get("/depth.mjpeg", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [this](size_t, httplib::DataSink& sink) {
                int lastSeq;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    lastSeq = depthSeq_;
                }
                {
                    std::unique_lock<std::mutex> lock(frameMtx_);
                    depthCv_.wait_for(lock, std::chrono::seconds(2),
                        [&] { return depthSeq_ != lastSeq || !running_; });
                }
                if (!running_) return false;
                std::vector<uint8_t> pixels;
                int w, h;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    if (depthBgr_.empty()) return true;
                    pixels = depthBgr_;
                    w = depthW_;
                    h = depthH_;
                }
                auto jpeg = encodeJpeg(pixels.data(), w, h);
                std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                    + std::to_string(jpeg.size()) + "\r\n\r\n";
                sink.write(header.data(), header.size());
                sink.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                sink.write("\r\n", 2);
                return true;
            });
    });

    // GET /color.mjpeg — MJPEG stream of color frames
    svr.Get("/color.mjpeg", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [this](size_t, httplib::DataSink& sink) {
                int lastSeq;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    lastSeq = colorSeq_;
                }
                {
                    std::unique_lock<std::mutex> lock(frameMtx_);
                    colorCv_.wait_for(lock, std::chrono::seconds(2),
                        [&] { return colorSeq_ != lastSeq || !running_; });
                }
                if (!running_) return false;
                std::vector<uint8_t> pixels;
                int w, h;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    if (colorBgr_.empty()) return true;
                    pixels = colorBgr_;
                    w = colorW_;
                    h = colorH_;
                }
                auto jpeg = encodeJpeg(pixels.data(), w, h);
                std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                    + std::to_string(jpeg.size()) + "\r\n\r\n";
                sink.write(header.data(), header.size());
                sink.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                sink.write("\r\n", 2);
                return true;
            });
    });

    // GET /threshold — get or set software threshold
    svr.Get("/threshold", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        if (req.has_param("enabled")) {
            thresholdEnabled_.store(req.get_param_value("enabled") == "1");
            changed = true;
        }
        if (req.has_param("value")) {
            int val = std::stoi(req.get_param_value("value"));
            if (val < 200) val = 200;
            if (val > 1200) val = 1200;
            thresholdMm_.store(val);
            changed = true;
        }
        if (req.has_param("dilate")) {
            int val = std::stoi(req.get_param_value("dilate"));
            if (val < 0) val = 0;
            if (val > 10) val = 10;
            dilateIterations_.store(val);
            changed = true;
        }
        if (changed) saveSettings();
        res.set_content("{\"threshold\":" + std::to_string(thresholdMm_.load()) +
                        ",\"enabled\":" + (thresholdEnabled_.load() ? "true" : "false") +
                        ",\"dilate\":" + std::to_string(dilateIterations_.load()) + "}",
                        "application/json");
    });

    // GET /blobdetect — get or set blob detection settings
    svr.Get("/blobdetect", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        if (req.has_param("enabled")) {
            blobDetectEnabled_.store(req.get_param_value("enabled") == "1");
            changed = true;
        }
        if (req.has_param("maxsize")) {
            int val = std::stoi(req.get_param_value("maxsize"));
            if (val < 10) val = 10;
            if (val > 100000) val = 100000;
            maxBlobPixels_.store(val);
            changed = true;
        }
        if (req.has_param("minsize")) {
            int val = std::stoi(req.get_param_value("minsize"));
            if (val < 1) val = 1;
            if (val > 100000) val = 100000;
            minBlobPixels_.store(val);
            changed = true;
        }
        if (changed) saveSettings();
        bool enabled = blobDetectEnabled_.load();
        int maxsz = maxBlobPixels_.load();
        int minsz = minBlobPixels_.load();
        res.set_content("{\"enabled\":" + std::string(enabled ? "true" : "false") +
                        ",\"maxsize\":" + std::to_string(maxsz) +
                        ",\"minsize\":" + std::to_string(minsz) + "}",
                        "application/json");
    });

    // GET /stereoconfig — get or set stereo depth settings
    svr.Get("/stereoconfig", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        if (req.has_param("confidence")) {
            int val = std::stoi(req.get_param_value("confidence"));
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            confidenceThreshold_.store(val);
            configDirty_.store(true);
            changed = true;
        }
        if (req.has_param("extended")) {
            extendedDisparity_.store(req.get_param_value("extended") == "1");
            configDirty_.store(true);
            changed = true;
        }
        if (req.has_param("preset")) {
            int val = std::stoi(req.get_param_value("preset"));
            if (val < 0) val = 0;
            if (val > 3) val = 3;
            stereoPreset_.store(val);
            restartRequested_.store(true);
            changed = true;
        }
        if (req.has_param("resolution")) {
            int val = std::stoi(req.get_param_value("resolution"));
            if (val < 0) val = 0;
            if (val > 3) val = 3;
            monoResolution_.store(val);
            restartRequested_.store(true);
            changed = true;
        }
        if (req.has_param("fps")) {
            int val = std::stoi(req.get_param_value("fps"));
            if (val < 1) val = 1;
            if (val > 60) val = 60;
            cameraFps_.store(val);
            restartRequested_.store(true);
            changed = true;
        }
        if (changed) saveSettings();
        int conf = confidenceThreshold_.load();
        bool ext = extendedDisparity_.load();
        int preset = stereoPreset_.load();
        int resolution = monoResolution_.load();
        int fps = cameraFps_.load();
        res.set_content("{\"confidence\":" + std::to_string(conf) +
                        ",\"extended\":" + std::string(ext ? "true" : "false") +
                        ",\"preset\":" + std::to_string(preset) +
                        ",\"resolution\":" + std::to_string(resolution) +
                        ",\"fps\":" + std::to_string(fps) + "}",
                        "application/json");
    });

    // GET /postproc — get or set post-processing filter settings
    svr.Get("/postproc", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(postProcMtx_);
            if (req.has_param("median")) {
                int v = std::stoi(req.get_param_value("median"));
                if (v != 0 && v != 3 && v != 5 && v != 7) v = 7;
                postProc_.medianKernel = v;
                changed = true;
            }
            if (req.has_param("spatialEnable")) {
                postProc_.spatialEnable = req.get_param_value("spatialEnable") == "1";
                changed = true;
            }
            if (req.has_param("spatialAlpha")) {
                int v = std::stoi(req.get_param_value("spatialAlpha"));
                if (v < 0) v = 0; if (v > 100) v = 100;
                postProc_.spatialAlpha = v;
                changed = true;
            }
            if (req.has_param("spatialDelta")) {
                postProc_.spatialDelta = std::stoi(req.get_param_value("spatialDelta"));
                changed = true;
            }
            if (req.has_param("spatialIter")) {
                int v = std::stoi(req.get_param_value("spatialIter"));
                if (v < 1) v = 1; if (v > 5) v = 5;
                postProc_.spatialIter = v;
                changed = true;
            }
            if (req.has_param("temporalEnable")) {
                postProc_.temporalEnable = req.get_param_value("temporalEnable") == "1";
                changed = true;
            }
            if (req.has_param("temporalAlpha")) {
                int v = std::stoi(req.get_param_value("temporalAlpha"));
                if (v < 0) v = 0; if (v > 100) v = 100;
                postProc_.temporalAlpha = v;
                changed = true;
            }
            if (req.has_param("temporalDelta")) {
                postProc_.temporalDelta = std::stoi(req.get_param_value("temporalDelta"));
                changed = true;
            }
            if (req.has_param("temporalPersist")) {
                int v = std::stoi(req.get_param_value("temporalPersist"));
                if (v < 0) v = 0; if (v > 8) v = 8;
                postProc_.temporalPersistency = v;
                changed = true;
            }
            if (req.has_param("speckleEnable")) {
                postProc_.speckleEnable = req.get_param_value("speckleEnable") == "1";
                changed = true;
            }
            if (req.has_param("speckleRange")) {
                int v = std::stoi(req.get_param_value("speckleRange"));
                if (v < 0) v = 0; if (v > 240) v = 240;
                postProc_.speckleRange = v;
                changed = true;
            }
            if (req.has_param("speckleDiff")) {
                int v = std::stoi(req.get_param_value("speckleDiff"));
                if (v < 0) v = 0; if (v > 16) v = 16;
                postProc_.speckleDiff = v;
                changed = true;
            }
            if (req.has_param("decimation")) {
                int v = std::stoi(req.get_param_value("decimation"));
                if (v < 1) v = 1; if (v > 4) v = 4;
                postProc_.decimationFactor = v;
                changed = true;
            }
            if (req.has_param("decimationMode")) {
                int v = std::stoi(req.get_param_value("decimationMode"));
                if (v < 0) v = 0; if (v > 2) v = 2;
                postProc_.decimationMode = v;
                changed = true;
            }
            if (req.has_param("subpixelEnable")) {
                postProc_.subpixelEnable = req.get_param_value("subpixelEnable") == "1";
                changed = true;
            }
            if (req.has_param("subpixelBits")) {
                int v = std::stoi(req.get_param_value("subpixelBits"));
                if (v < 3) v = 3; if (v > 5) v = 5;
                postProc_.subpixelBits = v;
                changed = true;
            }
            if (req.has_param("disparityShift")) {
                int v = std::stoi(req.get_param_value("disparityShift"));
                if (v < 0) v = 0; if (v > 128) v = 128;
                postProc_.disparityShift = v;
                changed = true;
            }
            if (req.has_param("lrCheckThreshold")) {
                int v = std::stoi(req.get_param_value("lrCheckThreshold"));
                if (v < 0) v = 0; if (v > 128) v = 128;
                postProc_.lrCheckThreshold = v;
                changed = true;
            }
            if (req.has_param("brightnessFilterMin")) {
                int v = std::stoi(req.get_param_value("brightnessFilterMin"));
                if (v < 0) v = 0; if (v > 255) v = 255;
                postProc_.brightnessFilterMin = v;
                changed = true;
            }
            if (req.has_param("brightnessFilterMax")) {
                int v = std::stoi(req.get_param_value("brightnessFilterMax"));
                if (v < 0) v = 0; if (v > 256) v = 256;
                postProc_.brightnessFilterMax = v;
                changed = true;
            }
            if (req.has_param("thresholdFilterEnable")) {
                postProc_.thresholdFilterEnable = req.get_param_value("thresholdFilterEnable") == "1";
                changed = true;
            }
            if (req.has_param("thresholdFilterMin")) {
                int v = std::stoi(req.get_param_value("thresholdFilterMin"));
                if (v < 0) v = 0; if (v > 1200) v = 1200;
                postProc_.thresholdFilterMin = v;
                changed = true;
            }
            if (req.has_param("thresholdFilterMax")) {
                int v = std::stoi(req.get_param_value("thresholdFilterMax"));
                if (v < 0) v = 0; if (v > 1200) v = 1200;
                postProc_.thresholdFilterMax = v;
                changed = true;
            }
            if (req.has_param("spatialHoleFilling")) {
                int v = std::stoi(req.get_param_value("spatialHoleFilling"));
                if (v < 0) v = 0; if (v > 16) v = 16;
                postProc_.spatialHoleFillingRadius = v;
                changed = true;
            }
            if (req.has_param("lumaDenoise")) {
                int v = std::stoi(req.get_param_value("lumaDenoise"));
                if (v < 0) v = 0; if (v > 4) v = 4;
                postProc_.lumaDenoise = v;
                changed = true;
            }
            if (req.has_param("antiBanding")) {
                int v = std::stoi(req.get_param_value("antiBanding"));
                if (v < 0) v = 0; if (v > 3) v = 3;
                postProc_.antiBanding = v;
                changed = true;
            }
            if (req.has_param("aeCompensation")) {
                int v = std::stoi(req.get_param_value("aeCompensation"));
                if (v < -9) v = -9; if (v > 9) v = 9;
                postProc_.aeCompensation = v;
                changed = true;
            }
        }
        if (changed) {
            configDirty_.store(true);
            saveSettings();
        }
        PostProcSettings pp;
        { std::lock_guard<std::mutex> lock(postProcMtx_); pp = postProc_; }
        res.set_content(
            "{\"median\":" + std::to_string(pp.medianKernel) +
            ",\"spatialEnable\":" + (pp.spatialEnable ? "true" : "false") +
            ",\"spatialAlpha\":" + std::to_string(pp.spatialAlpha) +
            ",\"spatialDelta\":" + std::to_string(pp.spatialDelta) +
            ",\"spatialIter\":" + std::to_string(pp.spatialIter) +
            ",\"spatialHoleFilling\":" + std::to_string(pp.spatialHoleFillingRadius) +
            ",\"temporalEnable\":" + (pp.temporalEnable ? "true" : "false") +
            ",\"temporalAlpha\":" + std::to_string(pp.temporalAlpha) +
            ",\"temporalDelta\":" + std::to_string(pp.temporalDelta) +
            ",\"temporalPersist\":" + std::to_string(pp.temporalPersistency) +
            ",\"speckleEnable\":" + (pp.speckleEnable ? "true" : "false") +
            ",\"speckleRange\":" + std::to_string(pp.speckleRange) +
            ",\"speckleDiff\":" + std::to_string(pp.speckleDiff) +
            ",\"decimation\":" + std::to_string(pp.decimationFactor) +
            ",\"decimationMode\":" + std::to_string(pp.decimationMode) +
            ",\"subpixelEnable\":" + (pp.subpixelEnable ? "true" : "false") +
            ",\"subpixelBits\":" + std::to_string(pp.subpixelBits) +
            ",\"disparityShift\":" + std::to_string(pp.disparityShift) +
            ",\"lrCheckThreshold\":" + std::to_string(pp.lrCheckThreshold) +
            ",\"brightnessFilterMin\":" + std::to_string(pp.brightnessFilterMin) +
            ",\"brightnessFilterMax\":" + std::to_string(pp.brightnessFilterMax) +
            ",\"thresholdFilterEnable\":" + (pp.thresholdFilterEnable ? "true" : "false") +
            ",\"thresholdFilterMin\":" + std::to_string(pp.thresholdFilterMin) +
            ",\"thresholdFilterMax\":" + std::to_string(pp.thresholdFilterMax) +
            ",\"lumaDenoise\":" + std::to_string(pp.lumaDenoise) +
            ",\"antiBanding\":" + std::to_string(pp.antiBanding) +
            ",\"aeCompensation\":" + std::to_string(pp.aeCompensation) + "}",
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
