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

// ---- HTML page (split into segments to stay under MSVC 16KB raw string limit) ----

static const std::string kHtmlHead = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>DepthPalette (Orbbec)</title>
<style>
  body { background: #1a1a2e; color: #eee; font-family: Arial, sans-serif;
         margin: 0; display: flex; flex-direction: column; align-items: center; }
  h1 { margin: 16px 0 8px; }
  .images { display: flex; gap: 8px; margin: 8px 0; }
  .images img { border: 1px solid #444; max-width: 640px; height: auto; transform: scaleX(-1); }
  .images canvas { border: 1px solid #444; max-width: 640px; height: auto; transform: scaleX(-1); }
  .depth-wrap { position: relative; }
  .depth-wrap canvas { position: absolute; top: 0; left: 0; }
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
  h2 { font-size: 15px; margin: 12px 0 4px; color: #8cf; width: 100%; }
  .unsupported { display: none !important; }
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
<h1>DepthPalette (Orbbec)</h1>
)HTML";

static const std::string kHtmlControls1 = R"HTML(
<div class="controls">
  <div class="toggle">
    <label class="switch">
      <input id="threshToggle" type="checkbox" checked>
      <span class="slider-track"></span>
    </label>
    <span>Threshold<span class="help-btn" onclick="showHelp('Threshold','Software depth threshold. Pixels closer than this distance are shown as black.')">?</span></span>
  </div>
  <label>
    <input id="threshSlider" class="slider" type="range" min="200" max="1200" step="10" value="550">
  </label>
  <span id="threshVal" class="val">550 mm</span>
  <label>Dilate<span class="help-btn" onclick="showHelp('Dilate','Expands black regions by N pixels. Helps connect nearby blobs.')">?</span>:
    <input id="dilateSlider" class="slider" type="range" min="0" max="10" step="1" value="0" style="width:100px">
  </label>
  <span id="dilateVal" class="val">0</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="blobToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Blob Detection<span class="help-btn" onclick="showHelp('Blob Detection','Connected-component blob detection on depth image.')">?</span></span>
  </div>
  <label>Max:
    <input id="blobSlider" class="slider" type="range" min="100" max="50000" step="100" value="5000">
  </label>
  <span id="blobVal" class="val">5000 px</span>
  <label>Min:
    <input id="minBlobSlider" class="slider" type="range" min="1" max="4000" step="1" value="20">
  </label>
  <span id="minBlobVal" class="val">20 px</span>
  <div class="sep"></div>
  <label>Mode<span class="help-btn" onclick="showHelp('Mode','No Dots: depth image only. Dots Only: shows blob positions on black canvas. Dots+Pitches: plays a pitched tone when a new blob appears (pitch = horizontal position, 2 octaves C4-C6). Dots+Sound: placeholder for future audio mode.')">?</span>:
    <select id="modeSelect">
      <option value="0">No Dots</option>
      <option value="1">Dots Only</option>
      <option value="2">Dots + Pitches</option>
      <option value="3">Dots + Sound</option>
    </select>
  </label>
  <label>Key:
    <select id="keySelect">
      <option value="0">C</option>
      <option value="1">C&#9839;</option>
      <option value="2">D</option>
      <option value="3">D&#9839;</option>
      <option value="4">E</option>
      <option value="5">F</option>
      <option value="6">F&#9839;</option>
      <option value="7">G</option>
      <option value="8">G&#9839;</option>
      <option value="9">A</option>
      <option value="10">A&#9839;</option>
      <option value="11">B</option>
    </select>
  </label>
  <label>Scale:
    <select id="scaleSelect">
      <option value="chromatic">Chromatic</option>
      <option value="major">Major</option>
      <option value="minor">Minor</option>
      <option value="pentatonic">Pentatonic</option>
      <option value="blues">Blues</option>
      <option value="lydian">Lydian</option>
      <option value="dorian">Dorian</option>
      <option value="mixolydian">Mixolydian</option>
      <option value="fifths">Fifths</option>
      <option value="wholetone">Whole Tone</option>
    </select>
  </label>
  <label>Decay:
    <input id="decaySlider" class="slider" type="range" min="1" max="50" step="1" value="20" style="width:100px">
  </label>
  <span id="decayVal" class="val">2.0 s</span>
  <label>Release:
    <input id="releaseSlider" class="slider" type="range" min="0" max="50" step="1" value="5" style="width:100px">
  </label>
  <span id="releaseVal" class="val">0.5 s</span>
  <label>Move:
    <input id="moveThreshSlider" class="slider" type="range" min="0" max="100" step="1" value="0" style="width:100px">
  </label>
  <span id="moveThreshVal" class="val">off</span>
  <label>Quantize:
    <select id="quantizeSelect">
      <option value="0">none</option>
      <option value="4">1</option>
      <option value="2">1/2</option>
      <option value="1">1/4</option>
      <option value="0.5">1/8</option>
      <option value="0.25">1/16</option>
    </select>
  </label>
  <label>Tempo:
    <input id="tempoSlider" class="slider" type="range" min="60" max="240" step="1" value="120" style="width:100px">
  </label>
  <span id="tempoVal" class="val">120 bpm</span>
  <div class="sep"></div>
  <div class="toggle">
    <label class="switch">
      <input id="showDepthToggle" type="checkbox">
      <span class="slider-track"></span>
    </label>
    <span>Show Depth</span>
  </div>
  <div class="sep"></div>
  <span id="fpsDisplay" class="fps">-- fps</span>
</div>
)HTML";

static const std::string kHtmlControls2 = R"HTML(
<div class="controls">
  <label>Resolution<span class="help-btn" onclick="showHelp('Resolution','Depth stream resolution. Requires restart.')">?</span>:
    <select id="resolutionSelect">
      <option value="0">1280x800</option>
      <option value="1">848x480</option>
    </select>
  </label>
  <div class="sep"></div>
  <label>Camera FPS<span class="help-btn" onclick="showHelp('Camera FPS','Sensor frame rate. Requires restart.')">?</span>:
    <input id="camFpsSlider" class="slider" type="range" min="1" max="60" step="1" value="30">
  </label>
  <span id="camFpsVal" class="val">30 fps</span>
  <div class="sep"></div>
  <span id="depthPresetWrap">
    <label>Preset<span class="help-btn" onclick="showHelp('Depth Preset','Camera depth work mode preset. Requires restart.')">?</span>:
      <select id="depthPreset"></select>
    </label>
  </span>
  <div class="sep"></div>
  <span id="dispRangeWrap">
    <label>Disparity<span class="help-btn" onclick="showHelp('Disparity Range','Stereo search range. 256 = more range but slower. Requires restart.')">?</span>:
      <select id="dispRange">
        <option value="1">128</option>
        <option value="2">256</option>
      </select>
    </label>
  </span>
  <span id="restartNote" class="restart-note"></span>
</div>
)HTML";

static const std::string kHtmlControls3 = R"HTML(
<div class="controls">
  <h2>Depth Filters</h2>
  <span id="speckleWrap">
    <div class="toggle">
      <label class="switch">
        <input id="speckleToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Speckle Filter<span class="help-btn" onclick="showHelp('Speckle Filter','On-device noise removal. Removes small isolated depth patches.')">?</span></span>
    </div>
    <label>Max Size:
      <input id="speckleSize" class="slider" type="range" min="0" max="1000" step="1" value="200" style="width:120px">
    </label>
    <span id="speckleSizeVal" class="val">200</span>
    <label>Max Diff:
      <input id="speckleDiff" class="slider" type="range" min="0" max="255" step="1" value="64" style="width:120px">
    </label>
    <span id="speckleDiffVal" class="val">64</span>
  </span>
  <div class="sep"></div>
  <span id="confidenceWrap">
    <div class="toggle">
      <label class="switch">
        <input id="confToggle" type="checkbox" checked>
        <span class="slider-track"></span>
      </label>
      <span>Confidence<span class="help-btn" onclick="showHelp('Confidence Filter','Filters out low-confidence depth pixels.')">?</span></span>
    </div>
    <label>Threshold:
      <input id="confSlider" class="slider" type="range" min="0" max="255" step="1" value="15" style="width:120px">
    </label>
    <span id="confVal" class="val">15</span>
  </span>
  <div class="sep"></div>
  <span id="holeFillWrap">
    <div class="toggle">
      <label class="switch">
        <input id="holeFillToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Hole Fill<span class="help-btn" onclick="showHelp('Hole Filling','Fills in missing depth values using surrounding data.')">?</span></span>
    </div>
  </span>
  <div class="sep"></div>
  <span id="hwDepthWrap">
    <label>HW Min<span class="help-btn" onclick="showHelp('HW Depth Min','Hardware minimum depth in mm.')">?</span>:
      <input id="hwMinSlider" class="slider" type="range" min="0" max="2000" step="10" value="100" style="width:120px">
    </label>
    <span id="hwMinVal" class="val">100 mm</span>
    <label>HW Max:
      <input id="hwMaxSlider" class="slider" type="range" min="500" max="16000" step="100" value="10000" style="width:120px">
    </label>
    <span id="hwMaxVal" class="val">10000 mm</span>
  </span>
</div>
)HTML";

