# Sonarc

A lightweight real-time audio meter for Windows 10/11, written in C++20 with JUCE. It captures live audio (system output via WASAPI loopback, or any input device), analyzes it and draws meters.

**Status: Phase 1 (MVP).** It has a spectrum analyzer, a stereo peak/RMS meter, device selection with hot-plug reconnection, and persistent settings.

---

## Architecture

```
 ┌──────────────── capture thread (MMCSS "Audio") ───────────────┐
 │ CaptureBackend (WASAPI shared mode, loopback or input)        │
 │   GetBuffer → convert any format to float stereo → push       │
 └───────────────────────────────┬───────────────────────────────┘
                                 │ lock-free SPSC ring buffer (no locks, no allocation)
 ┌───────────────────────────────▼──────── analysis thread ──────┐
 │ AudioEngine::analysisLoop                                     │
 │   SpectrumAnalyzer (Hann window + radix-2 FFT, ballistics)    │
 │   StereoLevelMeter (sample peak, RMS, hold, max, clip)        │
 │   → publishes an AnalysisSnapshot (short mutex, UI side only) │
 └───────────────────────────────┬───────────────────────────────┘
                                 │ copy of latest snapshot
 ┌───────────────────────────────▼──────────── UI thread ────────┐
 │ MainComponent (VBlank-synced, capped at 60 FPS)               │
 │   SpectrumView · StereoPeakMeterView · device/settings bar    │
 └───────────────────────────────────────────────────────────────┘
```

- **`sonarc_core`** (static lib, plain C++20 + Win32, no JUCE) holds capture, DSP and the engine, so it can be unit-tested and reused, for example in a future DAW plugin bridge.
- **`Sonarc`** (JUCE GUI app) holds the UI, visualizers and settings. JUCE 8 renders through Direct2D on Windows, so drawing is GPU-accelerated.
- The capture thread does only format conversion and a ring-buffer push. It never blocks and never allocates. If analysis ever falls behind, frames are dropped from the visualizer's ring (shown in the status bar), and the audio itself is never affected.
- Capture uses **shared mode** with the device's own mix format. Sonarc never takes exclusive control, never changes the format and never alters routing. It only listens.

### Project layout

```
Sonarc/
├── CMakeLists.txt              top level: fetches JUCE, adds src/ and tests/
├── src/
│   ├── CMakeLists.txt          sonarc_core library + Sonarc app targets
│   ├── app/Main.cpp            JUCEApplication + main window
│   ├── audio/                  capture backends and engine
│   │   ├── AudioTypes.h            frames, device/format/status types, FrameSink
│   │   ├── CaptureBackend.h        backend interface (WASAPI today; ASIO/plugin later)
│   │   ├── WasapiCaptureBackend.*  loopback + input capture, device notifications
│   │   ├── SampleConversion.h      float32 / int16 / int24 / int32 → float stereo
│   │   └── AudioEngine.*           threads, ring buffer, reconnection, snapshots
│   ├── dsp/                    analysis
│   │   ├── Fft.*                   radix-2 real-input FFT
│   │   ├── SpectrumAnalyzer.*      windowed FFT, calibration, smoothing/decay/peak hold
│   │   ├── LevelMeter.*            peak / RMS / hold / max / clip
│   │   ├── Decibels.h
│   │   └── AnalysisSnapshot.h
│   ├── visualizers/            one reusable component per meter
│   │   ├── SpectrumView.*
│   │   └── StereoPeakMeterView.*
│   ├── ui/                     main layout, look and feel, theme
│   ├── settings/AppSettings.*  JSON settings (%APPDATA%\Sonarc\settings.json)
│   └── util/SpscRingBuffer.h
└── tests/                      DSP unit tests (no external test framework)
```

The `presets/` module and the other folders from the roadmap arrive in their phases. No empty placeholder modules are checked in.

---

## Capturing FL Studio when it uses Focusrite ASIO

**The problem.** With an ASIO driver selected (Focusrite USB ASIO, the default for a Scarlett in FL Studio), FL Studio talks to the interface directly and **bypasses the Windows mixer**. WASAPI loopback only sees what goes through the Windows mixer, so with ASIO, FL Studio's output is *not* in the loopback stream of the Scarlett's playback endpoint. Every WASAPI-based visualizer has this limitation; it isn't specific to Sonarc.

Options, simplest first:

