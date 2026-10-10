//
//  SampleDisplay.cpp
//  Kitbox
//

#include "SampleDisplay.h"
#include "Palette.h"
#include "PluginProcessor.h"
#include "TextUtf8.h"

SampleDisplay::SampleDisplay (KitboxProcessor& processorToUse)
    : processor (processorToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SampleDisplay::setPad (int newPad)
{
    pad = newPad;
    shownSample = nullptr;
    shownStart = -1.0f;
    repaint();
}

void SampleDisplay::refresh()
{
    const auto sample = processor.getPadSample (pad);
    const auto start = processor.parameters.getRawParameterValue (KitParams::padId (pad, KitParams::Pad::start))->load();
    const auto loading = processor.isPadLoading (pad);
    const auto routing = routingText();
    const auto syn = synText();
    const auto status = statusText();

    if (sample.get() != shownSample || ! juce::exactlyEqual (start, shownStart) || loading != shownLoading
        || routing != shownRouting || syn != shownSyn || status != shownStatus)
    {
        shownSample = sample.get();
        shownStart = start;
        shownLoading = loading;
        shownRouting = routing;
        shownSyn = syn;
        shownStatus = status;
        repaint();
    }
}

juce::String SampleDisplay::synText() const
{
    const auto progress = processor.getSynProgress();
    return progress.total > 0 ? "SYN " + juce::String (progress.done) + "/" + juce::String (progress.total) : "SYN";
}

juce::String SampleDisplay::statusText() const
{
    const auto fit = processor.getFitProgress (pad);
    if (fit >= 0.0f)
        return "fitting... " + juce::String (juce::roundToInt (fit * 100.0f)) + " %";

    if (processor.isPadSynth (pad))
        if (const auto& params = processor.getPadSynthParams (pad))
            return juce::String ("synth ") + transmute::modelTitle (params->model);

    return {};
}

juce::Rectangle<float> SampleDisplay::synBounds() const
{
    const auto inner = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    const auto width = shownSyn.length() > 3 ? 62.0f : 34.0f;
    return { inner.getRight() - width, inner.getY(), width, 15.0f };
}

juce::String SampleDisplay::routingText() const
{
    if (processor.getLearnPad() == pad)
        return "play a key to set the note...";

    const auto value = [this] (const char* suffix)
    {
        return (int) processor.parameters.getRawParameterValue (KitParams::padId (pad, suffix))->load();
    };

    const auto note = processor.getPadNote (pad);
    auto text = KitParams::noteName (note) + utf8 (" \xc2\xb7 note ") + juce::String (note);

    if (const auto choke = value (KitParams::Pad::choke); choke > 0)
        text += utf8 (" \xc2\xb7 choke ") + juce::String (choke);

    if (const auto output = value (KitParams::Pad::output); output > 0)
        text += utf8 (" \xc2\xb7 Out ") + juce::String (output);

    return text;
}

void SampleDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (synBounds().contains (event.position))
    {
        processor.transmuteAll();
        refresh();
        return;
    }

    processor.playPad (pad, 1.0f);
}

void SampleDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::display);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto inner = bounds.reduced (14.0f, 10.0f);

    // SYN: lit when there is a sample to transmute, its count while it runs.
    {
        const auto button = synBounds();
        const auto anything = [this]
        {
            for (int p = 0; p < KitParams::numPads; ++p)
                if (processor.canTransmute (p))
                    return true;
            return false;
        }();

        g.setColour (anything ? Palette::displayText : Palette::displayDim);
        g.drawRoundedRectangle (button.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (Palette::mono (10.5f, true));
        g.drawText (shownSyn.isEmpty() ? synText() : shownSyn, button, juce::Justification::centred);
    }

    auto info  = inner.removeFromLeft (200.0f);
    inner.removeFromLeft (12.0f);

    const auto sample = processor.getPadSample (pad);

    // Left: the pad, the sample, the facts about it.
    g.setColour (Palette::displayText);
    g.setFont (Palette::mono (17.0f, true));
    g.drawText ("PAD " + juce::String (pad + 1).paddedLeft ('0', 2), info.removeFromTop (22.0f), juce::Justification::topLeft);

    g.setFont (Palette::mono (11.0f));
    g.setColour (processor.getLearnPad() == pad ? Palette::displayText : Palette::displayDim);
    g.drawFittedText (routingText(), info.removeFromTop (16.0f).toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

    info.removeFromTop (6.0f);

    if (processor.isPadLoading (pad))
    {
        g.setColour (Palette::displayText);
        g.drawText ("loading...", info.removeFromTop (16.0f), juce::Justification::topLeft);
    }
    else if (sample == nullptr)
    {
        const auto error = processor.getPadError (pad);
        g.setColour (error.isNotEmpty() ? Palette::warning : Palette::displayDim);
        g.drawFittedText (error.isNotEmpty() ? error : "Drop a sample on the pad",
                          info.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
    }
    else
    {
        g.setColour (Palette::displayText);
        g.setFont (Palette::mono (12.0f, true));
        g.drawFittedText (sample->getName(), info.removeFromTop (16.0f).toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

        const auto channels = sample->getSourceChannels() == 1 ? juce::String ("mono")
                                                               : juce::String (sample->getSourceChannels()) + utf8 (" ch \xe2\x86\x92 mono");
        const auto rate = juce::String (sample->getSampleRate() / 1000.0, 1) + " kHz";
        const auto seconds = sample->getSeconds() < 1.0 ? juce::String (juce::roundToInt (sample->getSeconds() * 1000.0)) + " ms"
                                                        : juce::String (sample->getSeconds(), 2) + " s";

        g.setFont (Palette::mono (11.0f));
        g.setColour (Palette::displayDim);

        // The last line: a fit under way, or why one failed, or the facts -
        // a synth's model in place of its channels (it is mono, and made here).
        const auto status = statusText();
        const auto& error = processor.getSynthError (pad);
        const auto line = info.removeFromTop (16.0f);

        if (status.startsWith ("fitting"))
        {
            g.setColour (Palette::displayText);
            g.drawText (status, line, juce::Justification::topLeft);
        }
        else if (error.isNotEmpty())
        {
            g.setColour (Palette::warning);
            g.drawFittedText (error, line.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
        }
        else
        {
            const auto last = processor.isPadSynth (pad) ? status : channels;
            g.drawText (seconds + utf8 (" \xc2\xb7 ") + rate + utf8 (" \xc2\xb7 ") + last, line, juce::Justification::topLeft);
        }
    }

    // Right: the waveform, drawn as a filled min/max band around a centre line.
    const auto wave = inner;

    g.setColour (Palette::displayGrid);
    g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());

    if (sample == nullptr)
        return;

    const auto& overview = sample->getOverview();
    const auto columns = (int) overview.size();
    const auto half = wave.getHeight() * 0.5f;
    const auto mid  = wave.getCentreY();

    // Normalised to the sample's own peak: a quiet hat and a loud kick should
    // both fill the screen, because the display is for seeing the shape.
    float peak = 1.0e-6f;
    for (const auto& [low, high] : overview)
        peak = juce::jmax (peak, -low, high);

    // One continuous outline - down the top edge and back along the bottom -
    // so the band fills as one shape.
    juce::Path band;

    for (int i = 0; i < columns; ++i)
    {
        const auto x = wave.getX() + wave.getWidth() * (float) i / (float) (columns - 1);
        const auto y = mid - half * overview[(size_t) i].second / peak;
        if (i == 0) band.startNewSubPath (x, y); else band.lineTo (x, y);
    }

    for (int i = columns - 1; i >= 0; --i)
    {
        const auto x = wave.getX() + wave.getWidth() * (float) i / (float) (columns - 1);
        band.lineTo (x, mid - half * overview[(size_t) i].first / peak);
    }

    band.closeSubPath();

    const auto startFraction = shownStart >= 0.0f ? shownStart / 100.0f : 0.0f;
    const auto startX = wave.getX() + wave.getWidth() * startFraction;

    // What plays is bright, what the start point skips is dim.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (wave.withRight (startX).toNearestInt());
        g.setColour (Palette::displayDim.withAlpha (0.6f));
        g.fillPath (band);
    }
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (wave.withLeft (startX).toNearestInt());
        g.setColour (Palette::displayText);
        g.fillPath (band);
    }

    if (startFraction > 0.0f)
    {
        g.setColour (Palette::padText);
        g.fillRect (juce::Rectangle<float> (startX - 0.75f, wave.getY(), 1.5f, wave.getHeight()));
    }

    // A fit under way: a bar across the waveform, left to right, as far as it has got.
    if (const auto fit = processor.getFitProgress (pad); fit >= 0.0f)
    {
        const auto bar = wave.withWidth (wave.getWidth() * juce::jlimit (0.0f, 1.0f, fit));
        g.setColour (Palette::displayText.withAlpha (0.22f));
        g.fillRect (bar);
        g.setColour (Palette::displayText.withAlpha (0.7f));
        g.fillRect (bar.withLeft (bar.getRight() - 1.5f));   // its leading edge
    }
}
