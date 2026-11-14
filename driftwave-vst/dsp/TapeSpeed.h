#pragma once

#include "dsp/Smoothing.h"

#include <cstdint>
#include <vector>

namespace driftwave {

/** Live tape-style speed change for an insert effect.

    An insert effect is handed audio at the host's pace and cannot read ahead.
    The input is recorded into a ring buffer and read back at another speed, so
    pitch follows speed like a tape. There are two ways to deal with time:

    Tape mode (speed <= 1 only): the tempo drops with the pitch and the output
    falls further and further behind the input ("lag"). The lag goes back to zero
      - when the section is switched off, or resync() is called (short crossfade);
      - when everything still waiting in the buffer is silence (inaudible);
      - when the buffer is full (kMaxLagSeconds behind; short crossfade).

    Sync mode (any speed): the tempo is left alone so the output stays in time
    with the host. The sound is read at the new speed by one read head; before
    that head drifts too far, a new one takes over further along with a short
    crossfade (a splice). Slower than live, each splice skips a little of the
    source; faster than live it plays a little twice. Two things decide where:
      - the new head starts where its waveform best lines up with the one it
        replaces (cross-correlation), so sustained sound carries on unbroken;
      - drum hits are detected on the way in, and a splice is placed (or put
        off for a moment) so that the skipped or repeated stretch has no hit
        in it. A hit is neither lost nor played twice.
    A splice that would satisfy only one of the two is put off, for as long as
    the head has room to keep drifting, until a place turns up that suits both.
    Between splices the output is a single, plain resampled copy of the input.

    Reading uses a 64-tap Kaiser-windowed sinc, band-limited further when
    playing faster than 1 so that nothing aliases.

    With the section off and no lag the audio passes through bit-exact with no
    delay. Real-time safe: prepare() allocates, process() does not. */
class TapeSpeed {
public:
    static constexpr double kMaxLagSeconds = 180.0;
    static constexpr float kMinSpeed = 0.5f;
    static constexpr float kMaxSpeed = 1.5f; // above 1 only in sync mode
    static constexpr double kSyncBaseSeconds = 0.005;   // the closest a sync read head gets to the live input
    static constexpr double kSyncSpanSeconds = 0.028;   // how far a head drifts before a splice is wanted (lines up notes down to 41 Hz)
    static constexpr double kSyncLimitSeconds = 0.042;  // how far it may drift while a splice waits for a drum hit to pass
    static constexpr double kSyncRefSeconds = 0.024;    // length of sound compared when lining up a splice
    static constexpr double kSyncSpliceSeconds = 0.008; // crossfade from one read head to the next

    void prepare(double sampleRate);
    void reset() noexcept;

    /** @param on    false = live (any lag is crossfaded away)
        @param speed playback speed, kMinSpeed..kMaxSpeed. Faster than 1 is only possible
                     in sync mode (nothing can be played before it arrives), so a
                     speed above 1 always uses it.
        @param sync  true = keep the pitch change but stay in time with the input */
    void setTarget(bool on, float speed, bool sync = false) noexcept;

    /** Crossfade back to the live input, keeping the current speed. */
    void resync() noexcept { resyncPending = true; }

    void process(float* left, float* right, int numSamples) noexcept;

    /** How far the output is behind the input right now. */
    double lagSeconds() const noexcept { return lag / sampleRate; }

private:
    static constexpr int kTaps = 64, kHalfTaps = kTaps / 2, kPhases = 256;
    static constexpr int kTables = 3;      // full band, and two narrower ones for speeds above 1
    static constexpr int kSearchStep = 4;  // the alignment search runs on a 4x decimated copy

    void read(double lagSamples, float& outL, float& outR) const noexcept;
    float at(int channel, int64_t samplesBack) const noexcept;
    bool trySplice(double slip, bool forced) noexcept;
    double alignSplice(double nearest, double furthest, double playingDelay, bool preferNearest, float& match) noexcept;

    double sampleRate = 48000.0;
    std::vector<float> buffer[2];
    int64_t size = 0;
    int64_t writeIndex = 0;
    int64_t written = 0;       // samples recorded before the current one since the last reset

    std::vector<float> sincTables[kTables]; // (kPhases + 1) rows of kTaps
    const float* sinc = nullptr;            // the table in use for the current speed

    double lag = 0.0;          // samples between the write and read heads
    double fadeLag = 0.0;      // the old read head while crossfading to live
    double maxLag = 0.0;
    int fadeLength = 1, fadeLeft = 0;
    int64_t silentRun = 0;     // consecutive silent input samples up to now

    Smoother speedSm, syncSm;
    // Sync mode. Delays are in samples behind the write head.
    static constexpr int kOnsets = 8;
    double syncBase = 1.0, syncSpan = 1.0, syncLimit = 1.0;
    double minJump = 1.0, minRange = 1.0, onsetPre = 1.0, onsetBody = 1.0;
    double headDelay = 0.0;    // the read head being heard
    double oldDelay = 0.0;     // the one it replaced, while the splice lasts
    int spliceLength = 1, spliceLeft = 0;
    int spliceWait = 0, spliceRetry = 1; // samples until the next look for a place to splice
    bool syncRunning = false;
    int64_t clock = 0;         // samples processed
    int64_t onsets[kOnsets] {}; // when the last few drum hits came in
    int nextOnset = 0;
    float envFast = 0.0f, envSlow = 0.0f, fastRelease = 0.0f, slowRate = 0.0f;
    int onsetHold = 0, onsetHoldLength = 1;
    std::vector<float> searchRef, searchArea;
    bool sync = false;
    float targetSpeed = 1.0f;
    bool on = false;
    bool resyncPending = false;
    bool needsSnap = true;
};

} // namespace driftwave
