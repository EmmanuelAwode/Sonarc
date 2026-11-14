# Sonarc

A lightweight real-time audio visualiser for Windows 10/11, written in C++20 with JUCE. It captures live audio (what you hear, a microphone, or a single app such as Spotify, a browser or FL Studio) and shows a spectrum, waveform, spectrogram, stereo meters, BPM and musical key. It can dock to the top or bottom of the screen as a bar that stays above other windows, which suits livestreams.

This repository also holds **Driftwave FX**, a slowed + reverb VST3 effect plugin for FL Studio: see [driftwave-vst](driftwave-vst/README.md) for its download and instructions.

## Download & install

**You need:** a 64-bit PC with Windows 11, or Windows 10 version 2004 or later. No admin rights, no other software.

### Install in 5 steps

1. **Download.** Open the [**latest release**](../../releases/latest). Under **Assets**, click **`Sonarc-<version>-windows-x64.zip`** (about 2 MB).
   Don't pick "Source code": that's for developers and has no app in it.
2. **Unzip.** In your Downloads folder, right-click the zip and choose **Extract All… → Extract**.
   Don't just double-click into the zip: the installer can't run from inside it.
3. **Install.** In the extracted folder, double-click **`Install Sonarc.cmd`**.
   - If Windows says *"Windows protected your PC"*, click **More info → Run anyway**. This appears because Sonarc isn't code-signed by a paid certificate.
   - A small window lists what it did and ends with *"Done"*. Press any key to close it.
4. **Open it.** Sonarc is now in the **Start menu** and on your **desktop**. Click either one. The first time it opens, Windows may show the same *"protected your PC"* message: again **More info → Run anyway**.
5. **Pin to the taskbar (optional).** While Sonarc is open, right-click its icon in the taskbar and choose **Pin to taskbar**. Windows doesn't let apps pin themselves, so this step is yours.

That's it. Play some music or a video and the visualiser reacts straight away. You can delete the zip and the extracted folder afterwards; Sonarc runs from `%LOCALAPPDATA%\Programs\Sonarc`.

### Updating, uninstalling, portable use

- **Update:** download the newer zip and repeat steps 2–3. It replaces the old version and keeps your settings. Close Sonarc first (the installer will try to close it for you).
- **Uninstall:** double-click **`Uninstall Sonarc.cmd`** in the extracted folder. It removes the app and its shortcuts. Your settings in `%APPDATA%\Sonarc` are kept; delete that folder too for a clean removal.
- **No install:** you can also just double-click `Sonarc.exe` in the extracted folder and use it from there.

### If something goes wrong

- **"Run anyway" isn't shown:** click the **More info** link first; the button only appears after that.
- **The install window flashes and closes, or shows red text:** make sure you extracted the zip (step 2), then try again. If it says Sonarc is still running, close it and run the installer again.
- **No movement in the visualiser:** check that sound is actually playing, then pick your speakers or headphones in the box at the top left (or right-click anywhere → **Audio source**).

### First steps