| Option | How | Trade-off |
|---|---|---|
| **1. FL Studio on a WASAPI driver** | FL Studio → Options → Audio Settings → Device: *Primary Sound Driver* or *FL Studio ASIO* (which itself outputs through WASAPI). In Sonarc, pick the Scarlett's playback device (loopback). | Works immediately. Latency is a bit higher than native Focusrite ASIO, which is usually fine for mixing and mastering but less ideal for live tracking. |
| **2. Focusrite's loopback input** | Newer Scarlett 3rd/4th Gen units (with Focusrite Control 2) can expose a **Loopback** input pair that carries the interface's output mix. Enable it, then select the Scarlett *recording* device in Sonarc. | Native ASIO stays on. Depends on your model and firmware. Check whether *Focusrite Control* shows a "Loopback" input. |
| **2b. Per-app capture** | Select *FL Studio* under *Applications*. This works when FL Studio outputs through Windows (WASAPI / FL Studio ASIO), and you see only FL Studio, not YouTube or Discord. | It can't see audio sent straight to the Focusrite ASIO driver, for the same reason loopback can't. |
| **3. Virtual audio cable** | Route FL Studio's master (or a send) to a virtual device such as VB-Audio Cable / VoiceMeeter, and select that device in Sonarc. | Flexible, but adds a routing layer you have to manage. |
| **4. Plugin bridge (planned)** | A small VST3 on FL Studio's master that streams audio to Sonarc over shared memory, implemented as another `CaptureBackend`. | Works with any driver and gives sample-accurate, bit-exact audio. Planned for a later phase. |

The engine was built for this: `AudioEngine` only depends on the `CaptureBackend` interface, so ASIO input capture or a plugin bridge are new backend classes and need no DSP or UI changes. (ASIO input capture would also have to respect that most ASIO drivers allow only one client at a time.)

---

## Build on Windows

### 1. Install dependencies (once)

- **Visual Studio 2022 Build Tools** with the *Desktop development with C++* workload (MSVC + Windows SDK). The full Visual Studio 2022 Community IDE works too.
- **CMake 3.22+**
- **Git** (CMake uses it to download JUCE 8.0.9 automatically)

With winget (run in PowerShell; the VS installer may show a UAC prompt):

```powershell
winget install --id Kitware.CMake -e
winget install --id Git.Git -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### 2. Put the project in a short path

Windows' 260-character path limit breaks MSBuild and git inside deep folders. Keep the project somewhere short, such as `C:\dev\Sonarc`.

### 3. Configure, build, test

Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu, then:

```bat
cd C:\dev\Sonarc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\tests\sonarc_tests.exe
```

To use the Visual Studio IDE instead: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`, then `cmake --build build --config Release` (or open `build\Sonarc.sln`).

The first configure downloads JUCE (~150 MB) into `build\_deps`. To use an existing JUCE checkout instead, add `-DSONARC_JUCE_DIR=C:\path\to\JUCE`.

### 4. Run

```bat
build\src\Sonarc_artefacts\Release\Sonarc.exe
```

Play music. With **System default output** selected, the spectrum and meters follow whatever Windows is playing.

---

## Using it

- **Device list:** *Playback* devices are captured by loopback (what you hear). *Recording inputs* are microphones and line inputs. "System default …" follows Windows when you switch the default device.
- **FFT 1024–8192:** larger sizes give finer low-frequency resolution but respond more slowly.
- **Smooth / Decay:** time averaging, and the maximum fall speed in dB/s. Double-click a slider to reset it.
- **Tilt:** a display-only slope (dB/octave, pivot at 1 kHz). 4.5 dB/oct makes a typical mix look roughly flat. The hover readout always shows the **unweighted** level.
- **Peak hold:** a grey line on the spectrum and a white tick on the meter.
- **Meter:** solid bar = RMS (300 ms), lighter bar = sample peak, top boxes = max peak since reset (red = a sample reached 0 dBFS). **Click the meter** to reset.
- **H:** hide or show the controls for a clean view.
- **View (V):** *Spectrum*, *Waveform*, *Spectrogram*, or the combinations *Spectrum + Wave*, *Spectrum + Spectrogram* and all three. Panes sit side by side in a wide window and stack in a tall one.
- **Waveform:** a scrolling view of the real signal, with newest audio at the right.
  - **Spectral style (default):** colour shows what's sounding: bass is pink-red, mids amber and highs cyan (split at 250 Hz and 2.5 kHz). It is drawn mirrored, with a solid RMS core inside a softer peak body and a glow. Heights use a square-root scale so a mix fills the lane. **Classic style** is a plain, linear min/max trace per channel.
  - Every pixel shows the true min/max of the samples it covers, so short peaks never disappear when zoomed out.
  - **Mouse wheel** zooms from 20 ms (individual cycles) to 10 s.
  - Right-click → *Waveform* for presets, **Stacked L/R**, **Overlaid L+R** or **Mono (mid)**, and minimum line thickness.
  - Red marks at a lane's edge show samples at or beyond 0 dBFS.
