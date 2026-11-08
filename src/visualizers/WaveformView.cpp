#include "visualizers/WaveformView.h"
#include "ui/Theme.h"

#include <cmath>

namespace sonarc {

namespace {

const juce::Colour leftColour = theme::accent;
const juce::Colour rightColour = theme::warn;

void channelRange(const WaveBucket& b, int channel, float& lo, float& hi) noexcept
{
    switch (channel)
    {
        case 0:  lo = b.minLeft;  hi = b.maxLeft;  break;
        case 1:  lo = b.minRight; hi = b.maxRight; break;
        default: lo = b.minMid;   hi = b.maxMid;   break;
    }
}

juce::String formatSeconds(double seconds)
{
    if (seconds < 1.0)
        return juce::String(juce::roundToInt(seconds * 1000.0)) + " ms";
    return juce::String(seconds, seconds < 10.0 ? 1 : 0) + " s";
}

} // namespace

WaveformView::WaveformView()
{
    setOpaque(true);
}

void WaveformView::setSampleRate(std::uint32_t rate)
{
    if (rate == sampleRate)
        return;

    sampleRate = rate;
    const auto capacity = static_cast<std::size_t>(std::ceil(maxWindowSeconds * rate / WaveformBuilder::kFramesPerBucket)) + 64;
    history.assign(rate > 0 ? capacity : 0, {});
    total = 0;
    background = {};
    repaint();
}

void WaveformView::append(const WaveBucket* buckets, std::size_t count)
{
    if (history.empty() || count == 0)
        return;

    const auto size = history.size();
    for (std::size_t i = 0; i < count; ++i)
        history[(total + i) % size] = buckets[i];
    total += count;
    repaint();
}

void WaveformView::clear()
{
    total = 0;
    repaint();
}

void WaveformView::setWindowSeconds(float seconds)
{
    window = juce::jlimit(minWindowSeconds, maxWindowSeconds, seconds);
    background = {};
    repaint();
}

void WaveformView::setStereoMode(StereoMode newMode)
{
    mode = newMode;
    background = {};
    repaint();
}

void WaveformView::setThickness(float pixels)
{
    minSpanHeight = juce::jlimit(1.0f, 6.0f, pixels);
    repaint();
}

void WaveformView::resized()
{
    background = {};
}

void WaveformView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY == 0.0f)
        return;

    // Wheel up = zoom in (shorter window).
    setWindowSeconds(window * std::pow(1.25f, -wheel.deltaY * 4.0f));
    if (onWindowChanged)
        onWindowChanged(window);
}

juce::Rectangle<float> WaveformView::plotArea() const noexcept
{
    return getLocalBounds().toFloat().reduced(1.0f, 4.0f).withTrimmedBottom(14.0f);
}

std::vector<WaveformView::Lane> WaveformView::lanes()
{
    const auto area = plotArea();
    switch (mode)
    {
        case StereoMode::Stacked:
        {
            auto top = area;
            const auto bottom = top.removeFromBottom(area.getHeight() * 0.5f);
            return { { top.withTrimmedBottom(2.0f), 0, &spansA }, { bottom.withTrimmedTop(2.0f), 1, &spansB } };
        }
        case StereoMode::Overlaid:
            return { { area, 0, &spansA }, { area, 1, &spansB } };
        case StereoMode::Mid:
            break;
    }
    return { { area, 2, &spansA } };
}

bool WaveformView::aggregate(std::uint64_t start, std::uint64_t end, WaveBucket& result) const noexcept
{
    const std::uint64_t size = history.size();
    const std::uint64_t oldest = total > size ? total - size : 0;
    start = std::max(start, oldest);
    end = std::min(end, total);
    if (start >= end)
        return false;

    result = history[start % size];
    for (auto i = start + 1; i < end; ++i)
    {
        const auto& b = history[i % size];
        result.minLeft = std::min(result.minLeft, b.minLeft);
        result.maxLeft = std::max(result.maxLeft, b.maxLeft);
        result.minRight = std::min(result.minRight, b.minRight);
        result.maxRight = std::max(result.maxRight, b.maxRight);
        result.minMid = std::min(result.minMid, b.minMid);
        result.maxMid = std::max(result.maxMid, b.maxMid);
    }
    return true;
}

float WaveformView::yFor(const Lane& lane, float value) const noexcept
{
    return lane.area.getCentreY() - juce::jlimit(-1.0f, 1.0f, value) * lane.area.getHeight() * 0.5f;
}

