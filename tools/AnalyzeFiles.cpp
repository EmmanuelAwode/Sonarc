// sonarc_analyze: runs Sonarc's real-time BPM and key detectors over audio
// files, exactly as the app would hear them, and reports a song-level answer.
//
//   sonarc_analyze [--fold 85] [--json] <file or folder>...
//
// The detectors only ever see the last few seconds (as in the app), so the
// song-level answer is the reading that was shown for most of the track.

#include "dsp/KeyDetector.h"
#include "dsp/TempoAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <map>

using namespace sonarc;

namespace {

struct FileResult
{
    juce::String name;
    double seconds = 0.0;

    float finalBpm = 0.0f;
    bool finalBpmValid = false;
    float modalBpm = 0.0f;       // most-shown tempo (0.5 BPM buckets)
    double modalBpmShare = 0.0;  // fraction of the analysed time it was shown
    double firstBpmSeconds = -1.0;
    float bpmConfidence = 0.0f;

    int finalKey = -1;
    int modalKey = -1;
    double modalKeyShare = 0.0;
    double firstKeySeconds = -1.0;
    float keyConfidence = 0.0f;
};

FileResult analyse(juce::AudioFormatReader& reader, const juce::String& name, float foldMinimum)
{
    FileResult r;
    r.name = name;
    const auto rate = (std::uint32_t) reader.sampleRate;
    r.seconds = (double) reader.lengthInSamples / reader.sampleRate;

    TempoAnalyzer tempo;
    tempo.prepare(rate);
    tempo.setFoldRange(foldMinimum);
    KeyDetector key;
    key.prepare(rate);

    constexpr int block = 4096;
    juce::AudioBuffer<float> buffer((int) juce::jmax(2u, reader.numChannels), block);
    std::vector<StereoFrame> frames(block);

    std::map<int, double> bpmSeconds; // tempo * 2 (0.5 BPM buckets) -> seconds shown
    std::map<int, double> keySeconds;
    double bpmTotal = 0.0, keyTotal = 0.0;
    float confidenceSum = 0.0f, keyConfidenceSum = 0.0f;

    for (juce::int64 position = 0; position < reader.lengthInSamples; position += block)
    {
        const int n = (int) juce::jmin<juce::int64>(block, reader.lengthInSamples - position);
        reader.read(&buffer, 0, n, position, true, true);
        const float* left = buffer.getReadPointer(0);
        const float* right = buffer.getReadPointer(reader.numChannels > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
            frames[(size_t) i] = { left[i], right[i] };

        tempo.process(frames.data(), (std::size_t) n);
        key.process(frames.data(), (std::size_t) n);

        const double now = (double) (position + n) / reader.sampleRate;
        const double dt = (double) n / reader.sampleRate;
        const auto& t = tempo.latest();
        if (t.valid)
        {
            if (r.firstBpmSeconds < 0.0)
                r.firstBpmSeconds = now;
            bpmSeconds[juce::roundToInt(t.bpm * 2.0f)] += dt;
            bpmTotal += dt;
            confidenceSum += t.confidence * (float) dt;
        }
        const auto& k = key.latest();
        if (k.valid)
        {
            if (r.firstKeySeconds < 0.0)
                r.firstKeySeconds = now;
            keySeconds[k.key] += dt;
            keyTotal += dt;
            keyConfidenceSum += k.confidence * (float) dt;
        }
    }

    r.finalBpmValid = tempo.latest().valid;
    r.finalBpm = tempo.latest().bpm;
    r.finalKey = key.latest().valid ? key.latest().key : -1;

    // Neighbouring 0.5 BPM buckets are the same tempo; pick the best 1.5 BPM-wide window.
    double best = 0.0;
    for (const auto& [bucket, seconds] : bpmSeconds)
    {
        double window = 0.0, weighted = 0.0;
        for (int d = -1; d <= 1; ++d)
            if (const auto it = bpmSeconds.find(bucket + d); it != bpmSeconds.end())
            {
                window += it->second;
                weighted += it->second * (bucket + d) * 0.5;
            }
        if (window > best)
        {
            best = window;
            r.modalBpm = (float) (weighted / window);
        }
    }
    r.modalBpmShare = bpmTotal > 0.0 ? best / bpmTotal : 0.0;
    r.bpmConfidence = bpmTotal > 0.0 ? confidenceSum / (float) bpmTotal : 0.0f;

    double bestKey = 0.0;
    for (const auto& [k, seconds] : keySeconds)
        if (seconds > bestKey)
        {
            bestKey = seconds;
            r.modalKey = k;
        }
    r.modalKeyShare = keyTotal > 0.0 ? bestKey / keyTotal : 0.0;
    r.keyConfidence = keyTotal > 0.0 ? keyConfidenceSum / (float) keyTotal : 0.0f;
    return r;
}

juce::String keyText(int key) { return key < 0 ? juce::String("--") : juce::String(keys::name(key)); }

} // namespace

int main(int argc, char* argv[])
{
    float foldMinimum = 85.0f;
    bool json = false;
    juce::Array<juce::File> files;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg = juce::CharPointer_UTF8(argv[i]);
        if (arg == "--fold" && i + 1 < argc)
            foldMinimum = juce::String(argv[++i]).getFloatValue();
        else if (arg == "--json")
            json = true;
        else
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(arg);
            if (file.isDirectory())
                files.addArray(file.findChildFiles(juce::File::findFiles, true, "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff"));
            else
                files.add(file);
        }
    }

    if (files.isEmpty())
    {
        std::printf("usage: sonarc_analyze [--fold 85] [--json] <file or folder>...\n");
        return 1;
    }
    files.sort();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::Array<juce::var> results;
    if (! json)
        std::printf("%-34s %7s | %8s %6s %5s %6s | %-10s %-10s %6s %5s %6s\n", "file", "length",
                    "BPM", "share", "conf", "first", "key", "final key", "share", "conf", "first");

    for (const auto& file : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            std::printf("%-34s  (can't read)\n", file.getFileName().toRawUTF8());
            continue;
        }

        const auto r = analyse(*reader, file.getFileName(), foldMinimum);

        if (json)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("file", r.name);
            o->setProperty("seconds", r.seconds);
            o->setProperty("bpm", r.modalBpm);
            o->setProperty("bpmShare", r.modalBpmShare);
            o->setProperty("bpmConfidence", r.bpmConfidence);
            o->setProperty("finalBpm", r.finalBpmValid ? juce::var(r.finalBpm) : juce::var());
            o->setProperty("firstBpmSeconds", r.firstBpmSeconds);
            o->setProperty("key", r.modalKey >= 0 ? juce::var(keys::name(r.modalKey)) : juce::var());
            o->setProperty("camelot", r.modalKey >= 0 ? juce::var(keys::camelot(r.modalKey)) : juce::var());
            o->setProperty("keyShare", r.modalKeyShare);
            o->setProperty("keyConfidence", r.keyConfidence);
            o->setProperty("finalKey", r.finalKey >= 0 ? juce::var(keys::name(r.finalKey)) : juce::var());
            o->setProperty("firstKeySeconds", r.firstKeySeconds);
            results.add(juce::var(o));
        }
        else
        {
            std::printf("%-34s %6.1fs | %8s %5.0f%% %5.2f %5.1fs | %-10s %-10s %5.0f%% %5.2f %5.1fs\n",
                        r.name.substring(0, 34).toRawUTF8(), r.seconds,
                        r.modalBpm > 0.0f ? juce::String(r.modalBpm, 1).toRawUTF8() : "--", r.modalBpmShare * 100.0,
                        r.bpmConfidence, r.firstBpmSeconds,
                        keyText(r.modalKey).toRawUTF8(), keyText(r.finalKey).toRawUTF8(), r.modalKeyShare * 100.0,
                        r.keyConfidence, r.firstKeySeconds);
        }
    }

    if (json)
        std::printf("%s\n", juce::JSON::toString(juce::var(results)).toRawUTF8());
    return 0;
}
