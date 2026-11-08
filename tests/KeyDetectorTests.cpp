#include "TestHarness.h"
#include "dsp/KeyDetector.h"

#include <numbers>
#include <random>
#include <string>

using namespace sonarc;

namespace {

constexpr std::uint32_t kRate = 48000;

struct Chord
{
    int root;   // pitch class, C = 0
    bool minor;
};

double hzFor(int midi)
{
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

// Chords as harmonic tones (four partials) plus a bass note, two seconds each.
std::vector<StereoFrame> progression(const std::vector<Chord>& chords, int repeats = 2)
{
    const std::size_t chordFrames = 2 * kRate;
    std::vector<StereoFrame> out(chordFrames * chords.size() * (std::size_t) repeats);

    for (std::size_t c = 0; c < chords.size() * (std::size_t) repeats; ++c)
    {
        const auto& chord = chords[c % chords.size()];
        const int rootMidi = 60 + chord.root;
        const int notes[] = { rootMidi, rootMidi + (chord.minor ? 3 : 4), rootMidi + 7, 48 + chord.root };

        for (std::size_t i = 0; i < chordFrames; ++i)
        {
            const double t = (double) i / kRate;
            double s = 0.0;
            for (int n = 0; n < 4; ++n)
                for (int h = 1; h <= 4; ++h)
                    s += std::sin(2.0 * std::numbers::pi * hzFor(notes[n]) * h * t) / (h * 1.5);
            const auto v = (float) (0.05 * s);
            out[c * chordFrames + i] = { v, v };
        }
    }
    return out;
}

KeyEstimate analyse(const std::vector<StereoFrame>& audio)
{
    KeyDetector detector;
    detector.prepare(kRate);
    for (std::size_t i = 0; i < audio.size(); i += 480)
        detector.process(audio.data() + i, std::min<std::size_t>(480, audio.size() - i));
    return detector.latest();
}

std::string nameOf(const KeyEstimate& e) { return e.valid ? keys::name(e.key) : "--"; }

} // namespace

TEST_CASE("Key: names, Camelot codes and relative keys")
{
    CHECK(std::string(keys::name(0)) == "C major");
    CHECK(std::string(keys::camelot(0)) == "8B");
    CHECK(std::string(keys::name(21)) == "A minor");
    CHECK(std::string(keys::camelot(21)) == "8A");
    CHECK(std::string(keys::name(18)) == "F# minor");
    CHECK(std::string(keys::camelot(18)) == "11A");
    CHECK(std::string(keys::camelot(3)) == "5B");   // Eb major
    CHECK(std::string(keys::camelot(23)) == "10A"); // B minor
    CHECK(keys::relative(0) == 21);
    CHECK(keys::relative(21) == 0);
    CHECK(keys::relative(18) == 9); // F#m -> A
    CHECK(std::string(keys::name(-1)) == "--");
}

TEST_CASE("Key: chord progressions in known keys")
{
    struct Case { const char* expected; std::vector<Chord> chords; };
    const Case cases[] = {
        { "C major",  { { 0, false }, { 5, false }, { 7, false }, { 0, false } } },  // C F G C
        { "A minor",  { { 9, true }, { 2, true }, { 4, false }, { 9, true } } },     // Am Dm E Am
        { "F# minor", { { 6, true }, { 11, true }, { 1, false }, { 6, true } } },    // F#m Bm C# F#m
        { "Eb major", { { 3, false }, { 8, false }, { 10, false }, { 3, false } } }, // Eb Ab Bb Eb
        { "G major",  { { 7, false }, { 0, false }, { 2, false }, { 7, false } } },  // G C D G
    };

    for (const auto& c : cases)
    {
        const auto result = analyse(progression(c.chords));
        const bool correct = nameOf(result) == c.expected;
        if (! correct)
            std::printf("    expected %s, got %s\n", c.expected, nameOf(result).c_str());
        CHECK(correct);
        CHECK(result.confidence > 0.2f);
    }
}

TEST_CASE("Key: silence, drums only and noise give no key")
{
    CHECK(! analyse(std::vector<StereoFrame>(kRate * 12)).valid);

    std::mt19937 rng(3);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);

    std::vector<StereoFrame> drums(kRate * 14);
    for (std::size_t i = 0; i < drums.size(); ++i)
    {
        const double t = (double) i / kRate;
        const double inBeat = std::fmod(t, 0.5);
        const double kickHz = 50.0 + 90.0 * std::exp(-inBeat * 30.0);
        float s = 0.6f * (float) (std::sin(2.0 * std::numbers::pi * kickHz * inBeat) * std::exp(-inBeat * 12.0));
        s += 0.15f * noise(rng) * (float) std::exp(-std::fmod(t, 0.25) * 60.0);
        drums[i] = { s, s };
    }
    CHECK(! analyse(drums).valid);

    std::vector<StereoFrame> white(kRate * 12);
    for (auto& f : white)
        f = { 0.2f * noise(rng), 0.2f * noise(rng) };
    CHECK(! analyse(white).valid);
}

TEST_CASE("Key: a short burst is not enough to call a key")
{
    // 1.5 s of tonal audio is less than the evidence required.
    auto audio = progression({ { 0, false } }, 1);
    audio.resize(kRate * 3 / 2);
    CHECK(! analyse(audio).valid);
}