static const std::string kHtmlControls4 = R"HTML(
<div class="controls">
  <h2>Laser & Exposure</h2>
  <span id="laserWrap">
    <div class="toggle">
      <label class="switch">
        <input id="laserToggle" type="checkbox" checked>
        <span class="slider-track"></span>
      </label>
      <span>Laser<span class="help-btn" onclick="showHelp('Laser','Enable/disable the IR dot projector.')">?</span></span>
    </div>
    <label>Power:
      <input id="laserSlider" class="slider" type="range" min="0" max="100" step="1" value="100" style="width:120px">
    </label>
    <span id="laserVal" class="val">100</span>
  </span>
  <div class="sep"></div>
  <span id="depthExpWrap">
    <div class="toggle">
      <label class="switch">
        <input id="depthAEToggle" type="checkbox" checked>
        <span class="slider-track"></span>
      </label>
      <span>Depth Auto Exp<span class="help-btn" onclick="showHelp('Depth Auto Exposure','Automatic exposure for depth/IR sensor.')">?</span></span>
    </div>
    <label>Exposure:
      <input id="depthExpSlider" class="slider" type="range" min="1" max="33000" step="1" value="8330" style="width:120px">
    </label>
    <span id="depthExpVal" class="val">8330</span>
    <label>Gain:
      <input id="depthGainSlider" class="slider" type="range" min="1" max="128" step="1" value="16" style="width:80px">
    </label>
    <span id="depthGainVal" class="val">16</span>
  </span>
  <div class="sep"></div>
  <span id="hdrWrap">
    <div class="toggle">
      <label class="switch">
        <input id="hdrToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>HDR Merge<span class="help-btn" onclick="showHelp('HDR Merge','Merges multiple exposures for improved depth range.')">?</span></span>
    </div>
  </span>
  <div class="sep"></div>
  <span id="precisionWrap">
    <label>Precision<span class="help-btn" onclick="showHelp('Depth Precision','Depth precision level. May change depth unit.')">?</span>:
      <input id="precisionSlider" class="slider" type="range" min="0" max="10" step="1" value="0" style="width:100px">
    </label>
    <span id="precisionVal" class="val">0</span>
  </span>
</div>
)HTML";

static const std::string kHtmlControls5 = R"HTML(
<div class="controls">
  <h2>Mirror & Flip</h2>
  <span id="depthMirrorWrap">
    <div class="toggle">
      <label class="switch">
        <input id="depthMirrorToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Depth Mirror</span>
    </div>
  </span>
  <span id="depthFlipWrap">
    <div class="toggle">
      <label class="switch">
        <input id="depthFlipToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Depth Flip</span>
    </div>
  </span>
  <span id="colorMirrorWrap">
    <div class="toggle">
      <label class="switch">
        <input id="colorMirrorToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Color Mirror</span>
    </div>
  </span>
  <span id="colorFlipWrap">
    <div class="toggle">
      <label class="switch">
        <input id="colorFlipToggle" type="checkbox">
        <span class="slider-track"></span>
      </label>
      <span>Color Flip</span>
    </div>
  </span>
</div>
)HTML";

static const std::string kHtmlControls6 = R"HTML(
<div class="controls" id="colorControlsSection">
  <h2>Color Camera</h2>
  <span id="colorExpWrap">
    <div class="toggle">
      <label class="switch">
        <input id="colorAEToggle" type="checkbox" checked>
        <span class="slider-track"></span>
      </label>
      <span>Auto Exp</span>
    </div>
    <label>Exp:
      <input id="colorExpSlider" class="slider" type="range" min="1" max="10000" step="1" value="156" style="width:120px">
    </label>
    <span id="colorExpVal" class="val">156</span>
    <label>Gain:
      <input id="colorGainSlider" class="slider" type="range" min="0" max="128" step="1" value="0" style="width:80px">
    </label>
    <span id="colorGainVal" class="val">0</span>
  </span>
  <div class="sep"></div>
  <span id="colorWBWrap">
    <div class="toggle">
      <label class="switch">
        <input id="colorAWBToggle" type="checkbox" checked>
        <span class="slider-track"></span>
      </label>
      <span>Auto WB</span>
    </div>
    <label>WB:
      <input id="colorWBSlider" class="slider" type="range" min="2800" max="6500" step="10" value="4600" style="width:120px">
    </label>
    <span id="colorWBVal" class="val">4600</span>
  </span>
  <div class="sep"></div>
  <label>Bright:
    <input id="colorBrightSlider" class="slider" type="range" min="-64" max="64" step="1" value="0" style="width:80px">
  </label>
  <span id="colorBrightVal" class="val">0</span>
  <label>Sharp:
    <input id="colorSharpSlider" class="slider" type="range" min="0" max="100" step="1" value="0" style="width:80px">
  </label>
  <span id="colorSharpVal" class="val">0</span>
  <label>Sat:
    <input id="colorSatSlider" class="slider" type="range" min="0" max="100" step="1" value="0" style="width:80px">
  </label>
  <span id="colorSatVal" class="val">0</span>
  <label>Contrast:
    <input id="colorContrastSlider" class="slider" type="range" min="0" max="100" step="1" value="0" style="width:80px">
  </label>
  <span id="colorContrastVal" class="val">0</span>
  <label>Gamma:
    <input id="colorGammaSlider" class="slider" type="range" min="100" max="500" step="1" value="220" style="width:80px">
  </label>
  <span id="colorGammaVal" class="val">220</span>
</div>
)HTML";

static const std::string kHtmlImages = R"HTML(
<div class="images">
  <img id="colorImg" src="/color.mjpeg" alt="Color">
  <div class="depth-wrap">
    <img id="depthImg" src="/depth.mjpeg" alt="Depth (B/W)">
    <canvas id="dotsCanvas" style="display:none"></canvas>
  </div>
</div>
)HTML";