void WaveformView::addSpan(Lane& lane, float x, float width, const WaveBucket& bucket)
{
    float lo, hi;
    channelRange(bucket, lane.channel, lo, hi);

    float top = yFor(lane, hi);
    float bottom = yFor(lane, lo);

    // Join to the previous column so the trace is continuous even where the
    // signal moves faster than one column's range.
    if (lane.hasPrevious)
    {
        const float rawTop = top, rawBottom = bottom;
        top = juce::jmin(top, lane.previousBottom);
        bottom = juce::jmax(bottom, lane.previousTop);
        lane.previousTop = rawTop;
        lane.previousBottom = rawBottom;
    }
    else
    {
        lane.previousTop = top;
        lane.previousBottom = bottom;
        lane.hasPrevious = true;
    }

    if (bottom - top < minSpanHeight)
    {
        const float mid = 0.5f * (top + bottom);
        top = mid - 0.5f * minSpanHeight;
        bottom = mid + 0.5f * minSpanHeight;
    }

    lane.spans->addWithoutMerging({ x, top, width, bottom - top });

    // Mark samples at or beyond full scale at the lane edge.
    if (hi >= 1.0f)
        clips.addWithoutMerging({ x, lane.area.getY(), width, 2.0f });
    if (lo <= -1.0f)
        clips.addWithoutMerging({ x, lane.area.getBottom() - 2.0f, width, 2.0f });
}

// Zoomed in (a bucket is wider than a pixel): a smooth filled band between
// each bucket's minimum and maximum.
void WaveformView::drawEnvelope(juce::Graphics& g, const Lane& lane, float pixelsPerBucket, juce::Colour colour)
{
    const auto area = plotArea();
    const std::uint64_t size = history.size();
    const std::uint64_t available = total > size ? size : total;
    const auto count = std::min<std::uint64_t>(available, (std::uint64_t) std::ceil(area.getWidth() / pixelsPerBucket) + 2);
    if (count == 0)
        return;

    const float half = 0.5f * minSpanHeight;
    auto bucketAt = [&](std::uint64_t i) -> const WaveBucket& { return history[(total - 1 - i) % size]; };
    auto xAt = [&](std::uint64_t i) { return area.getRight() - ((float) i + 0.5f) * pixelsPerBucket; };

    envelope.clear();
    float lo, hi;

    // Upper edge, newest (right) to oldest (left).
    for (std::uint64_t i = 0; i < count; ++i)
    {
        channelRange(bucketAt(i), lane.channel, lo, hi);
        const float x = xAt(i);
        const float y = yFor(lane, hi) - half;
        if (i == 0)
            envelope.startNewSubPath(area.getRight(), y);
        envelope.lineTo(x, y);

        if (hi >= 1.0f)
            clips.addWithoutMerging({ x - 0.5f * pixelsPerBucket, lane.area.getY(), pixelsPerBucket, 2.0f });
        if (lo <= -1.0f)
            clips.addWithoutMerging({ x - 0.5f * pixelsPerBucket, lane.area.getBottom() - 2.0f, pixelsPerBucket, 2.0f });
    }

    // Lower edge back again.
    for (std::uint64_t i = count; i-- > 0;)
    {
        channelRange(bucketAt(i), lane.channel, lo, hi);
        envelope.lineTo(xAt(i), yFor(lane, lo) + half);
    }
    channelRange(bucketAt(0), lane.channel, lo, hi);
    envelope.lineTo(area.getRight(), yFor(lane, lo) + half);
    envelope.closeSubPath();

    g.saveState();
    g.reduceClipRegion(area.toNearestInt());
    g.setColour(colour);
    g.fillPath(envelope);
    g.restoreState();
}

