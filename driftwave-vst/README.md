# Driftwave FX

A real-time VST3 effect for FL Studio (Windows x64): the effect side of the [Driftwave](https://github.com/EmmanuelAwode/driftwave) slowed + reverb app, as a plugin for mixer tracks. C++20, JUCE 8, CMake.

**This version (0.6.0):** presets, speed from 50% to 150% (in time with the song, or a true tape slow-down), reverb, bass boost, output gain with clip guard, live displays, a large visualiser in four styles, a choice of interface colour, resizable interface. Zero latency. Independent pitch comes in a later phase (see [Roadmap](#roadmap)).

## Download & install

**You need:** a 64-bit PC with Windows 10 or 11 and a DAW that loads VST3 effects (made for FL Studio). No other software.

1. **Download.** Open the [**Driftwave FX 0.6.0 release**](https://github.com/EmmanuelAwode/Sonarc/releases/tag/driftwave-fx-v0.6.0). Under **Assets**, click **`DriftwaveFX-0.6.0-windows-x64.zip`** (about 2 MB). Don't pick "Source code": that has no plugin in it.
2. **Unzip.** Right-click the zip and choose **Extract All**. The installer can't run from inside the zip.
3. **Install.** Close your DAW, double-click **`Install Driftwave FX.cmd`** and say **Yes** to the Windows prompt. If Windows says *"Windows protected your PC"*, click **More info**, then **Run anyway**; this appears because the installer isn't code-signed.
4. **Find it.** In FL Studio: *Options > Manage plugins > Find installed plugins*, then add **Driftwave FX** from Effects, on the master channel.

To update, install a newer zip the same way. To remove it, double-click **`Uninstall Driftwave FX.cmd`**. Your presets and colours in `Documents\Driftwave FX` are kept either way.

## Build on Windows

### 1. Install the tools (once)

- **Visual Studio 2022 Build Tools** with the *Desktop development with C++* workload (the full Visual Studio 2022 IDE works too)
- **CMake 3.22+**
- **Git** (CMake uses it to download JUCE 8.0.9)

```powershell
winget install --id Kitware.CMake -e
```

```powershell
winget install --id Git.Git -e
```

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### 2. Build and test

Keep the project in a short path such as `C:\dev\driftwave-vst`; deep folders hit Windows' 260-character path limit. Then double-click `build.cmd`, or from any command prompt:

```bat
build.cmd
```

It finds Visual Studio, configures with Ninja, builds the Release plugin and runs the DSP tests. The first run downloads JUCE (about 150 MB) into `build\_deps`. To use a JUCE checkout you already have:

```bat
build.cmd -DDRIFTWAVE_JUCE_DIR=C:\dev\JUCE
```

The manual equivalent, from the *x64 Native Tools Command Prompt for VS 2022*:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The plugin is written to `build\DriftwaveFX_artefacts\Release\VST3\Driftwave FX.vst3` (a folder; the whole folder is the plugin).

### 3. Install into FL Studio

Copy the `Driftwave FX.vst3` folder into `C:\Program Files\Common Files\VST3`. From an **administrator** command prompt in the project folder:

```bat
xcopy /E /I /Y "build\DriftwaveFX_artefacts\Release\VST3\Driftwave FX.vst3" "%CommonProgramFiles%\VST3\Driftwave FX.vst3"
```

(Or configure with `-DDRIFTWAVE_COPY_PLUGIN=ON` from an administrator prompt and every build copies it for you.)

Then in FL Studio: **Options → Manage plugins → Find installed plugins**. *Driftwave FX* appears under *Effects*; tick the star to add it to the plugin list, and load it into a mixer insert slot.

Close FL Studio (or at least remove the plugin from the project) before rebuilding and copying again: Windows locks the file while it is loaded.

## Controls

| Section | Control | Range | Notes |
| --- | --- | --- | --- |
| Speed | Speed | 50–150% | Tape style: pitch follows speed. See [Speed on the master](#speed-on-the-master) |
| | Sync to song | switch, off by default | Changes the pitch but stays in time with FL Studio; change the project tempo for the tempo part. Presets never change this switch. Always used above 100% |
| | Catch up | button | Jumps back to what FL Studio is playing now (not needed with Sync on) |
| Reverb | Mix | 0-100% | Equal-power dry/wet. 40% is the classic amount |
| | Decay | 0.5-5 s | How long the reverb rings. 3 s is the classic length |
| | Low cut / High cut | 20 Hz-1 kHz / 1-20 kHz | Optional tone shaping of the reverb only. Both are off at their end stops, which is the classic sound |
| Bass | Boost | 0–18 dB | Low shelf |
| | Freq | 40–200 Hz | |
| | Drive | 0–100% | Saturates the low band only |
| Output | Gain | −24 to +12 dB | |
| | Ceiling + Clip guard | −12 to 0 dB | Soft ceiling: untouched below 85% of the ceiling, never above it |

Each section's switch (the small square next to its name) bypasses it without a click. On any knob: drag to change, hold Shift while dragging for fine steps, double-click to reset, and click the value under it to type a number. Hover a control for a short description. Drag the window's corner to resize. Every control can be automated (right-click a knob in FL Studio's own parameter list, or use *Last tweaked*).

## Sharing it with others

After a build, this makes one zip that anyone on 64-bit Windows 10 or 11 can install:

```
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

It writes `dist\DriftwaveFX-<version>-windows-x64.zip` (about 2 MB) with the plugin, `Install Driftwave FX.cmd`, `Uninstall Driftwave FX.cmd`, the licence and a read-me. The person downloading it unzips it, double-clicks the installer and approves the Windows prompt; the plugin lands in `C:\Program Files\Common Files\VST3`. Nothing else has to be installed (the plugin carries its own runtime), and their presets and colours survive updates and uninstalling.

The files are not code-signed, so Windows shows "Windows protected your PC" the first time (*More info* > *Run anyway*). Removing that warning needs a paid code-signing certificate.

The plugin is built with JUCE under its AGPLv3 licence, so anyone you give the zip to must also be able to get this source code (a public repository is enough).

## Presets

The bar at the top lists the built-in presets, then your own. The arrows step through them.

| Built in | What it is |
| --- | --- |
| Default | Everything at its starting value (reverb only, speed off) |
| Slowed + Reverb | The reference sound: 80% speed, 40% reverb, 3 s |
| Sped Up | The reference sped-up sound: 120%, no reverb |
| Slowed Only / Reverb Only | Each half of the classic sound on its own |
| Deep Slowed | 70%, longer and darker reverb, a little bass |
| Dreamy | 85%, long soft reverb with the highs rolled off |
| Night Drive | 88%, shorter reverb, warm driven bass |
| Heavy Bass | 80%, big saturated low end, less reverb |
| Light Reverb | No speed change, short quiet reverb |
| Nightcore | 130%, no reverb |

Presets do not store or change **Sync to song**: it stays however you set it, whichever preset you pick.

**SAVE** stores the current settings under a name of your choice; saving under the same name again updates it. **DELETE** moves one of your presets to the recycle bin (built-in ones cannot be deleted). Your presets are small files in `Documents\Driftwave FX\Presets`, one per preset, so you can back them up or send them to someone: dropping a `.dwpreset` file into that folder makes it appear in the list. The name of the preset in use is saved with the FL Studio project.

## Speed on the master

An effect only receives audio as FL Studio plays it. It cannot play anything before it arrives, and if it plays slower it falls behind. So the Speed section has two modes:

- **Sync to song on:** the pitch moves with the Speed knob, down or up, and the audio stays on the beat with FL Studio's playhead. The tempo stays the project's own.
- **Sync to song off (the default):** a true tape slow-down, where the tempo drops too and the sound drifts behind the playhead. This is the cleanest sound, because the audio is only resampled. It only exists below 100%; above 100% the plugin always uses Sync.

### Sync to song off: true slow-down

With Sync off, the plugin records what comes in and plays it back at the chosen speed, so the sound falls steadily behind FL Studio's playhead: at 80%, by 12 seconds for every minute played. The readout under the knob shows how far behind it is.

- **Stopping playback:** the slowed audio carries on until it has finished, then the plugin returns to *LIVE* by itself.
- **Starting playback again, or moving the playhead:** the plugin starts again from the live input at the new position instead of finishing the old audio. A looping section restarts the same way each time round.
- **Catch up**, or switching Speed off, jumps back to the live input with a short crossfade.
- **Exporting:** the slowed song is longer than the project. In the export dialog set *Tail* to **Leave remainder** so FL Studio keeps rendering until the plugin has finished. Rendering is where this works best, because nothing needs to stay in sync.
- **Limit:** the plugin can hold 3 minutes of delay (about 70 MB at 48 kHz). That is a 15-minute song at 80%, or 6 minutes at 50%. Past that it catches up with a crossfade.
- **Sync:** anything after the plugin (and FL Studio's playhead, metronome and visuals) is no longer lined up with what you hear. That is unavoidable for a live slow-down.

### Sync to song on

The pitch changes exactly as far as the Speed knob says (80% is about 3.9 semitones down, 125% about 3.9 up), but the song keeps FL Studio's tempo and nothing drifts, so the playhead, the playlist and anything on screen stay on the beat. To get the full slowed or sped-up sound, also change the project tempo by the same percentage (140 BPM at 80% is 112 BPM, at 125% it is 175 BPM); that only works for projects that follow the tempo (patterns, or audio clips set to stretch). A plugin cannot change FL Studio's tempo itself.

It works by reading the sound at the new speed with one read head. Before that head drifts too far from the live input, a new one takes over with an 8 ms crossfade (a splice). Slower than 100%, each splice skips a little of the song; faster, it plays a little twice. Two things decide where a splice goes:

- **Held sound:** the new head starts where its waveform lines up with the old one, so notes and bass carry on unbroken.
- **Drum hits:** hits are detected as they come in, and a splice is placed, or put off for a moment, so that the skipped or repeated stretch has no hit in it.

Between splices the output is a single plain copy of the input, exactly as the tape mode would play it. This is still the less clean of the two modes: very dense material (fast rolls over busy chords) can leave no good place for a splice, and then one is made anyway. The sound comes out about 25 ms late on average. Exports need no special tail setting in this mode.

## Sound quality

- **Nothing is touched unless asked:** with Speed off, or at exactly 100%, the audio passes through the speed section bit for bit. The same goes for a switched-off Bass or Reverb section.
- **Speed change:** the audio is resampled with a 64-point windowed-sinc filter, the method high-quality sample-rate converters use. In the tests a tone slowed to 80% keeps its level within 0.1 dB up to 20 kHz and leaves no image above -70 dB. This is the same "speed and pitch together" change that slowed + reverb edits are made with, so the tape mode is as accurate as doing it offline.
- **Faster than 100%:** the filter narrows so that nothing above the new top frequency folds back as aliasing.
- **Sync:** one copy of the sound at a time, spliced with cross-correlation alignment (the technique classic studio pitch shifters use), refined to the exact sample, with the splices kept clear of drum hits. In the tests a held note stays level within 1.5 dB from 41 Hz up, and every drum hit comes through once and whole at 70%, 80%, 90%, 120% and 150%, alone and over a held bass note.
- **Cost:** the whole plugin uses about 3% of one processor core, most of it the reverb (`build\driftwave_speed_bench.exe` measures it).
- **Reverb:** a true convolution with stereo noise that dies away exponentially after a 10 ms gap. This is exactly how the popular browser slowed + reverb tools make their reverb (a Web Audio convolver fed a generated noise burst, 3 seconds long in decay, mixed at 40% with an equal-power crossfade), reproduced the same way rather than imitated. Measured against that reference in a browser: the gap is 10 ms in both, the decay is 20 dB per second in both, left and right are unrelated in both, and the wet level is within 0.3 dB. The noise itself is random in the browser tools (different on every page load) and fixed in the plugin, so two renders of the same project are identical.
- **Decay** changes the length without recomputing anything, so it can be automated without clicks. The reverb's level stays the same whatever the decay.
- **Order:** speed first, then reverb, so the reverb tail itself is never slowed or pitched.

## Interface

A thin header, a band of three live displays, and one strip of controls, in a restrained dark palette with a single amber accent. Every colour and text size lives in `plugin/LookAndFeel.h`.

**Displays.** All three draw real data; none of the motion is decorative.

| Display | What it shows | Where the data comes from |
| --- | --- | --- |
| Scope (over Speed) | The last four seconds of output, the pitch change in semitones, and whether the plugin is live, in sync, or how many seconds behind the song | The plugin's output audio |
| Tail (over Reverb) | The reverb tail the controls ask for: 10 ms gap, falling 60 dB in the Decay time, as tall as Mix allows, on a seconds scale. An inner shape shows how much of the range Low cut and High cut leave in. It lights up with the reverb that is actually sounding | Shape from the control values, brightness from the measured reverb level |
| Spectrum (over Bass and Output) | Input (grey line) against output (amber). The white curve is the bass shelf's response, the shaded area on the left is the band Drive works on, and the bar along the top is the range the reverb works in | A 2048-point FFT of the input and output audio; the shelf curve is computed from the same filter coefficients the audio goes through |

The spectrum is tilted by 3 dB per octave so that music reads roughly level instead of falling away to the right. The tail's height follows level through a display curve, like a meter does.

**Meter.** Stereo peak level with peak hold and clip lamps. The thin bar to its left shows how much the clip guard is taking off the loudest peaks, to the same dB scale.

**Motion.** Knob rings glide to a new value when a preset or automation moves them and follow the hand directly while dragged. Hovering a knob lifts it slightly, and its pointer turns amber while it is turned. Switch lamps and bypassed sections fade over about a tenth of a second. Nothing moves unless a control or the audio does.

**Visualiser.** The **Visualiser** button in the header opens a large panel above the displays, made for recording videos; the window grows taller and the setting is saved with the project. Everything in it comes from the audio or a control:

- **Bars:** the spectrum of the plugin's output, 30 Hz to 16 kHz.
- **Trail behind the bars (blue-grey):** where the bars have been, falling at the reverb's own rate, so a longer Decay lingers longer and a higher Mix shows stronger. Hidden with Reverb off.
- **Brighter low bars:** the ones the bass boost is lifting, by how much.
- **Ribbon:** the output waveform. It holds six seconds at 100% and moves slower or faster with the Speed knob.
- **Text:** the preset's name and what is switched on.

Click the panel to step through its four styles (the choice is saved with the project). It redraws 60 times a second.

| Style | What it draws |
| --- | --- |
| Bars | Spectrum bars standing on a line, the reverb trail behind them |
| Mirror | The same bars and trail growing both ways from a centre line |
| Circle | Spectrum spokes around a disc that swells with the low end, the trail as an outline, the waveform running across behind |
| Ridges | The spectrum now at the front and at earlier moments behind it, reaching back as far as the reverb rings |

**Colour.** The **Colour** button in the header changes the accent colour of the whole interface, displays and visualiser included; the reverb's displays use a quiet companion colour worked out from it.

- **Built in:** Amber (the default), Rose, Crimson, Violet, Ocean, Cyan, Mint, Lime and Silver.
- **Custom colour...** opens a picker; the interface follows it as you drag.
- **Save this colour...** stores the current colour under a name, so it appears in the list in every project. **Remove a saved colour** takes one out again.
- **What is remembered:** the colour is saved with the FL Studio project, and the colour you used last is what new instances of the plugin start with. Saved colours live in `Documents\Driftwave FX\Colours.xml`.
- Sound presets do not change the colour.

**Cost and safety.** The audio thread only copies samples into two lock-free pipes (`plugin/SignalFeed.h`) and stores a few numbers; if the interface falls behind, a display skips and the audio never waits. The copying, the FFTs and all drawing stop while the plugin window is closed.

## How it is put together

```
driftwave-vst/
├── CMakeLists.txt
├── build.cmd              one-step configure + build + test
├── dsp/                   driftwave_dsp: plain C++20, no JUCE, no allocation while processing
│   ├── Smoothing.h            parameter smoother
│   ├── Biquad.h               filter coefficients + state
│   ├── TapeSpeed.*            live slow-down (buffered varispeed)
│   ├── BassBoost.h            low shelf + low-band saturation
│   ├── ConvReverb.*           convolution reverb (decaying stereo noise)
│   ├── Fft.h                  the FFT it uses
│   ├── OutputStage.h          gain + soft ceiling
│   └── EffectChain.*          the signal path; takes a plain ChainParams struct per block
├── plugin/                the JUCE layer
│   ├── Parameters.*           parameter IDs, ranges, lock-free reader for the audio thread
│   ├── Presets.*              factory presets, user preset files
│   ├── PluginProcessor.*      VST3 processor: state, layout, calls EffectChain
│   ├── PluginEditor.*         interface
│   ├── Displays.*             scope, reverb tail and spectrum displays
│   ├── SignalFeed.h           lock-free audio pipe from the audio thread to the displays
│   └── LookAndFeel.*          colours and control drawing
└── tests/
    ├── DspTests.cpp           unit tests for everything in dsp/
    ├── PluginSmokeTest.cpp    loads the built .vst3 in a minimal host
    ├── PresetTests.cpp        factory and user presets
    └── UiSnapshot.cpp         renders the interface to a PNG
```

Signal path, in the same order as the Driftwave app:

```
input → tape speed → bass shelf (+ drive) → reverb (dry + wet) → output gain → soft ceiling → output
```

- **Audio thread:** reads parameters from atomics, never allocates, locks or does I/O. All buffers are sized in `prepareToPlay`.
- **Parameter changes:** every value is smoothed (gains per sample, filter settings every 16 samples), so automation and the section switches do not click.
- **State:** saved as XML through JUCE's parameter tree; anything unrecognised is ignored on load. Parameter IDs are fixed, so projects saved now will load in later versions.
- **Reverb:** a frequency-domain convolution in two stages (128-sample blocks for the first third of a second, 8192-sample blocks for the rest, with the large stage's work spread out so it never causes a spike). The decay control is applied as an exponential weighting inside the convolution, so the stored impulse response never has to be rebuilt.

## Tests

`ctest --test-dir build --output-on-failure` runs three suites.

`driftwave_plugin_smoke` is a minimal VST3 host: it loads the built `.vst3` the way a DAW does and checks discovery, the stereo layout, zero reported latency, processing at two sample rates with a stop/start in between, that slowed audio keeps playing after the input stops, state save and restore, rejection of a corrupt state, and that the editor opens.

`build\driftwave_ui_snapshot.exe out.png` renders the interface to an image without a DAW, which is handy while changing the UI.

`driftwave_preset_tests` loads every factory preset, saves and reloads a user preset, checks the preset name comes back with the plugin state, and feeds it corrupt and out-of-range preset files.

`driftwave_dsp_tests` covers the DSP: resampling accuracy and image rejection, no flutter and no aliasing in Sync, a 1 kHz tone at 80% speed comes out at 800 Hz and falls behind at the right rate, speed off is bit-exact, switching speed on and off makes no click, slowed audio plays out after the input stops and the plugin then returns to live, silence in gives silence out, the bypassed chain is transparent with no delay, the shelf gain is correct, drive adds harmonics, the ceiling is never exceeded, the reverb matches the reference (10 ms gap, 3 s decay, level, flat spectrum, unrelated left and right), it is an exact time-invariant convolution whatever the buffer size, the measured decay time follows the Decay control and the level stays constant, parameter jumps are smoothed, and random automation at random block sizes never produces NaN or exceeds the ceiling.

## Roadmap

- **Done:** live tape-style slow-down (buffered varispeed), and a synced mode (the default) that changes the pitch up or down without drifting
- **Phase 2, remaining:** tape stop/start gestures, and a file-player mode for a true tempo change in both directions without touching the project tempo
- **Phase 3:** independent pitch shifting and time stretching
- **Done:** presets (built in and your own)
- **Phase 4:** waveform display, chorus, widening, wobble, filters, accent colours
- **Phase 5:** optimisation, FL Studio test pass, Windows installer

## Licence

JUCE is used under its AGPLv3 option, which is compatible with Driftwave's GPLv3. If you ever want to sell a closed-source build, you need a JUCE commercial licence instead.
