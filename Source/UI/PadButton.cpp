//
//  PadButton.cpp
//  Kitbox
//

#include "PadButton.h"
#include "Controls.h"
#include "Palette.h"
#include "PluginProcessor.h"
#include "TextUtf8.h"

namespace
{
    const juce::String dragPrefix = "kitbox-pad:";

    /** Menu ids of the Sample / Auto / model items, in PadSources::all's order. */
    constexpr int sourceItem = 100;

    /** Menu ids of Colour's swatches, in Palette::padChoices' order, and its other two items. */
    constexpr int colourItem = 200, customColourItem = 230, defaultColourItem = 231;

    /** Variante B's pad: a 25-point title bar, a 22-point note row; SYN
        34 x 17 at the bar's right end, M and S 18 x 16 bottom right. */
    constexpr float barHeight = 25.0f, noteHeight = 22.0f;
    constexpr float toggleWidth = 34.0f, toggleHeight = 17.0f;
    constexpr float switchWidth = 18.0f, switchHeight = 16.0f;

    /** A small switch on a pad: dark and in the pad's colour when on. */
    void drawPadSwitch (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on,
                        juce::Colour padColour, juce::Colour ink, float fontSize, float alpha = 1.0f)
    {
        const auto dark = juce::Colour (0xff0e0e10).withAlpha (0.85f);
        const auto back = on ? dark : juce::Colours::black.withAlpha (0.12f);
        const auto edge = on ? dark : juce::Colours::black.withAlpha (0.28f);
        const auto type = on ? padColour : ink.withAlpha (0.72f);

        g.setColour (back.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (edge.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        g.setColour (type.withMultipliedAlpha (alpha));
        g.setFont (Palette::font (fontSize, true, 0.06f));
        g.drawText (text, r, juce::Justification::centred);
    }

    /** A swatch for the Colour menu. */
    std::unique_ptr<juce::Drawable> swatch (juce::Colour colour)
    {
        auto drawable = std::make_unique<juce::DrawablePath>();
        juce::Path path;
        path.addRoundedRectangle (0.0f, 0.0f, 14.0f, 14.0f, 3.0f);
        drawable->setPath (path);
        drawable->setFill (colour);
        drawable->setStrokeFill (colour.darker (0.4f));
        drawable->setStrokeType (juce::PathStrokeType (1.0f));
        return drawable;
    }

    /** Custom...: the colour picker, which recolours the pad as it moves. */
    class PadColourPicker : public juce::ColourSelector,
                            private juce::ChangeListener
    {
    public:
        PadColourPicker (KitboxProcessor& processorToUse, int padIndex, juce::Colour start)
            : juce::ColourSelector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace),
              processor (processorToUse), pad (padIndex)
        {
            setCurrentColour (start, juce::dontSendNotification);
            setSize (260, 280);
            addChangeListener (this);
        }

        ~PadColourPicker() override { removeChangeListener (this); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            processor.setPadColour (pad, getCurrentColour());
        }

        KitboxProcessor& processor;
        const int pad;
    };
}

PadButton::PadButton (KitboxProcessor& processorToUse, int padIndex)
    : processor (processorToUse), index (padIndex)
{
    setRepaintsOnMouseActivity (true);
    lastHitCount = processor.getEngine().getHitCount (index);
}

juce::String PadButton::dragDescription (int pad)
{
    return dragPrefix + juce::String (pad);
}

bool PadButton::parseDragDescription (const juce::var& description, int& pad)
{
    const auto text = description.toString();

    if (! text.startsWith (dragPrefix))
        return false;

    pad = text.fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
    return pad >= 0 && pad < KitParams::numPads;
}

void PadButton::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void PadButton::setDropHover (bool hover)
{
    dropHover = hover;
    repaint();
}

juce::Rectangle<float> PadButton::toggleBounds() const
{
    // Top right, 5 in from the side and 4 down, in the title bar.
    const auto pad = getLocalBounds().toFloat().reduced ((float) ringMargin);
    return { pad.getRight() - 5.0f - toggleWidth, pad.getY() + 4.0f, toggleWidth, toggleHeight };
}

juce::Rectangle<float> PadButton::soloBounds() const
{
    const auto pad = getLocalBounds().toFloat().reduced ((float) ringMargin);
    return { pad.getRight() - 5.0f - switchWidth, pad.getBottom() - 4.0f - switchHeight, switchWidth, switchHeight };
}

juce::Rectangle<float> PadButton::muteBounds() const
{
    return soloBounds().translated (-(switchWidth + 3.0f), 0.0f);
}

bool PadButton::isMuted() const
{
    return processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::mute))->load() > 0.5f;
}