void WaveformView::renderBackground(float pixelScale)
{
    background = juce::Image(juce::Image::RGB,
                             juce::jmax(1, juce::roundToInt((float) getWidth() * pixelScale)),
                             juce::jmax(1, juce::roundToInt((float) getHeight() * pixelScale)),
                             false);
    backgroundScale = pixelScale;

    juce::Graphics g(background);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    g.fillAll(theme::panel);

    const auto area = plotArea();

    // Time divisions: 10 across the window, labelled as "time ago".
    g.setFont(theme::font(10.5f));
    for (int i = 0; i <= 10; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 10.0f;
        g.setColour(i == 10 ? theme::gridStrong : theme::grid);
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

        if (i % 2 == 0 && i < 10)
        {
            g.setColour(theme::textDim);
            const auto ago = "-" + formatSeconds(window * (10 - i) / 10.0);
            g.drawText(ago, juce::Rectangle<float>(x + 3.0f, area.getBottom() + 1.0f, 70.0f, 13.0f),
                       juce::Justification::centredLeft, false);
        }
    }
    g.setColour(theme::textDim);
    g.drawText("now", juce::Rectangle<float>(area.getRight() - 40.0f, area.getBottom() + 1.0f, 38.0f, 13.0f),
               juce::Justification::centredRight, false);

    for (const auto& lane : lanes())
    {
        const auto& a = lane.area;
        g.setColour(theme::gridStrong);
        g.drawHorizontalLine(juce::roundToInt(a.getCentreY()), a.getX(), a.getRight());
        g.setColour(theme::grid);
        g.drawHorizontalLine(juce::roundToInt(a.getY() + a.getHeight() * 0.25f), a.getX(), a.getRight()); // +0.5 (-6 dB)
        g.drawHorizontalLine(juce::roundToInt(a.getY() + a.getHeight() * 0.75f), a.getX(), a.getRight()); // -0.5
    }

    // Lane labels.
    g.setFont(theme::font(10.5f, true));
    auto label = [&](const juce::String& text, juce::Colour colour, juce::Rectangle<float> a)
    {
        g.setColour(colour.withAlpha(0.8f));
        g.drawText(text, a.reduced(5.0f, 2.0f), juce::Justification::topLeft, false);
    };
    const auto laneList = lanes();
    if (mode == StereoMode::Stacked)
    {
        label("L", leftColour, laneList[0].area);
        label("R", rightColour, laneList[1].area);
    }
    else if (mode == StereoMode::Overlaid)
    {
        label("L", leftColour, laneList[0].area);
        label("R", rightColour, laneList[0].area.withTrimmedLeft(12.0f));
    }
    else
    {
        label("MID", theme::accent, laneList[0].area);
    }

    g.setColour(theme::textDim);
    g.setFont(theme::font(10.5f));
    g.drawText("window " + formatSeconds(window) + "  |  wheel to zoom", area.reduced(6.0f, 2.0f),
               juce::Justification::topRight, false);
}

void WaveformView::paint(juce::Graphics& g)
{
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! background.isValid() || pixelScale != backgroundScale)
        renderBackground(pixelScale);
    g.drawImage(background, getLocalBounds().toFloat());

    const auto area = plotArea();
    if (sampleRate == 0 || total == 0)
    {
        g.setColour(theme::textDim);
        g.setFont(theme::font(13.0f));
        g.drawText("No audio stream", area, juce::Justification::centred, false);
        return;
    }

    spansA.clear();
    spansB.clear();
    clips.clear();
    auto laneList = lanes();

    const bool overlaid = mode == StereoMode::Overlaid;
    auto colourFor = [&](const Lane& lane)
    {
        if (lane.channel == 2)
            return theme::accent;
        return lane.channel == 0 ? leftColour.withAlpha(overlaid ? 0.75f : 1.0f)
                                 : rightColour.withAlpha(overlaid ? 0.6f : 0.9f);
    };

    const int width = juce::jmax(1, juce::roundToInt(area.getWidth()));
    const double bucketsInWindow = window * sampleRate / (double) WaveformBuilder::kFramesPerBucket;
    const double bucketsPerPixel = bucketsInWindow / width;

    if (bucketsPerPixel >= 1.0)
    {
        // Columns aligned to absolute bucket positions, so the picture scrolls
        // without shimmering as new audio arrives.
        const auto perColumn = (std::uint64_t) juce::jmax(1LL, (long long) std::llround(bucketsPerPixel));
        const auto lastColumn = (long long) (total / perColumn);

        for (int c = 0; c < width; ++c)
        {
            const long long column = lastColumn - (width - 1 - c);
            if (column < 0)
                continue;

            WaveBucket bucket;
            const auto start = (std::uint64_t) column * perColumn;
            if (! aggregate(start, start + perColumn, bucket))
                continue;

            for (auto& lane : laneList)
                addSpan(lane, area.getX() + (float) c, 1.0f, bucket);
        }

        for (const auto& lane : laneList)
        {
            g.setColour(colourFor(lane));
            g.fillRectList(*lane.spans);
        }
    }
    else
    {
        for (const auto& lane : laneList)
            drawEnvelope(g, lane, (float) (1.0 / bucketsPerPixel), colourFor(lane));
    }

    g.setColour(theme::hot);
    g.fillRectList(clips);
}

} // namespace sonarc