static const std::string kHtmlScript1 = R"HTML(
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
  const blobToggle = document.getElementById('blobToggle');
  const blobSlider = document.getElementById('blobSlider');
  const blobVal = document.getElementById('blobVal');
  const minBlobSlider = document.getElementById('minBlobSlider');
  const minBlobVal = document.getElementById('minBlobVal');
  const colorImg = document.getElementById('colorImg');
  const depthImg = document.getElementById('depthImg');
  const modeSelect = document.getElementById('modeSelect');
  const dotsCanvas = document.getElementById('dotsCanvas');
  const dotsCtx = dotsCanvas.getContext('2d');
  const showDepthToggle = document.getElementById('showDepthToggle');
  const resolutionSelect = document.getElementById('resolutionSelect');
  const camFpsSlider = document.getElementById('camFpsSlider');
  const camFpsVal = document.getElementById('camFpsVal');
  const restartNote = document.getElementById('restartNote');

  let restartTimer = null;
  function showRestart() {
    restartNote.textContent = 'Restarting...';
    if (restartTimer) clearTimeout(restartTimer);
    restartTimer = setTimeout(function() { restartNote.textContent = ''; }, 5000);
  }

  // Mode: 0=No Dots, 1=Dots Only, 2=Dots+Pitches, 3=Dots+Sound
  let currentMode = 0;
  let evtSource = null;
  let activeTones = new Map();
  let audioCtx = null;

  const keySelect = document.getElementById('keySelect');
  const scaleSelect = document.getElementById('scaleSelect');
  const decaySlider = document.getElementById('decaySlider');
  const decayValSpan = document.getElementById('decayVal');
  decaySlider.addEventListener('input', function() {
    decayValSpan.textContent = (decaySlider.value / 10).toFixed(1) + ' s';
  });
  decaySlider.addEventListener('change', function() {
    fetch('/soundsettings?soundDecay=' + decaySlider.value);
  });
  const releaseSlider = document.getElementById('releaseSlider');
  const releaseValSpan = document.getElementById('releaseVal');
  releaseSlider.addEventListener('input', function() {
    releaseValSpan.textContent = (releaseSlider.value / 10).toFixed(1) + ' s';
  });
  releaseSlider.addEventListener('change', function() {
    fetch('/soundsettings?soundRelease=' + releaseSlider.value);
  });
  const moveThreshSlider = document.getElementById('moveThreshSlider');
  const moveThreshValSpan = document.getElementById('moveThreshVal');
  moveThreshSlider.addEventListener('input', function() {
    const v = parseInt(moveThreshSlider.value);
    moveThreshValSpan.textContent = v === 0 ? 'off' : v + ' mm';
  });
  moveThreshSlider.addEventListener('change', function() {
    fetch('/soundsettings?soundMoveThresh=' + moveThreshSlider.value);
  });
  const quantizeSelect = document.getElementById('quantizeSelect');
  const tempoSlider = document.getElementById('tempoSlider');
  const tempoValSpan = document.getElementById('tempoVal');
  tempoSlider.addEventListener('input', function() {
    tempoValSpan.textContent = tempoSlider.value + ' bpm';
  });
  tempoSlider.addEventListener('change', function() {
    fetch('/soundsettings?soundTempo=' + tempoSlider.value);
  });
  let pendingTones = new Map();
  const noteNames = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  const scales = {
    chromatic:   [0,1,2,3,4,5,6,7,8,9,10,11],
    major:       [0,2,4,5,7,9,11],
    minor:       [0,2,3,5,7,8,10],
    pentatonic:  [0,2,4,7,9],
    blues:       [0,3,5,6,7,10],
    lydian:      [0,2,4,6,7,9,11],
    dorian:      [0,2,3,5,7,9,10],
    mixolydian:  [0,2,4,5,7,9,10],
    fifths:      [0,7],
    wholetone:   [0,2,4,6,8,10]
  };

  function startTone(freq, blobId, cx, cy) {
    if (!audioCtx) audioCtx = new AudioContext();
    if (audioCtx.state === 'suspended') audioCtx.resume();
    const osc = audioCtx.createOscillator();
    const g = audioCtx.createGain();
    const decay = decaySlider.value / 10;
    osc.type = 'sine';
    osc.frequency.value = freq;
    g.gain.setValueAtTime(0.3, audioCtx.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + decay);
    osc.connect(g).connect(audioCtx.destination);
    osc.start();
    activeTones.set(blobId, {osc: osc, gain: g, startTime: audioCtx.currentTime, cx: cx, cy: cy});
  }

  function cancelPending(blobId) {
    const p = pendingTones.get(blobId);
    if (p) { clearTimeout(p); pendingTones.delete(blobId); }
  }

  function stopTone(blobId) {
    cancelPending(blobId);
    const t = activeTones.get(blobId);
    if (!t) return;
    const rel = Math.max(0.01, releaseSlider.value / 10);
    t.gain.gain.cancelScheduledValues(audioCtx.currentTime);
    t.gain.gain.setValueAtTime(t.gain.gain.value, audioCtx.currentTime);
    t.gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + rel);
    t.osc.stop(audioCtx.currentTime + rel + 0.01);
    activeTones.delete(blobId);
  }

  function stopAllTones() {
    for (const id of pendingTones.keys()) cancelPending(id);
    for (const id of activeTones.keys()) stopTone(id);
  }

  function quantizeToNote(freq) {
    const key = parseInt(keySelect.value);
    const si = scales[scaleSelect.value] || scales.chromatic;
    const mf = 12 * Math.log2(freq / 440) + 69;
    let bestMidi = Math.round(mf);
    let bestDist = Infinity;
    const oct = Math.floor(mf / 12);
    for (let o = oct - 2; o <= oct + 2; o++) {
      for (const iv of si) {
        const m = o * 12 + key + iv;
        const d = Math.abs(m - mf);
        if (d < bestDist) { bestDist = d; bestMidi = m; }
      }
    }
    const f = 440 * Math.pow(2, (bestMidi - 69) / 12);
    const name = noteNames[((bestMidi % 12) + 12) % 12] + (Math.floor(bestMidi / 12) - 1);
    return { freq: f, name: name };
  }

  function triggerTone(b, j, label) {
    const ratio = 1.0 - (b.cx / j.w);
    const rawFreq = 261.63 * Math.pow(2, 2.0 * ratio);
    let freq = rawFreq;
    let info = '';
    if (currentMode === 2) {
      const q = quantizeToNote(rawFreq);
      freq = q.freq;
      info = ' note=' + q.name;
    }
    const qVal = parseFloat(quantizeSelect.value);
    if (qVal > 0) {
      const bpm = parseInt(tempoSlider.value);
      const beatSec = 60.0 / bpm * qVal / 4;
      const now = performance.now() / 1000;
      const nextBeat = Math.ceil(now / beatSec) * beatSec;
      const delayMs = Math.max(0, (nextBeat - now) * 1000);
      cancelPending(b.id);
      const fq = freq;
      const cx = b.cx;
      const cy = b.cy;
      const bid = b.id;
      console.log(label + ' id=' + bid + ' Mode=' + currentMode + ' raw=' + rawFreq.toFixed(2) + ' played=' + fq.toFixed(2) + info + ' qDelay=' + delayMs.toFixed(0) + 'ms');
      const tid = setTimeout(function() {
        pendingTones.delete(bid);
        startTone(fq, bid, cx, cy);
      }, delayMs);
      pendingTones.set(b.id, tid);
    } else {
      console.log(label + ' id=' + b.id + ' Mode=' + currentMode + ' raw=' + rawFreq.toFixed(2) + ' played=' + freq.toFixed(2) + info);
      startTone(freq, b.id, b.cx, b.cy);
    }
  }

  function checkNewBlobs(j) {
    if (!j.blobs || j.w <= 0) return;
    const moveThresh = parseInt(moveThreshSlider.value);
    const currentIds = new Set();
    for (const b of j.blobs) {
      currentIds.add(b.id);
      const existing = activeTones.get(b.id);
      if (!existing && !pendingTones.has(b.id)) {
        triggerTone(b, j, 'START');
      } else if (existing && moveThresh > 0) {
        const dx = b.cx - existing.cx;
        const dy = b.cy - existing.cy;
        const pxDist = Math.sqrt(dx * dx + dy * dy);
        const depth = b.avg > 0 ? b.avg : 500;
        const mmDist = pxDist * depth / 640;
        if (mmDist > moveThresh) {
          stopTone(b.id);
          triggerTone(b, j, 'MOVE');
        }
      }
    }
    for (const id of activeTones.keys()) {
      if (!currentIds.has(id)) {
        console.log('END id=' + id);
        stopTone(id);
      }
    }
    for (const id of pendingTones.keys()) {
      if (!currentIds.has(id)) {
        console.log('END(pending) id=' + id);
        cancelPending(id);
      }
    }
    if (audioCtx) {
      const now = audioCtx.currentTime;
      for (const [id, t] of activeTones) {
        if (now - t.startTime > 5) {
          console.log('TIMEOUT id=' + id);
          stopTone(id);
        }
      }
    }
  }

  function drawDots(j) {
    if (j.w > 0 && j.h > 0) { dotsCanvas.width = j.w; dotsCanvas.height = j.h; }
    dotsCtx.clearRect(0, 0, dotsCanvas.width, dotsCanvas.height);
    if (!showDepthToggle.checked) {
      dotsCtx.fillStyle = '#000';
      dotsCtx.fillRect(0, 0, dotsCanvas.width, dotsCanvas.height);
    }
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

  function onSseMessage(e) {
    const j = JSON.parse(e.data);
    drawDots(j);
    if (currentMode >= 2) checkNewBlobs(j);
  }

  function startDotsStream() {
    depthImg.style.display = showDepthToggle.checked ? '' : 'none';
    dotsCanvas.style.display = '';
    stopAllTones();
    evtSource = new EventSource('/events');
    evtSource.onmessage = onSseMessage;
  }

  function stopDotsStream() {
    depthImg.style.display = '';
    dotsCanvas.style.display = 'none';
    if (evtSource) { evtSource.close(); evtSource = null; }
    stopAllTones();
  }

  showDepthToggle.addEventListener('change', function() {
    if (currentMode >= 1) {
      depthImg.style.display = showDepthToggle.checked ? '' : 'none';
    }
    fetch('/soundsettings?showDepth=' + (showDepthToggle.checked ? '1' : '0'));
  });

  keySelect.addEventListener('change', function() {
    fetch('/soundsettings?soundKey=' + keySelect.value);
  });
  scaleSelect.addEventListener('change', function() {
    fetch('/soundsettings?soundScale=' + scaleSelect.value);
  });
  quantizeSelect.addEventListener('change', function() {
    fetch('/soundsettings?soundQuantize=' + quantizeSelect.value);
  });

  modeSelect.addEventListener('change', function() {
    const newMode = parseInt(modeSelect.value);
    const wasDots = currentMode >= 1;
    const isDots = newMode >= 1;
    currentMode = newMode;
    if (isDots && !wasDots) {
      if (newMode >= 2 && !audioCtx) audioCtx = new AudioContext();
      startDotsStream();
    } else if (!isDots && wasDots) {
      stopDotsStream();
    } else if (isDots) {
      if (newMode >= 2 && !audioCtx) audioCtx = new AudioContext();
      stopAllTones();
    }
    fetch('/soundsettings?soundMode=' + modeSelect.value);
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
    blobVal.textContent = blobSlider.value + ' px';
    fetch('/blobdetect?maxsize=' + blobSlider.value);
  });
  minBlobSlider.addEventListener('input', function() {
    minBlobVal.textContent = minBlobSlider.value + ' px';
    fetch('/blobdetect?minsize=' + minBlobSlider.value);
  });

  resolutionSelect.addEventListener('change', function() {
    showRestart();
    fetch('/cameraconfig?resolution=' + resolutionSelect.value);
  });
  camFpsSlider.addEventListener('input', function() {
    camFpsVal.textContent = camFpsSlider.value + ' fps';
  });
  camFpsSlider.addEventListener('change', function() {
    showRestart();
    fetch('/cameraconfig?fps=' + camFpsSlider.value);
  });
)HTML";

