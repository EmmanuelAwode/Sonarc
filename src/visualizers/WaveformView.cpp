#include "visualizers/WaveformView.h"
#include "ui/Theme.h"

#include <cmath>
#include <numbers>

namespace sonarc {

namespace {

constexpr float kMaxWindowSeconds = 10.0f;

void channelRange(const WaveBucket& b, int channel, float& lo, float& hi) noexcept
{
    switch (channel)
    {
        case 0:  lo = b.minLeft;  hi = b.maxLeft;  break;
        case 1:  lo = b.minRight; hi = b.maxRight; break;
        default: lo = b.minMid;   hi = b.maxMid;   break;
    }
}

float channelPeak(const WaveBucket& b, int channel) noexcept
{
    float lo, hi;
    channelRange(b, channel, lo, hi);
    return juce::jmax(std::abs(lo), std::abs(hi));
}

float channelMeanSquare(const WaveBucket& b, int channel) noexcept
{
    return channel == 0 ? b.msLeft : channel == 1 ? b.msRight : b.msMid;
}

float channelMean(const WaveBucket& b, int channel) noexcept
{
    return channel == 0 ? b.meanLeft : channel == 1 ? b.meanRight : b.meanMid;
}

// Square-root height scale: a mix fills more of the lane and quiet passages stay
// visible, while transients still stand out (-6 dBFS reaches 71 %, -20 dBFS 32 %).
float toHeight(float amplitude) noexcept
{
    return std::sqrt(juce::jlimit(0.0f, 1.0f, amplitude));
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

//==============================================================================
// Data

void WaveformView::setSampleRate(std::uint32_t rate)
{
    if (rate == sampleRate)
        return;

    sampleRate = rate;
    const auto capacity = static_cast<std::size_t>(std::ceil(kMaxWindowSeconds * rate / WaveformBuilder::kFramesPerBucket)) + 64;
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

void WaveformView::setStyle(const WaveformStyle& newStyle)
{
    if (newStyle == currentStyle)
        return;
    currentStyle = newStyle;
    currentStyle.windowSeconds = juce::jlimit(WaveformStyle::windowRange.min, WaveformStyle::windowRange.max, currentStyle.windowSeconds);
    background = {};
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

void WaveformView::themeChanged()
{
    background = {};
    invalidateSpectral();
    repaint();
}

void WaveformView::setBadge(const juce::String& text)
{
    if (text == badge)
        return;
    badge = text;
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
    auto zoomed = currentStyle;
    zoomed.windowSeconds = currentStyle.windowSeconds * std::pow(1.25f, -wheel.deltaY * 4.0f);
    setStyle(zoomed);
    if (onWindowChanged)
        onWindowChanged(currentStyle.windowSeconds);
}

//==============================================================================
// Geometry & colour helpers

juce::Rectangle<float> WaveformView::plotArea() const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    const bool timeAxis = ! cleanMode && currentStyle.shape != Shape::Circular;
    return timeAxis ? bounds.reduced(1.0f, 4.0f).withTrimmedBottom(14.0f) : bounds.reduced(1.0f, 2.0f);
}

// Stereo separation 0 = one mid lane. Above 0 the left lane sits at the top and
// the right lane at the bottom, overlapping less as separation grows; at 1 they
// are fully stacked.
std::vector<WaveformView::Lane> WaveformView::lanes()
{
    const auto area = plotArea();
    const float separation = currentStyle.stereoSeparation;
    if (separation <= 0.001f)
        return { { area, 2, &spansA } };

    const float laneHeight = area.getHeight() / (1.0f + separation);
    const float gap = separation >= 0.999f ? 2.0f : 0.0f;
    return { { area.withHeight(laneHeight).withTrimmedBottom(gap), 0, &spansA },
             { area.withTop(area.getBottom() - laneHeight).withTrimmedTop(gap), 1, &spansB } };
}

// Peaks are combined by min/max, energies and means by averaging.
bool WaveformView::aggregate(std::uint64_t start, std::uint64_t end, WaveBucket& result) const noexcept
{
    const std::uint64_t size = history.size();
    if (size == 0)
        return false;
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
        result.meanLeft += b.meanLeft;
        result.meanRight += b.meanRight;
        result.meanMid += b.meanMid;
    }

    const float count = (float) (end - start);
    result.msLeft /= count;
    result.msRight /= count;
    result.msMid /= count;
    result.msLow /= count;
    result.msMidBand /= count;
    result.msHigh /= count;
    result.meanLeft /= count;
    result.meanRight /= count;
    result.meanMid /= count;
    return true;
}

float WaveformView::yFor(const Lane& lane, float value) const noexcept
{
    return lane.area.getCentreY() - gain(value) * lane.area.getHeight() * 0.5f;
}

double WaveformView::bucketsInWindow() const noexcept
{
    return currentStyle.windowSeconds * sampleRate / (double) WaveformBuilder::kFramesPerBucket;
}

float WaveformView::effectiveGlow() const noexcept
{
    return currentStyle.glow * theme::glow;
}

juce::Colour WaveformView::bandColour(const WaveBucket& b) const noexcept
{
    // Music has far less energy up high, so highs are weighted up to stay visible.
    const float low = std::sqrt(b.msLow);
    const float mid = std::sqrt(b.msMidBand) * 1.8f;
    const float high = std::sqrt(b.msHigh) * 3.5f;
    const float sum = low + mid + high;
    if (sum <= 1.0e-9f)
        return theme::waveMid;

    const auto& lc = theme::waveLow;
    const auto& mc = theme::waveMid;
    const auto& hc = theme::waveHigh;
    float r = (low * lc.getFloatRed() + mid * mc.getFloatRed() + high * hc.getFloatRed()) / sum;
    float g = (low * lc.getFloatGreen() + mid * mc.getFloatGreen() + high * hc.getFloatGreen()) / sum;
    float bl = (low * lc.getFloatBlue() + mid * mc.getFloatBlue() + high * hc.getFloatBlue()) / sum;

    // Keep mixes vivid: scale so the strongest component is at full brightness.
    const float peak = juce::jmax(r, g, bl, 1.0e-6f);
    return juce::Colour::fromFloatRGBA(r / peak, g / peak, bl / peak, 1.0f);
}

juce::Colour WaveformView::gradientAt(float t) const noexcept
{
    const auto& stops = theme::spectrumStops;
    const float position = juce::jlimit(0.0f, 1.0f, t) * (float) (stops.size() - 1);
    const auto i = juce::jmin((size_t) position, stops.size() - 2);
    return stops[i].interpolatedWith(stops[i + 1], position - (float) i);
}

// `level` is 0..1, used by the spectrum-gradient source (quiet = low colours, loud = high).
juce::Colour WaveformView::sourceColour(const WaveBucket& b, int channel, float level) const noexcept
{
    switch (currentStyle.colour)
    {
        case WaveformStyle::ColourSource::SpectralBands:    return bandColour(b);
        case WaveformStyle::ColourSource::Channels:         return channel == 1 ? theme::waveRight : theme::waveLeft;
        case WaveformStyle::ColourSource::SpectrumGradient: return gradientAt(level);
        case WaveformStyle::ColourSource::Accent:           break;
    }
    return theme::accent;
}

// For filled shapes: a left-to-right gradient that follows the colour of the audio underneath.
juce::ColourGradient WaveformView::horizontalGradient(juce::Rectangle<float> area, const std::vector<WaveBucket>& columns,
                                                      float alpha) const
{
    if (columns.empty())
        return juce::ColourGradient(theme::accent.withAlpha(alpha), area.getX(), 0.0f,
                                    theme::accent.withAlpha(alpha), area.getRight(), 0.0f, false);

    constexpr int kStops = 16;
    auto colourAt = [&](int stop)
    {
        const auto index = (size_t) ((double) stop / (kStops - 1) * (double) (columns.size() - 1));
        const auto& b = columns[index];
        return sourceColour(b, 2, toHeight(gain(channelPeak(b, 2)))).withMultipliedAlpha(alpha);
    };

    juce::ColourGradient gradient(colourAt(0), area.getX(), 0.0f, colourAt(kStops - 1), area.getRight(), 0.0f, false);
    for (int s = 1; s < kStops - 1; ++s)
        gradient.addColour((double) s / (kStops - 1), colourAt(s));
    return gradient;
}

void WaveformView::setFill(juce::Graphics& g, const juce::FillType& fill)
{
    currentFill = fill;
    g.setFillType(fill);
}

// Strokes with the fill last set by setFill(), with soft halo passes under it.
void WaveformView::strokeWithGlow(juce::Graphics& g, const juce::Path& path, float thickness)
{
    const float glow = effectiveGlow();
    const auto fill = currentFill;
    if (glow > 0.0f)
    {
        auto halo = fill;
        halo.setOpacity(juce::jmin(1.0f, 0.10f * glow));
        g.setFillType(halo);
        g.strokePath(path, juce::PathStrokeType(thickness + 7.0f * glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        halo.setOpacity(juce::jmin(1.0f, 0.22f * glow));
        g.setFillType(halo);
        g.strokePath(path, juce::PathStrokeType(thickness + 2.5f * glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setFillType(fill);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// `count` equal slices of the time window ending now, oldest first.
void WaveformView::fillSlices(int count, std::vector<WaveBucket>& out) const
{
    out.assign((size_t) count, {});
    const double perSlice = juce::jmax(1.0, bucketsInWindow() / count);
    const double end = (double) total;
    for (int i = 0; i < count; ++i)
    {
        const double sliceEnd = end - (double) (count - 1 - i) * perSlice;
        const auto from = (std::uint64_t) juce::jmax(0.0, std::floor(sliceEnd - perSlice));
        const auto to = (std::uint64_t) juce::jmax(0.0, std::ceil(sliceEnd));
        aggregate(from, juce::jmax(from + 1, to), out[(size_t) i]);
    }
}

// Rise fast, fall at a rate set by smoothing and animation speed; peak caps hold then fall.
void WaveformView::smoothLevels(std::vector<float>& shown, const std::vector<float>& target, std::vector<float>& peaks, float dt) const
{
    if (shown.size() != target.size())
    {
        shown = target;
        peaks = target;
    }

    const float speed = juce::jmax(0.05f, currentStyle.animationSpeed);
    const float smoothing = currentStyle.smoothing;
    const float attack = 1.0f - std::pow(0.6f * smoothing, juce::jmax(0.0f, dt) * 60.0f); // 1 = instant
    const float fall = (0.4f + 3.6f * (1.0f - smoothing)) * speed * dt;                    // full-scale per second
    const float peakFall = 0.35f * speed * dt;

    for (size_t i = 0; i < target.size(); ++i)
    {
        const float t = target[i];
        shown[i] = t > shown[i] ? shown[i] + (t - shown[i]) * attack : juce::jmax(t, shown[i] - fall);
        peaks[i] = juce::jmax(shown[i], peaks[i] - peakFall);
    }
}

//==============================================================================
// Classic

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

    const float minHeight = currentStyle.lineThickness;
    if (bottom - top < minHeight)
    {
        const float mid = 0.5f * (top + bottom);
        top = mid - 0.5f * minHeight;
        bottom = mid + 0.5f * minHeight;
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

    const float half = 0.5f * currentStyle.lineThickness;
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
    if (effectiveGlow() > 0.0f)
    {
        g.setColour(colour.withAlpha(juce::jmin(1.0f, 0.18f * effectiveGlow())));
        g.strokePath(envelope, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
    }
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

    // One colour per lane: the theme's channel colours, or the colour of what's on screen.
    WaveBucket visible;
    const auto span = (std::uint64_t) juce::jmax(1.0, bucketsInWindow());
    const bool haveVisible = aggregate(total > span ? total - span : 0, total, visible);
    const bool overlapping = laneList.size() == 2 && currentStyle.stereoSeparation < 0.999f;
    auto colourFor = [&](const Lane& lane)
    {
        const auto colour = haveVisible ? sourceColour(visible, lane.channel, toHeight(gain(channelPeak(visible, lane.channel))))
                                        : theme::accent;
        return overlapping ? colour.withMultipliedAlpha(lane.channel == 0 ? 0.75f : 0.6f) : colour;
    };

    const int width = juce::jmax(1, juce::roundToInt(area.getWidth()));
    const double bucketsPerPixel = bucketsInWindow() / width;

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
// Mirrored

void WaveformView::renderMirroredColumn(int x, std::uint64_t column)
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

    // Lanes in image rows (same layout as lanes(), in physical pixels).
    struct MirrorLane { float top, bottom; int channel; };
    MirrorLane laneList[2];
    int laneCount = 1;
    const float separation = currentStyle.stereoSeparation;
    if (separation <= 0.001f)
    {
        laneList[0] = { 0.0f, (float) height, 2 };
    }
    else
    {
        const float laneHeight = (float) height / (1.0f + separation);
        const float gap = separation >= 0.999f ? juce::jmax(2.0f, (float) height / 60.0f) * 0.5f : 0.0f;
        laneList[0] = { 0.0f, laneHeight - gap, 0 };
        laneList[1] = { (float) height - laneHeight + gap, (float) height, 1 };
        laneCount = 2;
    }

    // Composite lanes ("over") into premultiplied RGBA.
    columnPixels.assign((size_t) height * 4, 0.0f);

    const float glow = effectiveGlow();
    const float glowPixels = 6.0f * spectralScale * juce::jmax(0.01f, glow);
    const float glowAlpha = juce::jmin(1.0f, 0.38f * glow);
    const float minPixels = juce::jmax(1.0f, currentStyle.lineThickness * spectralScale * 0.5f);
    const float bodyAlpha = currentStyle.fillOpacity;

    for (int l = 0; l < laneCount; ++l)
    {
        const auto& lane = laneList[l];
        const float centre = 0.5f * (lane.top + lane.bottom);
        const float halfHeight = 0.5f * (lane.bottom - lane.top);
        if (halfHeight < 1.0f)
            continue;

        const float peak = juce::jmin(1.0f, channelPeak(b, lane.channel) * currentStyle.amplitude);
        const float rms = juce::jmin(peak, std::sqrt(channelMeanSquare(b, lane.channel)) * currentStyle.amplitude);
        const float peakPixels = juce::jmax(minPixels, toHeight(peak) * halfHeight);
        const float rmsPixels = toHeight(rms) * halfHeight;

        const auto colour = sourceColour(b, lane.channel, toHeight(peak));
        const float cr = colour.getFloatRed(), cg = colour.getFloatGreen(), cb = colour.getFloatBlue();

        const int from = juce::jmax(0, (int) (centre - peakPixels - glowPixels));
        const int to = juce::jmin(height, (int) std::ceil(centre + peakPixels + glowPixels));
        for (int y = from; y < to; ++y)
        {
            const float d = std::abs((float) y + 0.5f - centre); // distance from the centre line, pixels
            float alpha, whiteness = 0.0f;

            if (d <= rmsPixels)
            {
                // Core: full colour, lifting slightly towards the centre line.
                alpha = 1.0f;
                const float t = 1.0f - d / juce::jmax(1.0f, rmsPixels);
                whiteness = 0.35f * t * t;
            }
            else if (d <= peakPixels)
            {
                // Body: colour fading towards the peak edge, so the RMS core reads as the solid part.
                alpha = bodyAlpha * (1.0f - 0.5f * (d - rmsPixels) / juce::jmax(1.0f, peakPixels - rmsPixels));
            }
            else if (d <= peakPixels + glowPixels)
            {
                const float t = 1.0f - (d - peakPixels) / glowPixels;
                alpha = glowAlpha * t * t;
            }
            else
            {
                continue;
            }

            float* p = &columnPixels[(size_t) y * 4];
            const float inverse = 1.0f - alpha;
            p[0] = (cr + (1.0f - cr) * whiteness) * alpha + p[0] * inverse;
            p[1] = (cg + (1.0f - cg) * whiteness) * alpha + p[1] * inverse;
            p[2] = (cb + (1.0f - cb) * whiteness) * alpha + p[2] * inverse;
            p[3] = alpha + p[3] * inverse;
        }

        // Clip marks at the lane edges.
        if (peak >= 1.0f)
        {
            for (int y : { (int) lane.top, (int) lane.top + 1, (int) lane.bottom - 2, (int) lane.bottom - 1 })
                if (y >= 0 && y < height)
                {
                    float* p = &columnPixels[(size_t) y * 4];
                    p[0] = theme::hot.getFloatRed();
                    p[1] = theme::hot.getFloatGreen();
                    p[2] = theme::hot.getFloatBlue();
                    p[3] = 1.0f;
                }
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const float* p = &columnPixels[(size_t) y * 4];
        setPixel(y, juce::PixelARGB((juce::uint8) (juce::jlimit(0.0f, 1.0f, p[3]) * 255.0f),
                                    (juce::uint8) (juce::jlimit(0.0f, 1.0f, p[0]) * 255.0f),
                                    (juce::uint8) (juce::jlimit(0.0f, 1.0f, p[1]) * 255.0f),
                                    (juce::uint8) (juce::jlimit(0.0f, 1.0f, p[2]) * 255.0f)));
    }
}

void WaveformView::paintMirrored(juce::Graphics& g, juce::Rectangle<float> area, float pixelScale)
{
    const int width = juce::jmax(1, juce::roundToInt(area.getWidth() * pixelScale));
    const int height = juce::jmax(1, juce::roundToInt(area.getHeight() * pixelScale));
    const double windowBuckets = bucketsInWindow();

    // Zoomed in further than one bucket per pixel: a smooth envelope in the current colour.
    if (windowBuckets < width)
    {
        WaveBucket recent;
        const auto span = (std::uint64_t) juce::jmax(1.0, windowBuckets);
        clips.clear();
        for (const auto& lane : lanes())
        {
            const auto colour = aggregate(total > span ? total - span : 0, total, recent)
                                    ? sourceColour(recent, lane.channel, toHeight(gain(channelPeak(recent, lane.channel))))
                                    : theme::accent;
            drawEnvelope(g, lane, (float) (area.getWidth() / windowBuckets), colour);
        }
        g.setColour(theme::hot);
        g.fillRectList(clips);
        return;
    }

    const auto perColumn = (std::uint64_t) juce::jmax(1LL, (long long) std::llround(windowBuckets / width));
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
        renderMirroredColumn(spectralWriteX, nextSpectralColumn);
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
// Oscilloscope

void WaveformView::paintOscilloscope(juce::Graphics& g, juce::Rectangle<float> area)
{
    const std::uint64_t size = history.size();
    const std::uint64_t oldest = total > size ? total - size : 0;
    const auto wanted = (std::uint64_t) juce::jlimit(8.0, (double) size, bucketsInWindow());
    if (total - oldest < 8)
        return;

    // Trigger: end the trace on the most recent rising zero crossing of the mid
    // signal (within the newest half window), so a steady tone stands still.
    std::uint64_t end = total;
    for (std::uint64_t back = 1; back < wanted / 2 && total - back > oldest + 1; ++back)
    {
        const auto i = total - back;
        if (history[(i - 1) % size].meanMid < 0.0f && history[i % size].meanMid >= 0.0f)
        {
            end = i + 1;
            break;
        }
    }
    const std::uint64_t count = std::min(wanted, end - oldest);
    const std::uint64_t start = end - count;

    const int points = (int) juce::jmin<std::uint64_t>(count, (std::uint64_t) juce::jmax(2, juce::roundToInt(area.getWidth())));
    const int radius = juce::roundToInt(currentStyle.smoothing * 6.0f);
    std::vector<float> values((size_t) points), smoothed((size_t) points);

    WaveBucket visible;
    aggregate(start, end, visible);

    for (const auto& lane : lanes())
    {
        for (int p = 0; p < points; ++p)
        {
            const auto from = start + (std::uint64_t) ((double) p / points * (double) count);
            const auto to = juce::jmax(from + 1, start + (std::uint64_t) ((double) (p + 1) / points * (double) count));
            double sum = 0.0;
            for (auto i = from; i < to; ++i)
                sum += channelMean(history[i % size], lane.channel);
            values[(size_t) p] = (float) (sum / (double) (to - from));
        }

        for (int p = 0; p < points; ++p)
        {
            double sum = 0.0;
            int used = 0;
            for (int k = juce::jmax(0, p - radius); k <= juce::jmin(points - 1, p + radius); ++k, ++used)
                sum += values[(size_t) k];
            smoothed[(size_t) p] = (float) (sum / used);
        }

        shapePath.clear();
        for (int p = 0; p < points; ++p)
        {
            const float x = area.getX() + area.getWidth() * (float) p / (float) (points - 1);
            const float y = yFor(lane, smoothed[(size_t) p] * 1.0f);
            if (p == 0)
                shapePath.startNewSubPath(x, y);
            else
                shapePath.lineTo(x, y);
        }

        if (currentStyle.colour == WaveformStyle::ColourSource::SpectrumGradient)
        {
            juce::ColourGradient gradient(gradientAt(0.0f), area.getX(), 0.0f, gradientAt(1.0f), area.getRight(), 0.0f, false);
            for (int s = 1; s < 5; ++s)
                gradient.addColour(s / 5.0, gradientAt(s / 5.0f));
            setFill(g, juce::FillType(gradient));
        }
        else
        {
            setFill(g, juce::FillType(sourceColour(visible, lane.channel, 0.7f)));
        }
        strokeWithGlow(g, shapePath, currentStyle.lineThickness);
    }
}

//==============================================================================
// Bars

void WaveformView::paintBars(juce::Graphics& g, juce::Rectangle<float> area, float dt)
{
    const int count = juce::jlimit(WaveformStyle::minBarCount, WaveformStyle::maxBarCount, currentStyle.barCount);
    fillSlices(count, slices);

    const bool stereo = currentStyle.stereoSeparation > 0.001f;
    targetA.assign((size_t) count, 0.0f);
    targetB.assign((size_t) count, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        targetA[(size_t) i] = toHeight(gain(channelPeak(slices[(size_t) i], stereo ? 0 : 2)));
        targetB[(size_t) i] = stereo ? toHeight(gain(channelPeak(slices[(size_t) i], 1))) : 0.0f;
    }
    smoothLevels(levelsA, targetA, peaksA, dt);
    smoothLevels(levelsB, targetB, peaksB, dt);

    const float slot = area.getWidth() / (float) count;
    const float gap = juce::jmax(1.0f, slot * 0.22f);
    const float barWidth = juce::jmax(1.0f, slot - gap);
    // Mono: bars stand on the floor. Stereo: left rises from the centre, right hangs below it.
    const float centreGap = stereo ? currentStyle.stereoSeparation * 6.0f : 0.0f;
    const float baseline = stereo ? area.getCentreY() : area.getBottom();
    const float reach = stereo ? area.getHeight() * 0.5f - centreGap * 0.5f : area.getHeight();
    const float glow = effectiveGlow();
    const bool gradientFill = currentStyle.colour == WaveformStyle::ColourSource::SpectrumGradient;

    auto drawBar = [&](float x, float level, float peak, bool down, juce::Colour colour)
    {
        const float h = juce::jmax(1.0f, level * reach);
        const auto bar = down ? juce::Rectangle<float>(x, baseline + centreGap * 0.5f, barWidth, h)
                              : juce::Rectangle<float>(x, baseline - centreGap * 0.5f - h, barWidth, h);

        if (gradientFill)
        {
            // Quiet colours at the baseline, loud colours at full reach.
            const float far = down ? baseline + reach : baseline - reach;
            juce::ColourGradient gradient(gradientAt(0.0f), 0.0f, baseline, gradientAt(1.0f), 0.0f, far, false);
            for (int s = 1; s < 5; ++s)
                gradient.addColour(s / 5.0, gradientAt(s / 5.0f));
            g.setGradientFill(gradient);
        }
        else
        {
            g.setColour(colour);
        }

        if (glow > 0.0f)
        {
            g.setOpacity(juce::jmin(1.0f, 0.18f * glow));
            g.fillRoundedRectangle(bar.expanded(2.5f * glow, 1.5f * glow), 3.0f);
        }
        g.setOpacity(currentStyle.fillOpacity);
        g.fillRoundedRectangle(bar, juce::jmin(2.5f, barWidth * 0.3f));
        g.setOpacity(1.0f);

        if (currentStyle.peakHold && peak > level + 0.01f)
        {
            const float py = down ? baseline + centreGap * 0.5f + peak * reach : baseline - centreGap * 0.5f - peak * reach;
            g.setColour(theme::textBright.withAlpha(0.85f));
            g.fillRect(juce::Rectangle<float>(x, down ? py - 2.0f : py, barWidth, 2.0f));
        }
    };

    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + (float) i * slot + gap * 0.5f;
        const auto& slice = slices[(size_t) i];
        drawBar(x, levelsA[(size_t) i], peaksA[(size_t) i], false, sourceColour(slice, stereo ? 0 : 2, levelsA[(size_t) i]));
        if (stereo)
            drawBar(x, levelsB[(size_t) i], peaksB[(size_t) i], true, sourceColour(slice, 1, levelsB[(size_t) i]));
    }
}

//==============================================================================
// Filled

void WaveformView::paintFilled(juce::Graphics& g, juce::Rectangle<float> area)
{
    const int width = juce::jmax(2, juce::roundToInt(area.getWidth()));
    const auto perColumn = (std::uint64_t) juce::jmax(1LL, (long long) std::llround(bucketsInWindow() / width));
    const auto lastColumn = (long long) (total / perColumn);
    const int radius = juce::roundToInt(currentStyle.smoothing * 10.0f);

    slices.assign((size_t) width, {});
    std::vector<bool> present((size_t) width, false);
    for (int c = 0; c < width; ++c)
    {
        const long long column = lastColumn - (width - 1 - c);
        if (column >= 0)
            present[(size_t) c] = aggregate((std::uint64_t) column * perColumn, (std::uint64_t) (column + 1) * perColumn, slices[(size_t) c]);
    }

    int first = 0;
    while (first < width && ! present[(size_t) first])
        ++first;
    if (first >= width - 1)
        return;

    for (const auto& lane : lanes())
    {
        std::vector<float> top((size_t) width), bottom((size_t) width);
        for (int c = first; c < width; ++c)
        {
            float lo, hi;
            channelRange(slices[(size_t) c], lane.channel, lo, hi);
            top[(size_t) c] = hi;
            bottom[(size_t) c] = lo;
        }

        auto smooth = [&](std::vector<float>& v)
        {
            if (radius == 0)
                return;
            std::vector<float> copy = v;
            for (int c = first; c < width; ++c)
            {
                double sum = 0.0;
                int used = 0;
                for (int k = juce::jmax(first, c - radius); k <= juce::jmin(width - 1, c + radius); ++k, ++used)
                    sum += copy[(size_t) k];
                v[(size_t) c] = (float) (sum / used);
            }
        };
        smooth(top);
        smooth(bottom);

        shapePath.clear();
        outlinePath.clear();
        for (int c = first; c < width; ++c)
        {
            const float x = area.getX() + (float) c;
            const float y = yFor(lane, top[(size_t) c]);
            if (c == first)
            {
                shapePath.startNewSubPath(x, y);
                outlinePath.startNewSubPath(x, y);
            }
            else
            {
                shapePath.lineTo(x, y);
                outlinePath.lineTo(x, y);
            }
        }
        for (int c = width - 1; c >= first; --c)
            shapePath.lineTo(area.getX() + (float) c, yFor(lane, bottom[(size_t) c]));
        shapePath.closeSubPath();

        outlinePath.startNewSubPath(area.getX() + (float) first, yFor(lane, bottom[(size_t) first]));
        for (int c = first + 1; c < width; ++c)
            outlinePath.lineTo(area.getX() + (float) c, yFor(lane, bottom[(size_t) c]));

        g.saveState();
        g.reduceClipRegion(lane.area.toNearestInt());
        if (currentStyle.colour == WaveformStyle::ColourSource::SpectrumGradient)
        {
            // Gradient by height: quiet near the centre line, loud at the edges.
            const float centre = lane.area.getCentreY();
            juce::ColourGradient gradient(gradientAt(0.0f).withMultipliedAlpha(currentStyle.fillOpacity), 0.0f, centre,
                                          gradientAt(1.0f).withMultipliedAlpha(currentStyle.fillOpacity), 0.0f, lane.area.getY(), false);
            for (int s = 1; s < 5; ++s)
                gradient.addColour(s / 5.0, gradientAt(s / 5.0f).withMultipliedAlpha(currentStyle.fillOpacity));
            g.setGradientFill(gradient);
            g.fillPath(shapePath);
            gradient.multiplyOpacity(1.0f / juce::jmax(0.05f, currentStyle.fillOpacity));
            setFill(g, juce::FillType(gradient));
        }
        else if (currentStyle.colour == WaveformStyle::ColourSource::SpectralBands)
        {
            std::vector<WaveBucket> visible(slices.begin() + first, slices.end());
            g.setGradientFill(horizontalGradient(area.withLeft(area.getX() + (float) first), visible, currentStyle.fillOpacity));
            g.fillPath(shapePath);
            setFill(g, juce::FillType(horizontalGradient(area.withLeft(area.getX() + (float) first), visible, 1.0f)));
        }
        else
        {
            const auto colour = sourceColour(slices.back(), lane.channel, 0.7f);
            g.setColour(colour.withMultipliedAlpha(currentStyle.fillOpacity));
            g.fillPath(shapePath);
            setFill(g, juce::FillType(colour));
        }
        strokeWithGlow(g, outlinePath, currentStyle.lineThickness);
        g.restoreState();
    }
}

//==============================================================================
// Circular

void WaveformView::paintCircular(juce::Graphics& g, juce::Rectangle<float> area, float dt)
{
    constexpr int kSlices = 180;
    fillSlices(kSlices, slices);

    const bool stereo = currentStyle.stereoSeparation > 0.001f;
    targetA.assign(kSlices, 0.0f);
    targetB.assign(kSlices, 0.0f);
    for (int i = 0; i < kSlices; ++i)
    {
        targetA[(size_t) i] = toHeight(gain(channelPeak(slices[(size_t) i], stereo ? 0 : 2)));
        targetB[(size_t) i] = stereo ? toHeight(gain(channelPeak(slices[(size_t) i], 1))) : 0.0f;
    }
    smoothLevels(levelsA, targetA, peaksA, dt);
    smoothLevels(levelsB, targetB, peaksB, dt);

    rotation += currentStyle.animationSpeed * 0.35f * dt;
    rotation = std::fmod(rotation, juce::MathConstants<float>::twoPi);

    // The ring breathes with the level of the last ~60 ms.
    WaveBucket recent;
    const auto pulseSpan = (std::uint64_t) juce::jmax(1.0, 0.06 * sampleRate / WaveformBuilder::kFramesPerBucket);
    const float pulse = aggregate(total > pulseSpan ? total - pulseSpan : 0, total, recent)
                            ? toHeight(gain(std::sqrt(recent.msMid)))
                            : 0.0f;

    const auto centre = area.getCentre();
    const float outer = 0.5f * juce::jmin(area.getWidth(), area.getHeight()) * 0.94f;
    const float base = outer * (0.38f + 0.08f * pulse);
    const float span = outer - base;
    const float offset = stereo ? currentStyle.stereoSeparation * span * 0.22f : 0.0f;

    auto ringPath = [&](const std::vector<float>& levels, float radiusOffset, float levelScale, juce::Path& path)
    {
        path.clear();
        for (int i = 0; i <= kSlices; ++i)
        {
            const int index = i % kSlices;
            const float angle = rotation + juce::MathConstants<float>::twoPi * (float) index / kSlices - juce::MathConstants<float>::halfPi;
            const float r = base + radiusOffset + levels[(size_t) index] * span * levelScale;
            const auto point = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * r;
            if (i == 0)
                path.startNewSubPath(point);
            else
                path.lineTo(point);
        }
        path.closeSubPath();
    };

    auto drawRing = [&](const std::vector<float>& levels, const std::vector<float>& peaks, float radiusOffset, int channel)
    {
        const float scale = stereo ? 0.75f : 1.0f;
        ringPath(levels, radiusOffset, scale, shapePath);

        juce::Colour colour = sourceColour(recent, channel, pulse);
        if (currentStyle.colour == WaveformStyle::ColourSource::SpectrumGradient)
        {
            juce::ColourGradient gradient(gradientAt(0.0f), centre.x, centre.y, gradientAt(1.0f), centre.x + outer, centre.y, true);
            for (int s = 1; s < 5; ++s)
                gradient.addColour(s / 5.0, gradientAt(s / 5.0f));
            setFill(g, juce::FillType(gradient));
        }
        else
        {
            setFill(g, juce::FillType(colour));
        }

        if (currentStyle.fillOpacity > 0.0f)
        {
            auto translucent = currentFill;
            translucent.setOpacity(currentStyle.fillOpacity * 0.35f);
            g.setFillType(translucent);
            g.fillPath(shapePath);
            g.setFillType(currentFill);
        }
        strokeWithGlow(g, shapePath, currentStyle.lineThickness);

        if (currentStyle.peakHold)
        {
            ringPath(peaks, radiusOffset, scale, outlinePath);
            g.setColour(theme::textBright.withAlpha(0.35f));
            g.strokePath(outlinePath, juce::PathStrokeType(1.0f));
        }
    };

    if (stereo)
    {
        drawRing(levelsA, peaksA, offset, 0);
        drawRing(levelsB, peaksB, -offset, 1);
    }
    else
    {
        drawRing(levelsA, peaksA, 0.0f, 2);
    }
}

//==============================================================================
// Background and paint

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
    const auto shape = currentStyle.shape;
    if (shape == Shape::Circular)
        return; // no axes

    const bool soft = shape != Shape::Classic;
    const bool newestLeft = currentStyle.direction == WaveformStyle::Direction::NewestLeft;

    // Time divisions: 10 across the window, labelled as "time ago".
    g.setFont(theme::font(10.5f));
    for (int i = 0; i <= 10; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 10.0f;
        g.setColour(soft ? theme::grid.withMultipliedAlpha(0.6f) : ((i == 10) != newestLeft ? theme::gridStrong : theme::grid));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    }
    if (! cleanMode)
    {
        g.setColour(theme::textDim);
        for (int i = 0; i < 10; i += 2)
        {
            const float position = area.getX() + area.getWidth() * (float) i / 10.0f;
            const float x = newestLeft ? area.getRight() - (position - area.getX()) - 70.0f : position + 3.0f;
            if (! newestLeft && x + 70.0f >= area.getRight() - 30.0f)
                continue; // would collide with "now"
            const auto ago = "-" + formatSeconds(currentStyle.windowSeconds * (10 - i) / 10.0);
            g.drawText(ago, juce::Rectangle<float>(x, area.getBottom() + 1.0f, 67.0f, 13.0f),
                       newestLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }
        g.drawText("now", juce::Rectangle<float>(newestLeft ? area.getX() + 2.0f : area.getRight() - 40.0f, area.getBottom() + 1.0f, 38.0f, 13.0f),
                   newestLeft ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }

    const bool barsMono = shape == Shape::Bars && currentStyle.stereoSeparation <= 0.001f;
    for (const auto& lane : lanes())
    {
        const auto& a = lane.area;
        g.setColour(theme::gridStrong);
        g.drawHorizontalLine(juce::roundToInt(barsMono ? a.getBottom() - 1.0f : a.getCentreY()), a.getX(), a.getRight());
        if (shape == Shape::Classic)
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
    const auto laneList = lanes();
    auto label = [&](const juce::String& text, juce::Rectangle<float> a)
    {
        g.setColour(theme::text.withAlpha(0.8f));
        g.drawText(text, a.reduced(5.0f, 2.0f), juce::Justification::topLeft, false);
    };
    if (laneList.size() == 2)
    {
        label("L", laneList[0].area);
        label("R", laneList[1].area.withTrimmedTop(juce::jmax(0.0f, laneList[0].area.getBottom() - laneList[1].area.getY())));
    }
    else
    {
        label("MID", laneList[0].area);
    }

    g.setColour(theme::textDim);
    g.setFont(theme::font(10.5f));
    g.drawText("window " + formatSeconds(currentStyle.windowSeconds) + "  |  wheel to zoom", area.reduced(6.0f, 2.0f),
               juce::Justification::topRight, false);
}

void WaveformView::paint(juce::Graphics& g)
{
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! background.isValid() || pixelScale != backgroundScale)
        renderBackground(pixelScale);
    g.drawImage(background, getLocalBounds().toFloat());

    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastPaintMs > 0.0 ? (float) juce::jlimit(0.0, 0.25, (now - lastPaintMs) / 1000.0) : 0.0f;
    lastPaintMs = now;

    const auto area = plotArea();
    if (sampleRate == 0 || total == 0)
    {
        if (! cleanMode && badge.isEmpty())
        {
            g.setColour(theme::textDim);
            g.setFont(theme::font(13.0f));
            g.drawText("No audio stream", area, juce::Justification::centred, false);
        }
    }
    else
    {
        g.saveState();
        // "Newest on the left" mirrors the scrolling shapes horizontally.
        if (currentStyle.direction == WaveformStyle::Direction::NewestLeft && currentStyle.shape != Shape::Circular)
            g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, area.getCentreX(), 0.0f));

        switch (currentStyle.shape)
        {
            case Shape::Classic:      paintClassic(g, area); break;
            case Shape::Mirrored:     paintMirrored(g, area, pixelScale); break;
            case Shape::Oscilloscope: paintOscilloscope(g, area); break;
            case Shape::Bars:         paintBars(g, area, dt); break;
            case Shape::Filled:       paintFilled(g, area); break;
            case Shape::Circular:     paintCircular(g, area, dt); break;
        }
        g.restoreState();
    }

    if (badge.isNotEmpty())
    {
        const auto font = theme::font(11.0f, true);
        const float width = juce::GlyphArrangement::getStringWidth(font, badge) + 14.0f;
        const auto box = juce::Rectangle<float>(area.getCentreX() - width * 0.5f, area.getY() + 4.0f, width, 18.0f); // clear of lane labels
        g.setColour(theme::background.withAlpha(0.75f));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(theme::warn);
        g.setFont(font);
        g.drawText(badge, box, juce::Justification::centred, false);
    }
}

} // namespace sonarc
