#include "visualizers/WaveformView.h"
#include "ui/Theme.h"

#include <cmath>

namespace sonarc {

namespace {

const juce::Colour leftColour = theme::accent;
const juce::Colour rightColour = theme::warn;

// Spectral colouring: what dominates the sound decides the hue.
struct Rgb { float r, g, b; };
constexpr Rgb kLowColour { 1.00f, 0.24f, 0.42f };  // bass: hot pink-red
constexpr Rgb kMidColour { 1.00f, 0.72f, 0.18f };  // mids: amber
constexpr Rgb kHighColour { 0.35f, 0.90f, 1.00f }; // highs: cyan

void channelRange(const WaveBucket& b, int channel, float& lo, float& hi) noexcept
{
    switch (channel)
    {
        case 0:  lo = b.minLeft;  hi = b.maxLeft;  break;
        case 1:  lo = b.minRight; hi = b.maxRight; break;
        default: lo = b.minMid;   hi = b.maxMid;   break;
    }
}

float channelMeanSquare(const WaveBucket& b, int channel) noexcept
{
    return channel == 0 ? b.msLeft : channel == 1 ? b.msRight : b.msMid;
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
    invalidateSpectral();
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
    invalidateSpectral();
    repaint();
}

void WaveformView::setWindowSeconds(float seconds)
{
    window = juce::jlimit(minWindowSeconds, maxWindowSeconds, seconds);
    background = {};
    invalidateSpectral();
    repaint();
}

void WaveformView::setStereoMode(StereoMode newMode)
{
    mode = newMode;
    background = {};
    invalidateSpectral();
    repaint();
}

void WaveformView::setStyle(Style newStyle)
{
    currentStyle = newStyle;
    background = {};
    invalidateSpectral();
    repaint();
}

void WaveformView::setThickness(float pixels)
{
    minSpanHeight = juce::jlimit(1.0f, 6.0f, pixels);
    invalidateSpectral();
    repaint();
}

void WaveformView::setCleanMode(bool clean)
{
    if (clean == cleanMode)
        return;
    cleanMode = clean;
    background = {};
    invalidateSpectral();
    repaint();
}

void WaveformView::resized()
{
    background = {};
    invalidateSpectral();
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
    const auto bounds = getLocalBounds().toFloat();
    return cleanMode ? bounds.reduced(1.0f, 2.0f) : bounds.reduced(1.0f, 4.0f).withTrimmedBottom(14.0f);
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

// Peaks are combined by min/max, energies by averaging.
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
        result.msLeft += b.msLeft;
        result.msRight += b.msRight;
        result.msMid += b.msMid;
        result.msLow += b.msLow;
        result.msMidBand += b.msMidBand;
        result.msHigh += b.msHigh;
    }