Sonarc follows your default output (what you hear) out of the box. Pick a different source, such as a microphone or a single app like Spotify, in the top-left box, or right-click anywhere for every option. The [Using it](#using-it) section covers each feature, and [Build on Windows](#build-on-windows) covers building from source.

Licence: [AGPLv3](LICENSE). Sonarc uses [JUCE](https://juce.com) under its AGPLv3 option.

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
├── resources/                  logo.png / logo-small.png (app icon, from tools/make_logo.py)
├── tools/                      sonarc_analyze, test-set generators, make_logo.py, install.ps1
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

### 5. Install (Start menu, desktop, taskbar)

```bat
powershell -ExecutionPolicy Bypass -File tools\install.ps1
```

This copies `Sonarc.exe` to `%LOCALAPPDATA%\Programs\Sonarc` (no admin rights needed) and adds **Sonarc** to the Start menu and the desktop, with the logo as its icon. Add `-NoDesktop` to skip the desktop shortcut, or `-Uninstall` to remove it all (settings are kept).

- **Taskbar:** open Sonarc from Start, right-click its taskbar icon and choose **Pin to taskbar**. Windows doesn't let programs pin themselves.
- **Updating:** after a rebuild, run the script again. It closes the installed Sonarc if it's running, then replaces it.
- **Logo:** a musical "S" whose tail ends in a note head, in the Sonarc spectrum colours. It's drawn by `python tools/make_logo.py` into `resources/`, and the build turns it into the exe icon (taskbar, Start, title bar) and the mark next to the wordmark in the control bar.

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
- **Waveform:** the real signal, drawn in one of six styles: **Classic** (per-channel min/max trace), **Mirrored** (symmetric, an RMS core inside the peak body), **Oscilloscope** (a continuous line, triggered on rising zero crossings so steady tones stand still), **Vertical bars**, **Filled**, and **Circular** (time wrapped around a ring that breathes with the level).
  - Every pixel uses the true min/max of the samples it covers, so peaks never disappear. The mouse wheel zooms the time window (20 ms to 10 s).
  - Colours always come from your colour theme. A style only picks *which* theme colours to use: spectral bands (bass/mid/high), channel colours, the spectrum gradient, or the accent.
- **Waveform Style Editor (W, or right-click → Waveform → Waveform Style Editor...):**
  - **Settings per style:** line thickness, glow, amplitude, smoothing, time shown (scroll speed), animation speed, stereo separation, peak hold, fill opacity, bar count and direction. Only the settings the chosen style uses are shown, and each has its own *Reset*. *Reset style* undoes all unsaved changes.
  - **Live preview:** uses the real audio. When nothing is playing it switches to a clearly labelled **TEST SIGNAL (simulated)**, and you can also choose live-only or test-only.
  - **Presets:** built-ins *Spectral Mirror* (the default, and what *Restore default* returns to), *Classic*, *Neon Oscilloscope*, *Minimal*, *Bass Pulse*, *Retro Bars* and *Circular Glow*. Manage them with **Save**, **Save As**, **Rename**, **Duplicate**, **Delete** and **Favourite** (favourites appear first, in the editor and in right-click → *Waveform*). Built-ins can't be changed or deleted; use *Save As* to keep your version.
  - **Import / Export** shares presets as JSON (`"format": "sonarc-waveform-preset", "version": 1`). Imports are validated: unknown shapes, wrong types, out-of-range values and files from a newer format version are rejected with a reason, and a bad preset doesn't stop the good ones in the same file.
  - **Storage:** your presets live in `%APPDATA%\Sonarc\waveform-presets.json`, separate from settings and colours. The selected preset and any unsaved edits are restored at launch, and switching presets never interrupts audio capture.
- **BPM module:**
  - **Detection:** finds the tempo of what's playing (spectral-flux onsets, autocorrelation over the last 8 s, comb and half-period scoring), smoothed so it doesn't flicker. A new tempo has to hold for about 3 s before it's shown.
  - **Readings:** the bar under the number shows confidence (periodicity strength and stability). It shows **"--"** when there's no reliable tempo, including silence and noise.
  - **Half and double time:** tempos are folded into a range (default 85–170 BPM). **1/2** and **x2** switch, for example, 70 ↔ 140, and right-click → *Modules* → *BPM range* sets the range.
  - **TAP / B:** three taps set and lock a tapped tempo. **LOCK** holds the current value.
  - **Beat pulse:** an optional dot that flashes in time with the detected beat.
- **Key module:**
  - **Detection:** finds the key (all 24 major and minor keys) from a chroma of spectral peaks (110 Hz–4 kHz), correlated with the Krumhansl-Kessler key profiles. It has about 20 s of memory and hysteresis, so a new key must lead for about 4 s.
  - **Readings:** shows the key name, its **Camelot** code (e.g. `11A` for F# minor) in a colour-coded pill, and a confidence bar. **LOCK** holds it.
  - **Gating:** silent, noisy or drum-only passages don't count as evidence, so the module shows **"--"** until there's enough tonal music (about 3 s).
- **Modules (right-click → Modules):** show or hide BPM and Key. Each detector only runs while its module is visible. The layout is *[visualisers] [BPM] [Key] [meters]*, and it fits the stick bar.
- **Spectrum readout:** the top-left of the spectrum shows the loudest peak's level, frequency and nearest musical note with cents (e.g. `-12.6 dB  87.1 Hz  F2 -3c`). It updates 8 times a second.
- **Clean look:** with the controls hidden (overlay, stick or H), the visualisers drop their axis labels and info text for a stream-ready picture.
- **Spectrogram:** a scrolling history of the spectrum. Time runs left to right with the newest at the right edge, frequency runs from 20 Hz at the bottom to 20 kHz at the top on a log scale, and colour shows level.
  - Right-click → *Spectrogram* sets the **speed** (15–240 px/s), the **resolution** (FFT 1024–8192: larger is sharper in frequency, especially the low end, but blurrier in time), the **palette** (Sonarc, Ember, Ice, Grayscale) and the **range** (60/90/120 dB below 0 dBFS).
  - It uses the same calibration and *Tilt* as the spectrum.
  - It is computed only while it is on screen.
- **Colours & theme (C, or right-click → Colours & theme...):** change the look of everything.
  - **Presets:** *Sonarc*, *Neon Night*, *Sunset*, *Arctic*, *Forest* and *Studio Mono*. For a one-click switch, use right-click → *Theme*. In the panel, the up/down arrows flip through presets.
  - **Customise any colour:** click a swatch, then use the picker (colour field, hue, RGB/alpha and hex). The swatches cover window & panels (background, panels, buttons, grid, labels, text, accent), the spectrum's six frequency colours (20 Hz to 20 kHz) and peak-hold line, the waveform's bass/mid/high colours (spectral style) and left/right colours (classic style), and the five meter colours from quiet to clip.
  - **Spectrum fill** and **Glow** sliders set the area opacity and glow strength (0 turns the glow off).
  - Changes show live and are saved automatically. Once you edit a preset it becomes *Custom*, and *Reset to preset* goes back.
  - The spectrogram has a *Theme colours* palette that follows your spectrum colours (right-click → *Spectrogram*).
  - Warning and clip-red stay fixed, because they carry meaning.
- **Freeze (F):** pauses everything (spectrum, waveform, spectrogram and meters) so you can inspect a moment. Press F again to go live.
- **On top (T):** pins Sonarc above every other window, such as a browser, video player, FL Studio or OBS. A watchdog checks every 0.75 s and puts Sonarc back on top if another app (e.g. another always-on-top window) has covered it.
- **Stick (S, or the Stick button):** docks Sonarc across the full width of the screen's top edge, like a toolbar.
  - Windows reserves that strip, so maximised windows sit **below** the bar instead of covering it. It works like the taskbar.
  - Right-click → *Stick to screen edge* chooses top or bottom and the bar height (60–240 px).
  - When a fullscreen game or video opens, the bar steps aside and comes back when it closes. **With On top (T) switched on it stays over fullscreen apps instead**, e.g. a bar across a fullscreen video.
  - Press S again (or Esc) to return the window to where it was. Double-clicking the bar doesn't unstick it, so a stray double-click can't knock it loose.
- **Overlay (O):** removes the title bar and frame. Drag anywhere on the spectrum to move the window and drag the edges to resize it. **Double-click or press Esc** to get the frame back. The meters stay in the same place on screen when you switch.
- **Right-click** anywhere (visualisers, BPM, Key or meters) for every option, so nothing needs the control bar, even while stuck or in overlay:
  - **Audio source:** the same list as the source box (playback loopback, recording inputs, applications), with the current one named in the menu.
  - **Spectrum:** FFT size, smoothing, fall speed, tilt and peak hold.
  - View, waveform, spectrogram, modules, theme, freeze, stick, always on top, overlay, show controls, **opacity** (100–30%), reset peaks and quit.
- Pin, overlay, opacity and window position are remembered between launches.
- **Applications (per-app capture):** the source list has an *Applications* section listing every program with an audio session, such as Chrome, Spotify, a game or a video player. "- playing" marks the ones making sound right now. Pick one and Sonarc meters **only that program**, including audio from its child processes (browser tabs, Electron apps), even while other apps play. The list refreshes every few seconds. Sonarc remembers the app by name: if you close it, the status bar says it's waiting, and capture resumes automatically when it plays again. This needs Windows 10 version 2004 or later, or Windows 11. Windows mixes the app's audio to stereo float at your output's sample rate.
- Playback devices still show **everything** on that output.
- Unplugging a device shows a warning, and capture resumes automatically when the device returns.

Settings and the window position are saved to `%APPDATA%\Sonarc\settings.json`.

---

## Measuring BPM / key accuracy

`sonarc_analyze` runs the app's exact BPM and key detectors over audio files (WAV, MP3, FLAC, OGG, AIFF). It reports the reading shown for most of each track, how stable it was, and how long the first reading took:

```bat
build\tools\sonarc_analyze_artefacts\Release\sonarc_analyze.exe "C:\Music\My tracks"
```

`tools/make_test_set.js` (Node) and `tools/make_test_set.py` generate 15 music-like test tracks with known tempo and key: house, hip-hop with swing, trap, DnB, pop, techno, ballad, reggaeton, funk, afrobeat (3-3-2), lo-fi with 7th chords, EDM, a 3/4 waltz, piano only, and drums only. The JS version is deterministic and produces byte-identical WAVs in Node and in a browser, so web analysers can be fed exactly the same audio.

Comparison on that set (30 s tracks), against Tunebat's free in-browser analyser:

| | Key correct | "No key" on drums only | BPM exact | BPM allowing ½ / ×2 |
|---|---|---|---|---|
| Sonarc | 14 / 14 | yes | 13 / 15 | 15 / 15 |
| Tunebat (Standard) | 11 / 14 | no ("B major") | 13 / 15 | 15 / 15 |

Sonarc's two "BPM exact" misses are octave choices from the 85–170 range (174 → 87, 72 → 144), and the ½ / ×2 buttons switch them. Tunebat's were 140 → 70 and 174 → 87. Synthetic tracks are cleaner than real mixes, and Tunebat analyses whole files while Sonarc listens live.

## Measurement notes and limitations (read these)

- **BPM:** it's accurate to within about ±1 BPM on steady, beat-driven music. That's measured in tests from 80 to 175 BPM (including afrobeat 3-3-2 and swung rhythms), and as 124.4 on a 124 BPM track in the app. Music with free tempo (rubato), live drift, sparse or highly syncopated rhythms can read low-confidence or "--". Half and double time is inherently ambiguous, which is what the range setting and the 1/2 / x2 buttons are for.
- **Key:** it estimates the overall key from the last ~20 s, so short modulations, modal or atonal music, and the relative major/minor pair (e.g. C major vs A minor, which share every note) are the usual sources of disagreement with a human. It doesn't analyse chords.
- **Loopback:** BPM and key analyse whatever the selected source plays. With *System default output* that is *every* app mixed together, so use *Applications* to analyse just one player.

- **Spectrum calibration:** a full-scale sine centred on an FFT bin reads **0 dBFS**. The window is Hann, so a sine between bins can read up to **1.42 dB low** (scalloping). The spectrum shows the mid signal (L+R)/2, so a sine in only one channel reads 6 dB lower.
- **Peak meter = sample peak**, not true peak. Inter-sample (true) peak per ITU-R BS.1770 needs oversampling and comes with the loudness module in Phase 2. Nothing is labeled LUFS or dBTP yet.
- **RMS** uses an exponential 300 ms time constant. A full-scale sine reads −3.01 dBFS (no +3 dB "AES" offset).
- **Loopback while silent:** Windows delivers no loopback packets when nothing is playing. Sonarc treats that as digital silence so the meters fall naturally.
- **Loopback level:** loopback captures the stream Windows sends to the device. Depending on the device and driver, that level may or may not follow the Windows volume slider. Hardware volume knobs on the interface never affect it.
- **Surround devices:** only front L/R are shown.
- **Microphone inputs** need *Settings → Privacy & security → Microphone → Let desktop apps access your microphone*. Otherwise the status bar reports "access denied".
- Exclusive-mode apps that grab a device can prevent shared-mode capture on it.

## Roadmap

- **Phase 2:** goniometer/correlation, loudness (BS.1770 / R128) (waveform styles, spectrogram, BPM and key detection are done)
- **Phase 3:** modular, rearrangeable meters, layouts, themes, presets
- **Phase 4:** per-pixel transparent background, FPS setting (always-on-top, stick/dock mode, borderless overlay, clean stream look and window opacity are done)
- **Phase 5:** profiling, more reconnection hardening, installer, portable build
