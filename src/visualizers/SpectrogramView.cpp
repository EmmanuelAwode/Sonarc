#include "visualizers/SpectrogramView.h"
#include "dsp/Decibels.h"
#include "ui/Theme.h"

#include <cmath>

namespace sonarc {

namespace {

struct Stop
{
    float position;
    juce::uint32 argb;
};

// Original palettes: each runs from near-black (quiet) to bright (loud).
// "Theme" is built from the active spectrum colours.
std::vector<Stop> stopsFor(SpectrogramView::Palette palette)
{
    if (palette == SpectrogramView::Palette::Theme)
    {
        const auto& s = theme::spectrumStops;
        return { { 0.0f, theme::panel.darker(0.7f).getARGB() }, { 0.2f, s[1].darker(0.9f).getARGB() },
                 { 0.42f, s[2].darker(0.2f).getARGB() }, { 0.62f, s[3].getARGB() },
                 { 0.82f, s[4].getARGB() }, { 1.0f, s[5].brighter(0.6f).getARGB() } };
    }

    static const std::vector<Stop> sonarc { { 0.0f, 0xff07080b }, { 0.22f, 0xff101c4a }, { 0.45f, 0xff1f6fc8 },
                                            { 0.65f, 0xff3ad0e0 }, { 0.84f, 0xffffc35a }, { 1.0f, 0xfffff6e8 } };
    static const std::vector<Stop> ember { { 0.0f, 0xff07060a }, { 0.28f, 0xff3a1050 }, { 0.52f, 0xffa82d5a },
                                           { 0.75f, 0xfff0742c }, { 1.0f, 0xfffff0a6 } };
    static const std::vector<Stop> ice { { 0.0f, 0xff05070a }, { 0.35f, 0xff0c3560 }, { 0.68f, 0xff4aaed6 }, { 1.0f, 0xffffffff } };
    static const std::vector<Stop> grey { { 0.0f, 0xff000000 }, { 1.0f, 0xffffffff } };

    switch (palette)
    {
        case SpectrogramView::Palette::Ember:     return ember;
        case SpectrogramView::Palette::Ice:       return ice;
        case SpectrogramView::Palette::Grayscale: return grey;
        case SpectrogramView::Palette::Theme:
        case SpectrogramView::Palette::Sonarc:    break;
    }
    return sonarc;
}

float yForFrequency(float hz, juce::Rectangle<float> area)
{
    const float position = std::log(hz / kSpectrogramMinHz) / std::log(kSpectrogramMaxHz / kSpectrogramMinHz);
    return area.getBottom() - position * area.getHeight();
}

} // namespace

SpectrogramView::SpectrogramView()
{
    setOpaque(true);
    history.resize(kMaxColumns);
    rebuildPalette();
}

void SpectrogramView::setRowRate(double rowsPerSecond)
{
    if (rowsPerSecond == rowRate)
        return;

    rowRate = rowsPerSecond;
    clear();
}

void SpectrogramView::clear()
{
    columnCount = 0;
    hasPending = false;
    columnPhase = 0.0;
    image = {};
    repaint();
}

void SpectrogramView::setSpeed(float newPixelsPerSecond)
{
    pixelsPerSecond = juce::jlimit(5.0f, 400.0f, newPixelsPerSecond);
    overlay = {};
    repaint();
}

void SpectrogramView::setPalette(Palette palette)
{
    currentPalette = palette;
    rebuildPalette();
    image = {}; // re-render the history in the new colours
    repaint();
}

void SpectrogramView::setRangeDb(float decibels)
{
    range = juce::jlimit(30.0f, 150.0f, decibels);
    image = {};
    overlay = {};
    repaint();
}

void SpectrogramView::setTilt(float dbPerOctave)
{
    tilt = dbPerOctave;
    image = {};
    repaint();
}

void SpectrogramView::setFftSize(int size)
{
    if (size == fftSize)
        return;
    fftSize = size;
    overlay = {};
    repaint();
}

void SpectrogramView::themeChanged()
{
    rebuildPalette();
    image = {};   // re-render history in the new colours
    overlay = {};
    repaint();
}

void SpectrogramView::setCleanMode(bool clean)
{
    if (clean == cleanMode)
        return;
    cleanMode = clean;
    overlay = {};
    repaint();
}

void SpectrogramView::resized()
{
    image = {};
    overlay = {};
}

juce::Rectangle<int> SpectrogramView::plotArea() const noexcept
{
    return getLocalBounds().reduced(1, 4);
}

void SpectrogramView::rebuildPalette()
{
    const auto stops = stopsFor(currentPalette);
    for (int i = 0; i < 256; ++i)
    {
        const float t = (float) i / 255.0f;
        auto upper = std::size_t { 1 };
        while (upper < stops.size() - 1 && stops[upper].position < t)
            ++upper;

        const auto& a = stops[upper - 1];
        const auto& b = stops[upper];
        const float mix = juce::jlimit(0.0f, 1.0f, (t - a.position) / juce::jmax(1.0e-6f, b.position - a.position));
        const auto colour = juce::Colour(a.argb).interpolatedWith(juce::Colour(b.argb), mix);
        colours[(size_t) i] = juce::PixelARGB(255, colour.getRed(), colour.getGreen(), colour.getBlue());
    }
}

void SpectrogramView::appendRows(const SpectrogramRow* rows, std::size_t count)
{
    if (rowRate <= 0.0 || count == 0)
        return;

    // Each column covers 1 / pixelsPerSecond seconds; rows inside a column are
    // combined by maximum so short transients stay visible.
    const double columnsPerRow = pixelsPerSecond / rowRate;

    for (std::size_t r = 0; r < count; ++r)
    {
        const auto& row = rows[r].db;
        if (! hasPending)
        {
            pending = row;
            hasPending = true;
        }
        else
        {
            for (std::size_t b = 0; b < pending.size(); ++b)
                pending[b] = std::max(pending[b], row[b]);
        }

        columnPhase += columnsPerRow;
        bool emitted = false;
        while (columnPhase >= 1.0)
        {
            pushColumn(pending);
            columnPhase -= 1.0;
            emitted = true;
        }
        if (emitted)
            hasPending = false;
    }
}

void SpectrogramView::pushColumn(const Column& column)
{
    history[columnCount % kMaxColumns] = column;
    ++columnCount;

    if (image.isValid())
    {
        drawColumn(writeX, column);
        writeX = (writeX + 1) % image.getWidth();
    }
    repaint();
}

void SpectrogramView::drawColumn(int x, const Column& column)
{
    const int height = image.getHeight();
    juce::Image::BitmapData pixels(image, x, 0, 1, height, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < height; ++y)
    {
        const auto& p = pixelRows[(size_t) y];
        float level;
        if (p.firstBand >= 0)
        {
            level = column[(size_t) p.firstBand];
            for (int b = p.firstBand + 1; b <= p.lastBand; ++b)
                level = std::max(level, column[(size_t) b]);
        }
        else
        {
            const int i = juce::jmin((int) p.position, kSpectrogramBands - 2);
            const float t = p.position - (float) i;
            level = column[(size_t) i] + t * (column[(size_t) i + 1] - column[(size_t) i]);
        }

        const float normalised = (level + p.tiltDb + range) / range; // 0 = -range dB, 1 = 0 dBFS
        const int index = juce::jlimit(0, 255, (int) (normalised * 255.0f));
        *reinterpret_cast<juce::PixelARGB*>(pixels.getPixelPointer(0, y)) = colours[(size_t) index];
    }
}

void SpectrogramView::rebuildImage(float pixelScale)
{
    const auto area = plotArea();
    imageScale = pixelScale;
    if (area.isEmpty())
    {
        image = {};
        return;
    }

    // Width in logical pixels (one column each), height in physical pixels for a crisp frequency axis.
    const int width = juce::jmin((int) kMaxColumns, area.getWidth());
    const int height = juce::jmax(1, juce::roundToInt((float) area.getHeight() * pixelScale));
    image = juce::Image(juce::Image::ARGB, width, height, false, juce::SoftwareImageType());
    image.clear(image.getBounds(), juce::Colour(stopsFor(currentPalette).front().argb));

    // Map each pixel row to the bands it covers.
    pixelRows.resize((size_t) height);
    const float bandsPerPixel = (float) kSpectrogramBands / (float) height;
    for (int y = 0; y < height; ++y)
    {
        const float top = (float) (height - y) * bandsPerPixel - 0.5f;
        const float bottom = (float) (height - y - 1) * bandsPerPixel - 0.5f;
        const float centre = juce::jlimit(0.0f, (float) kSpectrogramBands - 1.0f, 0.5f * (top + bottom));

        auto& p = pixelRows[(size_t) y];
        p.position = centre;
        p.tiltDb = tilt * std::log2(SpectrogramAnalyzer::bandFrequency(centre) / 1000.0f);
        if (top - bottom >= 1.0f)
        {
            p.firstBand = juce::jlimit(0, kSpectrogramBands - 1, (int) std::ceil(bottom));
            p.lastBand = juce::jlimit(p.firstBand, kSpectrogramBands - 1, (int) std::floor(top));
        }
        else
        {
            p.firstBand = p.lastBand = -1;
        }
    }

    // Re-render what history we have, oldest first, ending at the right edge.
    writeX = 0;
    const auto available = (std::uint64_t) juce::jmin<std::uint64_t>(columnCount, (std::uint64_t) width);
    for (auto i = columnCount - available; i < columnCount; ++i)
    {
        drawColumn(writeX, history[i % kMaxColumns]);
        writeX = (writeX + 1) % width;
    }
    // Keep the newest column at the right edge even when history is shorter than the view.
    if (available < (std::uint64_t) width)
        writeX = (int) available;
}

void SpectrogramView::renderOverlay(float pixelScale)
{
    overlayScale = pixelScale;
    overlay = juce::Image(juce::Image::ARGB,
                          juce::jmax(1, juce::roundToInt((float) getWidth() * pixelScale)),
                          juce::jmax(1, juce::roundToInt((float) getHeight() * pixelScale)),
                          true);

    juce::Graphics g(overlay);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    const auto area = plotArea().toFloat();

    g.setFont(theme::font(10.5f));
    for (const auto& [hz, label] : { std::pair { 50.0f, "50" }, std::pair { 100.0f, "100" }, std::pair { 200.0f, "200" },
                                     std::pair { 500.0f, "500" }, std::pair { 1000.0f, "1k" }, std::pair { 2000.0f, "2k" },
                                     std::pair { 5000.0f, "5k" }, std::pair { 10000.0f, "10k" } })
    {
        const float y = yForFrequency(hz, area);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX() + 30.0f, area.getRight());

        const auto box = juce::Rectangle<float>(area.getX() + 3.0f, y - 7.0f, 26.0f, 14.0f);
        g.setColour(theme::background.withAlpha(0.55f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(theme::text);
        g.drawText(label, box, juce::Justification::centred, false);
    }

    if (cleanMode)
        return;

    const auto info = "FFT " + juce::String(fftSize) + "  |  " + juce::String(juce::roundToInt(pixelsPerSecond))
                    + " px/s  |  " + juce::String(juce::roundToInt(range)) + " dB range";
    const auto infoBox = juce::Rectangle<float>(area.getRight() - 210.0f, area.getY() + 3.0f, 206.0f, 15.0f);
    g.setColour(theme::background.withAlpha(0.55f));
    g.fillRoundedRectangle(infoBox, 3.0f);
    g.setColour(theme::text);
    g.drawText(info, infoBox, juce::Justification::centred, false);
}

void SpectrogramView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (! image.isValid() || pixelScale != imageScale)
        rebuildImage(pixelScale);

    const auto area = plotArea();
    if (image.isValid())
    {
        // Oldest part (writeX .. end) on the left, then the newest part (0 .. writeX) at the right.
        const int width = image.getWidth();
        const int left = width - writeX;
        const int destX = area.getRight() - width;
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        if (left > 0)
            g.drawImage(image, destX, area.getY(), left, area.getHeight(), writeX, 0, left, image.getHeight());
        if (writeX > 0)
            g.drawImage(image, destX + left, area.getY(), writeX, area.getHeight(), 0, 0, writeX, image.getHeight());
    }

    if (columnCount == 0 && ! cleanMode)
    {
        g.setColour(theme::textDim);
        g.setFont(theme::font(13.0f));
        g.drawText(rowRate > 0.0 ? "Waiting for audio" : "No audio stream", area, juce::Justification::centred, false);
    }

    if (! overlay.isValid() || pixelScale != overlayScale)
        renderOverlay(pixelScale);
    g.drawImage(overlay, getLocalBounds().toFloat());
}

} // namespace sonarc