bool PadButton::isSoloed() const
{
    return processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::solo))->load() > 0.5f;
}

bool PadButton::isSilenced() const
{
    if (isMuted())
        return true;

    if (isSoloed())
        return false;

    for (int p = 0; p < KitParams::numPads; ++p)
        if (processor.parameters.getRawParameterValue (KitParams::padId (p, KitParams::Pad::solo))->load() > 0.5f)
            return true;

    return false;
}

void PadButton::flip (const char* suffix)
{
    if (auto* parameter = processor.parameters.getParameter (KitParams::padId (index, suffix)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }

    repaint();
}

juce::Colour PadButton::fillColour() const
{
    return Palette::padColour (index, processor.getPadColour (index));
}

bool PadButton::toggleVisible() const
{
    return processor.getPadOriginal (index) != nullptr && ! processor.isPadLoading (index)
        && processor.getPadError (index).isEmpty();
}

bool PadButton::toggleEnabled() const
{
    return toggleVisible() && processor.canTransmute (index);
}

void PadButton::toggleSource()
{
    const auto current = processor.getPadSource (index);

    if (current == PadSource::sample)
    {
        processor.setPadSource (index, lastSynthSource);
    }
    else
    {
        lastSynthSource = current;
        processor.setPadSource (index, PadSource::sample);
    }
}

int PadButton::sourceState() const
{
    // What the switch shows, as bits: a fit's progress sends no change
    // message, so updateFlash() compares this instead.
    return (toggleVisible() ? 1 : 0)
         | (toggleEnabled() ? 2 : 0)
         | (processor.getPadSource (index) == PadSource::sample ? 4 : 0)
         | (processor.isPadSynth (index) ? 8 : 0)
         | (processor.getFitProgress (index) >= 0.0f ? 16 : 0)
         | (processor.getSynthError (index).isNotEmpty() ? 32 : 0);
}

void PadButton::updateFlash()
{
    // The note and choke group are drawn on the pad; a learnt note or a
    // restored kit changes them from outside the pad.
    const auto note  = processor.getPadNote (index);
    const auto choke = (int) processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::choke))->load();

    const auto state = sourceState();

    // Mute and Solo, this pad's and whether it is silenced by another's.
    const auto mix = (isMuted() ? 1 : 0) | (isSoloed() ? 2 : 0) | (isSilenced() ? 4 : 0);

    if (note != shownNote || choke != shownChoke || state != shownSourceState || mix != shownMix)
    {
        shownNote = note;
        shownChoke = choke;
        shownSourceState = state;
        shownMix = mix;
        repaint();
    }

    const auto hits = processor.getEngine().getHitCount (index);

    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        flash = 0.35f + 0.65f * processor.getEngine().getLastVelocity (index);
        repaint();
    }
    else if (flash > 0.0f)
    {
        flash = flash < 0.02f ? 0.0f : flash * 0.78f;
        repaint();
    }
}

void PadButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced ((float) ringMargin);
    const auto sample = processor.getPadSample (index);
    const auto loading = processor.isPadLoading (index);
    const auto filled = sample != nullptr || loading;
    constexpr float radius = 4.0f;

    auto colour = fillColour();
    const auto ink = filled ? Palette::padInk (colour) : Palette::inkDim;

    {
        // A silenced pad at half strength; the rings below stay whole.
        juce::Graphics::ScopedSaveState state (g);
        if (filled && isSilenced())
            g.beginTransparencyLayer (0.5f);

        juce::Path shape;
        shape.addRoundedRectangle (bounds, radius);

        if (filled)
        {
            // A hit lifts the colour towards white for a moment, as hard as it was played.
            auto fill = isMouseOver() ? colour.brighter (0.06f) : colour;
            fill = fill.interpolatedWith (juce::Colours::white, flash * 0.45f);

            g.setColour (fill);
            g.fillPath (shape);

            // The title bar: the pad's colour 22 % towards white, a hairline under it.
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (shape);
                const auto bar = bounds.withHeight (barHeight);
                g.setColour (fill.interpolatedWith (juce::Colours::white, 0.22f));
                g.fillRect (bar);
                g.setColour (juce::Colours::black.withAlpha (0.16f));
                g.fillRect (bar.withTop (bar.getBottom() - 1.0f));
            }

            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        }
        else
        {
            g.setColour (isMouseOver() ? Palette::padEmpty.brighter (0.05f) : Palette::padEmpty);
            g.fillPath (shape);

            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (shape);
                const auto bar = bounds.withHeight (barHeight);
                g.setColour (Palette::padEmptyBar);
                g.fillRect (bar);
                g.setColour (Palette::padEmptyBarLine);
                g.fillRect (bar.withTop (bar.getBottom() - 1.0f));
            }

            juce::Path dashed;
            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (0.5f), radius);
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (flash > 0.0f ? Palette::padEmptyLine.interpolatedWith (colour, flash) : Palette::padEmptyLine);
            g.fillPath (dashed);
        }

        // The bar: the number, then the name, clear of SYN.
        {
            auto row = bounds.withHeight (barHeight).withTrimmedLeft (9.0f).withTrimmedRight (48.0f).withTrimmedBottom (1.0f);
            const auto numberFont = Palette::font (13.0f, true);
            const auto number = juce::String (index + 1);

            g.setColour (ink.withMultipliedAlpha (0.8f));
            g.setFont (numberFont);
            const auto numberWidth = juce::GlyphArrangement::getStringWidth (numberFont, number) + 1.0f;
            g.drawText (number, row.removeFromLeft (numberWidth), juce::Justification::centredLeft);
            row.removeFromLeft (8.0f);

            juce::String name;
            auto nameColour = ink;

            if (loading)
                name = utf8 ("loading\xe2\x80\xa6");
            else if (processor.getPadError (index).isNotEmpty())
            {
                name = "can't read";
                nameColour = Palette::warning;
            }
            else if (sample != nullptr)
                name = sample->getName();

            g.setColour (nameColour);
            g.setFont (Palette::font (12.5f, true));
            g.drawText (name, row, juce::Justification::centredLeft, true);
        }

        // Bottom: the note that plays the pad, and its choke group if any.
        const auto noteRow = bounds.withTop (bounds.getBottom() - noteHeight).withTrimmedLeft (9.0f);
        {
            const auto chokeGroup = (int) processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::choke))->load();
            auto noteText = KitParams::noteName (processor.getPadNote (index));
            if (chokeGroup > 0)
                noteText += utf8 ("  \xc2\xb7  CH ") + juce::String (chokeGroup);

            g.setColour (ink.withMultipliedAlpha (filled ? 0.75f : 1.0f));
            g.setFont (Palette::font (11.0f, true));
            g.drawText (noteText, noteRow.withTrimmedRight (2.0f * switchWidth + 3.0f + 10.0f), juce::Justification::centredLeft, true);
        }

        // Between them, 38 high: the waveform, or the invitation to drop one.
        const auto content = bounds.withTrimmedTop (barHeight).withTrimmedBottom (noteHeight).reduced (8.0f, 0.0f);
        const auto waveArea = content.withSizeKeepingCentre (content.getWidth(), juce::jmin (38.0f, content.getHeight()));

        if (sample != nullptr)
        {
            const auto& overview = sample->getOverview();
            const auto columns = (int) overview.size();

            if (columns > 1)
            {
                float peak = 1.0e-6f;
                for (const auto& [low, high] : overview)
                    peak = juce::jmax (peak, -low, high);

                // Symmetric about the middle, as the mockup draws a hit: its
                // loudness, not its polarity, is what a pad needs to show.
                const auto half = waveArea.getHeight() * 0.5f - 1.0f;
                const auto mid = waveArea.getCentreY();
                juce::Path band;

                for (int i = 0; i < columns; ++i)
                {
                    const auto& [low, high] = overview[(size_t) i];
                    const auto x = waveArea.getX() + waveArea.getWidth() * (float) i / (float) (columns - 1);
                    const auto amp = juce::jmax (0.6f, half * juce::jmax (-low, high) / peak);
                    if (i == 0) band.startNewSubPath (x, mid - amp); else band.lineTo (x, mid - amp);
                }

                for (int i = columns - 1; i >= 0; --i)
                {
                    const auto& [low, high] = overview[(size_t) i];
                    const auto x = waveArea.getX() + waveArea.getWidth() * (float) i / (float) (columns - 1);
                    band.lineTo (x, mid + juce::jmax (0.6f, half * juce::jmax (-low, high) / peak));
                }

                band.closeSubPath();
                g.setColour (ink.withAlpha (0.78f));
                g.fillPath (band);
            }
        }
        else if (! loading && processor.getPadError (index).isEmpty())
        {
            const auto font = Palette::font (12.0f);
            const auto text = juce::String ("Drop sample");
            const auto width = 14.0f + 6.0f + juce::GlyphArrangement::getStringWidth (font, text);
            auto line = waveArea.withSizeKeepingCentre (width, 16.0f);
            const auto plus = line.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 9.0f);

            g.setColour (Palette::inkDim);
            g.drawLine (plus.getCentreX(), plus.getY(), plus.getCentreX(), plus.getBottom(), 1.4f);
            g.drawLine (plus.getX(), plus.getCentreY(), plus.getRight(), plus.getCentreY(), 1.4f);
            line.removeFromLeft (6.0f);
            g.setFont (font);
            g.drawText (text, line, juce::Justification::centredLeft);
        }

        // SYN: filled once the synth plays, an outline while it fits, faint while
        // the pad plays its sample; red type when its fit failed, and the whole
        // switch faint where the sample is too long to model.
        if (toggleVisible())
        {
            const auto toggle = toggleBounds();
            const auto synth = processor.getPadSource (index) != PadSource::sample;
            const auto playing = synth && processor.isPadSynth (index);
            const auto failed = processor.getSynthError (index).isNotEmpty();
            const auto alpha = toggleEnabled() ? 1.0f : 0.4f;

            if (synth && ! playing && ! failed)
            {
                // Fitting: an outline only.
                g.setColour (ink.withAlpha (0.85f * alpha));
                g.drawRoundedRectangle (toggle.reduced (0.5f), 2.0f, 1.0f);
                g.setFont (Palette::font (10.0f, true, 0.06f));
                g.drawText ("SYN", toggle, juce::Justification::centred);
            }
            else
            {
                drawPadSwitch (g, toggle, "SYN", playing || failed, failed ? Palette::warning : colour, ink, 10.0f, alpha);
            }

            drawPadSwitch (g, muteBounds(), "M", isMuted(), colour, ink, 9.5f);
            drawPadSwitch (g, soloBounds(), "S", isSoloed(), colour, ink, 9.5f);
        }

        if (filled && isSilenced())
            g.endTransparencyLayer();
    }

    // Selected: a light ring inside, the accent outside, as in the mockup.
    if (selected)
    {
        g.setColour (Palette::padRing);
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius - 1.0f, 2.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 2.0f);
    }

    if (dropHover)
    {
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.5f);
    }
}

