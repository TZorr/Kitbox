//
//  PadButton.h
//  Kitbox
//
//  One of the sixteen pads.
//
//  Click: select the pad and play it, louder the higher up it is hit - the top
//  edge is full velocity, the bottom edge about a third. Right-click: load,
//  rename or clear, its colour, and what the pad plays - its sample, or
//  Transmute's synth of it (Auto, or one of the five models). And Export
//  Samples, which writes every pad's sample and synth into a folder.
//
//  Since 0.9.0 the pad is filled with its colour - its group's, or one chosen
//  under Colour in its menu - and shows its number, name, waveform and note.
//  Bottom right, SYN: what the pad plays, at a click. On, it goes back to the
//  synth chosen last (Auto unless the menu picked a model); it is filled once
//  the synth plays, an outline while it fits, and faint while the pad plays
//  its sample.
//
//  Drag and drop, all three ways:
//
//  - files from the Finder onto a pad load it; several files fill that pad and
//    the ones after it, in name order;
//  - a pad dragged onto another pad swaps the two, sample and knobs;
//  - a pad dragged out of the window hands the host or the Finder the pad's
//    original file (the editor, as the drag container, supplies it).
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/PadSource.h"

class KitboxProcessor;

class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget
{
public:
    PadButton (KitboxProcessor& processorToUse, int padIndex);

    std::function<void (int)> onSelect;

    void setSelected (bool shouldBeSelected);

    /** 30 Hz, from the editor: picks up hits from MIDI and fades the flash. */
    void updateFlash();

    /** Room left around the pad for the selected pad's outer ring. */
    static constexpr int ringMargin = 3;

    /** The SYN switch, and whether it is shown and can be clicked. */
    juce::Rectangle<float> toggleBounds() const;
    bool toggleVisible() const;
    bool toggleEnabled() const;

    /** What the switch does: sample to the synth chosen last, or back. */
    void toggleSource();

    static bool parseDragDescription (const juce::var& description, int& pad);
    static juce::String dragDescription (int pad);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override            { setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { setDropHover (true); }
    void itemDragExit (const SourceDetails&) override  { setDropHover (false); }
    void itemDropped (const SourceDetails&) override;

private:
    void setDropHover (bool hover);
    void showMenu();
    void showRename();
    void showColourPicker();
    juce::Colour fillColour() const;
    void exportSamples();
    int sourceState() const;

    KitboxProcessor& processor;
    const int index;
    bool selected = false, dropHover = false, dragStarted = false, toggleHit = false;
    int lastHitCount = 0;
    int shownNote = -1, shownChoke = -1, shownSourceState = -1;
    PadSource lastSynthSource = PadSource::automatic;
    float flash = 0.0f;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> renameWindow;
};
