#include "webserver.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include <httplib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "webserver_common.hpp"
#include "web_ui_shared.hpp"

// ---- HTML page (Orbbec-specific segments; shared parts come from web_ui_shared.hpp) ----

static const std::string kOrbbecControls2 = R"HTML(
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

static const std::string kOrbbecControls3 = R"HTML(
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

static const std::string kOrbbecControls4 = R"HTML(
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

static const std::string kOrbbecControls5 = R"HTML(
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

static const std::string kOrbbecControls6 = R"HTML(
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

// Orbbec-specific JS: resolution/fps element refs and handlers
static const std::string kOrbbecScript1 = R"HTML(
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

static const std::string kOrbbecScript2 = R"HTML(
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

static const std::string kOrbbecScript3 = R"HTML(
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

static const std::string kOrbbecScript4 = R"HTML(
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

static const std::string kOrbbecScript5 = R"HTML(
  // Orbbec: fetch camera config
  fetch('/cameraconfig').then(r=>r.json()).then(j => {
    resolutionSelect.value = j.resolution;
    camFpsSlider.value = j.fps;
    camFpsVal.textContent = j.fps + ' fps';
  });

  // Orbbec: fetch device capabilities and apply to UI
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

  // Orbbec: fetch current device property values
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

    std::string json = "{\n"
        + saveSharedSettingsJson(thresholdMm_.load(), thresholdEnabled_.load(),
            dilateIterations_.load(), blobDetectEnabled_.load(),
            maxBlobPixels_.load(), minBlobPixels_.load(), cameraFps_.load(),
            soundMode_.load(), soundKey_.load(), sScale,
            soundDecay_.load(), soundRelease_.load(), soundMoveThresh_.load(),
            sQuantize, soundVolume_.load(), soundTempo_.load(), showDepth_.load())
        + ",\n"
        // Platform-specific fields
        "  \"depthResolution\": " + std::to_string(depthResolution_.load()) + ",\n"
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
        "  \"colorGamma\": " + std::to_string(ds.colorGamma) + "\n"
        "}\n";

    writeSettingsFile(json);
}

void WebServer::loadSettings() {
    std::ifstream file("settings.json");
    if (!file) return;

    std::string text((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    // Load shared settings (threshold, blob, sound)
    loadSharedSettings(text, thresholdMm_, thresholdEnabled_, dilateIterations_,
        blobDetectEnabled_, maxBlobPixels_, minBlobPixels_, cameraFps_,
        soundMode_, soundKey_, soundDecay_, soundRelease_, soundMoveThresh_,
        soundVolume_, soundTempo_, showDepth_);

    int iv; bool bv; std::string sv;
    if (jsonInt(text, "depthResolution", iv)) depthResolution_.store(iv);

    // Platform-specific: post-proc
    {
        std::lock_guard<std::mutex> lock(postProcMtx_);
        if (jsonBool(text, "thresholdFilterEnable", bv)) postProc_.thresholdFilterEnable = bv;
        if (jsonInt(text, "thresholdFilterMin", iv)) postProc_.thresholdFilterMin = iv;
        if (jsonInt(text, "thresholdFilterMax", iv)) postProc_.thresholdFilterMax = iv;
    }

    // Platform-specific: device settings + sound strings
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
        if (jsonString(text, "soundScale", sv)) soundScale_ = sv;
        if (jsonString(text, "soundQuantize", sv)) soundQuantize_ = sv;
    }

    std::cout << "Loaded settings from settings.json" << std::endl;
}

void WebServer::run() {
    httplib::Server svr;

    // Build full HTML page from shared + Orbbec-specific parts
    std::string fullHtml = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
        "<title>DepthPalette (Orbbec)</title>\n<style>"
        + kSharedCss +
        "</style>\n</head>\n<body>"
        + kSharedHelpOverlay
        + "\n<h1>DepthPalette (Orbbec)</h1>\n"
        + kSharedControls
        + kOrbbecControls2 + kOrbbecControls3 + kOrbbecControls4
        + kOrbbecControls5 + kOrbbecControls6
        + kSharedImages
        + "\n<script>\n"
        + kSharedSoundJs
        + kSharedHandlersJs
        + kOrbbecScript1 + kOrbbecScript2 + kOrbbecScript3
        + kOrbbecScript4 + kOrbbecScript5
        + kSharedInitJs
        + "\n</script>\n</body>\n</html>";

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
        if (req.has_param("soundVolume")) {
            int v = std::stoi(req.get_param_value("soundVolume"));
            if (v < 0) v = 0; if (v > 100) v = 100;
            soundVolume_.store(v); changed = true;
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
            + ",\"soundVolume\":" + std::to_string(soundVolume_.load())
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

    std::cout << "Web server starting on http://0.0.0.0:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);
}