- **Spectrum readout:** the top-left of the spectrum shows the loudest peak's level, frequency and nearest musical note with cents (e.g. `-12.6 dB  87.1 Hz  F2 -3c`). It updates 8 times a second.
- **Clean look:** with the controls hidden (overlay, stick or H), the visualisers drop their axis labels and info text for a stream-ready picture.
- **Spectrogram:** a scrolling history of the spectrum. Time runs left to right with the newest at the right edge, frequency runs from 20 Hz at the bottom to 20 kHz at the top on a log scale, and colour shows level.
  - Right-click → *Spectrogram* sets the **speed** (15–240 px/s), the **resolution** (FFT 1024–8192: larger is sharper in frequency, especially the low end, but blurrier in time), the **palette** (Sonarc, Ember, Ice, Grayscale) and the **range** (60/90/120 dB below 0 dBFS).
  - It uses the same calibration and *Tilt* as the spectrum.
  - It is computed only while it is on screen.
- **Freeze (F):** pauses everything (spectrum, waveform, spectrogram and meters) so you can inspect a moment. Press F again to go live.
- **On top (T):** pins Sonarc above every other window, such as a browser, video player, FL Studio or OBS. A watchdog checks every 0.75 s and puts Sonarc back on top if another app (e.g. another always-on-top window) has covered it.
- **Stick (S, or the Stick button):** docks Sonarc across the full width of the screen's top edge, like a toolbar.
  - Windows reserves that strip, so maximised windows sit **below** the bar instead of covering it. It works like the taskbar.
  - Right-click → *Stick to screen edge* chooses top or bottom and the bar height (60–240 px).
  - When a fullscreen game or video opens, the bar steps aside and comes back when it closes.
  - Press S again (or double-click) to return the window to where it was.
- **Overlay (O):** removes the title bar and frame. Drag anywhere on the spectrum to move the window and drag the edges to resize it. **Double-click or press Esc** to get the frame back. The meters stay in the same place on screen when you switch.
- **Right-click** anywhere on the spectrum for all window options: always on top, overlay, show controls, **opacity** (100–30%), reset peaks and quit.
- Pin, overlay, opacity and window position are remembered between launches.
- **Applications (per-app capture):** the source list has an *Applications* section listing every program with an audio session, such as Chrome, Spotify, a game or a video player. "- playing" marks the ones making sound right now. Pick one and Sonarc meters **only that program**, including audio from its child processes (browser tabs, Electron apps), even while other apps play. The list refreshes every few seconds. Sonarc remembers the app by name: if you close it, the status bar says it's waiting, and capture resumes automatically when it plays again. This needs Windows 10 version 2004 or later, or Windows 11. Windows mixes the app's audio to stereo float at your output's sample rate.
- Playback devices still show **everything** on that output.
- Unplugging a device shows a warning, and capture resumes automatically when the device returns.

Settings and the window position are saved to `%APPDATA%\Sonarc\settings.json`.

---

## Measurement notes and limitations (read these)

- **Spectrum calibration:** a full-scale sine centred on an FFT bin reads **0 dBFS**. The window is Hann, so a sine between bins can read up to **1.42 dB low** (scalloping). The spectrum shows the mid signal (L+R)/2, so a sine in only one channel reads 6 dB lower.
- **Peak meter = sample peak**, not true peak. Inter-sample (true) peak per ITU-R BS.1770 needs oversampling and comes with the loudness module in Phase 2. Nothing is labeled LUFS or dBTP yet.
- **RMS** uses an exponential 300 ms time constant. A full-scale sine reads −3.01 dBFS (no +3 dB "AES" offset).
- **Loopback while silent:** Windows delivers no loopback packets when nothing is playing. Sonarc treats that as digital silence so the meters fall naturally.
- **Loopback level:** loopback captures the stream Windows sends to the device. Depending on the device and driver, that level may or may not follow the Windows volume slider. Hardware volume knobs on the interface never affect it.
- **Surround devices:** only front L/R are shown.
- **Microphone inputs** need *Settings → Privacy & security → Microphone → Let desktop apps access your microphone*. Otherwise the status bar reports "access denied".
- Exclusive-mode apps that grab a device can prevent shared-mode capture on it.

## Roadmap

- **Phase 2:** goniometer/correlation, loudness (BS.1770 / R128) (waveform and spectrogram are done)
- **Phase 3:** modular, rearrangeable meters, layouts, themes, presets
- **Phase 4:** per-pixel transparent background, FPS setting (always-on-top, stick/dock mode, borderless overlay, clean stream look and window opacity are done)
- **Phase 5:** profiling, more reconnection hardening, installer, portable build