void PadButton::mouseDown (const juce::MouseEvent& event)
{
    dragStarted = false;
    toggleHit = false;

    // Mute and Solo: flipped, the pad neither selected nor played.
    if (! event.mods.isPopupMenu() && toggleVisible())
    {
        for (const auto& [area, suffix] : { std::pair { muteBounds(), KitParams::Pad::mute },
                                            std::pair { soloBounds(), KitParams::Pad::solo } })
        {
            if (area.contains (event.position))
            {
                toggleHit = true;
                flip (suffix);
                return;
            }
        }
    }

    // The switch: the pad selected, not played.
    if (! event.mods.isPopupMenu() && toggleVisible() && toggleBounds().contains (event.position))
    {
        toggleHit = true;

        if (onSelect)
            onSelect (index);

        if (toggleEnabled())
            toggleSource();

        return;
    }

    if (event.mods.isPopupMenu())
    {
        if (onSelect)
            onSelect (index);
        showMenu();
        return;
    }

    if (onSelect)
        onSelect (index);

    const auto height = (float) juce::jmax (1, getHeight());
    const auto velocity = juce::jmap (juce::jlimit (0.0f, 1.0f, event.position.y / height), 1.0f, 0.35f);
    processor.playPad (index, velocity);
}

void PadButton::mouseDrag (const juce::MouseEvent& event)
{
    if (dragStarted || toggleHit || event.mods.isPopupMenu() || event.getDistanceFromDragStart() < 6)
        return;

    if (processor.getPadSample (index) == nullptr)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        const auto image = createComponentSnapshot (getLocalBounds()).convertedToFormat (juce::Image::ARGB);
        // Not allowed onto other JUCE windows: that would make the drag image a
        // desktop window of its own, and JUCE only hands a drag to the OS once
        // the pointer is over no JUCE component at all - the image would count.
        container->startDragging (dragDescription (index), this, juce::ScaledImage (image), false);
    }
}

