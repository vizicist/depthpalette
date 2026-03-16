#pragma once

// Shared HTML, CSS, and JavaScript string constants used by both
// orbbec and luxonis webserver.cpp. Each platform composes its
// full HTML page by concatenating these with platform-specific parts.

#include <string>

// ---- Shared CSS ----
inline const std::string kSharedCss = R"HTML(
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
)HTML";

// ---- Help overlay HTML ----
inline const std::string kSharedHelpOverlay = R"HTML(
<div id="helpOverlay" class="help-overlay" onclick="if(event.target===this)closeHelp()">
  <div class="help-box">
    <h3 id="helpTitle"></h3>
    <p id="helpText"></p>
    <button onclick="closeHelp()">Close</button>
  </div>
</div>
)HTML";

// ---- Shared controls: threshold, blob detection, sound ----
inline const std::string kSharedControls = R"HTML(
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
  <label>Vol:
    <input id="volumeSlider" class="slider" type="range" min="0" max="100" step="1" value="50" style="width:80px">
  </label>
  <span id="volumeVal" class="val">50%</span>
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
  <span id="usbInfo" class="fps" style="margin-left:12px"></span>
</div>
)HTML";

// ---- Images section ----
inline const std::string kSharedImages = R"HTML(
<div class="images">
  <img id="colorImg" src="/color.mjpeg" alt="Color">
  <div class="depth-wrap">
    <img id="depthImg" src="/depth.mjpeg" alt="Depth (B/W)">
    <canvas id="dotsCanvas" style="display:none"></canvas>
  </div>
</div>
)HTML";

// ---- Shared JS: sound engine + blob drawing ----
inline const std::string kSharedSoundJs = R"HTML(
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

  // Mode: 0=No Dots, 1=Dots Only, 2=Dots+Pitches, 3=Dots+Sound
  let currentMode = 0;
  let evtSource = null;
  let activeTones = new Map();
  let audioCtx = null;
  let masterGain = null;

  const keySelect = document.getElementById('keySelect');
  const scaleSelect = document.getElementById('scaleSelect');
  const volumeSlider = document.getElementById('volumeSlider');
  const volumeValSpan = document.getElementById('volumeVal');
  volumeSlider.addEventListener('input', function() {
    volumeValSpan.textContent = volumeSlider.value + '%';
    if (masterGain) masterGain.gain.value = volumeSlider.value / 100;
  });
  volumeSlider.addEventListener('change', function() {
    fetch('/soundsettings?soundVolume=' + volumeSlider.value);
  });
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
    if (!masterGain) {
      masterGain = audioCtx.createGain();
      masterGain.gain.value = volumeSlider.value / 100;
      masterGain.connect(audioCtx.destination);
    }
    const osc = audioCtx.createOscillator();
    const g = audioCtx.createGain();
    const decay = decaySlider.value / 10;
    osc.type = 'sine';
    osc.frequency.value = freq;
    g.gain.setValueAtTime(0.3, audioCtx.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + decay);
    osc.connect(g).connect(masterGain);
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
)HTML";

// ---- Shared JS: control event handlers ----
inline const std::string kSharedHandlersJs = R"HTML(
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
)HTML";

// ---- Shared JS: page-load init + FPS polling ----
inline const std::string kSharedInitJs = R"HTML(
  // Restore sound settings on page load
  fetch('/soundsettings').then(r=>r.json()).then(d => {
    modeSelect.value = d.soundMode;
    currentMode = d.soundMode;
    keySelect.value = d.soundKey;
    scaleSelect.value = d.soundScale;
    volumeSlider.value = d.soundVolume;
    volumeValSpan.textContent = d.soundVolume + '%';
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
    if (currentMode >= 1) startDotsStream();
  });

  // Restore threshold/blob settings on page load
  fetch('/threshold').then(r=>r.json()).then(d => {
    threshToggle.checked = d.enabled;
    threshSlider.value = d.threshold;
    threshVal.textContent = d.threshold + ' mm';
    threshSlider.disabled = !d.enabled;
    dilateSlider.value = d.dilate;
    dilateVal.textContent = d.dilate;
  });
  fetch('/blobdetect').then(r=>r.json()).then(d => {
    blobToggle.checked = d.enabled;
    blobSlider.value = d.maxsize;
    blobVal.textContent = d.maxsize + ' px';
    minBlobSlider.value = d.minsize;
    minBlobVal.textContent = d.minsize + ' px';
  });

  // FPS polling
  const fpsDisplay = document.getElementById('fpsDisplay');
  function refreshFps() {
    fetch('/fps').then(r=>r.json()).then(j => { fpsDisplay.textContent = j.fps.toFixed(1) + ' fps'; });
  }
  setInterval(refreshFps, 1000);
  refreshFps();

  // Device info (USB version etc.)
  fetch('/deviceinfo').then(r=>r.json()).then(d => {
    if (d.connectionType) {
      document.getElementById('usbInfo').textContent = d.connectionType;
    }
  }).catch(function(){});
)HTML";
