//
//  PadButton.cpp
//  Kitbox
//

#include "PadButton.h"
#include "Palette.h"
#include "PluginProcessor.h"

namespace
{
    const juce::String dragPrefix = "kitbox-pad:";
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

void PadButton::updateFlash()
{
    // The note and choke group are drawn on the pad; a learnt note or a
    // restored kit changes them from outside the pad.
    const auto note  = processor.getPadNote (index);
    const auto choke = (int) processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::choke))->load();

    if (note != shownNote || choke != shownChoke)
    {
        shownNote = note;
        shownChoke = choke;
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
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto colour = Palette::padColour (index);
    const auto sample = processor.getPadSample (index);

    auto fill = isMouseOver() ? Palette::padHover : Palette::pad;
    fill = fill.interpolatedWith (colour, flash * 0.85f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);

    // The group colour as a strip along the top: bright when the pad holds a
    // sample, faint when it is empty.
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, 6.0f);
        g.reduceClipRegion (clip);
        g.setColour (sample != nullptr ? colour : colour.withAlpha (0.25f));
        g.fillRect (bounds.withHeight (4.0f));
    }

    if (selected || dropHover)
    {
        g.setColour (dropHover ? Palette::padText : Palette::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 6.0f, 2.0f);
    }

    auto text = bounds.reduced (10.0f, 8.0f);
    text.removeFromTop (4.0f);

    const auto textColour = flash > 0.45f ? Palette::pad : Palette::padText;

    g.setFont (Palette::mono (11.0f, true));
    g.setColour (flash > 0.45f ? Palette::pad : Palette::padTextDim);
    auto topRow = text.removeFromTop (14.0f);
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), topRow, juce::Justification::topLeft);

    // Top right: the note that plays this pad, and its choke group if any.
    const auto chokeGroup = (int) processor.parameters.getRawParameterValue (KitParams::padId (index, KitParams::Pad::choke))->load();
    auto noteText = KitParams::noteName (processor.getPadNote (index));
    if (chokeGroup > 0)
        noteText = "CH" + juce::String (chokeGroup) + "  " + noteText;

    g.setFont (Palette::mono (10.5f));
    g.drawText (noteText, topRow, juce::Justification::topRight);

    juce::String name;
    auto nameColour = textColour;

    if (processor.isPadLoading (index))
    {
        name = "loading...";
        nameColour = Palette::padTextDim;
    }
    else if (processor.getPadError (index).isNotEmpty())
    {
        name = "can't read";
        nameColour = Palette::warning;
    }
    else if (sample != nullptr)
    {
        name = sample->getName();
    }
    else
    {
        name = "empty";
        nameColour = Palette::padTextDim.withAlpha (0.7f);
    }

    g.setFont (Palette::label (11.0f));
    g.setColour (nameColour);
    g.drawFittedText (name, text.removeFromBottom (28.0f).toNearestInt(), juce::Justification::bottomLeft, 2, 0.85f);
}

void PadButton::mouseDown (const juce::MouseEvent& event)
{
    dragStarted = false;

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
    if (dragStarted || event.mods.isPopupMenu() || event.getDistanceFromDragStart() < 6)
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
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

void PadButton::showRename()
{
    const auto sample = processor.getPadSample (index);

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