static const std::string kHtmlScript2 = R"HTML(
  // Device property helpers
  function setDev(params) { fetch('/deviceprops?' + params); }
  function setDevRestart(params) { showRestart(); fetch('/deviceprops?' + params); }

  // Preset
  const depthPreset = document.getElementById('depthPreset');
  depthPreset.addEventListener('change', function() {
    setDevRestart('depthWorkMode=' + encodeURIComponent(depthPreset.value));
  });

  // Disparity range
  const dispRange = document.getElementById('dispRange');
  dispRange.addEventListener('change', function() {
    setDevRestart('disparityRange=' + dispRange.value);
  });

  // Speckle filter
  const speckleToggle = document.getElementById('speckleToggle');
  const speckleSize = document.getElementById('speckleSize');
  const speckleSizeVal = document.getElementById('speckleSizeVal');
  const speckleDiff = document.getElementById('speckleDiff');
  const speckleDiffVal = document.getElementById('speckleDiffVal');
  speckleToggle.addEventListener('change', function() {
    setDev('speckleEnable=' + (speckleToggle.checked ? '1' : '0'));
  });
  speckleSize.addEventListener('input', function() {
    speckleSizeVal.textContent = speckleSize.value;
    setDev('speckleMaxSize=' + speckleSize.value);
  });
  speckleDiff.addEventListener('input', function() {
    speckleDiffVal.textContent = speckleDiff.value;
    setDev('speckleMaxDiff=' + speckleDiff.value);
  });

  // Confidence filter
  const confToggle = document.getElementById('confToggle');
  const confSlider = document.getElementById('confSlider');
  const confVal = document.getElementById('confVal');
  confToggle.addEventListener('change', function() {
    setDev('confidenceEnable=' + (confToggle.checked ? '1' : '0'));
  });
  confSlider.addEventListener('input', function() {
    confVal.textContent = confSlider.value;
    setDev('confidenceThreshold=' + confSlider.value);
  });

  // Hole fill
  const holeFillToggle = document.getElementById('holeFillToggle');
  holeFillToggle.addEventListener('change', function() {
    setDev('holeFillEnable=' + (holeFillToggle.checked ? '1' : '0'));
  });

  // HW depth range
  const hwMinSlider = document.getElementById('hwMinSlider');
  const hwMinVal = document.getElementById('hwMinVal');
  const hwMaxSlider = document.getElementById('hwMaxSlider');
  const hwMaxVal = document.getElementById('hwMaxVal');
  hwMinSlider.addEventListener('input', function() {
    hwMinVal.textContent = hwMinSlider.value + ' mm';
    setDev('hwDepthMin=' + hwMinSlider.value);
  });
  hwMaxSlider.addEventListener('input', function() {
    hwMaxVal.textContent = hwMaxSlider.value + ' mm';
    setDev('hwDepthMax=' + hwMaxSlider.value);
  });

  // Laser
  const laserToggle = document.getElementById('laserToggle');
  const laserSlider = document.getElementById('laserSlider');
  const laserVal = document.getElementById('laserVal');
  laserToggle.addEventListener('change', function() {
    setDev('laserEnable=' + (laserToggle.checked ? '1' : '0'));
  });
  laserSlider.addEventListener('input', function() {
    laserVal.textContent = laserSlider.value;
    setDev('laserPower=' + laserSlider.value);
  });
)HTML";

static const std::string kHtmlScript3 = R"HTML(
  // Depth exposure
  const depthAEToggle = document.getElementById('depthAEToggle');
  const depthExpSlider = document.getElementById('depthExpSlider');
  const depthExpVal = document.getElementById('depthExpVal');
  const depthGainSlider = document.getElementById('depthGainSlider');
  const depthGainVal = document.getElementById('depthGainVal');
  depthAEToggle.addEventListener('change', function() {
    setDev('depthAutoExposure=' + (depthAEToggle.checked ? '1' : '0'));
    depthExpSlider.disabled = depthAEToggle.checked;
    depthGainSlider.disabled = depthAEToggle.checked;
  });
  depthExpSlider.addEventListener('input', function() {
    depthExpVal.textContent = depthExpSlider.value;
    setDev('depthExposure=' + depthExpSlider.value);
  });
  depthGainSlider.addEventListener('input', function() {
    depthGainVal.textContent = depthGainSlider.value;
    setDev('depthGain=' + depthGainSlider.value);
  });

  // HDR merge
  const hdrToggle = document.getElementById('hdrToggle');
  hdrToggle.addEventListener('change', function() {
    setDev('hdrMerge=' + (hdrToggle.checked ? '1' : '0'));
  });

  // Precision
  const precisionSlider = document.getElementById('precisionSlider');
  const precisionVal = document.getElementById('precisionVal');
  precisionSlider.addEventListener('input', function() {
    precisionVal.textContent = precisionSlider.value;
    setDev('depthPrecisionLevel=' + precisionSlider.value);
  });

  // Mirror/Flip
  const depthMirrorToggle = document.getElementById('depthMirrorToggle');
  const depthFlipToggle = document.getElementById('depthFlipToggle');
  const colorMirrorToggle = document.getElementById('colorMirrorToggle');
  const colorFlipToggle = document.getElementById('colorFlipToggle');
  depthMirrorToggle.addEventListener('change', function() {
    setDev('depthMirror=' + (depthMirrorToggle.checked ? '1' : '0'));
  });
  depthFlipToggle.addEventListener('change', function() {
    setDev('depthFlip=' + (depthFlipToggle.checked ? '1' : '0'));
  });
  colorMirrorToggle.addEventListener('change', function() {
    setDev('colorMirror=' + (colorMirrorToggle.checked ? '1' : '0'));
  });
  colorFlipToggle.addEventListener('change', function() {
    setDev('colorFlip=' + (colorFlipToggle.checked ? '1' : '0'));
  });

  // Color exposure
  const colorAEToggle = document.getElementById('colorAEToggle');
  const colorExpSlider = document.getElementById('colorExpSlider');
  const colorExpVal = document.getElementById('colorExpVal');
  const colorGainSlider = document.getElementById('colorGainSlider');
  const colorGainVal = document.getElementById('colorGainVal');
  colorAEToggle.addEventListener('change', function() {
    setDev('colorAutoExposure=' + (colorAEToggle.checked ? '1' : '0'));
    colorExpSlider.disabled = colorAEToggle.checked;
    colorGainSlider.disabled = colorAEToggle.checked;
  });
  colorExpSlider.addEventListener('input', function() {
    colorExpVal.textContent = colorExpSlider.value;
    setDev('colorExposure=' + colorExpSlider.value);
  });
  colorGainSlider.addEventListener('input', function() {
    colorGainVal.textContent = colorGainSlider.value;
    setDev('colorGain=' + colorGainSlider.value);
  });
)HTML";

static const std::string kHtmlScript4 = R"HTML(
  // Color white balance
  const colorAWBToggle = document.getElementById('colorAWBToggle');
  const colorWBSlider = document.getElementById('colorWBSlider');
  const colorWBVal = document.getElementById('colorWBVal');
  colorAWBToggle.addEventListener('change', function() {
    setDev('colorAutoWhiteBalance=' + (colorAWBToggle.checked ? '1' : '0'));
    colorWBSlider.disabled = colorAWBToggle.checked;
  });
  colorWBSlider.addEventListener('input', function() {
    colorWBVal.textContent = colorWBSlider.value;
    setDev('colorWhiteBalance=' + colorWBSlider.value);
  });

  // Color brightness/sharpness/saturation/contrast/gamma
  const colorBrightSlider = document.getElementById('colorBrightSlider');
  const colorBrightVal = document.getElementById('colorBrightVal');
  const colorSharpSlider = document.getElementById('colorSharpSlider');
  const colorSharpVal = document.getElementById('colorSharpVal');
  const colorSatSlider = document.getElementById('colorSatSlider');
  const colorSatVal = document.getElementById('colorSatVal');
  const colorContrastSlider = document.getElementById('colorContrastSlider');
  const colorContrastVal = document.getElementById('colorContrastVal');
  const colorGammaSlider = document.getElementById('colorGammaSlider');
  const colorGammaVal = document.getElementById('colorGammaVal');

  colorBrightSlider.addEventListener('input', function() {
    colorBrightVal.textContent = colorBrightSlider.value;
    setDev('colorBrightness=' + colorBrightSlider.value);
  });
  colorSharpSlider.addEventListener('input', function() {
    colorSharpVal.textContent = colorSharpSlider.value;
    setDev('colorSharpness=' + colorSharpSlider.value);
  });
  colorSatSlider.addEventListener('input', function() {
    colorSatVal.textContent = colorSatSlider.value;
    setDev('colorSaturation=' + colorSatSlider.value);
  });
  colorContrastSlider.addEventListener('input', function() {
    colorContrastVal.textContent = colorContrastSlider.value;
    setDev('colorContrast=' + colorContrastSlider.value);
  });
  colorGammaSlider.addEventListener('input', function() {
    colorGammaVal.textContent = colorGammaSlider.value;
    setDev('colorGamma=' + colorGammaSlider.value);
  });
)HTML";