bool PadButton::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (processor.isAudioFile (juce::File (path)))
            return true;

    return false;
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    setDropHover (false);

    juce::Array<juce::File> audio;

    for (const auto& path : files)
        if (processor.isAudioFile (juce::File (path)))
            audio.add (juce::File (path));

    if (audio.isEmpty())
        return;

    processor.loadFiles (index, audio);

    if (onSelect)
        onSelect (index);
}

bool PadButton::isInterestedInDragSource (const SourceDetails& details)
{
    int source = -1;
    return parseDragDescription (details.description, source) && source != index;
}

void PadButton::itemDropped (const SourceDetails& details)
{
    setDropHover (false);

    int source = -1;

    if (parseDragDescription (details.description, source))
    {
        processor.swapPads (source, index);

        if (onSelect)
            onSelect (index);
    }
}

void PadButton::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load Sample...");
    menu.addItem (3, "Rename...", processor.getPadSample (index) != nullptr);
    menu.addItem (2, "Clear Pad", processor.getPadSample (index) != nullptr);

    // How the pad looks: one of twelve, any at all, or its group's again.
    {
        juce::PopupMenu colours;
        const auto chosen = processor.getPadColour (index);

        for (size_t i = 0; i < Palette::padChoices.size(); ++i)
        {
            const auto colour = juce::Colour (Palette::padChoices[i].argb);
            juce::PopupMenu::Item item (Palette::padChoices[i].name);
            item.itemID = colourItem + (int) i;
            item.isTicked = chosen == colour;
            item.image = swatch (colour);
            colours.addItem (std::move (item));
        }

        colours.addSeparator();
        colours.addItem (customColourItem, utf8 ("Custom\xe2\x80\xa6"));
        juce::PopupMenu::Item standard ("Default");
        standard.itemID = defaultColourItem;
        standard.isTicked = ! chosen.has_value();
        standard.image = swatch (Palette::padColour (index));
        colours.addItem (std::move (standard));

        menu.addSubMenu ("Colour", colours);
    }

    // What the pad plays: the sample, or Transmute's synth of it.
    menu.addSeparator();
    const auto current = processor.getPadSource (index);
    const auto modelled = processor.canTransmute (index);

    for (size_t i = 0; i < PadSources::all.size(); ++i)
    {
        const auto source = PadSources::all[i];
        juce::String text;

        if (source == PadSource::sample)
            text = "Sample";
        else if (source == PadSource::automatic)
            text = processor.getSuggestedModel (index) ? juce::String ("Auto (") + transmute::modelTitle (*processor.getSuggestedModel (index)) + ")"
                                                       : juce::String ("Auto");
        else
            text = transmute::modelTitle (*PadSources::model (source));

        menu.addItem (sourceItem + (int) i, text, source == PadSource::sample || modelled, source == current);
    }

    // The whole kit's files, not just this pad's: the same folders as in the container.
    menu.addSeparator();
    menu.addItem (4, "Export Samples...", processor.hasAnySample());

    menu.showMenuAsync (panelMenu (*this),
                        [safe = juce::Component::SafePointer<PadButton> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 2)
        {
            safe->processor.clearPad (safe->index);
            return;
        }

        if (result == 3)
        {
            safe->showRename();
            return;
        }

        if (result == 4)
        {
            safe->exportSamples();
            return;
        }

        if (result >= colourItem && result < colourItem + (int) Palette::padChoices.size())
        {
            safe->processor.setPadColour (safe->index, juce::Colour (Palette::padChoices[(size_t) (result - colourItem)].argb));
            return;
        }

        if (result == customColourItem)
        {
            safe->showColourPicker();
            return;
        }

        if (result == defaultColourItem)
        {
            safe->processor.setPadColour (safe->index, std::nullopt);
            return;
        }

        if (result >= sourceItem && result < sourceItem + (int) PadSources::all.size())
        {
            safe->processor.setPadSource (safe->index, PadSources::all[(size_t) (result - sourceItem)]);
            return;
        }

        if (result != 1)
            return;

        safe->chooser = std::make_unique<juce::FileChooser> ("Load a sample onto pad " + juce::String (safe->index + 1),
                                                             juce::File(), safe->processor.getAudioFileWildcard());

        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectMultipleItems,
                                    [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            juce::Array<juce::File> files;
            for (const auto& file : fc.getResults())
                files.add (file);

            if (! files.isEmpty())
                safe->processor.loadFiles (safe->index, files);
        });
    });
}

