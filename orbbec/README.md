# DepthPalette for Orbbec cameras

Uses Orbbec SDK v2.7.6, which supports both Gemini 305 and Gemini 335.
Connect one camera and run from this directory to use `orbbec/settings.json`:

```powershell
cd C:\Users\nosuc\github\depthpalette\orbbec
.\build\bin\depthpalette.exe --color
```

Open http://127.0.0.1:8080 on the same computer. Omit `--color` for
depth only; add `--window` for the native viewer. Stop with Ctrl+C when
running in a terminal. Stop the program before switching cameras, then
run it again. No camera-model flag is required.

The **Color streaming** checkbox switches the actual color camera stream
on or off during a session. It also hides/shows the color preview and controls,
and stops browser color downloads while off. Switching briefly restarts the
camera pipeline (including depth). `--color` sets the initial state each time
the application launches; the checkbox does not change that startup option.

The program selects an advertised Y16 depth profile and a supported RGB,
BGR, MJPEG or YUYV color profile. Unsupported saved depth work modes are
ignored. Depth values are converted using each frame's reported scale.
Existing threshold and blob-size settings still apply when changing cameras;
adjust them in the web UI for the new camera's range and placement.

Build with `.\build.bat` (Visual Studio 2022 C++ tools and CMake required).
In a Visual Studio developer terminal, run the camera-free regression checks:

```powershell
cmake --build build --target colorstream_test
.\build\bin\colorstream_test.exe
```

Validated with Gemini 335 firmware 1.4.60 over USB3.2 at 848x480 / 60 FPS,
including depth and color HTTP images. Gemini 305 was not physically retested
for this change; its RGB conversion path is covered by the regression checks.

At 1280x800 the Gemini 335's standard minimum working distance is about
260 mm and its maximum frame rate is 30 FPS (848x480: about 180 mm and
60 FPS). A 250 mm threshold can therefore produce an entirely white image
at 1280x800 even while the camera is streaming. Increase the threshold and
keep the subject beyond the minimum distance, or use 848x480 for closer
objects. The UI reports the actual stream profile and nearest valid depth,
and explains when no depth falls within the threshold.
See the [Orbbec datasheet](https://www.orbbec.com/wp-content/uploads/2025/06/Gemini-330-series-Datasheet-V1.6.pdf).

Depth diagnostics regression check: build target `depthstats_test`, then run
`build\bin\depthstats_test.exe`.

`--color` displays the RGB camera stream. Saturation zero makes that stream
greyscale; it is separate from the depth preview. Unspecified sharpness,
saturation, contrast and gamma use the connected camera's defaults (`-1`
in settings.json). Explicit saved values, including saturation zero, are
preserved. The Gemini 335 reports defaults of 50, 64, 50 and 300 respectively.
Run target `color_settings_test` to check default resolution and overrides.
