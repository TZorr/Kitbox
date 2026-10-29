//
//  SampleDisplay.cpp
//  Kitbox
//

#include "SampleDisplay.h"
#include "Palette.h"
#include "PluginProcessor.h"
#include "TextUtf8.h"

namespace
{
    constexpr float padding = 16.0f, infoWidth = 232.0f, gap = 16.0f, scaleHeight = 20.0f;

    float param (const KitboxProcessor& processor, int pad, const char* suffix)
    {
        return processor.parameters.getRawParameterValue (KitParams::padId (pad, suffix))->load();
    }

    juce::String seconds (double s)
    {
        return juce::String (s, 2);
    }
}

SampleDisplay::SampleDisplay (KitboxProcessor& processorToUse)
    : processor (processorToUse)
{
}

void SampleDisplay::setPad (int newPad)
{
    pad = newPad;
    shownKey.clear();
    repaint();
}

juce::String SampleDisplay::stateKey() const
{
    // Everything drawn, as one string: a fit's progress, a learnt note and
    // the envelope's knobs send no change message, so refresh() compares this.
    namespace P = KitParams::Pad;
    const auto sample = processor.getPadSample (pad);

    return juce::String::toHexString ((juce::pointer_sized_int) sample.get())
         + "|" + (sample != nullptr ? sample->getName() : juce::String())
         + "|" + juce::String (processor.isPadLoading (pad) ? 1 : 0)
         + "|" + routingText() + "|" + statusText()
         + "|" + juce::String ((int) processor.getPadSource (pad))
         + "|" + juce::String (param (processor, pad, P::start), 2)
         + "|" + juce::String (param (processor, pad, P::attack), 1)
         + "|" + juce::String (param (processor, pad, P::hold), 1)
         + "|" + juce::String (param (processor, pad, P::decay), 1)
         + "|" + Palette::padColour (pad, processor.getPadColour (pad)).toString()
         + "|" + juce::String (processor.getFitProgress (pad), 2);
}

void SampleDisplay::refresh()
{
    const auto key = stateKey();

    if (key != shownKey)
    {
        shownKey = key;
        repaint();
    }
}

juce::String SampleDisplay::statusText() const
{
    const auto fit = processor.getFitProgress (pad);
    if (fit >= 0.0f)
        return utf8 ("fitting\xe2\x80\xa6 ") + juce::String (juce::roundToInt (fit * 100.0f)) + " %";

    if (processor.isPadSynth (pad))
        if (const auto& params = processor.getPadSynthParams (pad))
            return juce::String ("synth ") + transmute::modelTitle (params->model);

    return {};
}

juce::String SampleDisplay::routingText() const
{
    if (processor.getLearnPad() == pad)
        return utf8 ("play a key to set the note\xe2\x80\xa6");

    const auto note = processor.getPadNote (pad);
    auto text = KitParams::noteName (note) + utf8 (" \xc2\xb7 note ") + juce::String (note);

    if (const auto choke = (int) param (processor, pad, KitParams::Pad::choke); choke > 0)
        text += utf8 (" \xc2\xb7 choke ") + juce::String (choke);

    if (const auto output = (int) param (processor, pad, KitParams::Pad::output); output > 0)
        text += utf8 (" \xc2\xb7 Out ") + juce::String (output);

    return text;
}

//==============================================================================
juce::Rectangle<float> SampleDisplay::infoBounds() const
{
    return getLocalBounds().toFloat().reduced (padding).withWidth (infoWidth);
}

juce::Rectangle<float> SampleDisplay::waveBounds() const
{
    return getLocalBounds().toFloat().reduced (padding).withTrimmedLeft (infoWidth + gap);
}

juce::Rectangle<float> SampleDisplay::playBounds() const
{
    const auto info = infoBounds();
    return { info.getX(), info.getBottom() - 28.0f, 32.0f, 28.0f };
}

juce::Rectangle<float> SampleDisplay::synBounds() const
{
    const auto play = playBounds();
    return { play.getRight() + 6.0f, play.getY(), 46.0f, 28.0f };
}

bool SampleDisplay::synVisible() const
{
    return processor.getPadOriginal (pad) != nullptr && ! processor.isPadLoading (pad);
}

void SampleDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (synVisible() && synBounds().contains (event.position))
    {
        if (processor.canTransmute (pad) && onToggleSynth)
            onToggleSynth();
        return;
    }

    if (playBounds().contains (event.position) || waveBounds().contains (event.position))
        processor.playPad (pad, 1.0f);
}

//==============================================================================
void SampleDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto colour = Palette::padColour (pad, processor.getPadColour (pad));
    const auto sample = processor.getPadSample (pad);

    g.setColour (Palette::card);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    // Left: the pad, the file, the facts about it.
    auto info = infoBounds();
    {
        auto row = info.removeFromTop (29.0f);
        g.setColour (colour);
        g.fillEllipse (row.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
        row.removeFromLeft (10.0f);
        g.setColour (Palette::accent);
        g.setFont (Palette::font (24.0f, true));
        g.drawText ("PAD " + juce::String (pad + 1).paddedLeft ('0', 2), row, juce::Justification::centredLeft);
    }

    info.removeFromTop (8.0f);

    juce::String file;
    auto fileColour = Palette::title;

    if (processor.isPadLoading (pad))
        file = utf8 ("loading\xe2\x80\xa6");
    else if (sample != nullptr)
        file = sample->getFileName().isNotEmpty() ? sample->getFileName() : sample->getName();
    else if (processor.getPadError (pad).isNotEmpty())
    {
        file = processor.getPadError (pad);
        fileColour = Palette::warning;
    }
    else
        file = "No sample loaded";

    g.setColour (fileColour);
    g.setFont (Palette::font (15.0f, true));
    g.drawText (file, info.removeFromTop (19.0f), juce::Justification::centredLeft, true);

    info.removeFromTop (4.0f);
    g.setColour (processor.getLearnPad() == pad ? Palette::accent : Palette::inkSoft);
    g.setFont (Palette::font (12.0f));
    g.drawText (routingText(), info.removeFromTop (15.0f), juce::Justification::centredLeft, true);

    info.removeFromTop (2.0f);
    {
        // The facts, or a fit under way, or why one failed.
        const auto line = info.removeFromTop (14.0f);
        const auto status = statusText();
        const auto& error = processor.getSynthError (pad);
        g.setFont (Palette::font (11.0f));

        if (sample == nullptr)
        {
            g.setColour (Palette::inkFaint);
            g.drawText (utf8 ("\xe2\x80\x94"), line, juce::Justification::centredLeft);
        }
        else if (processor.getFitProgress (pad) >= 0.0f)
        {
            g.setColour (Palette::accent);
            g.drawText (status, line, juce::Justification::centredLeft, true);
        }
        else if (error.isNotEmpty())
        {
            g.setColour (Palette::warning);
            g.drawFittedText (error, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
        else
        {
            const auto rate = juce::String (sample->getSampleRate() / 1000.0, 1) + " kHz";
            const auto kind = processor.isPadSynth (pad) ? status
                            : sample->getSourceChannels() == 1 ? juce::String ("mono")
                                                               : juce::String (sample->getSourceChannels()) + utf8 (" ch \xe2\x86\x92 mono");
            g.setColour (Palette::inkFaint);
            g.drawText (rate + utf8 (" \xc2\xb7 ") + kind + utf8 (" \xc2\xb7 ") + seconds (sample->getSeconds()) + " s",
                        line, juce::Justification::centredLeft, true);
        }
    }

    // Play, and the pad's SYN.
    {
        const auto play = playBounds();
        g.setColour (Palette::field);
        g.fillRoundedRectangle (play, 6.0f);
        g.setColour (Palette::fieldLine);
        g.drawRoundedRectangle (play.reduced (0.5f), 6.0f, 1.0f);

        const auto c = play.getCentre();
        juce::Path triangle;
        triangle.addTriangle (c.x - 2.5f, c.y - 3.8f, c.x - 2.5f, c.y + 3.8f, c.x + 3.8f, c.y);
        g.setColour (Palette::ink);
        g.fillPath (triangle);

        if (synVisible())
        {
            const auto syn = synBounds();
            const auto on = processor.getPadSource (pad) != PadSource::sample;
            const auto alpha = processor.canTransmute (pad) ? 1.0f : 0.4f;

            g.setColour (Palette::field);
            g.fillRoundedRectangle (syn, 6.0f);
            g.setColour ((on ? Palette::accent : Palette::fieldLine).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (syn.reduced (0.5f), 6.0f, 1.0f);
            g.setColour ((on ? Palette::accent : Palette::inkDim).withMultipliedAlpha (alpha));
            g.setFont (Palette::font (11.0f, true, 0.04f));
            g.drawText ("SYN", syn, juce::Justification::centred);
        }
    }

    paintWave (g, waveBounds());
}

void SampleDisplay::paintWave (juce::Graphics& g, juce::Rectangle<float> well)
{
    namespace P = KitParams::Pad;

    const auto colour = Palette::padColour (pad, processor.getPadColour (pad));
    const auto sample = processor.getPadSample (pad);

    g.setColour (Palette::wave);
    g.fillRoundedRectangle (well, 8.0f);
    g.setColour (Palette::waveLine);
    g.drawRoundedRectangle (well.reduced (0.5f), 8.0f, 1.0f);

    juce::Graphics::ScopedSaveState clip (g);
    {
        juce::Path shape;
        shape.addRoundedRectangle (well.reduced (1.0f), 7.0f);
        g.reduceClipRegion (shape);
    }

    auto scale = well.removeFromBottom (scaleHeight);
    // 6 above the waveform and 8 under it, at any height the display has.
    const auto plot = well.withTrimmedTop (6.0f).withTrimmedBottom (8.0f).reduced (12.0f, 0.0f);

    // The centre line, and the scale's rule.
    g.setColour (Palette::waveGrid);
    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getCentreY() - 0.5f, plot.getWidth(), 1.0f));
    g.setColour (Palette::waveLine);
    g.fillRect (scale.withHeight (1.0f));

    if (sample == nullptr)
    {
        if (! processor.isPadLoading (pad))
        {
            const auto box = getLocalBounds().toFloat().reduced (padding).withTrimmedLeft (infoWidth + gap)
                                 .withTrimmedTop (10.0f).withTrimmedBottom (30.0f).reduced (10.0f, 0.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (box, 6.0f);
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Palette::padEmptyLine);
            g.fillPath (dashed);
            g.setColour (Palette::inkDim);
            g.setFont (Palette::font (12.0f));
            g.drawText ("Drop a sample on the pad", box, juce::Justification::centred);
        }
        return;
    }

    // The waveform: a bar every five points, as tall as the loudest moment
    // under it, normalised to the sample's own peak - a quiet hat and a loud
    // kick both fill the well, because it is there to show the shape.
    const auto& overview = sample->getOverview();
    const auto columns = (int) overview.size();
    float peak = 1.0e-6f;
    for (const auto& [low, high] : overview)
        peak = juce::jmax (peak, -low, high);

    constexpr float step = 5.0f;
    const auto bars = juce::jmax (1, (int) (plot.getWidth() / step));
    const auto half = plot.getHeight() * 0.5f - 1.5f;
    juce::Path barsPath;

    for (int b = 0; b < bars && columns > 0; ++b)
    {
        const auto from = b * columns / bars;
        const auto to = juce::jmax (from + 1, (b + 1) * columns / bars);
        float loudest = 0.0f;
        for (int c = from; c < juce::jmin (to, columns); ++c)
            loudest = juce::jmax (loudest, -overview[(size_t) c].first, overview[(size_t) c].second);

        const auto x = plot.getX() + (float) b * step + step * 0.5f;
        const auto amp = juce::jmax (0.5f, half * loudest / peak);
        barsPath.startNewSubPath (x, plot.getCentreY() - amp);
        barsPath.lineTo (x, plot.getCentreY() + amp);
    }

    g.setColour (colour.withAlpha (0.8f));
    g.strokePath (barsPath, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // What the start point skips is dimmed.
    const auto length = juce::jmax (1.0e-3, sample->getSeconds());
    const auto startFraction = juce::jlimit (0.0f, 1.0f, param (processor, pad, P::start) / 100.0f);
    const auto startX = plot.getX() + plot.getWidth() * startFraction;

    if (startFraction > 0.0f)
    {
        g.setColour (Palette::wave.withAlpha (0.6f));
        g.fillRect (well.withRight (startX));
    }

    // The amp envelope from the start point: Attack up, Hold, then Decay to
    // -60 dB at its time - or "Full", flat to the end - in linear amplitude.
    {
        const auto attack = param (processor, pad, P::attack) / 1000.0;
        const auto hold   = param (processor, pad, P::hold) / 1000.0;
        const auto decayMs = param (processor, pad, P::decay);
        const auto full = decayMs >= KitParams::decayMaxMs - 0.5f;
        const auto decay = decayMs / 1000.0;

        const auto top = plot.getY(), bottom = plot.getBottom() - 2.0f;
        const auto xAt = [&] (double t) { return (float) (startX + (plot.getRight() - startX) * t / (length * (1.0 - startFraction) + 1.0e-9)); };
        const auto yAt = [&] (double level) { return (float) (bottom - (bottom - top) * level); };
        const auto end = length * (1.0 - startFraction);

        juce::Path env;
        env.startNewSubPath (startX, bottom);
        env.lineTo (xAt (juce::jmin (attack, end)), yAt (attack > 0.0 ? juce::jmin (1.0, end / attack) : 1.0));

        if (attack + hold < end)
        {
            env.lineTo (xAt (attack + hold), yAt (1.0));

            for (int i = 1; i <= 80; ++i)
            {
                const auto t = attack + hold + (end - attack - hold) * i / 80.0;
                const auto level = full ? 1.0 : std::exp (-6.9077553 * (t - attack - hold) / juce::jmax (1.0e-3, decay));
                env.lineTo (xAt (t), yAt (level));
            }
        }

        g.setColour (Palette::accent);
        g.strokePath (env, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The start point: an orange line and its tag.
    g.setColour (Palette::accent);
    g.fillRect (juce::Rectangle<float> (startX - 1.0f, well.getY(), 2.0f, well.getHeight()));
    {
        const auto tag = juce::Rectangle<float> (startX - 1.0f, well.getY(), 12.0f, 13.0f);
        juce::Path box;
        box.addRoundedRectangle (tag.getX(), tag.getY(), tag.getWidth(), tag.getHeight(), 4.0f, 4.0f, false, false, false, true);
        g.fillPath (box);
        g.setColour (juce::Colour (0xff111113));
        g.setFont (Palette::font (9.0f, true));
        g.drawText ("S", tag, juce::Justification::centred);
    }

    // The time scale: five marks across the sample.
    {
        g.setColour (Palette::inkFaint);
        g.setFont (Palette::font (10.0f));
        const auto row = scale.reduced (12.0f, 0.0f);

        for (int k = 0; k <= 4; ++k)
        {
            const auto text = seconds (length * k / 4.0) + (k == 4 ? " s" : "");
            const auto x = row.getX() + row.getWidth() * (float) k / 4.0f;
            const auto box = k == 0 ? juce::Rectangle<float> (x, row.getY(), 60.0f, row.getHeight())
                           : k == 4 ? juce::Rectangle<float> (x - 60.0f, row.getY(), 60.0f, row.getHeight())
                                    : juce::Rectangle<float> (x - 30.0f, row.getY(), 60.0f, row.getHeight());
            g.drawText (text, box, k == 0 ? juce::Justification::centredLeft
                                 : k == 4 ? juce::Justification::centredRight : juce::Justification::centred);
        }
    }

    // A fit under way: a bar across the waveform, left to right, as far as it has got.
    if (const auto fit = processor.getFitProgress (pad); fit >= 0.0f)
    {
        const auto bar = well.withWidth (well.getWidth() * juce::jlimit (0.0f, 1.0f, fit));
        g.setColour (Palette::accent.withAlpha (0.16f));
        g.fillRect (bar);
        g.setColour (Palette::accent.withAlpha (0.7f));
        g.fillRect (bar.withLeft (bar.getRight() - 1.5f));
    }
}
