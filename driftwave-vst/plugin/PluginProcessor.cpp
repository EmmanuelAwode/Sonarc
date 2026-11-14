#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace {
constexpr const char* kStateType = "DriftwaveFX";
constexpr int kStateVersion = 1;
}

DriftwaveProcessor::DriftwaveProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, kStateType, driftwave::createParameterLayout()),
      reader(apvts)
{
}

void DriftwaveProcessor::prepareToPlay(double sampleRate, int)
{
    // The only place that allocates: delay lines are sized for this sample rate.
    chain.prepare(sampleRate);
    chain.setParams(reader.read());
    setLatencySamples(0); // no look-ahead; the lag of a slowed signal is the effect, not latency
}

void DriftwaveProcessor::reset()
{
    // Called by the host when processing is stopped: drop the reverb tail and any slowed audio.
    chain.reset();
    speedLag.store(0.0f, std::memory_order_relaxed);
}

bool DriftwaveProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void DriftwaveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    // The slowed signal follows the song position: when playback starts, or the
    // playhead jumps (a click in the playlist, a loop wrapping round), it starts
    // again from the live input rather than finishing what was left over.
    bool playing = wasPlaying;
    bool jumped = false;
    if (const auto* head = getPlayHead()) {
        if (const auto position = head->getPosition()) {
            playing = position->getIsPlaying();
            if (const auto time = position->getTimeInSamples()) {
                const auto tolerance = juce::int64(getSampleRate() * 0.1);
                jumped = playing && wasPlaying && expectedTime >= 0 && std::abs(*time - expectedTime) > tolerance;
                expectedTime = playing ? *time + numSamples : -1;
            }
        }
    }
    if ((playing && !wasPlaying) || jumped || resyncRequested.exchange(false, std::memory_order_relaxed))
        chain.resyncSpeed();
    wasPlaying = playing;

    // The displays get a copy of the audio before and after, through lock-free pipes,
    // and only while an editor is open.
    const bool feeding = displaysActive.load(std::memory_order_relaxed);
    if (feeding)
        inputFeed.push(buffer.getReadPointer(0), buffer.getReadPointer(1), numSamples);

    chain.setParams(reader.read());
    chain.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    speedLag.store(float(chain.speedLagSeconds()), std::memory_order_relaxed);

    if (feeding)
        outputFeed.push(buffer.getReadPointer(0), buffer.getReadPointer(1), numSamples);
    if (chain.wetPeak() > wetPeak.load(std::memory_order_relaxed))
        wetPeak.store(chain.wetPeak(), std::memory_order_relaxed);
    if (chain.clipReductionDb() < clipReduction.load(std::memory_order_relaxed))
        clipReduction.store(chain.clipReductionDb(), std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch) {
        const float blockPeak = buffer.getMagnitude(ch, 0, numSamples);
        if (blockPeak > peak[ch].load(std::memory_order_relaxed))
            peak[ch].store(blockPeak, std::memory_order_relaxed);
    }
}

double DriftwaveProcessor::getTailLengthSeconds() const
{
    const auto p = reader.read();
    const double reverbTail = p.reverbOn ? double(p.reverb.decaySeconds) * 1.7 + 0.1 : 0.0;
    // Slowed audio keeps playing after the input stops, by up to the buffer length.
    return reverbTail + (p.speedOn && !p.speedSync && p.speed <= 1.0f ? driftwave::TapeSpeed::kMaxLagSeconds : 0.0);
}

juce::AudioProcessorEditor* DriftwaveProcessor::createEditor()
{
    return new DriftwaveEditor(*this);
}

void DriftwaveProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", kStateVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DriftwaveProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Anything that isn't our own XML is ignored and the current settings are kept.
    // Parameters missing from an older state keep their defaults; unknown ones are dropped.
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateType))
        return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (state.isValid())
        apvts.replaceState(state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DriftwaveProcessor();
}