static const std::string kHtmlScript5 = R"HTML(
  // Fetch initial settings
  fetch('/threshold').then(r=>r.json()).then(j => {
    threshToggle.checked = j.enabled;
    threshSlider.value = j.threshold;
    threshSlider.disabled = !j.enabled;
    threshVal.textContent = j.threshold + ' mm';
    dilateSlider.value = j.dilate;
    dilateVal.textContent = j.dilate;
  });

  fetch('/blobdetect').then(r=>r.json()).then(j => {
    blobToggle.checked = j.enabled;
    blobSlider.value = j.maxsize;
    blobVal.textContent = j.maxsize + ' px';
    minBlobSlider.value = j.minsize;
    minBlobVal.textContent = j.minsize + ' px';
  });

  fetch('/cameraconfig').then(r=>r.json()).then(j => {
    resolutionSelect.value = j.resolution;
    camFpsSlider.value = j.fps;
    camFpsVal.textContent = j.fps + ' fps';
  });

  fetch('/soundsettings').then(r=>r.json()).then(function(d) {
    modeSelect.value = d.soundMode;
    keySelect.value = d.soundKey;
    scaleSelect.value = d.soundScale;
    decaySlider.value = d.soundDecay;
    decayValSpan.textContent = (d.soundDecay / 10).toFixed(1) + ' s';
    releaseSlider.value = d.soundRelease;
    releaseValSpan.textContent = (d.soundRelease / 10).toFixed(1) + ' s';
    moveThreshSlider.value = d.soundMoveThresh;
    moveThreshValSpan.textContent = d.soundMoveThresh === 0 ? 'off' : d.soundMoveThresh + ' mm';
    quantizeSelect.value = d.soundQuantize;
    tempoSlider.value = d.soundTempo;
    tempoValSpan.textContent = d.soundTempo + ' bpm';
    showDepthToggle.checked = d.showDepth;
    modeSelect.dispatchEvent(new Event('change'));
  });

  // Fetch device capabilities and apply to UI
  fetch('/devicecaps').then(r=>r.json()).then(caps => {
    function applyRange(slider, valSpan, range, suffix) {
      if (!range || !range.supported) return;
      slider.min = range.min;
      slider.max = range.max;
      if (range.step > 0) slider.step = range.step;
    }
    function hideIf(id, show) {
      var el = document.getElementById(id);
      if (el && !show) el.classList.add('unsupported');
    }

    // Hide unsupported sections
    hideIf('speckleWrap', caps.speckleSupported);
    hideIf('confidenceWrap', caps.confidenceSupported);
    hideIf('holeFillWrap', caps.holeFillSupported);
    hideIf('hdrWrap', caps.hdrMergeSupported);
    hideIf('depthMirrorWrap', caps.depthMirrorSupported);
    hideIf('depthFlipWrap', caps.depthFlipSupported);
    hideIf('colorMirrorWrap', caps.colorMirrorSupported);
    hideIf('colorFlipWrap', caps.colorFlipSupported);
    hideIf('depthPresetWrap', caps.workModes && caps.workModes.length > 0);
    hideIf('dispRangeWrap', caps.disparityRange && caps.disparityRange.supported);
    hideIf('laserWrap', caps.laserEnableSupported || (caps.laserPower && caps.laserPower.supported));
    hideIf('depthExpWrap', caps.depthAutoExposureSupported || (caps.depthExposure && caps.depthExposure.supported));
    hideIf('precisionWrap', caps.depthPrecisionLevel && caps.depthPrecisionLevel.supported);
    hideIf('hwDepthWrap', (caps.hwDepthMin && caps.hwDepthMin.supported) || (caps.hwDepthMax && caps.hwDepthMax.supported));

    // Apply ranges
    applyRange(speckleSize, speckleSizeVal, caps.speckleMaxSize);
    applyRange(speckleDiff, speckleDiffVal, caps.speckleMaxDiff);
    applyRange(confSlider, confVal, caps.confidenceThreshold);
    applyRange(laserSlider, laserVal, caps.laserPower);
    applyRange(depthExpSlider, depthExpVal, caps.depthExposure);
    applyRange(depthGainSlider, depthGainVal, caps.depthGain);
    applyRange(precisionSlider, precisionVal, caps.depthPrecisionLevel);
    applyRange(hwMinSlider, hwMinVal, caps.hwDepthMin);
    applyRange(hwMaxSlider, hwMaxVal, caps.hwDepthMax);
    applyRange(colorExpSlider, colorExpVal, caps.colorExposure);
    applyRange(colorGainSlider, colorGainVal, caps.colorGain);
    applyRange(colorWBSlider, colorWBVal, caps.colorWhiteBalance);
    applyRange(colorBrightSlider, colorBrightVal, caps.colorBrightness);
    applyRange(colorSharpSlider, colorSharpVal, caps.colorSharpness);
    applyRange(colorSatSlider, colorSatVal, caps.colorSaturation);
    applyRange(colorContrastSlider, colorContrastVal, caps.colorContrast);
    applyRange(colorGammaSlider, colorGammaVal, caps.colorGamma);

    // Populate presets
    if (caps.workModes) {
      caps.workModes.forEach(function(name) {
        var opt = document.createElement('option');
        opt.value = name;
        opt.textContent = name;
        depthPreset.appendChild(opt);
      });
      if (caps.currentWorkMode) depthPreset.value = caps.currentWorkMode;
    }

    // Disparity range
    if (caps.disparityRange && caps.disparityRange.supported) {
      dispRange.value = caps.disparityRange.cur;
    }
  });
)HTML";

static const std::string kHtmlScript6 = R"HTML(
  // Fetch current device property values
  fetch('/deviceprops').then(r=>r.json()).then(d => {
    speckleToggle.checked = d.speckleEnable;
    speckleSize.value = d.speckleMaxSize;
    speckleSizeVal.textContent = d.speckleMaxSize;
    speckleDiff.value = d.speckleMaxDiff;
    speckleDiffVal.textContent = d.speckleMaxDiff;
    confToggle.checked = d.confidenceEnable;
    confSlider.value = d.confidenceThreshold;
    confVal.textContent = d.confidenceThreshold;
    holeFillToggle.checked = d.holeFillEnable;
    hwMinSlider.value = d.hwDepthMin;
    hwMinVal.textContent = d.hwDepthMin + ' mm';
    hwMaxSlider.value = d.hwDepthMax;
    hwMaxVal.textContent = d.hwDepthMax + ' mm';
    laserToggle.checked = d.laserEnable;
    if (d.laserPower >= 0) { laserSlider.value = d.laserPower; laserVal.textContent = d.laserPower; }
    depthAEToggle.checked = d.depthAutoExposure;
    depthExpSlider.value = d.depthExposure;
    depthExpVal.textContent = d.depthExposure;
    depthExpSlider.disabled = d.depthAutoExposure;
    depthGainSlider.value = d.depthGain;
    depthGainVal.textContent = d.depthGain;
    depthGainSlider.disabled = d.depthAutoExposure;
    hdrToggle.checked = d.hdrMerge;
    if (d.depthPrecisionLevel >= 0) {
      precisionSlider.value = d.depthPrecisionLevel;
      precisionVal.textContent = d.depthPrecisionLevel;
    }
    depthMirrorToggle.checked = d.depthMirror;
    depthFlipToggle.checked = d.depthFlip;
    colorMirrorToggle.checked = d.colorMirror;
    colorFlipToggle.checked = d.colorFlip;
    colorAEToggle.checked = d.colorAutoExposure;
    colorExpSlider.value = d.colorExposure;
    colorExpVal.textContent = d.colorExposure;
    colorExpSlider.disabled = d.colorAutoExposure;
    colorGainSlider.value = d.colorGain;
    colorGainVal.textContent = d.colorGain;
    colorGainSlider.disabled = d.colorAutoExposure;
    colorAWBToggle.checked = d.colorAutoWhiteBalance;
    colorWBSlider.value = d.colorWhiteBalance;
    colorWBVal.textContent = d.colorWhiteBalance;
    colorWBSlider.disabled = d.colorAutoWhiteBalance;
    colorBrightSlider.value = d.colorBrightness;
    colorBrightVal.textContent = d.colorBrightness;
    colorSharpSlider.value = d.colorSharpness;
    colorSharpVal.textContent = d.colorSharpness;
    colorSatSlider.value = d.colorSaturation;
    colorSatVal.textContent = d.colorSaturation;
    colorContrastSlider.value = d.colorContrast;
    colorContrastVal.textContent = d.colorContrast;
    colorGammaSlider.value = d.colorGamma;
    colorGammaVal.textContent = d.colorGamma;
    if (d.disparityRange > 0) dispRange.value = d.disparityRange;
  });

  const fpsDisplay = document.getElementById('fpsDisplay');
  function refreshFps() {
    fetch('/fps').then(r=>r.json()).then(j => { fpsDisplay.textContent = j.fps.toFixed(1) + ' fps'; });
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
                     std::atomic<bool>& configDirty,
                     std::atomic<bool>& restartRequested,
                     std::atomic<int>& depthResolution,
                     std::atomic<int>& cameraFps,
                     std::atomic<bool>& devicePropsDirty,
                     bool colorEnabled)
    : thresholdMm_(thresholdMm)
    , thresholdEnabled_(thresholdEnabled)
    , dilateIterations_(dilateIterations)
    , blobDetectEnabled_(blobDetectEnabled)
    , maxBlobPixels_(maxBlobPixels)
    , minBlobPixels_(minBlobPixels)
    , fpsTenths_(fpsTenths)
    , configDirty_(configDirty)
    , restartRequested_(restartRequested)
    , depthResolution_(depthResolution)
    , cameraFps_(cameraFps)
    , devicePropsDirty_(devicePropsDirty)
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
    blobsCv_.notify_all();
    depthCv_.notify_all();
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

DeviceSettings WebServer::getDeviceSettings() {
    std::lock_guard<std::mutex> lock(devSettingsMtx_);
    return devSettings_;
}

void WebServer::setDeviceCaps(const DeviceCaps& caps) {
    std::lock_guard<std::mutex> lock(devSettingsMtx_);
    devCaps_ = caps;
}

// Helper: serialize a PropertyRange to JSON
static std::string rangeJson(const std::string& name, const PropertyRange& r) {
    return "\"" + name + "\":{\"supported\":" + (r.supported ? "true" : "false") +
           ",\"min\":" + std::to_string(r.min) +
           ",\"max\":" + std::to_string(r.max) +
           ",\"step\":" + std::to_string(r.step) +
           ",\"def\":" + std::to_string(r.def) +
           ",\"cur\":" + std::to_string(r.cur) + "}";
}