void PadButton::exportSamples()
{
    // A save panel, its name the new folder's: the place and the name chosen
    // in one step, as a Finder "New Folder" would need two.
    const auto kit = processor.getKitName();
    const auto suggested = juce::File::createLegalFileName ((kit.isNotEmpty() ? kit : juce::String ("Kitbox")) + " Samples");

    chooser = std::make_unique<juce::FileChooser> ("Export Samples",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (suggested));

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<PadButton> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto result = safe->processor.exportSamples (fc.getResult());

        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Export Samples", result.getErrorMessage());
        else
            fc.getResult().revealToUser();
    });
}

void PadButton::showColourPicker()
{
    // In the plugin window, pointing at the pad, like Rename.
    auto* parent = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::make_unique<PadColourPicker> (processor, index, fillColour()),
                                            parent->getLocalArea (this, getLocalBounds()), parent);
}

void PadButton::showRename()
{
    const auto sample = processor.getPadOriginal (index);

    if (sample == nullptr)
        return;

    // Inside the plugin window rather than a window of its own: a second
    // desktop window can open behind the host's floating plugin window.
    auto* parent = getTopLevelComponent();

    renameWindow = std::make_unique<juce::AlertWindow> ("Rename Pad " + juce::String (index + 1), juce::String(),
                                                        juce::MessageBoxIconType::NoIcon, parent);
    renameWindow->addTextEditor ("name", sample->getName());
    renameWindow->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = renameWindow->getTextEditor ("name"))
        editor->selectAll();

    parent->addAndMakeVisible (*renameWindow);
    renameWindow->setCentrePosition (parent->getLocalBounds().getCentre());

    renameWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<PadButton> (this)] (int result)
    {
        if (safe == nullptr || safe->renameWindow == nullptr)
            return;

        const auto name = safe->renameWindow->getTextEditorContents ("name");
        safe->renameWindow.reset();

        if (result == 1)
            safe->processor.renamePad (safe->index, name);
    }), false);
}