    const float count = (float) (end - start);
    result.msLeft /= count;
    result.msRight /= count;
    result.msMid /= count;
    result.msLow /= count;
    result.msMidBand /= count;
    result.msHigh /= count;
    return true;
}

float WaveformView::yFor(const Lane& lane, float value) const noexcept
{
    return lane.area.getCentreY() - juce::jlimit(-1.0f, 1.0f, value) * lane.area.getHeight() * 0.5f;
}

juce::Colour WaveformView::bandColour(const WaveBucket& b) const noexcept
{
    // Music has far less energy up high, so highs are weighted up to stay visible.
    const float low = std::sqrt(b.msLow);
    const float mid = std::sqrt(b.msMidBand) * 1.8f;
    const float high = std::sqrt(b.msHigh) * 3.5f;
    const float sum = low + mid + high;
    if (sum <= 1.0e-9f)
        return juce::Colour::fromFloatRGBA(kMidColour.r, kMidColour.g, kMidColour.b, 1.0f);

    float r = (low * kLowColour.r + mid * kMidColour.r + high * kHighColour.r) / sum;
    float g = (low * kLowColour.g + mid * kMidColour.g + high * kHighColour.g) / sum;
    float bl = (low * kLowColour.b + mid * kMidColour.b + high * kHighColour.b) / sum;

    // Keep mixes vivid: scale so the strongest component is at full brightness.
    const float peak = juce::jmax(r, g, bl, 1.0e-6f);
    r /= peak;
    g /= peak;
    bl /= peak;
    return juce::Colour::fromFloatRGBA(r, g, bl, 1.0f);
}

//==============================================================================
// Classic style

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
    // A soft halo first, then the trace itself.
    g.setColour(colour.withAlpha(0.18f));
    g.strokePath(envelope, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
    g.setColour(colour);
    g.fillPath(envelope);
    g.restoreState();
}

void WaveformView::paintClassic(juce::Graphics& g, juce::Rectangle<float> area)
{
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

//==============================================================================
// Spectral style

void WaveformView::renderSpectralColumn(int x, std::uint64_t column)
{
    const int height = spectralImage.getHeight();
    juce::Image::BitmapData pixels(spectralImage, x, 0, 1, height, juce::Image::BitmapData::writeOnly);
    auto setPixel = [&](int y, juce::PixelARGB p) { *reinterpret_cast<juce::PixelARGB*>(pixels.getPixelPointer(0, y)) = p; };

    WaveBucket b;
    const auto start = column * spectralPerColumn;
    if (! aggregate(start, start + spectralPerColumn, b))
    {
        for (int y = 0; y < height; ++y)
            setPixel(y, juce::PixelARGB(0, 0, 0, 0));
        return;
    }

    const auto colour = bandColour(b);
    const float cr = colour.getFloatRed(), cg = colour.getFloatGreen(), cb = colour.getFloatBlue();

    // Lanes in image rows: one mirrored lane, or L over R.
    struct SpectralLane { int top, bottom, channel; };
    SpectralLane laneList[2];
    int laneCount = 1;
    if (mode == StereoMode::Stacked)
    {
        const int gap = juce::jmax(2, height / 60);
        laneList[0] = { 0, height / 2 - gap / 2, 0 };
        laneList[1] = { height / 2 + gap / 2, height, 1 };
        laneCount = 2;
    }
    else
    {
        laneList[0] = { 0, height, 2 };
    }

    for (int y = 0; y < height; ++y)
        setPixel(y, juce::PixelARGB(0, 0, 0, 0));

    const float glowPixels = 6.0f * spectralScale;
    const float minPixels = juce::jmax(1.0f, minSpanHeight * spectralScale * 0.5f);

    for (int l = 0; l < laneCount; ++l)
    {
        const auto& lane = laneList[l];
        const float centre = 0.5f * (float) (lane.top + lane.bottom);
        const float halfHeight = 0.5f * (float) (lane.bottom - lane.top);
        if (halfHeight < 1.0f)
            continue;

        float lo, hi;
        channelRange(b, lane.channel, lo, hi);
        const float peak = juce::jmin(1.0f, juce::jmax(std::abs(lo), std::abs(hi)));
        const float rms = juce::jmin(peak, std::sqrt(channelMeanSquare(b, lane.channel)));

        // Square-root height scale rather than linear: a mix fills more of the
        // lane and quiet passages stay visible, while transients still stand
        // out (-6 dBFS reaches 71 % of the height, -20 dBFS 32 %).
        auto toHeight = [](float amplitude) { return std::sqrt(juce::jlimit(0.0f, 1.0f, amplitude)); };
        const float peakPixels = juce::jmax(minPixels, toHeight(peak) * halfHeight);
        const float rmsPixels = toHeight(rms) * halfHeight;

        for (int y = lane.top; y < lane.bottom; ++y)
        {
            const float d = std::abs((float) y + 0.5f - centre); // distance from the centre line, pixels
            float alpha, whiteness;

            if (d <= rmsPixels)
            {
                // Core: full colour, lifting slightly towards the centre line.
                alpha = 1.0f;
                const float t = 1.0f - d / juce::jmax(1.0f, rmsPixels);
                whiteness = 0.35f * t * t;
            }
            else if (d <= peakPixels)
            {
                // Body: colour fading towards the peak edge, so the RMS core reads as the "solid" part.
                alpha = 0.78f - 0.38f * (d - rmsPixels) / juce::jmax(1.0f, peakPixels - rmsPixels);
                whiteness = 0.0f;
            }
            else if (d <= peakPixels + glowPixels)
            {
                // Glow outside the peak edge.
                const float t = 1.0f - (d - peakPixels) / glowPixels;
                alpha = 0.38f * t * t;
                whiteness = 0.0f;
            }
            else
            {
                continue;
            }

            const float r = cr + (1.0f - cr) * whiteness;
            const float g = cg + (1.0f - cg) * whiteness;
            const float bl = cb + (1.0f - cb) * whiteness;
            setPixel(y, juce::PixelARGB((juce::uint8) (alpha * 255.0f), (juce::uint8) (r * alpha * 255.0f),
                                        (juce::uint8) (g * alpha * 255.0f), (juce::uint8) (bl * alpha * 255.0f)));
        }

        // Clip marks at the lane edges.
        if (peak >= 1.0f)
        {
            const auto hot = juce::PixelARGB(255, theme::hot.getRed(), theme::hot.getGreen(), theme::hot.getBlue());
            for (int y = lane.top; y < juce::jmin(lane.bottom, lane.top + 2); ++y)
                setPixel(y, hot);
            for (int y = juce::jmax(lane.top, lane.bottom - 2); y < lane.bottom; ++y)
                setPixel(y, hot);
        }
    }
}

void WaveformView::paintSpectral(juce::Graphics& g, juce::Rectangle<float> area, float pixelScale)
{
    const int width = juce::jmax(1, juce::roundToInt(area.getWidth() * pixelScale));
    const int height = juce::jmax(1, juce::roundToInt(area.getHeight() * pixelScale));
    const double bucketsInWindow = window * sampleRate / (double) WaveformBuilder::kFramesPerBucket;

    // Zoomed in further than one bucket per pixel: a smooth envelope in the current colour.
    if (bucketsInWindow < width)
    {
        WaveBucket recent;
        const auto span = (std::uint64_t) juce::jmax(1.0, bucketsInWindow);
        const auto colour = aggregate(total > span ? total - span : 0, total, recent) ? bandColour(recent) : theme::accent;
        clips.clear();
        for (const auto& lane : lanes())
        {
            const Lane single { lane.area, mode == StereoMode::Overlaid ? 2 : lane.channel, nullptr };
            drawEnvelope(g, single, (float) (area.getWidth() / bucketsInWindow), colour);
            if (mode == StereoMode::Overlaid)
                break;
        }
        g.setColour(theme::hot);
        g.fillRectList(clips);
        return;
    }

    const auto perColumn = (std::uint64_t) juce::jmax(1LL, (long long) std::llround(bucketsInWindow / width));
    const auto complete = total / perColumn; // columns whose audio has fully arrived

    if (spectralDirty || ! spectralImage.isValid() || spectralImage.getWidth() != width
        || spectralImage.getHeight() != height || spectralPerColumn != perColumn || spectralScale != pixelScale)
    {
        spectralImage = juce::Image(juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        spectralPerColumn = perColumn;
        spectralScale = pixelScale;
        spectralWriteX = 0;
        nextSpectralColumn = complete > (std::uint64_t) width ? complete - (std::uint64_t) width : 0;
        spectralDirty = false;
    }

    if (complete > nextSpectralColumn + (std::uint64_t) width)
        nextSpectralColumn = complete - (std::uint64_t) width; // fell far behind (e.g. minimised)

    for (; nextSpectralColumn < complete; ++nextSpectralColumn)
    {
        renderSpectralColumn(spectralWriteX, nextSpectralColumn);
        spectralWriteX = (spectralWriteX + 1) % width;
    }

    // The image is a ring: column spectralWriteX is the oldest. Draw it twice,
    // shifted, so the newest column lands at the right edge.
    g.saveState();
    g.reduceClipRegion(area.toNearestInt());
    const float inverse = 1.0f / pixelScale;
    g.drawImageTransformed(spectralImage, juce::AffineTransform::translation((float) -spectralWriteX, 0.0f)
                                              .scaled(inverse).translated(area.getX(), area.getY()));
    g.drawImageTransformed(spectralImage, juce::AffineTransform::translation((float) (width - spectralWriteX), 0.0f)
                                              .scaled(inverse).translated(area.getX(), area.getY()));
    g.restoreState();
}

//==============================================================================

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
    const bool spectral = currentStyle == Style::Spectral;

    // Time divisions: 10 across the window, labelled as "time ago".
    g.setFont(theme::font(10.5f));
    for (int i = 0; i <= 10; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 10.0f;
        g.setColour(spectral ? theme::grid.withMultipliedAlpha(0.6f) : (i == 10 ? theme::gridStrong : theme::grid));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

        // Skip labels that would collide with "now" at the right edge.
        if (! cleanMode && i % 2 == 0 && i < 10 && x + 70.0f < area.getRight() - 30.0f)
        {
            g.setColour(theme::textDim);
            const auto ago = "-" + formatSeconds(window * (10 - i) / 10.0);
            g.drawText(ago, juce::Rectangle<float>(x + 3.0f, area.getBottom() + 1.0f, 70.0f, 13.0f),
                       juce::Justification::centredLeft, false);
        }
    }
    if (! cleanMode)
    {
        g.setColour(theme::textDim);
        g.drawText("now", juce::Rectangle<float>(area.getRight() - 40.0f, area.getBottom() + 1.0f, 38.0f, 13.0f),
                   juce::Justification::centredRight, false);
    }

    for (const auto& lane : lanes())
    {
        const auto& a = lane.area;
        g.setColour(theme::gridStrong);
        g.drawHorizontalLine(juce::roundToInt(a.getCentreY()), a.getX(), a.getRight());
        if (! spectral)
        {
            g.setColour(theme::grid);
            g.drawHorizontalLine(juce::roundToInt(a.getY() + a.getHeight() * 0.25f), a.getX(), a.getRight()); // +0.5 (-6 dB)
            g.drawHorizontalLine(juce::roundToInt(a.getY() + a.getHeight() * 0.75f), a.getX(), a.getRight()); // -0.5
        }
    }

    if (cleanMode)
        return;

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
        label("L", spectral ? theme::text : leftColour, laneList[0].area);
        label("R", spectral ? theme::text : rightColour, laneList[1].area);
    }
    else if (mode == StereoMode::Overlaid && ! spectral)
    {
        label("L", leftColour, laneList[0].area);
        label("R", rightColour, laneList[0].area.withTrimmedLeft(12.0f));
    }
    else
    {
        label("MID", theme::text, laneList[0].area);
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
        if (! cleanMode)
        {
            g.setColour(theme::textDim);
            g.setFont(theme::font(13.0f));
            g.drawText("No audio stream", area, juce::Justification::centred, false);
        }
        return;
    }

    if (currentStyle == Style::Spectral)
        paintSpectral(g, area, pixelScale);
    else
        paintClassic(g, area);
}

} // namespace sonarc