void WebServer::saveSettings() {
    PostProcSettings pp;
    { std::lock_guard<std::mutex> lock(postProcMtx_); pp = postProc_; }

    DeviceSettings ds;
    std::string sScale, sQuantize;
    { std::lock_guard<std::mutex> lock(devSettingsMtx_); ds = devSettings_; sScale = soundScale_; sQuantize = soundQuantize_; }

    std::string json =
        std::string("{\n"
        "  \"thresholdMm\": ") + std::to_string(thresholdMm_.load()) + ",\n"
        "  \"thresholdEnabled\": " + (thresholdEnabled_.load() ? "true" : "false") + ",\n"
        "  \"dilateIterations\": " + std::to_string(dilateIterations_.load()) + ",\n"
        "  \"blobDetectEnabled\": " + (blobDetectEnabled_.load() ? "true" : "false") + ",\n"
        "  \"maxBlobPixels\": " + std::to_string(maxBlobPixels_.load()) + ",\n"
        "  \"minBlobPixels\": " + std::to_string(minBlobPixels_.load()) + ",\n"
        "  \"depthResolution\": " + std::to_string(depthResolution_.load()) + ",\n"
        "  \"cameraFps\": " + std::to_string(cameraFps_.load()) + ",\n"
        "  \"thresholdFilterEnable\": " + (pp.thresholdFilterEnable ? "true" : "false") + ",\n"
        "  \"thresholdFilterMin\": " + std::to_string(pp.thresholdFilterMin) + ",\n"
        "  \"thresholdFilterMax\": " + std::to_string(pp.thresholdFilterMax) + ",\n"
        // Device settings
        "  \"speckleEnable\": " + (ds.speckleEnable ? "true" : "false") + ",\n"
        "  \"speckleMaxSize\": " + std::to_string(ds.speckleMaxSize) + ",\n"
        "  \"speckleMaxDiff\": " + std::to_string(ds.speckleMaxDiff) + ",\n"
        "  \"hwDepthMin\": " + std::to_string(ds.hwDepthMin) + ",\n"
        "  \"hwDepthMax\": " + std::to_string(ds.hwDepthMax) + ",\n"
        "  \"confidenceEnable\": " + (ds.confidenceEnable ? "true" : "false") + ",\n"
        "  \"confidenceThreshold\": " + std::to_string(ds.confidenceThreshold) + ",\n"
        "  \"depthWorkMode\": \"" + ds.depthWorkMode + "\",\n"
        "  \"laserEnable\": " + (ds.laserEnable ? "true" : "false") + ",\n"
        "  \"laserPower\": " + std::to_string(ds.laserPower) + ",\n"
        "  \"holeFillEnable\": " + (ds.holeFillEnable ? "true" : "false") + ",\n"
        "  \"depthAutoExposure\": " + (ds.depthAutoExposure ? "true" : "false") + ",\n"
        "  \"depthExposure\": " + std::to_string(ds.depthExposure) + ",\n"
        "  \"depthGain\": " + std::to_string(ds.depthGain) + ",\n"
        "  \"disparityRange\": " + std::to_string(ds.disparityRange) + ",\n"
        "  \"depthMirror\": " + (ds.depthMirror ? "true" : "false") + ",\n"
        "  \"depthFlip\": " + (ds.depthFlip ? "true" : "false") + ",\n"
        "  \"colorMirror\": " + (ds.colorMirror ? "true" : "false") + ",\n"
        "  \"colorFlip\": " + (ds.colorFlip ? "true" : "false") + ",\n"
        "  \"depthPrecisionLevel\": " + std::to_string(ds.depthPrecisionLevel) + ",\n"
        "  \"hdrMerge\": " + (ds.hdrMerge ? "true" : "false") + ",\n"
        "  \"colorAutoExposure\": " + (ds.colorAutoExposure ? "true" : "false") + ",\n"
        "  \"colorExposure\": " + std::to_string(ds.colorExposure) + ",\n"
        "  \"colorGain\": " + std::to_string(ds.colorGain) + ",\n"
        "  \"colorAutoWhiteBalance\": " + (ds.colorAutoWhiteBalance ? "true" : "false") + ",\n"
        "  \"colorWhiteBalance\": " + std::to_string(ds.colorWhiteBalance) + ",\n"
        "  \"colorBrightness\": " + std::to_string(ds.colorBrightness) + ",\n"
        "  \"colorSharpness\": " + std::to_string(ds.colorSharpness) + ",\n"
        "  \"colorSaturation\": " + std::to_string(ds.colorSaturation) + ",\n"
        "  \"colorContrast\": " + std::to_string(ds.colorContrast) + ",\n"
        "  \"colorGamma\": " + std::to_string(ds.colorGamma) + ",\n"
        // Sound / UI settings
        "  \"soundMode\": " + std::to_string(soundMode_.load()) + ",\n"
        "  \"soundKey\": " + std::to_string(soundKey_.load()) + ",\n"
        "  \"soundScale\": \"" + sScale + "\",\n"
        "  \"soundDecay\": " + std::to_string(soundDecay_.load()) + ",\n"
        "  \"soundRelease\": " + std::to_string(soundRelease_.load()) + ",\n"
        "  \"soundMoveThresh\": " + std::to_string(soundMoveThresh_.load()) + ",\n"
        "  \"soundQuantize\": \"" + sQuantize + "\",\n"
        "  \"soundTempo\": " + std::to_string(soundTempo_.load()) + ",\n"
        "  \"showDepth\": " + (showDepth_.load() ? "true" : "false") + "\n"
        "}\n";

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

static bool jsonString(const std::string& text, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\":";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    if (pos >= text.size() || text[pos] != '"') return false;
    pos++; // skip opening quote
    std::string result;
    while (pos < text.size() && text[pos] != '"') {
        result += text[pos++];
    }
    out = result;
    return true;
}

void WebServer::loadSettings() {
    std::ifstream file("settings.json");
    if (!file) return;

    std::string text((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    int iv; bool bv; std::string sv;
    if (jsonInt(text, "thresholdMm", iv)) thresholdMm_.store(iv);
    if (jsonBool(text, "thresholdEnabled", bv)) thresholdEnabled_.store(bv);
    if (jsonInt(text, "dilateIterations", iv)) dilateIterations_.store(iv);
    if (jsonBool(text, "blobDetectEnabled", bv)) blobDetectEnabled_.store(bv);
    if (jsonInt(text, "maxBlobPixels", iv)) maxBlobPixels_.store(iv);
    if (jsonInt(text, "minBlobPixels", iv)) minBlobPixels_.store(iv);
    if (jsonInt(text, "depthResolution", iv)) depthResolution_.store(iv);
    if (jsonInt(text, "cameraFps", iv)) cameraFps_.store(iv);

    {
        std::lock_guard<std::mutex> lock(postProcMtx_);
        if (jsonBool(text, "thresholdFilterEnable", bv)) postProc_.thresholdFilterEnable = bv;
        if (jsonInt(text, "thresholdFilterMin", iv)) postProc_.thresholdFilterMin = iv;
        if (jsonInt(text, "thresholdFilterMax", iv)) postProc_.thresholdFilterMax = iv;
    }

    {
        std::lock_guard<std::mutex> lock(devSettingsMtx_);
        if (jsonBool(text, "speckleEnable", bv)) devSettings_.speckleEnable = bv;
        if (jsonInt(text, "speckleMaxSize", iv)) devSettings_.speckleMaxSize = iv;
        if (jsonInt(text, "speckleMaxDiff", iv)) devSettings_.speckleMaxDiff = iv;
        if (jsonInt(text, "hwDepthMin", iv)) devSettings_.hwDepthMin = iv;
        if (jsonInt(text, "hwDepthMax", iv)) devSettings_.hwDepthMax = iv;
        if (jsonBool(text, "confidenceEnable", bv)) devSettings_.confidenceEnable = bv;
        if (jsonInt(text, "confidenceThreshold", iv)) devSettings_.confidenceThreshold = iv;
        if (jsonString(text, "depthWorkMode", sv)) devSettings_.depthWorkMode = sv;
        if (jsonBool(text, "laserEnable", bv)) devSettings_.laserEnable = bv;
        if (jsonInt(text, "laserPower", iv)) devSettings_.laserPower = iv;
        if (jsonBool(text, "holeFillEnable", bv)) devSettings_.holeFillEnable = bv;
        if (jsonBool(text, "depthAutoExposure", bv)) devSettings_.depthAutoExposure = bv;
        if (jsonInt(text, "depthExposure", iv)) devSettings_.depthExposure = iv;
        if (jsonInt(text, "depthGain", iv)) devSettings_.depthGain = iv;
        if (jsonInt(text, "disparityRange", iv)) devSettings_.disparityRange = iv;
        if (jsonBool(text, "depthMirror", bv)) devSettings_.depthMirror = bv;
        if (jsonBool(text, "depthFlip", bv)) devSettings_.depthFlip = bv;
        if (jsonBool(text, "colorMirror", bv)) devSettings_.colorMirror = bv;
        if (jsonBool(text, "colorFlip", bv)) devSettings_.colorFlip = bv;
        if (jsonInt(text, "depthPrecisionLevel", iv)) devSettings_.depthPrecisionLevel = iv;
        if (jsonBool(text, "hdrMerge", bv)) devSettings_.hdrMerge = bv;
        if (jsonBool(text, "colorAutoExposure", bv)) devSettings_.colorAutoExposure = bv;
        if (jsonInt(text, "colorExposure", iv)) devSettings_.colorExposure = iv;
        if (jsonInt(text, "colorGain", iv)) devSettings_.colorGain = iv;
        if (jsonBool(text, "colorAutoWhiteBalance", bv)) devSettings_.colorAutoWhiteBalance = bv;
        if (jsonInt(text, "colorWhiteBalance", iv)) devSettings_.colorWhiteBalance = iv;
        if (jsonInt(text, "colorBrightness", iv)) devSettings_.colorBrightness = iv;
        if (jsonInt(text, "colorSharpness", iv)) devSettings_.colorSharpness = iv;
        if (jsonInt(text, "colorSaturation", iv)) devSettings_.colorSaturation = iv;
        if (jsonInt(text, "colorContrast", iv)) devSettings_.colorContrast = iv;
        if (jsonInt(text, "colorGamma", iv)) devSettings_.colorGamma = iv;
        // Sound / UI settings (strings guarded by this mutex)
        if (jsonString(text, "soundScale", sv)) soundScale_ = sv;
        if (jsonString(text, "soundQuantize", sv)) soundQuantize_ = sv;
    }

    if (jsonInt(text, "soundMode", iv)) soundMode_.store(iv);
    if (jsonInt(text, "soundKey", iv)) soundKey_.store(iv);
    if (jsonInt(text, "soundDecay", iv)) soundDecay_.store(iv);
    if (jsonInt(text, "soundRelease", iv)) soundRelease_.store(iv);
    if (jsonInt(text, "soundMoveThresh", iv)) soundMoveThresh_.store(iv);
    if (jsonInt(text, "soundTempo", iv)) soundTempo_.store(iv);
    if (jsonBool(text, "showDepth", bv)) showDepth_.store(bv);

    std::cout << "Loaded settings from settings.json" << std::endl;
}

std::vector<uint8_t> WebServer::makeBmp(const uint8_t* bgr, int width, int height) {
    int rowBytes = width * 3;
    int rowPadding = (4 - (rowBytes % 4)) % 4;
    int paddedRow = rowBytes + rowPadding;
    int imageSize = paddedRow * height;
    int fileSize = 54 + imageSize;

    std::vector<uint8_t> bmp(fileSize, 0);

    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &fileSize, 4);
    int dataOffset = 54;
    std::memcpy(&bmp[10], &dataOffset, 4);

    int infoSize = 40;
    std::memcpy(&bmp[14], &infoSize, 4);
    std::memcpy(&bmp[18], &width, 4);
    std::memcpy(&bmp[22], &height, 4);
    uint16_t planes = 1;
    std::memcpy(&bmp[26], &planes, 2);
    uint16_t bpp = 24;
    std::memcpy(&bmp[28], &bpp, 2);
    std::memcpy(&bmp[34], &imageSize, 4);

    uint8_t* dst = bmp.data() + 54;
    for (int y = height - 1; y >= 0; y--) {
        const uint8_t* srcRow = bgr + y * rowBytes;
        std::memcpy(dst, srcRow, rowBytes);
        dst += rowBytes;
        for (int p = 0; p < rowPadding; p++) *dst++ = 0;
    }

    return bmp;
}

void WebServer::run() {
    httplib::Server svr;

    // Build full HTML page
    std::string fullHtml = kHtmlHead + kHtmlControls1 + kHtmlControls2 +
                           kHtmlControls3 + kHtmlControls4 + kHtmlControls5 +
                           kHtmlControls6 + kHtmlImages +
                           kHtmlScript1 + kHtmlScript2 + kHtmlScript3 +
                           kHtmlScript4 + kHtmlScript5 + kHtmlScript6;

    // GET / — HTML page (hide color image if color stream is disabled)
    svr.Get("/", [this, &fullHtml](const httplib::Request&, httplib::Response& res) {
        std::string html = fullHtml;
        if (!colorEnabled_) {
            std::string target = "<img id=\"colorImg\" src=\"/color.mjpeg\" alt=\"Color\">";
            auto pos = html.find(target);
            if (pos != std::string::npos)
                html.replace(pos, target.size(), "");
            // Hide color controls section
            std::string target2 = "id=\"colorControlsSection\"";
            auto pos2 = html.find(target2);
            if (pos2 != std::string::npos) {
                // Find the div start
                auto divStart = html.rfind("<div", pos2);
                if (divStart != std::string::npos) {
                    html.insert(divStart + 4, " style=\"display:none\"");
                }
            }
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

    // GET /cameraconfig — get or set camera configuration (resolution, fps)
    svr.Get("/cameraconfig", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        if (req.has_param("resolution")) {
            int val = std::stoi(req.get_param_value("resolution"));
            if (val < 0) val = 0;
            if (val > 1) val = 1;
            depthResolution_.store(val);
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
        int resolution = depthResolution_.load();
        int fps = cameraFps_.load();
        res.set_content("{\"resolution\":" + std::to_string(resolution) +
                        ",\"fps\":" + std::to_string(fps) + "}",
                        "application/json");
    });

    // GET /devicecaps — returns device capabilities as JSON
    svr.Get("/devicecaps", [this](const httplib::Request&, httplib::Response& res) {
        DeviceCaps caps;
        { std::lock_guard<std::mutex> lock(devSettingsMtx_); caps = devCaps_; }

        std::string json = "{";
        json += rangeJson("speckleMaxSize", caps.speckleMaxSize) + ",";
        json += rangeJson("speckleMaxDiff", caps.speckleMaxDiff) + ",";
        json += rangeJson("hwDepthMin", caps.hwDepthMin) + ",";
        json += rangeJson("hwDepthMax", caps.hwDepthMax) + ",";
        json += rangeJson("confidenceThreshold", caps.confidenceThreshold) + ",";
        json += rangeJson("laserPower", caps.laserPower) + ",";
        json += rangeJson("depthExposure", caps.depthExposure) + ",";
        json += rangeJson("depthGain", caps.depthGain) + ",";
        json += rangeJson("disparityRange", caps.disparityRange) + ",";
        json += rangeJson("depthPrecisionLevel", caps.depthPrecisionLevel) + ",";
        json += rangeJson("colorExposure", caps.colorExposure) + ",";
        json += rangeJson("colorGain", caps.colorGain) + ",";
        json += rangeJson("colorWhiteBalance", caps.colorWhiteBalance) + ",";
        json += rangeJson("colorBrightness", caps.colorBrightness) + ",";
        json += rangeJson("colorSharpness", caps.colorSharpness) + ",";
        json += rangeJson("colorSaturation", caps.colorSaturation) + ",";
        json += rangeJson("colorContrast", caps.colorContrast) + ",";
        json += rangeJson("colorGamma", caps.colorGamma) + ",";

        json += "\"speckleSupported\":" + std::string(caps.speckleSupported ? "true" : "false") + ",";
        json += "\"confidenceSupported\":" + std::string(caps.confidenceSupported ? "true" : "false") + ",";
        json += "\"holeFillSupported\":" + std::string(caps.holeFillSupported ? "true" : "false") + ",";
        json += "\"depthMirrorSupported\":" + std::string(caps.depthMirrorSupported ? "true" : "false") + ",";
        json += "\"depthFlipSupported\":" + std::string(caps.depthFlipSupported ? "true" : "false") + ",";
        json += "\"colorMirrorSupported\":" + std::string(caps.colorMirrorSupported ? "true" : "false") + ",";
        json += "\"colorFlipSupported\":" + std::string(caps.colorFlipSupported ? "true" : "false") + ",";
        json += "\"hdrMergeSupported\":" + std::string(caps.hdrMergeSupported ? "true" : "false") + ",";
        json += "\"depthAutoExposureSupported\":" + std::string(caps.depthAutoExposureSupported ? "true" : "false") + ",";
        json += "\"colorAutoExposureSupported\":" + std::string(caps.colorAutoExposureSupported ? "true" : "false") + ",";
        json += "\"colorAutoWBSupported\":" + std::string(caps.colorAutoWBSupported ? "true" : "false") + ",";
        json += "\"laserEnableSupported\":" + std::string(caps.laserEnableSupported ? "true" : "false") + ",";

        json += "\"currentWorkMode\":\"" + caps.currentWorkMode + "\",";
        json += "\"workModes\":[";
        for (size_t i = 0; i < caps.workModes.size(); i++) {
            if (i > 0) json += ",";
            json += "\"" + caps.workModes[i] + "\"";
        }
        json += "]}";

        res.set_content(json, "application/json");
    });

    // GET /deviceprops — get or set device properties
    svr.Get("/deviceprops", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        bool needRestart = false;

        {
            std::lock_guard<std::mutex> lock(devSettingsMtx_);

            // Bool properties
            auto setBool = [&](const char* name, bool& field) {
                if (req.has_param(name)) {
                    field = (req.get_param_value(name) == "1");
                    changed = true;
                }
            };
            // Int properties
            auto setInt = [&](const char* name, int& field) {
                if (req.has_param(name)) {
                    field = std::stoi(req.get_param_value(name));
                    changed = true;
                }
            };

            setBool("speckleEnable", devSettings_.speckleEnable);
            setInt("speckleMaxSize", devSettings_.speckleMaxSize);
            setInt("speckleMaxDiff", devSettings_.speckleMaxDiff);
            setInt("hwDepthMin", devSettings_.hwDepthMin);
            setInt("hwDepthMax", devSettings_.hwDepthMax);
            setBool("confidenceEnable", devSettings_.confidenceEnable);
            setInt("confidenceThreshold", devSettings_.confidenceThreshold);
            setBool("laserEnable", devSettings_.laserEnable);
            setInt("laserPower", devSettings_.laserPower);
            setBool("holeFillEnable", devSettings_.holeFillEnable);
            setBool("depthAutoExposure", devSettings_.depthAutoExposure);
            setInt("depthExposure", devSettings_.depthExposure);
            setInt("depthGain", devSettings_.depthGain);
            setBool("depthMirror", devSettings_.depthMirror);
            setBool("depthFlip", devSettings_.depthFlip);
            setBool("colorMirror", devSettings_.colorMirror);
            setBool("colorFlip", devSettings_.colorFlip);
            setInt("depthPrecisionLevel", devSettings_.depthPrecisionLevel);
            setBool("hdrMerge", devSettings_.hdrMerge);
            setBool("colorAutoExposure", devSettings_.colorAutoExposure);
            setInt("colorExposure", devSettings_.colorExposure);
            setInt("colorGain", devSettings_.colorGain);
            setBool("colorAutoWhiteBalance", devSettings_.colorAutoWhiteBalance);
            setInt("colorWhiteBalance", devSettings_.colorWhiteBalance);
            setInt("colorBrightness", devSettings_.colorBrightness);
            setInt("colorSharpness", devSettings_.colorSharpness);
            setInt("colorSaturation", devSettings_.colorSaturation);
            setInt("colorContrast", devSettings_.colorContrast);
            setInt("colorGamma", devSettings_.colorGamma);

            // Restart-required properties
            if (req.has_param("depthWorkMode")) {
                devSettings_.depthWorkMode = req.get_param_value("depthWorkMode");
                changed = true;
                needRestart = true;
            }
            if (req.has_param("disparityRange")) {
                devSettings_.disparityRange = std::stoi(req.get_param_value("disparityRange"));
                changed = true;
                needRestart = true;
            }
        }

        if (changed) {
            if (needRestart) {
                restartRequested_.store(true);
            } else {
                devicePropsDirty_.store(true);
            }
            saveSettings();
        }

        // Return current values
        DeviceSettings ds;
        { std::lock_guard<std::mutex> lock(devSettingsMtx_); ds = devSettings_; }

        std::string json = "{"
            "\"speckleEnable\":" + std::string(ds.speckleEnable ? "true" : "false") +
            ",\"speckleMaxSize\":" + std::to_string(ds.speckleMaxSize) +
            ",\"speckleMaxDiff\":" + std::to_string(ds.speckleMaxDiff) +
            ",\"hwDepthMin\":" + std::to_string(ds.hwDepthMin) +
            ",\"hwDepthMax\":" + std::to_string(ds.hwDepthMax) +
            ",\"confidenceEnable\":" + std::string(ds.confidenceEnable ? "true" : "false") +
            ",\"confidenceThreshold\":" + std::to_string(ds.confidenceThreshold) +
            ",\"depthWorkMode\":\"" + ds.depthWorkMode + "\"" +
            ",\"laserEnable\":" + std::string(ds.laserEnable ? "true" : "false") +
            ",\"laserPower\":" + std::to_string(ds.laserPower) +
            ",\"holeFillEnable\":" + std::string(ds.holeFillEnable ? "true" : "false") +
            ",\"depthAutoExposure\":" + std::string(ds.depthAutoExposure ? "true" : "false") +
            ",\"depthExposure\":" + std::to_string(ds.depthExposure) +
            ",\"depthGain\":" + std::to_string(ds.depthGain) +
            ",\"disparityRange\":" + std::to_string(ds.disparityRange) +
            ",\"depthMirror\":" + std::string(ds.depthMirror ? "true" : "false") +
            ",\"depthFlip\":" + std::string(ds.depthFlip ? "true" : "false") +
            ",\"colorMirror\":" + std::string(ds.colorMirror ? "true" : "false") +
            ",\"colorFlip\":" + std::string(ds.colorFlip ? "true" : "false") +
            ",\"depthPrecisionLevel\":" + std::to_string(ds.depthPrecisionLevel) +
            ",\"hdrMerge\":" + std::string(ds.hdrMerge ? "true" : "false") +
            ",\"colorAutoExposure\":" + std::string(ds.colorAutoExposure ? "true" : "false") +
            ",\"colorExposure\":" + std::to_string(ds.colorExposure) +
            ",\"colorGain\":" + std::to_string(ds.colorGain) +
            ",\"colorAutoWhiteBalance\":" + std::string(ds.colorAutoWhiteBalance ? "true" : "false") +
            ",\"colorWhiteBalance\":" + std::to_string(ds.colorWhiteBalance) +
            ",\"colorBrightness\":" + std::to_string(ds.colorBrightness) +
            ",\"colorSharpness\":" + std::to_string(ds.colorSharpness) +
            ",\"colorSaturation\":" + std::to_string(ds.colorSaturation) +
            ",\"colorContrast\":" + std::to_string(ds.colorContrast) +
            ",\"colorGamma\":" + std::to_string(ds.colorGamma) +
            "}";

        res.set_content(json, "application/json");
    });

    // GET /fps — current measured FPS
    // GET /soundsettings — get or set sound / UI parameters
    svr.Get("/soundsettings", [this](const httplib::Request& req, httplib::Response& res) {
        bool changed = false;
        if (req.has_param("soundMode")) {
            int v = std::stoi(req.get_param_value("soundMode"));
            if (v < 0) v = 0; if (v > 3) v = 3;
            soundMode_.store(v); changed = true;
        }
        if (req.has_param("soundKey")) {
            int v = std::stoi(req.get_param_value("soundKey"));
            if (v < 0) v = 0; if (v > 11) v = 11;
            soundKey_.store(v); changed = true;
        }
        if (req.has_param("soundScale")) {
            std::lock_guard<std::mutex> lock(devSettingsMtx_);
            soundScale_ = req.get_param_value("soundScale");
            changed = true;
        }
        if (req.has_param("soundDecay")) {
            int v = std::stoi(req.get_param_value("soundDecay"));
            if (v < 1) v = 1; if (v > 50) v = 50;
            soundDecay_.store(v); changed = true;
        }
        if (req.has_param("soundRelease")) {
            int v = std::stoi(req.get_param_value("soundRelease"));
            if (v < 0) v = 0; if (v > 50) v = 50;
            soundRelease_.store(v); changed = true;
        }
        if (req.has_param("soundMoveThresh")) {
            int v = std::stoi(req.get_param_value("soundMoveThresh"));
            if (v < 0) v = 0; if (v > 100) v = 100;
            soundMoveThresh_.store(v); changed = true;
        }
        if (req.has_param("soundQuantize")) {
            std::lock_guard<std::mutex> lock(devSettingsMtx_);
            soundQuantize_ = req.get_param_value("soundQuantize");
            changed = true;
        }
        if (req.has_param("soundTempo")) {
            int v = std::stoi(req.get_param_value("soundTempo"));
            if (v < 60) v = 60; if (v > 240) v = 240;
            soundTempo_.store(v); changed = true;
        }
        if (req.has_param("showDepth")) {
            showDepth_.store(req.get_param_value("showDepth") == "1");
            changed = true;
        }
        if (changed) saveSettings();

        std::string sScale, sQuantize;
        { std::lock_guard<std::mutex> lock(devSettingsMtx_); sScale = soundScale_; sQuantize = soundQuantize_; }
        std::string json = "{\"soundMode\":" + std::to_string(soundMode_.load())
            + ",\"soundKey\":" + std::to_string(soundKey_.load())
            + ",\"soundScale\":\"" + sScale + "\""
            + ",\"soundDecay\":" + std::to_string(soundDecay_.load())
            + ",\"soundRelease\":" + std::to_string(soundRelease_.load())
            + ",\"soundMoveThresh\":" + std::to_string(soundMoveThresh_.load())
            + ",\"soundQuantize\":\"" + sQuantize + "\""
            + ",\"soundTempo\":" + std::to_string(soundTempo_.load())
            + ",\"showDepth\":" + (showDepth_.load() ? "true" : "false")
            + "}";
        res.set_content(json, "application/json");
    });

    svr.Get("/fps", [this](const httplib::Request&, httplib::Response& res) {
        int tenths = fpsTenths_.load();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "{\"fps\":%.1f}", tenths / 10.0);
        res.set_content(buf, "application/json");
    });

    // GET /events — Server-Sent Events for blob positions
    svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t, httplib::DataSink& sink) {
                int lastSeq;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    lastSeq = blobsSeq_;
                }
                {
                    std::unique_lock<std::mutex> lock(frameMtx_);
                    blobsCv_.wait_for(lock, std::chrono::seconds(2),
                        [&] { return blobsSeq_ != lastSeq || !running_; });
                }
                if (!running_) return false;
                std::string json;
                {
                    std::lock_guard<std::mutex> lock(frameMtx_);
                    json = blobsJson_;
                }
                std::string msg = "data: " + json + "\n\n";
                sink.write(msg.data(), msg.size());
                return true;
            });
    });

    std::cout << "Web server starting on http://127.0.0.1:8080" << std::endl;
    svr.listen("127.0.0.1", 8080);
}
